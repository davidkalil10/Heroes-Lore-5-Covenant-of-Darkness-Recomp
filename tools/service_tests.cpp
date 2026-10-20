#define SDL_MAIN_HANDLED
#include "service_data.h"
#include "service_net.h"
#include "services.h"
#include "service_ui.h"
#include "input_mapping.h"
#include "hle/hle.h"
#include "vendor/json.hpp"
#include <SDL.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace services;
using json=nlohmann::json;
namespace fs=std::filesystem;
static std::atomic<int> uploads{0};
static std::string remotePackage;
static bool remoteExists=true,corruptDownload=false;
static std::string updateBytes(128,'\0');
static json releaseJson() {
    updateBytes[0]='P';updateBytes[1]='K';
    std::string asset=platformName()=="Windows"?"heroes_lore_5_windows_x64.zip":"heroes_lore_5_linux_x86_64.AppImage";
    if(platformName()!="Windows") updateBytes.replace(0,4,std::string("\x7f" "ELF",4));
    return {{"tag_name","v9.2.1"},{"name","Test release"},{"body","Notes with \"quotes\"\nnext line"},
        {"draft",false},{"prerelease",false},{"assets",json::array({{{"name",asset},
            {"browser_download_url","https://github.com/"+std::string(Repository)+"/releases/download/v9.2.1/"+asset},
            {"size",updateBytes.size()},{"digest","sha256:"+sha256({updateBytes.begin(),updateBytes.end()})}}})}};
}
namespace services {
void initNetwork() {}
void shutdownNetwork() {}
bool installAndroidApk(const std::string&) {return false;}
void downloadFile(const std::string&,const std::string& path) {
    auto bytes=updateBytes;if(corruptDownload) bytes[10]='!';writeText(path,bytes);
}
HttpResponse httpExecute(const std::string& method,const std::string& url,const std::vector<std::string>&,const std::string& body) {
    if(url.find("api.github.com")!=std::string::npos) return {200,releaseJson().dump()};
    if(url.find("alt=media")!=std::string::npos) return {200,remotePackage};
    if(url.find("upload/drive")!=std::string::npos) {uploads++;return {200,"{\"id\":\"save\"}"};}
    if(url.find("mimeType")!=std::string::npos) return {200,"{\"files\":[{\"id\":\"folder\"}]}"};
    if(method=="PATCH") return {200,"{}"};
    if(url.find("orderBy=")!=std::string::npos) {
        return {200,remoteExists?"{\"files\":[{\"id\":\"save\",\"modifiedTime\":\"2026-10-20T12:34:56Z\",\"description\":\"Windows | 2 save files\",\"version\":\"1\"}]}":"{\"files\":[]}"};
    }
    throw std::runtime_error("Unexpected mock HTTP request: "+method+" "+url);
}
}
static void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
template<class F> static void rejects(F fn,const char* message) {bool threw=false;try{fn();}catch(...){threw=true;}require(threw,message);}
static void waitTask() {
    auto start=SDL_GetTicks();while(status().busy && SDL_GetTicks()-start<5000) SDL_Delay(10);
    require(!status().busy,"Background task timeout");
}
static void key(SDL_Keycode code) {SDL_Event event{};event.type=SDL_KEYDOWN;event.key.keysym.sym=code;serviceui::event(event,false);}
int main(int argc,char** argv) {
    fs::path temporary;
    try {
        require(sha256({})=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA256 empty vector");
        require(sha256({'a','b','c'})=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 abc");
        require(sha256(std::vector<uint8_t>(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA256 million a");
        std::vector<uint8_t> binary;for(int i=0;i<256;i++)binary.push_back(uint8_t(i));
        require(unbase64(base64(binary))==binary,"binary base64 roundtrip");
        for(auto bad:{"=AAA","AA=A","A===","AB==","AA!A","AA"}) rejects([&](){unbase64(bad);},"invalid base64 accepted");
        require(newerVersion("v1.10.0","1.2.9"),"numeric version ordering");
        require(!newerVersion("v1.0.0",Version),"downgrade accepted");
        rejects([](){newerVersion("v2.0.0-beta","1.0.0");},"prerelease version accepted");
        auto release=releaseJson();
        require(parseRelease(release.dump(),platformName()).notes.find("quotes")!=std::string::npos,"escaped release JSON");
        release["assets"][0]["browser_download_url"]="https://example.org/evil.zip";
        rejects([&](){parseRelease(release.dump(),platformName());},"foreign release asset accepted");
        for(auto bad:{"../cloud_auth.json","internal/../services.json","external/../../x","internal/C:/x","internal/xx\\x","internal/.hidden","internal/","data/dict.bin"})
            require(!safeSavePath(bad),"unsafe save path accepted");
        SaveFiles original={{"internal/kickass",binary},{"internal/LOCAL.sav",{'a',0,'b'}},{"external/slot/data",{0,1,2}}};
        auto package=packSaves(original,"Windows");
        require(unpackSaves(package)==original,"cross-platform binary save roundtrip");
        auto badPackage=json::parse(package);badPackage["app"]="heroes_lore_zero";
        rejects([&](){unpackSaves(badPackage.dump());},"different game backup accepted");
        badPackage=json::parse(package);badPackage["files"]["internal/LOCAL.sav"]["data"]="AAAA";
        rejects([&](){unpackSaves(badPackage.dump());},"damaged backup accepted");
        require(gameKey(SDLK_c)=='#' && gameKey(SDLK_TAB)=='#',"keyboard skill row mapping");
        require(gamepadKey(SDL_CONTROLLER_BUTTON_BACK,false)=='#',"Select mapping");
        require(gamepadKey(SDL_CONTROLLER_BUTTON_LEFTSTICK,true)=='0',"L3 map mapping");
        require(gamepadKey(SDL_CONTROLLER_BUTTON_B,true)==WK_SELECT,"Nintendo physical A attacks");
        require(gamepadKey(SDL_CONTROLLER_BUTTON_A,true)==WK_CLEAR,"Nintendo physical B cancels");
        require(gamepadKey(SDL_CONTROLLER_BUTTON_Y,true)=='1' && gamepadKey(SDL_CONTROLLER_BUTTON_X,true)=='3',"Nintendo X/Y swap");
        require(gamepadKey(SDL_CONTROLLER_BUTTON_A,false)==WK_SELECT,"Xbox mapping retained");

        temporary=fs::temp_directory_path()/("hl5-services-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(temporary/"internal");fs::create_directories(temporary/"external");
        writeText((temporary/"internal/old-save").string(),"previous");
        writeText((temporary/"cloud_auth.json").string(),"KEEP");
        auto backup=restoreSaves(temporary.string(),original);
        require(readSaves(temporary.string())==original,"restored files mismatch");
        require(!fs::exists(temporary/"internal/old-save") && readText(backup+"/internal/old-save")=="previous","old slot not isolated in safety backup");
        require(readText((temporary/"cloud_auth.json").string())=="KEEP","restore changed auth token");
        rejects([&](){restoreSaves(temporary.string(),{{"../escape",{1,2,3}}});},"restore accepted traversal");
        require(readSaves(temporary.string())==original,"rejected restore changed saves");

        SDL_setenv("SDL_VIDEODRIVER","dummy",1);require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)==0,"SDL init failed");
        writeText((temporary/"services.json").string(),"{\"check_updates\":false}");
        writeText((temporary/"cloud_auth.json").string(),"{\"refresh_token\":\"fake\",\"access_token\":\"fake\",\"expires_at\":9999999999}");
        init(temporary.string(),"not-launched",argc>1?argv[1]:"assets");
        remotePackage=package;
        refreshBackup();waitTask();require(status().backupExists,"Drive backup query");
        serviceui::init(resourcePath("ui_font.ttf"),[](){},[](){return AspectMode::Auto;});
        auto* window=SDL_CreateWindow("Test",0,0,960,720,SDL_WINDOW_HIDDEN);
        auto* renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
        require(renderer!=nullptr,"SDL test renderer");
        serviceui::beginGuestFrame();serviceui::notifyGameSettings();serviceui::endGuestFrame();
        require(serviceui::active(),"Native Settings did not open host options");
        for(int i=0;i<4;i++) {key(SDLK_DOWN);}
        key(SDLK_RETURN);waitTask(); // Cloud page
        key(SDLK_DOWN);key(SDLK_RETURN); // Upload confirmation
        key(SDLK_DOWN);key(SDLK_RETURN); // Cancel
        waitTask();require(uploads==0,"Cancel confirmation uploaded saves");
        serviceui::draw(renderer,960,720);
        auto capture=fs::path("build/services-verification");fs::create_directories(capture);
        auto saveImage=[&](int w,int h,const char* name) {
            SDL_SetWindowSize(window,w,h);SDL_RenderClear(renderer);serviceui::draw(renderer,w,h);
            auto* surface=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
            require(SDL_RenderReadPixels(renderer,nullptr,surface->format->format,surface->pixels,surface->pitch)==0,"UI capture failed");
            SDL_SaveBMP(surface,(capture/name).string().c_str());SDL_FreeSurface(surface);
        };
        saveImage(960,720,"cloud-landscape.bmp");saveImage(480,800,"cloud-portrait.bmp");
        uploadBackup();waitTask();require(uploads==1,"Drive upload route");
        checkUpdate(true);waitTask();require(status().updateAvailable,"release update not found");
        corruptDownload=true;downloadUpdate();waitTask();require(!status().downloadReady,"corrupt update accepted");
        corruptDownload=false;downloadUpdate();waitTask();require(status().downloadReady,"valid download rejected");
        restoreBackup();waitTask();require(status().restartRequired,"restore must require restart");
        serviceui::draw(renderer,480,800);saveImage(480,800,"restore-complete.bmp");
        require(readSaves(temporary.string())==original,"Drive restore changed file content");
        serviceui::shutdown();shutdown();SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
        // This directory was freshly created by this test, never a player data root.
        require(fs::canonical(temporary).parent_path()==fs::canonical(fs::temp_directory_path()) && temporary.filename().string().rfind("hl5-services-",0)==0,"Unexpected cleanup path");
        fs::remove_all(temporary);
        std::cout<<"PASS: save isolation/checksums/restore; mocked Drive upload/restore; update validation; settings cancel; input mapping.\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    return 0;
}
