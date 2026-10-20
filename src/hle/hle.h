// High-level emulation (HLE) of everything the HL5 native library expects from Android:
// bionic libc, libm, GLES1 (software compositor), JNI + the Java-side CletEntry/Midas managers.
#pragma once
#include "../core/cpu.h"
#include "../core/elf_loader.h"
#include <string>
#include <vector>

struct Screen {
    int w = 0, h = 0;
    std::vector<u32> px;   // ARGB8888, w*h  (logical ortho size set by the game)
    u64 draws = 0;         // number of glDrawArrays presented so far
};

extern std::string g_overrideDir;

struct Game {
    Cpu cpu;
    ElfImage img;
    Screen screen;
    bool quit = false;
    bool verbose = false;

    // apkDir: extracted APK (needs lib/armeabi/libHeroesLore5.so, assets/data.vfs.mp3)
    bool boot(const std::string& apkDir, const std::string& dataDir, int lcdW, int lcdH);
    bool resizeViewport(int width, int height);
    void frame();                                   // one nativeLoop()
    void key(int type, int wipiKey);                // type: 2=press 3=release
    void touch(int type, int x, int y, int id);     // type: 19=press 20=release 21=move
    void pause();
    void resume();
};

// WIPI key codes used by the original Java layer (WIPIConstant.getWIPIKeyCode)
enum { WK_UP = -1, WK_DOWN = -2, WK_LEFT = -3, WK_RIGHT = -4, WK_SELECT = -5, WK_CLEAR = -16 };
enum { EV_KEY_PRESS = 2, EV_KEY_RELEASE = 3, EV_PTR_PRESS = 19, EV_PTR_RELEASE = 20, EV_PTR_MOVE = 21 };
