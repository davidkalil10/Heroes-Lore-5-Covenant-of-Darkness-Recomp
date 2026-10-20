#pragma once
#include <SDL.h>
int gameKey(SDL_Keycode key);
int gamepadKey(Uint8 button,bool nintendoLayout);
bool nintendoController(SDL_JoystickID instance);
