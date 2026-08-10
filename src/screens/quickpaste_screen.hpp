#pragma once

#include <cstddef>

namespace ppc {
class App;

/// The paste list, drawn at the cursor: one row per enabled paste, picked by click or by the
/// number key beside it.
void draw_quickpaste_screen(App& app);

/// How big that window has to be for `entries` pastes, in pixels.
///
/// **Declared, not measured** — the same rule Settings' fixed size follows, and here it is not a
/// preference: `App::place_overlay` sizes the window before the frame that would measure it, so
/// a height taken from the last frame would place the popup for the previous item every time.
/// Every constant behind it is in the implementation, beside the code that draws to them.
void quickpaste_size(size_t entries, int* w, int* h);

} // namespace ppc
