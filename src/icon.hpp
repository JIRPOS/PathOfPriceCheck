#pragma once

struct SDL_Surface;

namespace ppc {

/// Decodes the embedded application icon. Caller owns the surface
/// (`SDL_DestroySurface`); returns nullptr if SDL cannot decode it.
SDL_Surface* load_app_icon();

} // namespace ppc
