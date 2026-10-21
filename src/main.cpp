// HL5 recomp - Windows host entry point (SDL2). Milestone 0 bootstrap: ARM interpreter + HLE.
#include "hle/hle.h"
#include "display_layout.h"
#include "input_mapping.h"
#include "services.h"
#include "service_ui.h"
#include "localization.h"
#include <SDL.h>
#include "audio/audio.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifdef __ANDROID__
#include <SDL_system.h>
#include <unistd.h>
#include <android/log.h>
#include <android/asset_manager_jni.h>
#define LOGI(...) do { __android_log_print(ANDROID_LOG_INFO, "HL5", __VA_ARGS__); fprintf(stderr, __VA_ARGS__); } while(0)
#define LOGE(...) do { __android_log_print(ANDROID_LOG_ERROR, "HL5", __VA_ARGS__); fprintf(stderr, __VA_ARGS__); } while(0)

// SDL_RWFromFile intentionally prefers internal storage over APK assets. Using
// it for an OTA refresh can open dst as src, then truncate that same file.
// Read only AAssetManager; validate a staged copy before replacing extracted data.
struct ApkAssets {
    JNIEnv* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity=nullptr,object=nullptr;AAssetManager* manager=nullptr;
    ApkAssets(){
        if(!env)return;
        activity=static_cast<jobject>(SDL_AndroidGetActivity());if(!activity)return;
        auto cls=env->GetObjectClass(activity);
        auto method=env->GetMethodID(cls,"getAssets","()Landroid/content/res/AssetManager;");
        if(method)object=env->CallObjectMethod(activity,method);
        if(env->ExceptionCheck()){env->ExceptionClear();object=nullptr;}
        if(object)manager=AAssetManager_fromJava(env,object);
        env->DeleteLocalRef(cls);
    }
    ~ApkAssets(){if(env){if(object)env->DeleteLocalRef(object);if(activity)env->DeleteLocalRef(activity);}}
};
static void ensureAndroidAssets(const std::string& storageDir) {
    ApkAssets apk;
    if(!apk.manager){LOGE("[android] Cannot access APK assets\n");return;}
    AAsset* mf=AAssetManager_open(apk.manager,"assets_list.txt",AASSET_MODE_STREAMING);
    if(!mf){LOGE("[android] APK asset manifest missing\n");return;}
    std::string content;char chunk[8192];int n=0;
    while((n=AAsset_read(mf,chunk,sizeof(chunk)))>0)content.append(chunk,n);
    AAsset_close(mf);
    if(n<0 || content.empty()){LOGE("[android] APK asset manifest unreadable\n");return;}

    auto manifestPath=storageDir+"/assets_manifest.sha256";
    auto fingerprint=services::sha256(std::vector<uint8_t>(content.begin(),content.end()));
    bool manifestUnchanged=false;
    try {manifestUnchanged=services::readText(manifestPath,128)==fingerprint;} catch(...) {}
    bool extractionFailed=false;
    std::istringstream iss(content);
    std::string line;
    int extracted = 0, skipped = 0;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.empty()) continue;
        auto sep = line.rfind(':');
        if (sep == std::string::npos) continue;
        std::string digest;
        std::string sizeText=line.substr(sep+1);
        if(sizeText.size()==64) {
            digest=sizeText;line=line.substr(0,sep);sep=line.rfind(':');
            if(sep==std::string::npos) continue;
            sizeText=line.substr(sep+1);
        }
        std::string relPath = line.substr(0, sep);
        size_t expectedSize = (size_t)std::stoull(sizeText);

        std::string dst = storageDir + "/" + relPath;
        std::error_code ec;
        if (std::filesystem::exists(dst, ec)) {
            auto curSize = std::filesystem::file_size(dst, ec);
            if (!ec && curSize == expectedSize) {
                bool current=manifestUnchanged || digest.empty();
                if(!current) {
                    try {auto bytes=services::readText(dst,128*1024*1024);current=services::sha256(std::vector<uint8_t>(bytes.begin(),bytes.end()))==digest;} catch(...) {}
                }
                if(current) {skipped++;continue;}
            }
        }

        AAsset* src=AAssetManager_open(apk.manager,relPath.c_str(),AASSET_MODE_STREAMING);
        if(!src){LOGE("[android] APK asset missing: %s\n",relPath.c_str());extractionFailed=true;continue;}
        bool ok=services::extractAsset(dst,expectedSize,digest,[&](char* buffer,int size){return AAsset_read(src,buffer,size);});
        AAsset_close(src);
        if(!ok){extractionFailed=true;continue;}
        extracted++;
    }
    if(!extractionFailed) {try {services::writeText(manifestPath,fingerprint);} catch(...) {}}
    LOGI("[android] Assets check done: %d extracted, %d skipped\n", extracted, skipped);
}
#else
#define LOGI(...) fprintf(stderr, __VA_ARGS__)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

