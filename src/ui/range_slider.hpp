#pragma once

#include <optional>

namespace ppc::ui {

/// The furthest any bound here will go, either sign. Nothing the game prints is within six orders
/// of magnitude of it; it is here so that a track grown a few times, or a number pasted into one
/// of the boxes beside it, cannot run away into a value arithmetic stops being exact at.
inline constexpr double kRangeLimit = 2147483647.0; // INT32_MAX

/// What a `range_slider` draws over, beside the pair of bounds it edits.
struct RangeTrack {
    double lo = 0, hi = 0; ///< the track's ends, before any growing this widget has done
    int dp = 0;            ///< decimals the values are rounded and read at
    float width = 0;
    /// Whether `lo`/`hi` are a range the game published or one the caller derived from the number
    /// in hand. **Only a published range gets the ticks**: their meaning is "the affix rolls
    /// between these", and drawing them around a derived track would turn a convenience into a
    /// claim. Everything else about the widget is the same either way.
    bool published = false;
    /// First frame on a new pair of bounds. The widget keeps a widened track between frames, and
    /// that state is keyed by id — which one popup reused for every row shares. Without this the
    /// track grown on one modifier would be inherited by the next one opened.
    bool reset = false;
};

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
/// **A knob released hard against an end grows the track**, by a fifth of the range it started
/// with, so the next drag has somewhere to go. Pegging is how a user says the number they want is
/// further out than the track offers, and answering it with a track that is still exactly as long
/// is how a slider reads as broken. Repeated, this walks outwards a fifth at a time, and
/// `kRangeLimit` is where it stops.
///
/// Values are rounded to `dp` decimals, and `min` is never left above `max` — pushing one
/// knob past the other carries the other along, which is what makes an interval collapsible to
/// a point without having to aim.
///
/// Returns true on the frames it changed something.
bool range_slider(const char* id, std::optional<double>& min, std::optional<double>& max,
                  const RangeTrack& track);

} // namespace ppc::ui
