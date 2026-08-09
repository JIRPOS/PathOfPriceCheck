#pragma once

#include <optional>

namespace ppc::ui {

/// A slider with **two knobs on one track** — the interval a trade filter asks for, set by
/// dragging its ends rather than by typing them.
///
/// ImGui has no such widget: `DragFloatRange2` is two drag boxes side by side, which says
/// nothing about where the asking sits inside what the modifier can actually roll. That
/// picture is the whole point here, so the track is drawn as the roll's own range and the
/// filled span between the knobs is what the search would accept.
///
/// **Either bound may be absent**, which is a real filter — a floor with no ceiling, or a
/// modifier asked for by presence alone. An absent bound parks its knob at that end of the
/// track and draws it hollow, so the widget still reads as an interval; dragging it sets a
/// value, and only clearing the box beside the slider takes it back to absent.
///
/// **The ends do not stop the drag.** `lo`/`hi` are only what the modifier is *known* to roll —
/// which for an affix is the tier in hand and nothing else, because no data we have enumerates
/// its other tiers — and a buyer is entitled to ask for a roll better than the one they are
/// holding. So a knob pushed past an end keeps going, the domain widens to hold it, and the
/// pair of ticks left behind marks where the known range was. The domain is frozen for the
/// duration of a drag: rescaling the track under a moving knob makes the number jump about.
///
/// Values are rounded to `dp` decimals, and `min` is never left above `max` — pushing one
/// knob past the other carries the other along, which is what makes an interval collapsible to
/// a point without having to aim.
///
/// Returns true on the frames it changed something.
bool range_slider(const char* id, std::optional<double>& min, std::optional<double>& max,
                  double lo, double hi, int dp, float width);

} // namespace ppc::ui
