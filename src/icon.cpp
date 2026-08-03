#include "icon.hpp"

#include <vector>

#include <SDL3/SDL.h>

namespace ppc {
namespace {

#include "icon_data.inc"

unsigned int decode85_byte(char c) { return c >= '\\' ? c - 36 : c - 35; }

} // namespace

SDL_Surface* load_app_icon() {
    std::vector<unsigned char> png((popc_icon_png_size + 3) / 4 * 4);
    const char* src = popc_icon_png_base85;
    for (size_t i = 0; i < png.size(); i += 4, src += 5) {
        unsigned int w = decode85_byte(src[0]) +
                         85 * (decode85_byte(src[1]) +
                               85 * (decode85_byte(src[2]) +
                                     85 * (decode85_byte(src[3]) + 85 * decode85_byte(src[4]))));
        for (int b = 0; b < 4; ++b) png[i + b] = (w >> (8 * b)) & 0xFF;
    }
    SDL_IOStream* io = SDL_IOFromConstMem(png.data(), popc_icon_png_size);
    if (!io) return nullptr;
    SDL_Surface* s = SDL_LoadPNG_IO(io, true);
    if (!s) SDL_Log("icon decode failed: %s", SDL_GetError());
    return s;
}

} // namespace ppc