static void dumpBmp(const std::string& path, const Screen& s) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f || s.w <= 0) return;
    u32 sz = 54 + s.w * s.h * 4;
    u8 hdr[54] = {'B', 'M'};
    auto p32 = [&](int o, u32 v) { memcpy(hdr + o, &v, 4); };
    p32(2, sz); p32(10, 54); p32(14, 40); p32(18, s.w); p32(22, (u32)(-s.h)); hdr[26] = 1; hdr[28] = 32; p32(34, s.w * s.h * 4);
    fwrite(hdr, 1, 54, f);
    fwrite(s.px.data(), 4, s.px.size(), f);
    fclose(f);
}

#ifdef _WIN32
#include <windows.h>
static std::string getExeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::string s(buf);
    size_t pos = s.find_last_of("\\/");
    return (pos != std::string::npos) ? s.substr(0, pos) : ".";
}
#elif defined(__linux__)
#include <unistd.h>
static std::string getExeDir() {
    char buf[1024] = {0};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        std::string s(buf, len);
        size_t pos = s.find_last_of("/");
        return (pos != std::string::npos) ? s.substr(0, pos) : ".";
    }
    return ".";
}
#else
static std::string getExeDir() { return "."; }
#endif

struct DirectionManager {
    // 0=UP, 1=DOWN, 2=LEFT, 3=RIGHT
    bool held[4] = {false, false, false, false};
    int lastOrder[4] = {0, 0, 0, 0};
    int orderCounter = 0;

    int stickX = 0;
    int stickY = 0;
    int stickDir = 0; // 0=none, or WK_UP, WK_DOWN, WK_LEFT, WK_RIGHT

    int engineActiveDir = 0;

    static int keyToDirIdx(int k) {
        if (k == WK_UP) return 0;
        if (k == WK_DOWN) return 1;
        if (k == WK_LEFT) return 2;
        if (k == WK_RIGHT) return 3;
        return -1;
    }

    static int dirIdxToKey(int idx) {
        switch (idx) {
            case 0: return WK_UP;
            case 1: return WK_DOWN;
            case 2: return WK_LEFT;
            case 3: return WK_RIGHT;
            default: return 0;
        }
    }

    void onButton(int k, bool down) {
        int idx = keyToDirIdx(k);
        if (idx >= 0) {
            held[idx] = down;
            if (down) lastOrder[idx] = ++orderCounter;
        }
    }

    void onAnalog(int axis, int val) {
        if (axis == SDL_CONTROLLER_AXIS_LEFTX) stickX = val;
        if (axis == SDL_CONTROLLER_AXIS_LEFTY) stickY = val;

        int ax = std::abs(stickX);
        int ay = std::abs(stickY);

        if (ax < 8000 && ay < 8000) {
            stickDir = 0;
            return;
        }

        if (stickDir != 0 || ax > 12000 || ay > 12000) {
            if (ax > ay + 4000) {
                stickDir = (stickX > 0) ? WK_RIGHT : WK_LEFT;
            } else if (ay > ax + 4000) {
                stickDir = (stickY > 0) ? WK_DOWN : WK_UP;
            } else if (stickDir == 0) {
                if (ax >= ay) stickDir = (stickX > 0) ? WK_RIGHT : WK_LEFT;
                else stickDir = (stickY > 0) ? WK_DOWN : WK_UP;
            }
        }
    }

    int resolveDesiredDir() const {
        int bestIdx = -1;
        int bestOrder = -1;
        for (int i = 0; i < 4; i++) {
            if (held[i] && lastOrder[i] > bestOrder) {
                bestOrder = lastOrder[i];
                bestIdx = i;
            }
        }
        if (bestIdx >= 0) return dirIdxToKey(bestIdx);
        if (stickDir != 0) return stickDir;
        return 0;
    }

    void update(Game& game) {
        int desired = resolveDesiredDir();
        if (desired != engineActiveDir) {
            if (desired == 0) {
                // All direction inputs released: stop movement and enter stand animation
                if (engineActiveDir != 0) {
                    game.key(EV_KEY_RELEASE, engineActiveDir);
                }
            } else {
                // Direction changed or movement started:
                // DO NOT send EV_KEY_RELEASE when switching direction, as that breaks walk motion!
                game.key(EV_KEY_PRESS, desired);
            }
            engineActiveDir = desired;
        }
    }
};

#ifdef __SWITCH__
static FILE* s_bootLog = nullptr;
#endif

