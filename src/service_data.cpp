#include "service_data.h"
#include "vendor/json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <set>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace services {
using json = nlohmann::json;
namespace fs = std::filesystem;
static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32-n)); }
std::string sha256(const std::vector<uint8_t>& bytes) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    size_t blocks=(bytes.size()+9+63)/64;
    for(size_t block=0;block<blocks;block++) {
        uint32_t w[64]={};
        for(size_t i=0;i<64;i++) {
            size_t p=block*64+i;
            uint8_t v=p<bytes.size()?bytes[p]:p==bytes.size()?0x80:0;
            if(p>=blocks*64-8) v=uint8_t((uint64_t(bytes.size())*8)>>((blocks*64-1-p)*8));
            w[i/4]|=uint32_t(v)<<(24-(i%4)*8);
        }
        for(int i=16;i<64;i++) {
            auto a=w[i-15],b=w[i-2];
            w[i]=w[i-16]+(rotr(a,7)^rotr(a,18)^(a>>3))+w[i-7]+(rotr(b,17)^rotr(b,19)^(b>>10));
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];
        for(int i=0;i<64;i++) {
            auto t=v+(rotr(e,6)^rotr(e,11)^rotr(e,25))+((e&f)^(~e&g))+k[i]+w[i];
            auto t2=(rotr(a,2)^rotr(a,13)^rotr(a,22))+((a&b)^(a&c)^(b&c));
            v=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=v;
    }
    std::ostringstream out;out<<std::hex<<std::setfill('0');
    for(auto v:h) out<<std::setw(8)<<v;
    return out.str();
}
static const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string base64(const std::vector<uint8_t>& data) {
    std::string out;
    for(size_t i=0;i<data.size();i+=3) {
        uint32_t n=uint32_t(data[i])<<16;
        if(i+1<data.size()) n|=uint32_t(data[i+1])<<8;
        if(i+2<data.size()) n|=data[i+2];
        out+=alphabet[n>>18];out+=alphabet[(n>>12)&63];
        out+=i+1<data.size()?alphabet[(n>>6)&63]:'=';
        out+=i+2<data.size()?alphabet[n&63]:'=';
    }
    return out;
}
std::vector<uint8_t> unbase64(const std::string& text) {
    if(text.size()%4 || text.size()>24*1024*1024) throw std::runtime_error("Invalid backup encoding");
    std::vector<uint8_t> out;
    for(size_t i=0;i<text.size();i+=4) {
        uint32_t value=0;int padding=0;
        for(int j=0;j<4;j++) {
            char ch=text[i+j];
            if(ch=='=') {if(j<2 || i+4!=text.size()) throw std::runtime_error("Invalid padding");padding++;value<<=6;}
            else {
                const char* pos=std::find(alphabet,alphabet+64,ch);
                if(pos==alphabet+64 || padding) throw std::runtime_error("Invalid base64");
                value=(value<<6)|uint32_t(pos-alphabet);
            }
        }
        out.push_back(uint8_t(value>>16));
        if(padding<2) out.push_back(uint8_t(value>>8));
        if(padding<1) out.push_back(uint8_t(value));
    }
    if(base64(out)!=text) throw std::runtime_error("Noncanonical base64");
    return out;
}
static std::array<unsigned,3> version(const std::string& text) {
    static const std::regex pattern(R"(^v?(\d{1,5})\.(\d{1,5})\.(\d{1,5})$)");
    std::smatch m;
    if(!std::regex_match(text,m,pattern)) throw std::runtime_error("Unsupported release version");
    return {unsigned(std::stoul(m[1])),unsigned(std::stoul(m[2])),unsigned(std::stoul(m[3]))};
}
bool newerVersion(const std::string& remote,const std::string& local) {return version(remote)>version(local);}
Release parseRelease(const std::string& text,const std::string& platform) {
    auto j=json::parse(text);
    if(j.value("draft",false) || j.value("prerelease",false)) throw std::runtime_error("No stable release");
    Release r;r.tag=j.at("tag_name");version(r.tag);
    r.name=j.contains("name") && j["name"].is_string()?j["name"].get<std::string>():r.tag;
    r.notes=j.contains("body") && j["body"].is_string()?j["body"].get<std::string>():"";
    std::string wanted=platform=="Windows"?"heroes_lore_5_windows_x64.zip":
        platform=="Nintendo Switch"?"heroes_lore_5.nro":
        platform=="Android"?"heroes_lore_5_android_universal.apk":"heroes_lore_5_linux_x86_64.AppImage";
    for(const auto& a:j.at("assets")) if(a.value("name",std::string())==wanted) {
        r.asset=wanted;r.url=a.at("browser_download_url");r.size=a.at("size");
        r.digest=a.value("digest",json()).is_string()?a["digest"].get<std::string>():"";
        std::string prefix="https://github.com/"+std::string(Repository)+"/releases/download/";
        if(r.url.rfind(prefix,0)!=0 || r.size<64 || r.size>128ULL*1024*1024)
            throw std::runtime_error("Invalid release asset");
        if(!r.digest.empty() && !std::regex_match(r.digest,std::regex("sha256:[a-f0-9]{64}")))
            throw std::runtime_error("Invalid asset digest");
        return r;
    }
    throw std::runtime_error("Release has no package for this platform");
}
std::string readText(const std::string& path,size_t limit) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f) throw std::runtime_error("Cannot open file");
    auto n=f.tellg();if(n<0 || uint64_t(n)>limit) throw std::runtime_error("File too large");
    std::string text(size_t(n),'\0');f.seekg(0);
    if(n && !f.read(text.data(),n)) throw std::runtime_error("Cannot read file");
    return text;
}
void writeText(const std::string& path,const std::string& text) {
    std::ofstream f(path,std::ios::binary|std::ios::trunc);
    if(!f || !f.write(text.data(),text.size()) || !(f.flush())) throw std::runtime_error("Cannot write file");
}
bool safeSavePath(const std::string& name) {
    auto split=name.find('/');
    if(split==std::string::npos || (name.substr(0,split)!="internal" && name.substr(0,split)!="external")) return false;
    std::string part;
    for(size_t i=split+1;i<=name.size();i++) {
        if(i==name.size() || name[i]=='/') {
            if(part.empty() || part[0]=='.' || part.back()=='.' || part.back()==' ') return false;
            std::string upper=part.substr(0,part.find('.'));
            for(char& c:upper) c=char(std::toupper((unsigned char)c));
            if(upper=="CON" || upper=="PRN" || upper=="AUX" || upper=="NUL" ||
                (upper.size()==4 && (upper.substr(0,3)=="COM" || upper.substr(0,3)=="LPT") && upper[3]>='1' && upper[3]<='9')) return false;
            part.clear();
        } else {
            auto c=(unsigned char)name[i];
            if(c<32 || c>126 || std::string("\\:*?\"<>|").find(char(c))!=std::string::npos) return false;
            part+=char(c);
        }
    }
    return name.size()<240;
}
SaveFiles readSaves(const std::string& root) {
    SaveFiles files;size_t total=0;
    for(const char* group:{"internal","external"}) {
        auto directory=fs::path(root)/group;
        if(fs::is_symlink(directory)) throw std::runtime_error("Save directory is a symlink");
        if(!fs::exists(directory)) continue;
        for(const auto& entry:fs::recursive_directory_iterator(directory)) {
            if(entry.is_symlink()) throw std::runtime_error("Save symlinks are unsupported");
            if(!entry.is_regular_file() || entry.path().filename().string().front()=='.') continue;
            auto name=entry.path().lexically_relative(root).generic_string();
            if(!safeSavePath(name)) throw std::runtime_error("Unsupported save filename");
            auto bytes=readText(entry.path().string(),16*1024*1024);
            total+=bytes.size();if(files.size()>=64 || total>16*1024*1024) throw std::runtime_error("Save backup too large");
            files[name]={bytes.begin(),bytes.end()};
        }
    }
    return files;
}
std::string platformName() {
#if defined(__SWITCH__)
    return "Nintendo Switch";
#elif defined(__ANDROID__)
    return "Android";
#elif defined(_WIN32)
    return "Windows";
#else
    return "Linux";
#endif
}
std::string packSaves(const SaveFiles& files,const std::string& platform) {
    if(files.empty()) throw std::runtime_error("No local saves to upload");
    json j={{"app","heroes_lore_5"},{"version",1},{"platform",platform},{"files",json::object()}};
    size_t total=0;
    for(auto& file:files) {
        if(!safeSavePath(file.first)) throw std::runtime_error("Invalid save path");
        total+=file.second.size();if(files.size()>64 || total>16*1024*1024) throw std::runtime_error("Save backup too large");
        j["files"][file.first]={{"data",base64(file.second)},{"sha256",sha256(file.second)}};
    }
    return j.dump();
}
SaveFiles unpackSaves(const std::string& text) {
    if(text.size()>32*1024*1024) throw std::runtime_error("Backup too large");
    auto j=json::parse(text);
    if(j.at("app")!="heroes_lore_5" || j.at("version")!=1 || !j.at("files").is_object())
        throw std::runtime_error("Backup belongs to a different game or format");
    SaveFiles out;size_t total=0;std::set<std::string> names;
    for(auto it=j["files"].begin();it!=j["files"].end();++it) {
        if(!safeSavePath(it.key())) throw std::runtime_error("Unsafe backup path");
        std::string folded=it.key();for(char& c:folded)c=char(std::tolower((unsigned char)c));
        if(!names.insert(folded).second) throw std::runtime_error("Backup has colliding filenames");
        auto data=unbase64(it.value().at("data"));
        if(sha256(data)!=it.value().at("sha256")) throw std::runtime_error("Backup checksum mismatch");
        total+=data.size();if(out.size()>=64 || total>16*1024*1024) throw std::runtime_error("Backup too large");
        out.emplace(it.key(),std::move(data));
    }
    if(out.empty()) throw std::runtime_error("Empty backup");
    return out;
}
std::string restoreSaves(const std::string& root,const SaveFiles& files) {
    // Revalidate before touching disk, even for callers other than Drive.
    auto checked=unpackSaves(packSaves(files,platformName()));
    fs::path base(root);
    for(const char* part:{"internal","external","save-backups"})
        if(fs::is_symlink(base/part) || (fs::exists(base/part) && !fs::is_directory(base/part))) throw std::runtime_error("Unsafe local save directory");
    std::string stamp=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    auto stage=base/("restore-"+stamp),backup=base/"save-backups"/stamp;
    for(const char* part:{"internal","external"}) fs::create_directories(stage/part);
    for(auto& file:checked) {
        auto target=stage/fs::path(file.first);fs::create_directories(target.parent_path());
        writeText(target.string(),std::string(file.second.begin(),file.second.end()));
    }
    fs::create_directories(backup);
    std::vector<std::string> oldMoved,newMoved;
    try {
        for(const char* part:{"internal","external"}) {
            if(fs::exists(base/part)) {fs::rename(base/part,backup/part);oldMoved.push_back(part);}
            fs::rename(stage/part,base/part);newMoved.push_back(part);
        }
    } catch(...) {
        for(auto it=newMoved.rbegin();it!=newMoved.rend();++it) fs::rename(base / *it,stage / *it);
        for(auto it=oldMoved.rbegin();it!=oldMoved.rend();++it) fs::rename(backup / *it,base / *it);
        throw;
    }
    fs::remove(stage);
    return backup.generic_string();
}
std::string urlEncode(const std::string& text) {
    std::ostringstream out;out<<std::hex<<std::uppercase;
    for(unsigned char c:text) {
        if((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='-' || c=='_' || c=='.' || c=='~') out<<char(c);
        else out<<'%'<<std::setw(2)<<std::setfill('0')<<int(c);
    }
    return out.str();
}
}
