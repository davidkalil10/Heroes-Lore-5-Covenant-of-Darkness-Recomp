#define SDL_MAIN_HANDLED
#include "display_layout.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static void expect(AspectMode mode,int w,int h,int expectedW,int expectedH) {
    auto size=viewportForAspect(mode,w,h);
    require(size.width==expectedW && size.height==expectedH,"viewport dimensions");
    require(size.width>=240 && size.height>=400,"native content must not be cropped");
}
int main() {
    expect(AspectMode::Native,1920,1080,240,400);
    expect(AspectMode::Wide,1080,2400,712,400);
    expect(AspectMode::Auto,1280,720,712,400);
    expect(AspectMode::Auto,1920,1200,640,400);
    expect(AspectMode::Auto,3440,1440,956,400);
    expect(AspectMode::Auto,1080,2400,240,534);
    expect(AspectMode::Auto,480,800,240,400);
    expect(AspectMode::Auto,7680,1080,1280,400);
    expect(AspectMode::Auto,100,4000,240,1280);
    auto button=aspectButtonRect(1280,720);
    require(insideRect(button.x+2,button.y+2,button),"touch button hit");
    require(!insideRect(button.x-1,button.y,button),"touch outside button");
    for (bool reverse:{false,true}) {
        AspectChord chord;
        Uint8 first=reverse?SDL_CONTROLLER_BUTTON_START:SDL_CONTROLLER_BUTTON_BACK;
        Uint8 second=reverse?SDL_CONTROLLER_BUTTON_BACK:SDL_CONTROLLER_BUTTON_START;
        require(chord.button(first,true)==AspectChord::Consumed,"defer individual action");
        require(chord.button(second,true)==AspectChord::Cycle,"either chord order");
        require(chord.button(second,true)==AspectChord::Consumed,"held chord only cycles once");
        require(chord.button(first,false)==AspectChord::Consumed,"chord must not open map/menu");
        require(chord.button(second,false)==AspectChord::Consumed,"chord release must not open map/menu");
    }
    AspectChord chord;
    chord.button(SDL_CONTROLLER_BUTTON_BACK,true);
    require(chord.button(SDL_CONTROLLER_BUTTON_BACK,false)==AspectChord::Map,"standalone map button");
    chord.button(SDL_CONTROLLER_BUTTON_START,true);
    require(chord.button(SDL_CONTROLLER_BUTTON_START,false)==AspectChord::Menu,"standalone menu button");
    chord.button(SDL_CONTROLLER_BUTTON_BACK,true);chord.reset();
    require(chord.button(SDL_CONTROLLER_BUTTON_START,true)==AspectChord::Consumed,"focus/disconnect resets chord");

    auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    auto path=std::filesystem::temp_directory_path()/std::filesystem::path("hl5-display-test-"+std::to_string(stamp)+".ini");
    require(loadAspectMode(path.string())==AspectMode::Native,"first-run default");
    require(saveAspectMode(path.string(),AspectMode::Auto),"save display choice");
    require(loadAspectMode(path.string())==AspectMode::Auto,"restore display choice");
    {std::ofstream file(path);file<<"aspect=invalid\n";}
    require(loadAspectMode(path.string())==AspectMode::Native,"invalid setting fallback");
    std::filesystem::remove(path);
    std::puts("PASS: viewport sizes, controller chord, touch bounds and saved preferences");
}