extern bool g_prof; extern std::vector<u32> g_profHist;
int main(int argc, char** argv) {
    if (getenv("HL5_PROF")) g_prof = true;
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/heroes_lore_5", 0777);
    mkdir("sdmc:/switch/heroes_lore_5/data", 0777);
    mkdir("sdmc:/switch/heroes_lore_5/data/internal", 0777);
    mkdir("sdmc:/switch/heroes_lore_5/data/external", 0777);
    s_bootLog = fopen("sdmc:/switch/heroes_lore_5/boot.log", "w");
    if (!s_bootLog) s_bootLog = fopen("sdmc:/heroes_lore_boot.log", "w");
    if (s_bootLog) {
        dup2(fileno(s_bootLog), STDOUT_FILENO);
        dup2(fileno(s_bootLog), STDERR_FILENO);
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
    fprintf(stderr, "[Switch] Booting Heroes Lore 5...\n");
    Result rc = romfsInit();
    if (R_FAILED(rc)) {
        fprintf(stderr, "[Switch] romfsInit failed: 0x%x\n", rc);
    } else {
        fprintf(stderr, "[Switch] romfsInit OK\n");
    }
#endif

    std::string exeDir = getExeDir();
    std::string apk = "reference/extracted_apk", data = "data", dumpDir;

#ifdef __SWITCH__
    apk = "romfs:/";
    data = "sdmc:/switch/heroes_lore_5/data";
#elif defined(__ANDROID__)
    const char* internalStorage = SDL_AndroidGetInternalStoragePath();
    std::string baseStorage = (internalStorage && internalStorage[0] != '\0') ? internalStorage : ".";
    std::string logPath = baseStorage + "/boot.log";
    static FILE* s_androidLog = fopen(logPath.c_str(), "w");
    if (s_androidLog) {
        dup2(fileno(s_androidLog), STDOUT_FILENO);
        dup2(fileno(s_androidLog), STDERR_FILENO);
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
    LOGI("[Android] baseStorage: %s\n", baseStorage.c_str());
    ensureAndroidAssets(baseStorage);
    apk = baseStorage;
    data = baseStorage + "/data";
    std::error_code ec;
    std::filesystem::create_directories(data, ec);
#else
    if (!std::filesystem::exists(apk)) {
        if (std::filesystem::exists(exeDir + "/libHeroesLore5.so")) apk = exeDir;
        else if (std::filesystem::exists(exeDir + "/reference/extracted_apk")) apk = exeDir + "/reference/extracted_apk";
        else if (std::filesystem::exists("../reference/extracted_apk")) apk = "../reference/extracted_apk";
    }
    if (!std::filesystem::exists(data)) {
        if (std::filesystem::exists(exeDir + "/data")) data = exeDir + "/data";
        else if (std::filesystem::exists("../data")) data = "../data";
    }
#endif
    int frames = -1, dumpEvery = 0, lw = 240, lh = 400;
    bool headless = false, verbose = false;
    int availableW=0,availableH=0,settingsAt=-1,titleAt=-1;
    std::string dumpUi,languageOverride;
    std::map<int,std::string> scriptLanguages;
    std::map<int,int> scriptMenus;
    std::map<int,std::pair<int,int>> scriptTouches;
    AspectMode requestedMode=AspectMode::Native;
    bool hasModeOverride=false;
    std::map<int,AspectMode> scriptModes;
    std::map<int,std::pair<int,int>> scriptWindows;
    std::map<int, int> scriptKeys;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--apk" && i + 1 < argc) apk = argv[++i];
        else if (a == "--data" && i + 1 < argc) data = argv[++i];
        else if (a == "--frames" && i + 1 < argc) frames = atoi(argv[++i]);
        else if (a == "--dump" && i + 1 < argc) dumpDir = argv[++i];
        else if (a == "--dump-every" && i + 1 < argc) dumpEvery = atoi(argv[++i]);
        else if (a == "--lcd" && i + 1 < argc) sscanf(argv[++i], "%dx%d", &lw, &lh);
        else if(a=="--aspect" && i+1<argc) {
            hasModeOverride=true;
            if(!parseAspectMode(argv[++i],requestedMode)) { LOGE("Invalid --aspect (native|wide|auto)\n");return 2; }
        } else if(a=="--aspect-at" && i+1<argc) {
            int frame=0;char mode[16]={};AspectMode parsed;
            if(sscanf(argv[++i],"%d:%15s",&frame,mode)!=2 || frame<0 || !parseAspectMode(mode,parsed)) return 2;
            scriptModes[frame]=parsed;
        } else if(a=="--window" && i+1<argc) {
            if(sscanf(argv[++i],"%dx%d",&availableW,&availableH)!=2 || availableW<1 || availableH<1) return 2;
        } else if(a=="--window-at" && i+1<argc) {
            int frame=0,w=0,h=0;
            if(sscanf(argv[++i],"%d:%dx%d",&frame,&w,&h)!=3 || frame<0 || w<1 || h<1) return 2;
            scriptWindows[frame]={w,h};
        }
        else if(a=="--game-menu-at" && i+1<argc) {
            int frame=0,menu=0;if(sscanf(argv[++i],"%d:%d",&frame,&menu)!=2 || frame<0 || menu< -1 || menu>20)return 2;scriptMenus[frame]=menu;
        }
        else if(a=="--touch-at" && i+1<argc) {
            int frame=0,x=0,y=0;if(sscanf(argv[++i],"%d:%d:%d",&frame,&x,&y)!=3 || frame<0)return 2;
            scriptTouches[frame]={x,y};
        }
        else if(a=="--language" && i+1<argc) languageOverride=argv[++i];
        else if(a=="--language-at" && i+1<argc) {
            int frame=0;char language[16]={};if(sscanf(argv[++i],"%d:%15s",&frame,language)!=2)return 2;scriptLanguages[frame]=language;
        }
        else if(a=="--return-title-at" && i+1<argc) titleAt=atoi(argv[++i]);
        else if(a=="--settings-at" && i+1<argc) settingsAt=atoi(argv[++i]);
        else if(a=="--dump-ui" && i+1<argc) dumpUi=argv[++i];
        else if (a == "--headless") headless = true;
        else if (a == "--verbose") verbose = true;
        else if (a == "--press-at" && i + 1 < argc) {
            int kf = 0, kk = 0;
            if (sscanf(argv[++i], "%d:%d", &kf, &kk) == 2) scriptKeys[kf] = kk;
        }
    }
    if (!dumpDir.empty()) {
        std::filesystem::create_directories(dumpDir);
        dumpDir = std::filesystem::absolute(dumpDir).generic_string();
    }
    if(lw<1 || lh<1 || lw>1280 || lh>1280) { LOGE("Invalid native canvas size\n");return 2; }
    const std::string displaySettings=data+"/display.ini";
    AspectMode aspectMode=hasModeOverride?requestedMode:loadAspectMode(displaySettings);
    SDL_Window* win=nullptr;SDL_Renderer* ren=nullptr;SDL_Texture* tex=nullptr;
    auto bootStart = std::chrono::steady_clock::now();
    SDL_SetHint("SDL_TOUCH_MOUSE_EVENTS","0");
    SDL_SetHint("SDL_MOUSE_TOUCH_EVENTS","0");
    if (!headless || getenv("HL5_AUDIO_TEST")) Audio::init();
    if(!headless) {
        if(SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_GAMECONTROLLER)!=0) {
            LOGE("[SDL] Video init failed: %s\n",SDL_GetError());Audio::shutdown();return 1;
        }
#ifdef __SWITCH__
        win=SDL_CreateWindow("Heroes Lore 5: Covenant of Darkness",0,0,1280,720,0);
#elif defined(__ANDROID__)
        // Allow SDLActivity to follow device rotation and report the new surface size.
        win=SDL_CreateWindow("Heroes Lore 5: Covenant of Darkness",0,0,0,0,SDL_WINDOW_FULLSCREEN|SDL_WINDOW_RESIZABLE);
#else
        int windowW=availableW>0?availableW:(aspectMode==AspectMode::Native?lw*2:960);
        int windowH=availableH>0?availableH:(aspectMode==AspectMode::Native?lh*2:540);
        win=SDL_CreateWindow("Heroes Lore 5: Covenant of Darkness",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
                             windowW,windowH,SDL_WINDOW_RESIZABLE);
#endif
        if(!win) { LOGE("[SDL] Window creation failed: %s\n",SDL_GetError());Audio::shutdown();SDL_Quit();return 1; }
        SDL_GetWindowSize(win,&availableW,&availableH);
    }
    if(availableW<1 || availableH<1) {availableW=1280;availableH=720;}
    std::string resourceRoot="assets";
    if(!std::filesystem::exists(resourceRoot+"/ui_font.ttf")) {
        if(std::filesystem::exists(exeDir+"/assets/ui_font.ttf")) resourceRoot=exeDir+"/assets";
        else resourceRoot=apk;
    }
    Locale::init(resourceRoot,data,languageOverride);
    const auto initialViewport=viewportForAspect(aspectMode,availableW,availableH,lw,lh);
    LOGI("[video] mode=%s available=%dx%d viewport=%dx%d\n",aspectModeName(aspectMode),availableW,availableH,initialViewport.width,initialViewport.height);
    static Game game;
    game.verbose = verbose;
    LOGI("[Init] Calling Game::boot(apk=%s, data=%s)...\n", apk.c_str(), data.c_str());
    if (!game.boot(apk, data, initialViewport.width, initialViewport.height)) {
        LOGE("[Init] Game::boot failed!\n");
        return 1;
    }
    LOGI("[Init] Game::boot OK, screen size: %dx%d\n", game.screen.w, game.screen.h);

    LOGI("[bench] boot_ms=%.3f\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-bootStart).count());

    std::vector<SDL_GameController*> controllers;
    DirectionManager dirMgr;
    bool isFullscreen = false;
    bool rendererVsync = false;
    AspectChord aspectChord;
    bool viewportDirty=false,gamePointerHeld=false;
    int touchX=0,touchY=0;
    Uint32 resizeSince=0;

    auto openController = [&](int id) {
        if (SDL_IsGameController(id)) {
            SDL_GameController* c = SDL_GameControllerOpen(id);
            if (c) {
                controllers.push_back(c);
                fprintf(stderr, "[gamepad] Connected controller: %s\n", SDL_GameControllerName(c));
            }
        }
    };

    int W = game.screen.w ? game.screen.w : lw, H = game.screen.h ? game.screen.h : lh;

    auto computeDstRect = [&](int winW, int winH) -> SDL_Rect {
        SDL_Rect r = { 0, 0, winW, winH };
        if (W > 0 && H > 0) {
            float scale = std::min((float)winW / W, (float)winH / H);
            r.w = (int)(W * scale);
            r.h = (int)(H * scale);
            r.x = (winW - r.w) / 2;
            r.y = (winH - r.h) / 2;
        }
        return r;
    };

    auto selectMode=[&](AspectMode mode,bool persist) {
        aspectMode=mode;viewportDirty=true;resizeSince=0;
        LOGI("[video] mode=%s\n",aspectModeName(mode));
        if(win) {
#if !defined(__SWITCH__) && !defined(__ANDROID__)
            int ww=0,wh=0;SDL_GetWindowSize(win,&ww,&wh);
            if(mode==AspectMode::Wide && !isFullscreen && ww<wh) SDL_SetWindowSize(win,960,540);
#endif
            std::string title=std::string("Heroes Lore 5 - ")+aspectModeName(mode);
            SDL_SetWindowTitle(win,title.c_str());
        }
        if(persist && !saveAspectMode(displaySettings,mode)) LOGE("[video] Could not save display preference\n");
    };
    auto recreateTexture=[&]() {
        if(tex) {SDL_DestroyTexture(tex);tex=nullptr;}
        tex=SDL_CreateTexture(ren,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,W,H);
        if(!tex) tex=SDL_CreateTexture(ren,SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_STREAMING,W,H);
        if(!tex) LOGE("[SDL] Texture creation failed: %s\n",SDL_GetError());
        return tex!=nullptr;
    };

    auto handlePointer = [&](int eventType, int mx, int my) {
        int winW = 0, winH = 0;
        SDL_GetWindowSize(win, &winW, &winH);
        SDL_Rect dst = computeDstRect(winW, winH);
        if (dst.w <= 0 || dst.h <= 0) return;

        float lx = (float)(mx - dst.x) * W / (float)dst.w;
        float ly = (float)(my - dst.y) * H / (float)dst.h;

        if (lx >= 0 && lx < W && ly >= 0 && ly < H) {
            touchX=(int)lx;touchY=(int)ly;
            if(eventType==EV_PTR_PRESS) gamePointerHeld=true;
            if(eventType==EV_PTR_RELEASE) gamePointerHeld=false;
            game.touch(eventType, touchX, touchY, 0);
        } else if (eventType == EV_PTR_RELEASE) {
            int clx = std::clamp((int)lx, 0, W - 1);
            int cly = std::clamp((int)ly, 0, H - 1);
            gamePointerHeld=false;
            game.touch(EV_PTR_RELEASE, clx, cly, 0);
        } else if (eventType == EV_PTR_MOVE) {
            int clx = std::clamp((int)lx, 0, W - 1);
            int cly = std::clamp((int)ly, 0, H - 1);
            game.touch(EV_PTR_MOVE, clx, cly, 0);
        }
    };

    if(!headless) {
        std::string executablePath=exeDir+"/hl5.exe";
#if defined(_WIN32)
        char modulePath[32768]={};GetModuleFileNameA(nullptr,modulePath,sizeof(modulePath));executablePath=modulePath;
#elif defined(__SWITCH__)
        executablePath=(argc>0 && argv[0])?argv[0]:"";
        if(executablePath.rfind("/",0)==0) executablePath="sdmc:"+executablePath;
#else
        executablePath=exeDir+"/hl5";
#endif
        services::init(data,executablePath,resourceRoot);
        serviceui::init(services::resourcePath("ui_font.ttf"),[&](){selectMode(nextAspectMode(aspectMode),true);},[&](){return aspectMode;});
    }
    bool wasModal=false;
    if (!headless) {
        int curWinW = 0, curWinH = 0;
        SDL_GetWindowSize(win, &curWinW, &curWinH);
        LOGI("[SDL] Window created (%dx%d)\n", curWinW, curWinH);
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren) {
            LOGE("[SDL] Accelerated renderer failed: %s, falling back to software\n", SDL_GetError());
            ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        }
        if (!ren) {
            LOGE("[SDL] SDL_CreateRenderer failed: %s\n", SDL_GetError());
            return 1;
        }
        SDL_RendererInfo rendererInfo{};
        if(SDL_GetRendererInfo(ren,&rendererInfo)==0) rendererVsync=(rendererInfo.flags & SDL_RENDERER_PRESENTVSYNC)!=0;
        LOGI("[SDL] Renderer created successfully (vsync=%d)\n",rendererVsync);
        if(!recreateTexture()) {Audio::shutdown();SDL_Quit();return 1;}
        LOGI("[SDL] Texture created (%dx%d)\n", W, H);

        for (int i = 0; i < SDL_NumJoysticks(); i++) openController(i);
    }
    const bool benchmark = headless || getenv("HL5_BENCH") != nullptr;
    std::vector<double> frameTimes;
    auto t0 = std::chrono::steady_clock::now();
#ifdef __SWITCH__
    for (int f = 0; appletMainLoop() && !game.quit && (frames < 0 || f < frames); f++) {
#else
    for (int f = 0; !game.quit && (frames < 0 || f < frames); f++) {
#endif
        auto frameStart = std::chrono::steady_clock::now();
        if(!headless){services::pollStartup(SDL_GetTicks64());serviceui::poll();}
        if(!headless && f==settingsAt) serviceui::open();
        if (!headless) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                bool padNintendo=false;
                if(e.type==SDL_CONTROLLERBUTTONDOWN || e.type==SDL_CONTROLLERBUTTONUP) padNintendo=nintendoController(e.cbutton.which);
                // Window/controller lifecycle events must still reach SDL host management.
                if(e.type!=SDL_QUIT && e.type!=SDL_WINDOWEVENT && e.type!=SDL_CONTROLLERDEVICEADDED && e.type!=SDL_CONTROLLERDEVICEREMOVED) {
                    if(serviceui::event(e,padNintendo)) continue;
                }
                if (e.type == SDL_QUIT) {
                    game.quit = true;
                } else if(e.type==SDL_WINDOWEVENT) {
                    if(e.window.event==SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event==SDL_WINDOWEVENT_RESIZED) {
                        if(aspectMode==AspectMode::Auto) {viewportDirty=true;resizeSince=SDL_GetTicks();}
                    } else if(e.window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
                        aspectChord.reset();
                    }
                } else if (e.type == SDL_CONTROLLERDEVICEADDED) {
                    openController(e.cdevice.which);
                } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                    aspectChord.reset();
                    SDL_JoystickID jid = e.cdevice.which;
                    for (auto it = controllers.begin(); it != controllers.end(); ) {
                        SDL_Joystick* j = SDL_GameControllerGetJoystick(*it);
                        if (j && SDL_JoystickInstanceID(j) == jid) {
                            fprintf(stderr, "[gamepad] Disconnected controller: %s\n", SDL_GameControllerName(*it));
                            SDL_GameControllerClose(*it);
                            it = controllers.erase(it);
                        } else {
                            ++it;
                        }
                    }
                } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                    auto chord=aspectChord.button(e.cbutton.button,true);
                    if(chord!=AspectChord::NotHandled) {
                        if(chord==AspectChord::Cycle) selectMode(nextAspectMode(aspectMode),true);
                        continue;
                    }
                    int k = gamepadKey(e.cbutton.button,nintendoController(e.cbutton.which));
                    if (k) {
                        if (DirectionManager::keyToDirIdx(k) >= 0) dirMgr.onButton(k, true);
                        else game.key(EV_KEY_PRESS, k);
                    }
                } else if (e.type == SDL_CONTROLLERBUTTONUP) {
                    auto chord=aspectChord.button(e.cbutton.button,false);
                    if(chord!=AspectChord::NotHandled) {
                        if(chord==AspectChord::SwapSkills) game.key(EV_KEY_PRESS,'#');
                        else if(chord==AspectChord::Menu) game.key(EV_KEY_PRESS,WK_CLEAR);
                        continue;
                    }
                    int k = gamepadKey(e.cbutton.button,nintendoController(e.cbutton.which));
                    if (k) {
                        if (DirectionManager::keyToDirIdx(k) >= 0) dirMgr.onButton(k, false);
                    }
                } else if (e.type == SDL_CONTROLLERAXISMOTION) {
                    if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                        dirMgr.onAnalog(e.caxis.axis, e.caxis.value);
                    }
                } else if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_F11 || (e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                        isFullscreen = !isFullscreen;
                        SDL_SetWindowFullscreen(win, isFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        if(aspectMode==AspectMode::Auto) {viewportDirty=true;resizeSince=0;}
                    } else if (e.key.keysym.sym == SDLK_F9) {
                        if(!e.key.repeat) selectMode(nextAspectMode(aspectMode),true);
                    } else {
                        int k = gameKey(e.key.keysym.sym);
                        if (k) {
                            if (DirectionManager::keyToDirIdx(k) >= 0) dirMgr.onButton(k, true);
                            else if (!e.key.repeat) game.key(EV_KEY_PRESS, k);
                        }
                    }
                } else if (e.type == SDL_KEYUP) {
                    int k = gameKey(e.key.keysym.sym);
                    if (k) {
                        if (DirectionManager::keyToDirIdx(k) >= 0) dirMgr.onButton(k, false);
                    }
                } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                    handlePointer(EV_PTR_PRESS, e.button.x, e.button.y);
                } else if (e.type == SDL_MOUSEBUTTONUP) {
                    handlePointer(EV_PTR_RELEASE, e.button.x, e.button.y);
                } else if (e.type == SDL_MOUSEMOTION && e.motion.state) {
                    handlePointer(EV_PTR_MOVE, e.motion.x, e.motion.y);
                } else if (e.type == SDL_FINGERDOWN) {
                    int winW = 0, winH = 0; SDL_GetWindowSize(win, &winW, &winH);
                    handlePointer(EV_PTR_PRESS, (int)(e.tfinger.x * winW), (int)(e.tfinger.y * winH));
                } else if (e.type == SDL_FINGERUP) {
                    int winW = 0, winH = 0; SDL_GetWindowSize(win, &winW, &winH);
                    handlePointer(EV_PTR_RELEASE, (int)(e.tfinger.x * winW), (int)(e.tfinger.y * winH));
                } else if (e.type == SDL_FINGERMOTION) {
                    int winW = 0, winH = 0; SDL_GetWindowSize(win, &winW, &winH);
                    handlePointer(EV_PTR_MOVE, (int)(e.tfinger.x * winW), (int)(e.tfinger.y * winH));
                }
            }
            if(!serviceui::active()) dirMgr.update(game);
        }
        if(scriptModes.count(f)) selectMode(scriptModes[f],false);
        if(scriptWindows.count(f)) {
            availableW=scriptWindows[f].first;availableH=scriptWindows[f].second;
            if(win) SDL_SetWindowSize(win,availableW,availableH);
            if(aspectMode==AspectMode::Auto) {viewportDirty=true;resizeSince=0;}
        }
        if(viewportDirty && (!resizeSince || SDL_GetTicks()-resizeSince>=120)) {
            if(win) SDL_GetWindowSize(win,&availableW,&availableH);
            auto target=viewportForAspect(aspectMode,availableW,availableH,lw,lh);
            if(target.width!=W || target.height!=H) {
                if(gamePointerHeld) {game.touch(EV_PTR_RELEASE,touchX,touchY,0);gamePointerHeld=false;}
                if(!game.resizeViewport(target.width,target.height)) {LOGE("[video] Viewport resize failed\n");game.quit=true;}
                W=game.screen.w;H=game.screen.h;
                if(ren && !recreateTexture()) game.quit=true;
                LOGI("[video] viewport=%dx%d\n",W,H);
            }
            viewportDirty=false;
        }
        if (scriptKeys.count(f)) {
            int k = scriptKeys[f];
            fprintf(stderr, "[script] Injecting key press %d at frame %d\n", k, f);
            if(serviceui::active()) {
                SDL_Event e{};e.type=SDL_KEYDOWN;
                e.key.keysym.sym=k==WK_CLEAR?SDLK_ESCAPE:k==WK_UP?SDLK_UP:k==WK_DOWN?SDLK_DOWN:k==WK_SELECT?SDLK_RETURN:SDLK_UNKNOWN;
                serviceui::event(e,false);
            } else game.key(EV_KEY_PRESS, k);
        }
        if (scriptKeys.count(f - 2)) {
            int k = scriptKeys[f - 2];
            if(!serviceui::active())game.key(EV_KEY_RELEASE, k);
        }
        if(scriptMenus.count(f) && scriptMenus[f]>=0) {
            if(gm::r32(0x1428bc)!=0xe59f3020)return 1;
            game.cpu.call(0x1428bc,{0,3,u32(scriptMenus[f])});
        }
        if(scriptTouches.count(f))game.touch(EV_PTR_PRESS,scriptTouches[f].first,scriptTouches[f].second,0);
        if(scriptTouches.count(f-2))game.touch(EV_PTR_RELEASE,scriptTouches[f-2].first,scriptTouches[f-2].second,0);
        if(scriptLanguages.count(f)) Locale::setLanguage(scriptLanguages[f],false);
        if(f==titleAt && !game.returnToTitle()) return 1;
        auto serviceStatus=(!headless)?services::status():services::Status{};
        if(!headless && serviceStatus.quitRequested) break;
        if(!headless && serviceStatus.restoreReady && !serviceStatus.busy) {
            // Release the old guest state before swapping files; destructors may touch old saves.
            if(!game.returnToTitle()) {LOGE("[restore] Cannot safely release guest state\n");game.quit=true;break;}
            if(services::applyPendingRestore()) {
                if(!game.returnToTitle()) {game.quit=true;break;}
                serviceui::restoredToTitle();dirMgr=DirectionManager{};aspectChord.reset();
                gamePointerHeld=false;wasModal=false;
            }
        }
        if(!headless && serviceStatus.restartRequired && !serviceui::active()) serviceui::open();
        bool modal=!headless && serviceui::active();
        if(modal && !wasModal) {
            if(dirMgr.engineActiveDir) game.key(EV_KEY_RELEASE,dirMgr.engineActiveDir);
            dirMgr=DirectionManager{};aspectChord.reset();
            if(gamePointerHeld) {game.touch(EV_PTR_RELEASE,touchX,touchY,0);gamePointerHeld=false;}
        }
        wasModal=modal;
        auto loopStart = std::chrono::steady_clock::now();
        if(!modal && !game.quit) {
            game.frame();
        }
        if(headless && scriptMenus.count(f) && scriptMenus[f]==-1) {
            // Diagnostic HUD preview on the current StateGame; never changes campaign state.
            if(gm::r32(0x142838)!=0xe92d47f0)return 1;
            u32 common=gm::r32(0x142850+gm::r32(0x1428b4)+gm::r32(0x1428b8));
            u32 state=gm::r32(common+0x244);if(!state || gm::r32(state+4)!=1)return 1;
            u32 ui=gm::r32(common+0x1454),input=gm::r32(common+0x15b8);
            u8 hidden=gm::r8(ui+0x75),enabled=gm::r8(input+4),touchLayout=gm::r8(common+0x8e);
            gm::w8(ui+0x75,0);gm::w8(input+4,1);gm::w8(common+0x8e,1);
            game.cpu.call(0x1dc1ac,{state});
            gm::w8(ui+0x75,hidden);gm::w8(input+4,enabled);gm::w8(common+0x8e,touchLayout);
            u32 graphic=gm::r32(game.img.exports.at("_lcdG")),surface=gm::r32(graphic);
            int w=gm::r32(surface),h=gm::r32(surface+4),pitch=gm::r32(surface+8);u32 pixels=gm::r32(surface+16);
            if(w!=game.screen.w || h!=game.screen.h || gm::r32(surface+12)!=16)return 1;
            for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
                u16 color=gm::r16(pixels+y*pitch+x*2);
                u32 red=(color>>11)&31,green=(color>>5)&63,blue=color&31;
                game.screen.px[y*w+x]=0xff000000|(((red<<3)|(red>>2))<<16)|(((green<<2)|(green>>4))<<8)|((blue<<3)|(blue>>2));
            }
        }
        if (benchmark) frameTimes.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-loopStart).count());
        if (!dumpDir.empty() && dumpEvery > 0 && (f % dumpEvery == 0)) dumpBmp(dumpDir + "/frame_" + std::to_string(f) + ".bmp", game.screen);
        if (!headless && game.screen.w > 0) {
            int winW = 0, winH = 0;
            SDL_GetWindowSize(win, &winW, &winH);
            SDL_Rect dst = computeDstRect(winW, winH);

            int res = SDL_UpdateTexture(tex, nullptr, game.screen.px.data(), game.screen.w * 4);
            if (res != 0) {
                static int errCnt = 0;
                if (errCnt++ < 5) LOGE("[SDL] SDL_UpdateTexture failed: %s\n", SDL_GetError());
            }
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, nullptr, &dst);

            serviceui::draw(ren,winW,winH);
            if(!dumpUi.empty() && f==frames-1) {
                Screen capture;SDL_GetRendererOutputSize(ren,&capture.w,&capture.h);
                capture.px.resize(size_t(capture.w)*capture.h);
                if(SDL_RenderReadPixels(ren,nullptr,SDL_PIXELFORMAT_ARGB8888,capture.px.data(),capture.w*4)==0) dumpBmp(dumpUi,capture);
            }
            SDL_RenderPresent(ren);
        }
        if (!headless && !rendererVsync) {
            auto frameElapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - frameStart).count();
            if (frameElapsedUs < 16666) {
                auto remUs = 16666 - frameElapsedUs;
                if (remUs > 2000) {
                    SDL_Delay((Uint32)((remUs - 1000) / 1000));
                }
                while (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - frameStart).count() < 16666) {
                    // spin for precise sub-millisecond pacing
                }
            }
        }
        if (f % 60 == 0) {
            double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            LOGI("[run] frame %d t=%.1fs icount=%.1fM draws=%llu screen=%dx%d\n", f, s, game.cpu.icount / 1e6, (unsigned long long)game.screen.draws, game.screen.w, game.screen.h);
        }
    }
    if (!frameTimes.empty()) {
        FILE* tf = fopen("frame_times.csv", "w");
        if (tf) { fprintf(tf, "frame,loop_ms\n"); for (size_t i=0;i<frameTimes.size();++i) fprintf(tf,"%zu,%.6f\n",i,frameTimes[i]); fclose(tf); }
        auto sorted = frameTimes; std::sort(sorted.begin(), sorted.end());
        double total=0; for(double ms: sorted) total+=ms;
        LOGI("[bench] frames=%zu mean_ms=%.3f p50_ms=%.3f p95_ms=%.3f max_ms=%.3f\n", sorted.size(),total/sorted.size(),sorted[sorted.size()/2],sorted[(sorted.size()-1)*95/100],sorted.back());
    }
    if (g_prof) {
        FILE* pf = fopen("prof.txt", "w");
        for (size_t i = 0; i < g_profHist.size(); i++) if (g_profHist[i]) fprintf(pf, "%08x %u\n", (unsigned)(0x00100000 + i * 4), g_profHist[i]);
        fclose(pf);
    }
    if (!dumpDir.empty()) dumpBmp(dumpDir + "/final.bmp", game.screen);
    {
        std::map<u32, int> colorCounts;
        for (u32 p : game.screen.px) colorCounts[p]++;
        fprintf(stderr, "[screen] unique colors: %zu\n", colorCounts.size());
    }
    for (auto* c : controllers) {
        if (c) SDL_GameControllerClose(c);
    }
    if(!headless) {services::shutdown();serviceui::shutdown();}
    Audio::shutdown();
    if (!headless) SDL_Quit();
#ifdef __SWITCH__
    if(!services::romfsWasReleased()) romfsExit();
    if (s_bootLog) {
        fclose(s_bootLog);
        s_bootLog = nullptr;
    }
#endif
    return 0;
}
