// HL5 recomp - Windows host entry point (SDL2). Milestone 0 bootstrap: ARM interpreter + HLE.
#include "hle/hle.h"
#include <SDL.h>
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
#endif

#ifdef __ANDROID__
#include <SDL_system.h>
#include <unistd.h>

static void ensureAndroidAssets(const std::string& storageDir) {
    SDL_RWops* mf = SDL_RWFromFile("assets_list.txt", "rb");
    if (!mf) {
        fprintf(stderr, "[android] assets_list.txt not found in APK assets\n");
        return;
    }
    Sint64 mfSize = SDL_RWsize(mf);
    std::string content;
    if (mfSize > 0) {
        content.resize((size_t)mfSize);
        SDL_RWread(mf, content.data(), 1, (size_t)mfSize);
    }
    SDL_RWclose(mf);

    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.empty()) continue;
        auto sep = line.rfind(':');
        if (sep == std::string::npos) continue;
        std::string relPath = line.substr(0, sep);
        size_t expectedSize = (size_t)std::stoull(line.substr(sep + 1));

        std::string dst = storageDir + "/" + relPath;
        if (std::filesystem::exists(dst)) {
            std::error_code ec;
            auto curSize = std::filesystem::file_size(dst, ec);
            if (!ec && curSize == expectedSize) {
                continue; // up to date
            }
        }

        std::filesystem::create_directories(std::filesystem::path(dst).parent_path());
        SDL_RWops* src = SDL_RWFromFile(relPath.c_str(), "rb");
        if (!src) continue;
        FILE* out = fopen(dst.c_str(), "wb");
        if (!out) { SDL_RWclose(src); continue; }

        std::vector<uint8_t> buf(64 * 1024);
        while (true) {
            size_t n = SDL_RWread(src, buf.data(), 1, buf.size());
            if (n == 0) break;
            fwrite(buf.data(), 1, n, out);
        }
        fclose(out);
        SDL_RWclose(src);
    }
}
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

static int mapKey(SDL_Keycode k) {
    switch (k) {
        case SDLK_UP: case SDLK_w: return WK_UP;
        case SDLK_DOWN: case SDLK_s: return WK_DOWN;
        case SDLK_LEFT: case SDLK_a: return WK_LEFT;
        case SDLK_RIGHT: case SDLK_d: return WK_RIGHT;
        case SDLK_RETURN: case SDLK_z: case SDLK_SPACE: case SDLK_j: return WK_SELECT;
        case SDLK_ESCAPE: case SDLK_x: case SDLK_BACKSPACE: case SDLK_k: return WK_CLEAR;
        case SDLK_u: case SDLK_q: return '1';
        case SDLK_i: case SDLK_e: return '3';
        case SDLK_o: case SDLK_r: return '7';
        case SDLK_p: case SDLK_t: return '9';
        case SDLK_m: return '0';
    }
    if (k >= SDLK_0 && k <= SDLK_9) return '0' + (k - SDLK_0);
    return 0;
}

static int mapControllerButton(Uint8 btn) {
    switch (btn) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return WK_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return WK_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return WK_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return WK_RIGHT;
        case SDL_CONTROLLER_BUTTON_A: return WK_SELECT; // A (Xbox/PC) -> Confirm / Attack / Interact
        case SDL_CONTROLLER_BUTTON_B: return WK_CLEAR;  // B (Xbox/PC) -> Cancel / Menu Back / Status
        case SDL_CONTROLLER_BUTTON_X: return '1';       // X -> Skill 1
        case SDL_CONTROLLER_BUTTON_Y: return '3';       // Y -> Skill 2
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return '7';  // LB -> Skill 3
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return '9'; // RB -> Use Item / Potion
        case SDL_CONTROLLER_BUTTON_START: return WK_CLEAR;    // Start -> Menu / Pause
        case SDL_CONTROLLER_BUTTON_BACK: return '0';         // Select -> Map
        case SDL_CONTROLLER_BUTTON_LEFTSTICK: return '0';    // L3 -> Map
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return '#';   // R3 -> Potion switch
        default: return 0;
    }
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

