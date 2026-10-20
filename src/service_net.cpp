// HTTP transports adapted from the existing Heroes Lore ports. TLS verification stays enabled.
#include "service_net.h"
#include "service_data.h"
#include <SDL.h>
#include <stdexcept>
#include <cstdlib>
#include <filesystem>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#elif defined(__ANDROID__)
#include <jni.h>
#include <SDL_system.h>
#else
#include <curl/curl.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
#endif
namespace services {
#if defined(_WIN32)
static HttpResponse winHttpRequest(const std::string& method, const std::string& url,
                                  const std::vector<std::string>& headers, const std::string& bodyData) {
  HttpResponse resp;
  std::wstring wurl(url.begin(), url.end());
  URL_COMPONENTS urlComp;
  ZeroMemory(&urlComp, sizeof(urlComp));
  urlComp.dwStructSize = sizeof(urlComp);
  urlComp.dwSchemeLength = (DWORD)-1;
  urlComp.dwHostNameLength = (DWORD)-1;
  urlComp.dwUrlPathLength = (DWORD)-1;
  urlComp.dwExtraInfoLength = (DWORD)-1;

  if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.length(), 0, &urlComp)) return resp;

  HINTERNET hSession = WinHttpOpen(L"Heroes-Lore-Cloud/1.0",
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) return resp;

  std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
  HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
  if (!hConnect) { WinHttpCloseHandle(hSession); return resp; }

  std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
  if (urlComp.dwExtraInfoLength > 0) {
    path.append(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
  }

  std::wstring wMethod(method.begin(), method.end());
  DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), path.c_str(),
                                         NULL, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!hRequest) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
  }

  // Adiciona cabeçalhos
  for (const auto& h : headers) {
    std::wstring wh(h.begin(), h.end());
    WinHttpAddRequestHeaders(hRequest, wh.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
  }

  // Timeouts: 10s connect, 15s send, 20s receive
  WinHttpSetTimeouts(hRequest, 10000, 10000, 15000, 20000);

  DWORD bodyLen = (DWORD)bodyData.size();
  BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                bodyLen > 0 ? (LPVOID)bodyData.data() : WINHTTP_NO_REQUEST_DATA,
                                bodyLen, bodyLen, 0);

  if (sent && WinHttpReceiveResponse(hRequest, NULL)) {
    DWORD dwStatusCode = 0;
    DWORD dwSize = sizeof(dwStatusCode);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX)) {
      resp.statusCode = (int)dwStatusCode;
    }

    std::vector<char> buffer(4096);
    DWORD dwDownloaded = 0;
    while (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0) {
      if(resp.body.size()+dwSize>128*1024*1024) {resp.statusCode=0;break;}
      if (dwSize > buffer.size()) buffer.resize(dwSize);
      if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded) && dwDownloaded > 0) {
        resp.body.append(buffer.data(), dwDownloaded);
      } else {
        break;
      }
    }
  }

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return resp;
}

#elif defined(__SWITCH__) || (defined(__linux__) && !defined(__ANDROID__))

static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total = size * nmemb;
  std::string* str = static_cast<std::string*>(userp);
  if(str->size()+total>128*1024*1024) return 0;
  str->append(static_cast<char*>(contents), total);
  return total;
}

static HttpResponse curlHttpRequest(const std::string& method, const std::string& url,
                                   const std::vector<std::string>& headers, const std::string& bodyData) {
  HttpResponse resp;
  CURL* curl = curl_easy_init();
  if (!curl) return resp;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Heroes-Lore-Cloud/1.0");
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#if defined(__SWITCH__)
  curl_easy_setopt(curl, CURLOPT_CAINFO, "romfs:/cacert.pem");
#endif
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);

  struct curl_slist* hList = nullptr;
  for (const auto& h : headers) {
    hList = curl_slist_append(hList, h.c_str());
  }
  if (hList) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hList);

  if (!bodyData.empty()) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyData.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)bodyData.size());
  }

  CURLcode res = curl_easy_perform(curl);
  if (res == CURLE_OK) {
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    resp.statusCode = (int)code;
  }

  if (hList) curl_slist_free_all(hList);
  curl_easy_cleanup(curl);
  return resp;
}

#elif defined(__ANDROID__)

static jclass s_cloudActivityClass = nullptr;
static jmethodID s_midHttpExecute = nullptr;

