#pragma once

#include <imgui.h>

#include "mapcheck/verdict.hpp"

namespace ppc {

class App;

/// The map check popup: the map under the cursor, redrawn as a tooltip, with a verdict on each
/// modifier and one line saying what the map as a whole is worth.
///
/// It reports the height it drew back to `App`, because a popup sized to its content cannot be
/// sized before the content is laid out. See `App::set_mapcheck_height`.
void draw_mapcheck_screen(App& app);

namespace ui {

/// The glyph a verdict is drawn with — a check, a warning triangle, a skull, and a question
/// mark for the state that is the absence of an answer. Shared with the settings list, which is
/// the other place a verdict is set.
const char* verdict_glyph(mapcheck::Verdict v);
/// The word behind it, for a font whose glyph subset and `ui/glyphs.hpp` have drifted apart.
const char* verdict_word(mapcheck::Verdict v);
/// What the row is tinted with. The alpha is a wash: the modifier's own text still has to read
/// as the game's mod blue over it.
ImVec4 verdict_colour(mapcheck::Verdict v);

} // namespace ui
} // namespace ppc
