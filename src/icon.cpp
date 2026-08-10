#include "icon.hpp"

#include <SDL3/SDL.h>

namespace ppc {
namespace {

// Regenerate with scripts/gen-icon-data.sh.
#include "icon_data.inc"

} // namespace

SDL_Surface* load_app_icon() {
    SDL_IOStream* io = SDL_IOFromConstMem(popc_icon_png, sizeof popc_icon_png);
    if (!io) return nullptr;
    SDL_Surface* s = SDL_LoadPNG_IO(io, true);
    if (!s) SDL_Log("icon decode failed: %s", SDL_GetError());
    return s;
}

} // namespace ppc