int main(int argc, char** argv) {
#ifdef __SWITCH__
    romfsInit();
#endif

    std::string exeDir = getExeDir();
    std::string apk = "reference/extracted_apk", data = "data", dumpDir;

#ifdef __SWITCH__
    apk = "romfs:";
    data = "sdmc:/switch/heroes_lore_5/data";
    std::filesystem::create_directories(data);
#elif defined(__ANDROID__)
    const char* internalStorage = SDL_AndroidGetInternalStoragePath();
    std::string baseStorage = (internalStorage && internalStorage[0] != '\0') ? internalStorage : ".";
    ensureAndroidAssets(baseStorage);
    apk = baseStorage;
    data = baseStorage + "/data";
    std::filesystem::create_directories(data);
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
    std::map<int, int> scriptKeys;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--apk" && i + 1 < argc) apk = argv[++i];
        else if (a == "--data" && i + 1 < argc) data = argv[++i];
        else if (a == "--frames" && i + 1 < argc) frames = atoi(argv[++i]);
        else if (a == "--dump" && i + 1 < argc) dumpDir = argv[++i];
        else if (a == "--dump-every" && i + 1 < argc) dumpEvery = atoi(argv[++i]);
        else if (a == "--lcd" && i + 1 < argc) sscanf(argv[++i], "%dx%d", &lw, &lh);
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
    static Game game;
    game.verbose = verbose;
    if (!game.boot(apk, data, lw, lh)) return 1;

    SDL_Window* win = nullptr; SDL_Renderer* ren = nullptr; SDL_Texture* tex = nullptr;
    std::vector<SDL_GameController*> controllers;
    DirectionManager dirMgr;
    bool isFullscreen = false;
    bool aspectFit = true; // true: 3:5 letterboxed, false: stretched fill

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
        if (aspectFit && W > 0 && H > 0) {
            float scale = std::min((float)winW / W, (float)winH / H);
            r.w = (int)(W * scale);
            r.h = (int)(H * scale);
            r.x = (winW - r.w) / 2;
            r.y = (winH - r.h) / 2;
        }
        return r;
    };

    auto handlePointer = [&](int eventType, int mx, int my) {
        int winW = 0, winH = 0;
        SDL_GetWindowSize(win, &winW, &winH);
        SDL_Rect dst = computeDstRect(winW, winH);
        if (dst.w <= 0 || dst.h <= 0) return;

        float lx = (float)(mx - dst.x) * W / (float)dst.w;
        float ly = (float)(my - dst.y) * H / (float)dst.h;

        if (lx >= 0 && lx < W && ly >= 0 && ly < H) {
            game.touch(eventType, (int)lx, (int)ly, 0);
        } else if (eventType == EV_PTR_RELEASE) {
            int clx = std::clamp((int)lx, 0, W - 1);
            int cly = std::clamp((int)ly, 0, H - 1);
            game.touch(EV_PTR_RELEASE, clx, cly, 0);
        } else if (eventType == EV_PTR_MOVE) {
            int clx = std::clamp((int)lx, 0, W - 1);
            int cly = std::clamp((int)ly, 0, H - 1);
            game.touch(EV_PTR_MOVE, clx, cly, 0);
        }
    };

    if (!headless) {
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER);
        win = SDL_CreateWindow("Heroes Lore 5: Knights of the Blue (Recomp)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W * 2, H * 2, SDL_WINDOW_RESIZABLE);
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);

        for (int i = 0; i < SDL_NumJoysticks(); i++) openController(i);
    }
    auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; !game.quit && (frames < 0 || f < frames); f++) {
        auto frameStart = std::chrono::steady_clock::now();
        if (!headless) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) {
                    game.quit = true;
                } else if (e.type == SDL_CONTROLLERDEVICEADDED) {
                    openController(e.cdevice.which);
                } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
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
                    int k = mapControllerButton(e.cbutton.button);
                    if (k) {
                        if (DirectionManager::keyToDirIdx(k) >= 0) dirMgr.onButton(k, true);
                        else game.key(EV_KEY_PRESS, k);
                    }
                } else if (e.type == SDL_CONTROLLERBUTTONUP) {
                    int k = mapControllerButton(e.cbutton.button);
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
                    } else if (e.key.keysym.sym == SDLK_F9) {
                        aspectFit = !aspectFit;
                        fprintf(stderr, "[video] Aspect: %s\n", aspectFit ? "Original 3:5 Letterbox" : "Fill / Stretched");
                    } else {
                        int k = mapKey(e.key.keysym.sym);
                        if (k) {
                            if (DirectionManager::keyToDirIdx(k) >= 0) dirMgr.onButton(k, true);
                            else if (!e.key.repeat) game.key(EV_KEY_PRESS, k);
                        }
                    }
                } else if (e.type == SDL_KEYUP) {
                    int k = mapKey(e.key.keysym.sym);
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
            dirMgr.update(game);
        }
        if (scriptKeys.count(f)) {
            int k = scriptKeys[f];
            fprintf(stderr, "[script] Injecting key press %d at frame %d\n", k, f);
            game.key(EV_KEY_PRESS, k);
        }
        if (scriptKeys.count(f - 2)) {
            int k = scriptKeys[f - 2];
            game.key(EV_KEY_RELEASE, k);
        }
        game.frame();
        if (!dumpDir.empty() && dumpEvery > 0 && (f % dumpEvery == 0)) dumpBmp(dumpDir + "/frame_" + std::to_string(f) + ".bmp", game.screen);
        if (!headless && game.screen.w > 0) {
            int winW = 0, winH = 0;
            SDL_GetWindowSize(win, &winW, &winH);
            SDL_Rect dst = computeDstRect(winW, winH);

            SDL_UpdateTexture(tex, nullptr, game.screen.px.data(), game.screen.w * 4);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, nullptr, &dst);
            SDL_RenderPresent(ren);
        }
        if (!headless) {
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
            fprintf(stderr, "[run] frame %d t=%.1fs icount=%.1fM draws=%llu screen=%dx%d\n", f, s, game.cpu.icount / 1e6, (unsigned long long)game.screen.draws, game.screen.w, game.screen.h);
        }
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
    if (!headless) SDL_Quit();
#ifdef __SWITCH__
    romfsExit();
#endif
    return 0;
}
