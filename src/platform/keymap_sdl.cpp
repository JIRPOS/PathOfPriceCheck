#include "platform/input.hpp"

#include <SDL3/SDL.h>

namespace ppc {

std::string key_name_from_sdl(uint32_t kc) {
    if (kc >= 'a' && kc <= 'z') return std::string(1, char(kc - 32)); // -> uppercase
    if (kc >= '0' && kc <= '9') return std::string(1, char(kc));
    switch (kc) {
        case SDLK_SPACE:     return "Space";
        case SDLK_RETURN:    return "Enter";
        case SDLK_ESCAPE:    return "Escape";
        case SDLK_TAB:       return "Tab";
        case SDLK_BACKSPACE: return "Backspace";
        case SDLK_DELETE:    return "Delete";
        case SDLK_INSERT:    return "Insert";
        case SDLK_HOME:      return "Home";
        case SDLK_END:       return "End";
        case SDLK_PAGEUP:    return "PageUp";
        case SDLK_PAGEDOWN:  return "PageDown";
        case SDLK_UP:        return "Up";
        case SDLK_DOWN:      return "Down";
        case SDLK_LEFT:      return "Left";
        case SDLK_RIGHT:     return "Right";
        default: break;
    }
    if (kc >= SDLK_F1 && kc <= SDLK_F12) return "F" + std::to_string(int(kc - SDLK_F1) + 1);
    return "";
}

} // namespace ppc
