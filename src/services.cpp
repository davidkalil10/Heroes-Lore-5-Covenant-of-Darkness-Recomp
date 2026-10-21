#include "services.h"
#include "service_net.h"
#include "google_oauth.h"
#include "vendor/json.hpp"
#include <SDL.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <ctime>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace services {
using json=nlohmann::json;
namespace fs=std::filesystem;
static std::mutex mutex;
static Status state;
static SDL_Thread* worker=nullptr;
static std::atomic<bool> cancelled{false};
static std::string dataRoot,executable,resources,accessToken,refreshToken,folderId,fileId,fileVersion;
static uint64_t tokenExpiry=0;
static SaveFiles pendingRestore;
static bool networkReady=false,romfsReleased=false;
static std::atomic<uint64_t> startupRetryAt{0};
static std::atomic<int> startupAttempts{0};
bool romfsWasReleased() {return romfsReleased;}
static const std::string drive="https://www.googleapis.com/drive/v3/files";
static const std::string cloudFile="heroes_lore_5_save.json";
static const std::string cloudFolder="Heroes Lore - 5";
static void publish(const std::function<void(Status&)>& fn) {std::lock_guard<std::mutex> lock(mutex);fn(state);}
Status status() {std::lock_guard<std::mutex> lock(mutex);return state;}
std::string resourcePath(const std::string& name) {return resources+(resources.empty() || resources.back()!='/'?"/":"")+name;}
static void message(const std::string& text) {publish([&](Status& s){s.message=text;});}
static std::string authPath() {return dataRoot+"/cloud_auth.json";}
static json response(const HttpResponse& r) {
    if(r.statusCode<200 || r.statusCode>=300)
        throw std::runtime_error("Network request failed (HTTP "+std::to_string(r.statusCode)+"). Check connection and login.");
    return json::parse(r.body);
}
static std::vector<std::string> authHeaders(bool jsonContent=false) {
    std::vector<std::string> headers={"Authorization: Bearer "+accessToken};
    if(jsonContent) headers.push_back("Content-Type: application/json; charset=UTF-8");
    return headers;
}
static void saveTokens() {
    json j={{"refresh_token",refreshToken},{"access_token",accessToken},{"expires_at",tokenExpiry}};
    auto tmp=authPath()+".tmp";writeText(tmp,j.dump());
#ifndef _WIN32
    chmod(tmp.c_str(),0600);
#endif
    // A failed replace leaves the previous usable token file intact.
#ifdef _WIN32
    if(!MoveFileExA(tmp.c_str(),authPath().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot save Google login");
#else
    fs::rename(tmp,authPath());
#endif
}
static void ensureToken() {
    if(refreshToken.empty()) throw std::runtime_error("Connect Google Drive first");
    if(!accessToken.empty() && tokenExpiry>uint64_t(std::time(nullptr))+60) return;
    auto body="client_id="+urlEncode(getGoogleClientId())+"&client_secret="+urlEncode(getGoogleClientSecret())+
        "&refresh_token="+urlEncode(refreshToken)+"&grant_type=refresh_token";
    auto j=response(httpExecute("POST","https://oauth2.googleapis.com/token",
        {"Content-Type: application/x-www-form-urlencoded"},body));
    accessToken=j.at("access_token");tokenExpiry=uint64_t(std::time(nullptr))+j.value("expires_in",3600);
    saveTokens();
}
static void launch(std::function<void()> operation) {
    if(status().busy) return;
    if(worker) {SDL_WaitThread(worker,nullptr);worker=nullptr;}
    cancelled=false;
    publish([](Status& s){s.busy=true;});
    auto task=new std::function<void()>(std::move(operation));
    worker=SDL_CreateThread([](void* ptr)->int {
        auto* task=static_cast<std::function<void()>*>(ptr);
        try {(*task)();} catch(const std::exception& e) {message(e.what());}
        delete task;
        publish([](Status& s){s.busy=false;});
        return 0;
    },"HL5 services",task);
    if(!worker) {delete task;publish([](Status& s){s.busy=false;s.checkingUpdates=false;s.downloading=false;s.message="Could not start network task";});}
}
static std::string findFolder(bool create) {
    if(!folderId.empty()) return folderId;
    auto query="name='"+cloudFolder+"' and mimeType='application/vnd.google-apps.folder' and trashed=false";
    auto j=response(httpExecute("GET",drive+"?q="+urlEncode(query)+"&fields=files(id)&pageSize=1",authHeaders()));
    if(!j.at("files").empty()) folderId=j["files"][0].at("id");
    else if(create) {
        auto body=json{{"name",cloudFolder},{"mimeType","application/vnd.google-apps.folder"}}.dump();
        folderId=response(httpExecute("POST",drive,authHeaders(true),body)).at("id");
    }
    return folderId;
}
static void queryBackup() {
    ensureToken();
    auto folder=findFolder(false);
    fileId.clear();fileVersion.clear();
    publish([](Status& s){s.backupExists=false;s.backupDate.clear();s.backupSummary.clear();});
    if(folder.empty()) return;
    auto query="name='"+cloudFile+"' and '"+folder+"' in parents and trashed=false";
    auto j=response(httpExecute("GET",drive+"?q="+urlEncode(query)+
        "&fields=files(id,modifiedTime,description,version)&orderBy=modifiedTime%20desc&pageSize=1",authHeaders()));
    if(!j.at("files").empty()) {
        auto f=j["files"][0];fileId=f.at("id");fileVersion=f.value("version",std::string());
        publish([&](Status& s){s.backupExists=true;s.backupDate=f.value("modifiedTime",std::string());
            s.backupSummary=f.value("description",std::string());});
    }
}
void init(const std::string& root,const std::string& exe,const std::string& assets) {
    state=Status{};startupAttempts=0;startupRetryAt=0;romfsReleased=false;
    accessToken.clear();refreshToken.clear();folderId.clear();fileId.clear();fileVersion.clear();tokenExpiry=0;
    dataRoot=root;executable=exe;resources=assets;
    fs::create_directories(dataRoot);
    try {auto j=json::parse(readText(dataRoot+"/services.json",16384));state.autoCheck=j.value("check_updates",true);} catch(...) {}
    try {
        auto j=json::parse(readText(authPath(),16384));refreshToken=j.at("refresh_token");
        accessToken=j.value("access_token",std::string());tokenExpiry=j.value("expires_at",uint64_t(0));
        state.connected=!refreshToken.empty();
    } catch(...) {}
    try {initNetwork();networkReady=true;} catch(const std::exception& e) {state.message=e.what();}
    if(state.autoCheck && networkReady) checkUpdate(false);
}
void shutdown() {
    cancelled=true;
    if(worker) {SDL_WaitThread(worker,nullptr);worker=nullptr;}
    if(networkReady) shutdownNetwork();
    networkReady=false;
}
void setAutoCheck(bool enabled) {
    if(!enabled)startupRetryAt=0;
    try {writeText(dataRoot+"/services.json",json{{"check_updates",enabled}}.dump());
        publish([&](Status& s){s.autoCheck=enabled;if(!enabled)s.updateNotice=false;});}
    catch(const std::exception& e) {message(e.what());}
}
static void checkFailed(bool manual,const std::string& error) {
    publish([&](Status& s){s.checkingUpdates=false;s.message=error;});
    if(!manual && status().autoCheck && startupAttempts.load()<3)
        startupRetryAt=SDL_GetTicks64()+(startupAttempts.load()==1?5000:15000);
}
void pollStartup(uint64_t now) {
    auto due=startupRetryAt.load();
    if(due && now>=due && status().autoCheck && !status().busy){startupRetryAt=0;checkUpdate(false);}
}
bool takeUpdateNotice() {
    std::lock_guard<std::mutex> lock(mutex);
    if(!state.updateNotice || state.busy)return false;
    state.updateNotice=false;return true;
}
void checkUpdate(bool manual) {
    if(status().busy)return;
    if(!networkReady) {if(manual)message("Network unavailable");return;}
    if(!manual)startupAttempts++;
    publish([](Status& s){s.checkingUpdates=true;s.message="Checking GitHub releases...";});
    launch([manual]() {
        try {
            auto r=httpExecute("GET","https://api.github.com/repos/"+std::string(Repository)+"/releases/latest",
                {"User-Agent: Heroes-Lore-5/"+std::string(Version),"Accept: application/vnd.github+json"});
            if(r.statusCode==404){publish([](Status& s){s.checkingUpdates=false;s.message="No public release yet. Actions builds are not releases.";});return;}
            if(r.statusCode!=200){checkFailed(manual,"Could not check updates. You can keep playing offline.");return;}
            auto release=parseRelease(r.body,platformName());bool available=newerVersion(release.tag,Version);
            startupRetryAt=0;
            publish([&](Status& s){s.release=release;s.updateAvailable=available;s.downloadReady=false;s.checkingUpdates=false;
                s.updateNotice=available && !manual && s.autoCheck;
                s.message=available?"Update available.":"You have the latest version.";});
        }catch(const std::exception& e){checkFailed(manual,e.what());}
    });
}
void downloadUpdate() {
    auto current=status();if(current.busy || !current.updateAvailable) return;
    publish([&](Status& s){s.downloadReady=false;s.downloading=true;s.downloadedBytes=0;s.totalBytes=current.release.size;});
    launch([release=current.release]() {
        try {
            message("Downloading update. Please wait...");
            auto directory=dataRoot+"/updates";fs::create_directories(directory);
            auto path=directory+"/"+release.asset+".download";
            downloadFile(release.url,path,[&](uint64_t done,uint64_t total){publish([&](Status& s){s.downloadedBytes=done;s.totalBytes=total?total:release.size;});});
            message("Verifying download...");verifyUpdateFile(path,release);
            publish([](Status& s){s.downloadReady=true;s.downloading=false;s.message="Download verified. Install and restart when ready.";});
        }catch(...) {publish([](Status& s){s.downloading=false;s.downloadReady=false;});throw;}
    });
    if(!status().busy)publish([](Status& s){s.downloading=false;});
}
void login() {
    if(!networkReady) {message("Network unavailable");return;}
    launch([]() {
        message("Requesting Google login code...");
        auto headers=std::vector<std::string>{"Content-Type: application/x-www-form-urlencoded"};
        auto j=response(httpExecute("POST","https://oauth2.googleapis.com/device/code",headers,
            "client_id="+urlEncode(getGoogleClientId())+"&scope="+urlEncode("https://www.googleapis.com/auth/drive.file")));
        std::string device=j.at("device_code"),code=j.at("user_code");
        std::string url=j.value("verification_url",std::string("https://www.google.com/device"));
        int interval=std::max(5,j.value("interval",5));
        auto expires=SDL_GetTicks64()+uint64_t(j.value("expires_in",1800))*1000;
        publish([&](Status& s){s.userCode=code;s.verificationUrl=url;s.message="Open the address on your phone or PC and enter this code.";});
        while(!cancelled && SDL_GetTicks64()<expires) {
            for(int i=0;i<interval*10 && !cancelled;i++) SDL_Delay(100);
            if(cancelled) break;
            auto r=httpExecute("POST","https://oauth2.googleapis.com/token",headers,
                "client_id="+urlEncode(getGoogleClientId())+"&client_secret="+urlEncode(getGoogleClientSecret())+
                "&device_code="+urlEncode(device)+"&grant_type=urn:ietf:params:oauth:grant-type:device_code");
            auto result=json::parse(r.body);
            if(r.statusCode==200 && !cancelled) {
                accessToken=result.at("access_token");refreshToken=result.at("refresh_token");
                tokenExpiry=uint64_t(std::time(nullptr))+result.value("expires_in",3600);saveTokens();
                publish([](Status& s){s.connected=true;s.userCode.clear();s.verificationUrl.clear();s.message="Google Drive connected.";});
                queryBackup();return;
            }
            auto error=result.value("error",std::string());
            if(error=="authorization_pending") continue;
            if(error=="slow_down") {interval+=5;continue;}
            throw std::runtime_error("Google authorization denied or expired. Please try again.");
        }
        publish([](Status& s){s.userCode.clear();s.verificationUrl.clear();s.message="Login cancelled or expired.";});
    });
}
void cancelLogin() {cancelled=true;}
void logout() {
    if(status().busy) return;
    if(worker) {SDL_WaitThread(worker,nullptr);worker=nullptr;}
    std::error_code error;fs::remove(authPath(),error);
    if(error) {message("Could not remove stored login");return;}
    refreshToken.clear();accessToken.clear();folderId.clear();fileId.clear();fileVersion.clear();tokenExpiry=0;
    publish([](Status& s){s.connected=false;s.backupExists=false;s.backupDate.clear();s.backupSummary.clear();s.message="Disconnected from Google Drive.";});
}
void refreshBackup() {
    if(!networkReady) return;
    launch([](){message("Checking cloud backup...");queryBackup();message(status().backupExists?"Cloud backup found.":"No cloud backup for Heroes Lore 5 yet.");});
}
void uploadBackup() {
    if(status().busy) return;
    // The settings modal pauses the guest before this operation; snapshot only closed/flushed save files.
    std::fflush(nullptr);
    auto expectedId=fileId,expectedVersion=fileVersion;
    launch([expectedId,expectedVersion]() {
        auto files=readSaves(dataRoot);auto package=packSaves(files,platformName());
        message("Uploading saved progress to Google Drive...");
        queryBackup();
        if(fileId!=expectedId || fileVersion!=expectedVersion)
            throw std::runtime_error("Cloud backup changed. Review its date and confirm upload again.");
        auto folder=findFolder(true);
        auto summary=platformName()+" | "+std::to_string(files.size())+" save files";
        if(!fileId.empty()) {
            response(httpExecute("PATCH","https://www.googleapis.com/upload/drive/v3/files/"+fileId+"?uploadType=media",authHeaders(true),package));
            response(httpExecute("PATCH",drive+"/"+fileId,authHeaders(true),json{{"description",summary}}.dump()));
        } else {
            std::string boundary="HL5_Backup_2026_boundary";
            auto metadata=json{{"name",cloudFile},{"parents",json::array({folder})},{"description",summary}}.dump();
            auto body="--"+boundary+"\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n"+metadata+
                "\r\n--"+boundary+"\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n"+package+"\r\n--"+boundary+"--\r\n";
            auto headers=authHeaders();headers.push_back("Content-Type: multipart/related; boundary="+boundary);
            response(httpExecute("POST","https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart",headers,body));
        }
        queryBackup();message("Backup uploaded. Restore it on the other platform using the same Google account.");
    });
}
void restoreBackup() {
    if(status().busy) return;
    auto expectedId=fileId,expectedVersion=fileVersion;
    launch([expectedId,expectedVersion]() {
        message("Downloading cloud backup...");queryBackup();
        if(fileId.empty()) throw std::runtime_error("No cloud backup found");
        if(fileId!=expectedId || fileVersion!=expectedVersion)
            throw std::runtime_error("Cloud backup changed. Review its date and confirm restore again.");
        auto r=httpExecute("GET",drive+"/"+fileId+"?alt=media",authHeaders());
        if(r.statusCode!=200) throw std::runtime_error("Could not download backup");
        pendingRestore=unpackSaves(r.body);
        publish([](Status& s){s.restoreReady=true;s.message="Backup validated. Returning to title...";});
    });
}
bool applyPendingRestore() {
    auto snapshot=status();if(snapshot.busy || !snapshot.restoreReady)return false;
    try {
        restoreSaves(dataRoot,pendingRestore);pendingRestore.clear();
        publish([](Status& s){s.restoreReady=false;s.message="Save restored! Returning to title...";});
        return true;
    } catch(const std::exception& error) {
        publish([&](Status& s){s.restoreReady=false;s.message=error.what();});return false;
    }
}
void requestQuit() {publish([](Status& s){s.quitRequested=true;});}
void applyUpdate() {
    auto current=status();if(current.busy || !current.downloadReady) return;
    auto package=dataRoot+"/updates/"+current.release.asset+".download";
    try {
#if defined(_WIN32)
        auto script=dataRoot+"/updates/install.ps1";
        writeText(script,readText(resourcePath("update_windows.ps1")));
        auto quote=[](const std::string& value) {if(value.find('"')!=std::string::npos) throw std::runtime_error("Unsupported path");return "\""+value+"\"";};
        auto command="powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "+quote(fs::absolute(script).string())+
            " -GamePid "+std::to_string(GetCurrentProcessId())+" -Package "+quote(fs::absolute(package).string())+
            " -Executable "+quote(fs::absolute(executable).string())+" -DataRoot "+quote(fs::absolute(dataRoot).string());
        STARTUPINFOA si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
        if(!CreateProcessA(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))
            throw std::runtime_error("Cannot start update installer");
        CloseHandle(pi.hProcess);CloseHandle(pi.hThread);requestQuit();
#elif defined(__ANDROID__)
        auto apk=dataRoot+"/updates/"+current.release.asset;
        fs::copy_file(package,apk,fs::copy_options::overwrite_existing);
        if(!installAndroidApk(apk)) throw std::runtime_error("Cannot open Android installer");
        message("Complete the Android installer. If permission is requested, allow it and press Install again.");
#else
        std::string target;
#ifdef __SWITCH__
        target=executable;
        if(target.rfind("sdmc:/",0)!=0 || target.size()<5 || target.substr(target.size()-4)!=".nro")
            throw std::runtime_error("Cannot identify the running NRO path. Install manually.");
#else
        const char* appimage=getenv("APPIMAGE");
        if(!appimage || !*appimage) throw std::runtime_error("Automatic installation requires the portable AppImage build");
        target=appimage;
#endif
        installPortableUpdate(package,target,current.release,[](){
#ifdef __SWITCH__
            romfsExit();romfsReleased=true;
#endif
        });
#ifdef __SWITCH__
        envSetNextLoad(target.c_str(),target.c_str());
#endif
        publish([](Status& s){s.restartRequired=true;s.message="Update installed. Close and reopen the game.";});
#endif
    } catch(const std::exception& e) {
        message(e.what());
        if(romfsReleased) publish([](Status& s){s.restartRequired=true;s.message+=" Close and reopen the game.";});
    }
}
}
