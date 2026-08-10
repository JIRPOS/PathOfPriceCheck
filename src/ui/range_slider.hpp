#pragma once

#include <optional>

#include "ui/track.hpp"

namespace ppc::ui {

/// What a `range_slider` draws over, beside the pair of bounds it edits.
struct RangeTrack {
    double lo = 0, hi = 0; ///< the track's ends, before any growing this widget has done
    /// The range the game published for this modifier, when there is one, drawn as a pair of
    /// ticks on the track. **Only a published range gets them**: their meaning is "the affix
    /// rolls between these", and ticking a track the caller derived from the number in hand would
    /// turn a convenience into a claim. Everything else about the widget is the same either way.
    ///
    /// They are not the ends. The track reaches past what the affix rolls on purpose — see
    /// `widen_track` — and these are what keeps that reach from reading as the affix's own range.
    std::optional<double> tick_lo, tick_hi;
    int dp = 0; ///< decimals the values are rounded and read at
    float width = 0;
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
/// **The ends do not stop the drag.** `lo`/`hi` already reach past what the modifier is known to
/// roll — the caller widens them, see `widen_track` — because for an affix the known range is the
/// tier in hand and nothing else, no data we have enumerates its other tiers, and a buyer is
/// entitled to ask for a roll better than the one they are holding. Past even that reach a knob
/// keeps going, the domain widens to hold it, and `tick_lo`/`tick_hi` go on marking what the game
/// actually published. The domain is frozen for the duration of a drag: rescaling the track under
/// a moving knob makes the number jump about.
///
/// **A knob released hard against an end grows the track**, by a quarter of the span it started
/// with, so the next drag has somewhere to go. Pegging is how a user says the number they want is
/// further out than the track offers, and answering it with a track that is still exactly as long
/// is how a slider reads as broken. The quarter is added beyond *where the knob was left* rather
/// than beyond the end it was pushed past, which is what leaves it visibly clear of the corner
/// afterwards — a drag does not stop at the end, so the two are not the same place. Repeated, this
/// walks outwards a quarter at a time, and `kRangeLimit` is where it stops.
///
/// Values are rounded to `dp` decimals. **A drag** never leaves `min` above `max` — pushing one
/// knob past the other carries the other along, which is what makes an interval collapsible to a
/// point without having to aim. A crossed interval arriving from anywhere else is **drawn as it
/// is**, not quietly put back in order: this is called every frame, including the ones in the
/// middle of a number being typed into the boxes beside it, and correcting there would make a
/// half-typed number permanent.
///
/// Returns true on the frames it changed something.
bool range_slider(const char* id, std::optional<double>& min, std::optional<double>& max,
                  const RangeTrack& track);

} // namespace ppc::ui
