#include "input_mapping.h"
#include "hle/hle.h"
int gameKey(SDL_Keycode key) {
    switch(key) {
        case SDLK_UP:case SDLK_w:return WK_UP;
        case SDLK_DOWN:case SDLK_s:return WK_DOWN;
        case SDLK_LEFT:case SDLK_a:return WK_LEFT;
        case SDLK_RIGHT:case SDLK_d:return WK_RIGHT;
        case SDLK_RETURN:case SDLK_z:case SDLK_SPACE:case SDLK_j:return WK_SELECT;
        case SDLK_ESCAPE:case SDLK_x:case SDLK_BACKSPACE:case SDLK_k:return WK_CLEAR;
        case SDLK_u:case SDLK_q:return '1';
        case SDLK_i:case SDLK_e:return '3';
        case SDLK_o:case SDLK_r:return '7';
        case SDLK_p:case SDLK_t:return '9';
        case SDLK_m:return '0';
        case SDLK_c:case SDLK_HASH:case SDLK_TAB:return '#';
        default:return key>=SDLK_0 && key<=SDLK_9?'0'+int(key-SDLK_0):0;
    }
}
int gamepadKey(Uint8 button,bool nintendoLayout) {
    if(nintendoLayout) {
        if(button==SDL_CONTROLLER_BUTTON_A) button=SDL_CONTROLLER_BUTTON_B;
        else if(button==SDL_CONTROLLER_BUTTON_B) button=SDL_CONTROLLER_BUTTON_A;
        else if(button==SDL_CONTROLLER_BUTTON_X) button=SDL_CONTROLLER_BUTTON_Y;
        else if(button==SDL_CONTROLLER_BUTTON_Y) button=SDL_CONTROLLER_BUTTON_X;
    }
    switch(button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:return WK_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:return WK_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:return WK_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:return WK_RIGHT;
        case SDL_CONTROLLER_BUTTON_A:return WK_SELECT;
        case SDL_CONTROLLER_BUTTON_B:return WK_CLEAR;
        case SDL_CONTROLLER_BUTTON_X:return '1';
        case SDL_CONTROLLER_BUTTON_Y:return '3';
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:return '7';
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:return '9';
        case SDL_CONTROLLER_BUTTON_START:return WK_CLEAR;
        case SDL_CONTROLLER_BUTTON_BACK:return '#';
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:return '0';
        default:return 0;
    }
}
bool nintendoController(SDL_JoystickID instance) {
#ifdef __SWITCH__
    return true;
#else
    auto* pad=SDL_GameControllerFromInstanceID(instance);
    if(!pad) return false;
    auto type=SDL_GameControllerGetType(pad);
    return type==SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO;
#endif
}
