#pragma once
#include <string>
#include <functional>
#include <cstdint>
#include <vector>
namespace services {
struct HttpResponse {int statusCode=0;std::string body;};
void initNetwork();
void shutdownNetwork();
HttpResponse httpExecute(const std::string& method,const std::string& url,
                         const std::vector<std::string>& headers={},const std::string& body="");
using DownloadProgress=std::function<void(uint64_t,uint64_t)>;
void downloadFile(const std::string& url,const std::string& path,const DownloadProgress& progress={});
bool installAndroidApk(const std::string& path);
}