static void androidCloudInitJni() {
  if (s_cloudActivityClass && s_midHttpExecute) return;
  JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
  if (!env) return;

  jobject activityObj = (jobject)SDL_AndroidGetActivity();
  if (activityObj && !s_cloudActivityClass) {
    jclass localClass = env->GetObjectClass(activityObj);
    if (localClass) {
      s_cloudActivityClass = (jclass)env->NewGlobalRef(localClass);
      env->DeleteLocalRef(localClass);
    }
  }

  if (!s_cloudActivityClass) {
    jclass localClass = env->FindClass("org/libsdl/app/SDLActivity");
    if (localClass) {
      s_cloudActivityClass = (jclass)env->NewGlobalRef(localClass);
      env->DeleteLocalRef(localClass);
    }
  }

  if (s_cloudActivityClass && !s_midHttpExecute) {
    s_midHttpExecute = env->GetStaticMethodID(s_cloudActivityClass, "httpExecute",
                                             "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
  }

  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
}

static HttpResponse androidHttpRequest(const std::string& method, const std::string& url,
                                      const std::vector<std::string>& headers, const std::string& bodyData) {
  HttpResponse resp;
  JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
  if (!env) return resp;

  if (!s_cloudActivityClass || !s_midHttpExecute) {
    androidCloudInitJni();
  }
  if (!s_cloudActivityClass || !s_midHttpExecute) {
    if (env->ExceptionCheck()) env->ExceptionClear();
    return resp;
  }

  std::string headersConcat;
  for (const auto& h : headers) {
    headersConcat += h + "\n";
  }

  jstring jUrl = env->NewStringUTF(url.c_str());
  jstring jMethod = env->NewStringUTF(method.c_str());
  jstring jHeaders = env->NewStringUTF(headersConcat.c_str());
  jstring jBody = env->NewStringUTF(bodyData.c_str());

  if (!jUrl || !jMethod || !jHeaders || !jBody) {
    if (jUrl) env->DeleteLocalRef(jUrl);
    if (jMethod) env->DeleteLocalRef(jMethod);
    if (jHeaders) env->DeleteLocalRef(jHeaders);
    if (jBody) env->DeleteLocalRef(jBody);
    if (env->ExceptionCheck()) env->ExceptionClear();
    return resp;
  }

  jstring jResult = (jstring)env->CallStaticObjectMethod(s_cloudActivityClass, s_midHttpExecute, jUrl, jMethod, jHeaders, jBody);

  env->DeleteLocalRef(jUrl);
  env->DeleteLocalRef(jMethod);
  env->DeleteLocalRef(jHeaders);
  env->DeleteLocalRef(jBody);

  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return resp;
  }

  if (jResult) {
    const char* utf = env->GetStringUTFChars(jResult, nullptr);
    if (utf) {
      std::string fullResult(utf);
      env->ReleaseStringUTFChars(jResult, utf);
      env->DeleteLocalRef(jResult);

      size_t nl = fullResult.find('\n');
      if (nl != std::string::npos) {
        resp.statusCode = std::atoi(fullResult.substr(0, nl).c_str());
        resp.body = fullResult.substr(nl + 1);
      } else {
        resp.statusCode = std::atoi(fullResult.c_str());
      }
    }
  }

  return resp;
}
#endif

HttpResponse httpExecute(const std::string& method, const std::string& url,
                               const std::vector<std::string>& headers, const std::string& bodyData) {
#if defined(_WIN32)
  return winHttpRequest(method, url, headers, bodyData);
#elif defined(__SWITCH__) || (defined(__linux__) && !defined(__ANDROID__))
  return curlHttpRequest(method, url, headers, bodyData);
#elif defined(__ANDROID__)
  return androidHttpRequest(method, url, headers, bodyData);
#else
  return HttpResponse{};
#endif
}


void initNetwork() {
#ifdef __ANDROID__
    androidCloudInitJni();
#elif defined(__SWITCH__)
    if(R_FAILED(socketInitializeDefault())) throw std::runtime_error("Network initialization failed");
    curl_global_init(CURL_GLOBAL_DEFAULT);
#elif !defined(_WIN32)
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
}
void shutdownNetwork() {
#ifdef __ANDROID__
    auto* env=(JNIEnv*)SDL_AndroidGetJNIEnv();
    if(env && s_cloudActivityClass) env->DeleteGlobalRef(s_cloudActivityClass);
    s_cloudActivityClass=nullptr;s_midHttpExecute=nullptr;
#elif defined(__SWITCH__)
    curl_global_cleanup();socketExit();
#elif !defined(_WIN32)
    curl_global_cleanup();
#endif
}
void downloadFile(const std::string& url,const std::string& path) {
#ifdef __ANDROID__
    auto* env=(JNIEnv*)SDL_AndroidGetJNIEnv();
    auto method=env->GetStaticMethodID(s_cloudActivityClass,"downloadFile","(Ljava/lang/String;Ljava/lang/String;)Z");
    jstring u=env->NewStringUTF(url.c_str()),p=env->NewStringUTF(path.c_str());
    bool ok=env->CallStaticBooleanMethod(s_cloudActivityClass,method,u,p)==JNI_TRUE;
    env->DeleteLocalRef(u);env->DeleteLocalRef(p);
    if(env->ExceptionCheck()) {env->ExceptionClear();ok=false;}
    if(!ok) throw std::runtime_error("Download failed");
#else
    auto result=httpExecute("GET",url,{"User-Agent: Heroes-Lore-5/1.1"});
    if(result.statusCode!=200) throw std::runtime_error("Download failed (HTTP "+std::to_string(result.statusCode)+")");
    writeText(path,result.body);
#endif
}
bool installAndroidApk(const std::string& path) {
#ifdef __ANDROID__
    auto* env=(JNIEnv*)SDL_AndroidGetJNIEnv();
    auto method=env->GetStaticMethodID(s_cloudActivityClass,"installApk","(Ljava/lang/String;)V");
    jstring p=env->NewStringUTF(path.c_str());
    env->CallStaticVoidMethod(s_cloudActivityClass,method,p);env->DeleteLocalRef(p);
    if(env->ExceptionCheck()) {env->ExceptionClear();return false;}
    return true;
#else
    return false;
#endif
}
}
