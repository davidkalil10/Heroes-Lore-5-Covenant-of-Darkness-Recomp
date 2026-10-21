#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace services {
constexpr const char* Version = "1.1.6";
constexpr const char* Repository = "davidkalil10/Heroes-Lore-5-Covenant-of-Darkness-Recomp";
struct Release {
    std::string tag, name, notes, asset, url, digest;
    uint64_t size = 0;
};
using SaveFiles = std::map<std::string, std::vector<uint8_t>>;
std::string sha256(const std::vector<uint8_t>& bytes);
std::string base64(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> unbase64(const std::string& text);
bool newerVersion(const std::string& remote, const std::string& local);
Release parseRelease(const std::string& json, const std::string& platform);
void verifyUpdateFile(const std::string& path,const Release& release);
std::string installPortableUpdate(const std::string& package,const std::string& target,const Release& release,
                                  const std::function<void()>& beforeSwap={});
bool safeSavePath(const std::string& name);
SaveFiles readSaves(const std::string& root);
std::string packSaves(const SaveFiles& files, const std::string& platform);
SaveFiles unpackSaves(const std::string& json);
std::string restoreSaves(const std::string& root, const SaveFiles& files);
std::string platformName();
std::string readText(const std::string& path, size_t limit = 32 * 1024 * 1024);
void writeText(const std::string& path, const std::string& text);
// The reader must open the immutable package, never the extraction destination.
bool extractAsset(const std::string& path,size_t expectedSize,const std::string& digest,
                  const std::function<int(char*,int)>& reader);
std::string urlEncode(const std::string& text);
}
