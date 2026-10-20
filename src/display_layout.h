#pragma once
#include <SDL.h>
#include <string>

enum class AspectMode { Native, Wide, Auto };
struct ViewportSize { int width, height; };

bool parseAspectMode(const std::string& value, AspectMode& mode);
const char* aspectModeName(AspectMode mode);
AspectMode nextAspectMode(AspectMode mode);
ViewportSize viewportForAspect(AspectMode mode, int availableWidth, int availableHeight,
                              int nativeWidth = 240, int nativeHeight = 400);
AspectMode loadAspectMode(const std::string& path);
bool saveAspectMode(const std::string& path, AspectMode mode);
SDL_Rect aspectButtonRect(int windowWidth, int windowHeight);
bool insideRect(int x, int y, const SDL_Rect& rect);
void drawAspectButton(SDL_Renderer* renderer, AspectMode mode, int windowWidth, int windowHeight);

// Defer the individual menu/map action until release, so the mode chord never
// sends either action to the game, regardless of which button was pressed first.
struct AspectChord {
    bool back = false, start = false, used = false;
    enum Result { NotHandled, Consumed, Cycle, SwapSkills, Menu };
    Result button(Uint8 button, bool down);
    void reset() { back = start = used = false; }
};
