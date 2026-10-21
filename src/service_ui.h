#pragma once
#include "display_layout.h"
#include <functional>
#include <string>
namespace serviceui {
void init(const std::string& fontPath,std::function<void()> cycleAspect,std::function<AspectMode()> mode);
void shutdown();
bool active();
void open();
void poll();
bool event(const SDL_Event& event,bool switchButtons);
void draw(SDL_Renderer* renderer,int width,int height);
void restoredToTitle();
}
