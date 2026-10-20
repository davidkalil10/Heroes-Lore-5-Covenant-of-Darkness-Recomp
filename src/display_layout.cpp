#include "display_layout.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

bool parseAspectMode(const std::string& value, AspectMode& mode) {
    if (value == "native") mode = AspectMode::Native;
    else if (value == "wide" || value == "widescreen") mode = AspectMode::Wide;
    else if (value == "auto") mode = AspectMode::Auto;
    else return false;
    return true;
}
const char* aspectModeName(AspectMode mode) {
    switch (mode) {
        case AspectMode::Wide: return "wide";
        case AspectMode::Auto: return "auto";
        default: return "native";
    }
}
AspectMode nextAspectMode(AspectMode mode) {
    return mode == AspectMode::Native ? AspectMode::Wide :
           mode == AspectMode::Wide ? AspectMode::Auto : AspectMode::Native;
}
ViewportSize viewportForAspect(AspectMode mode, int availableWidth, int availableHeight,
                              int nativeWidth, int nativeHeight) {
    if (mode == AspectMode::Native) return {nativeWidth, nativeHeight};
    double ratio = mode == AspectMode::Wide ? 16.0 / 9.0 :
        double(std::max(1, availableWidth)) / std::max(1, availableHeight);
    // Expand one dimension; never shrink or resample the native sprite canvas.
    int width = nativeWidth, height = nativeHeight;
    if (ratio >= double(nativeWidth) / nativeHeight)
        width = std::max(nativeWidth, int(std::lround(nativeHeight * ratio / 2)) * 2);
    else
        height = std::max(nativeHeight, int(std::lround(nativeWidth / ratio / 2)) * 2);
    return {std::min(width, 1280), std::min(height, 1280)};
}
AspectMode loadAspectMode(const std::string& path) {
    std::ifstream input(path);
    std::string line;
    AspectMode mode = AspectMode::Auto;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("aspect=", 0) == 0) parseAspectMode(line.substr(7), mode);
    }
    return mode;
}
bool saveAspectMode(const std::string& path, AspectMode mode) {
    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << "aspect=" << aspectModeName(mode) << '\n';
    return bool(output);
}
SDL_Rect aspectButtonRect(int windowWidth, int windowHeight) {
    int scale = std::clamp(std::min(windowWidth, windowHeight) / 360, 1, 3);
    return {std::max(0, windowWidth - 62 * scale - 8), 8, 62 * scale, 22 * scale};
}
bool insideRect(int x, int y, const SDL_Rect& rect) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}
static const unsigned char* glyph(char c) {
    static const unsigned char letters[][7] = {
        {14,17,17,31,17,17,17}, // A
        {30,17,17,17,17,17,30}, // D
        {31,16,16,30,16,16,31}, // E
        {31,4,4,4,4,4,31},     // I
        {17,25,25,21,19,19,17},// N
        {14,17,17,17,17,17,14},// O
        {31,4,4,4,4,4,4},      // T
        {17,17,17,17,17,17,14},// U
        {17,17,17,17,17,10,4}, // V
        {17,17,17,21,21,21,10} // W
    };
    const char* names = "ADEINOTUVW";
    for (int i = 0; names[i]; ++i) if (names[i] == c) return letters[i];
    return nullptr;
}
void drawAspectButton(SDL_Renderer* renderer, AspectMode mode, int windowWidth, int windowHeight) {
    SDL_Rect button = aspectButtonRect(windowWidth, windowHeight);
    int scale = button.h / 22;
    SDL_BlendMode oldBlend;
    SDL_GetRenderDrawBlendMode(renderer, &oldBlend);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 12, 20, 30, 205);
    SDL_RenderFillRect(renderer, &button);
    SDL_SetRenderDrawColor(renderer, 132, 195, 224, 230);
    SDL_RenderDrawRect(renderer, &button);
    const char* label = mode == AspectMode::Native ? "NATIVE" : mode == AspectMode::Wide ? "WIDE" : "AUTO";
    int length = mode == AspectMode::Native ? 6 : 4;
    int x = button.x + (button.w - (length * 6 - 1) * scale) / 2;
    int y = button.y + (button.h - 7 * scale) / 2;
    SDL_SetRenderDrawColor(renderer, 240, 246, 252, 255);
    for (const char* p = label; *p; ++p, x += 6 * scale) {
        const auto* rows = glyph(*p);
        if (!rows) continue;
        for (int row = 0; row < 7; ++row) for (int column = 0; column < 5; ++column)
            if (rows[row] & (1 << (4 - column))) {
                SDL_Rect pixel{x + column * scale, y + row * scale, scale, scale};
                SDL_RenderFillRect(renderer, &pixel);
            }
    }
    SDL_SetRenderDrawBlendMode(renderer, oldBlend);
}
AspectChord::Result AspectChord::button(Uint8 button, bool down) {
    if (button != SDL_CONTROLLER_BUTTON_BACK && button != SDL_CONTROLLER_BUTTON_START) return NotHandled;
    bool& held = button == SDL_CONTROLLER_BUTTON_BACK ? back : start;
    if (down) {
        if (held) return Consumed;
        held = true;
        if (back && start && !used) { used = true; return Cycle; }
        return Consumed;
    }
    if (!held) return Consumed;
    held = false;
    Result result = used ? Consumed : button == SDL_CONTROLLER_BUTTON_BACK ? SwapSkills : Menu;
    if (!back && !start) used = false;
    return result;
}
