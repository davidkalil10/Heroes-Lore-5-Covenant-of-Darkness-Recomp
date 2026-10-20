#pragma once
#include <string>
#include <vector>
namespace services {
struct HttpResponse {int statusCode=0;std::string body;};
void initNetwork();
void shutdownNetwork();
HttpResponse httpExecute(const std::string& method,const std::string& url,
                         const std::vector<std::string>& headers={},const std::string& body="");
void downloadFile(const std::string& url,const std::string& path);
bool installAndroidApk(const std::string& path);
}
