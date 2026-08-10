#pragma once

namespace ppc::ui {

/// The furthest any bound will go, either sign. Nothing the game prints is within six orders of
/// magnitude of it; it is here so that a track grown a few times, or a number pasted into one of
/// the boxes beside it, cannot run away into a value arithmetic stops being exact at.
inline constexpr double kRangeLimit = 2147483647.0; // INT32_MAX

/// How far a track reaches past the numbers it is drawn around, as a fraction of each end's own
/// magnitude. **One constant for every track**, published range or derived one, because the
/// argument for the reach is the same either way: a buyer is entitled to drag toward a roll better
/// than the one in hand, and a track that stops exactly where the item does gives them nowhere to
/// drag. Half again is wide enough to cover the tier either side of most affixes without making
/// the roll itself a speck in the middle of the track.
///
/// It is deliberately **not** a setting. There is no reading of the game data that makes one number
/// here more correct than another — see `widen_track` on what this may and may not claim — so a
/// setting would be asking the user a question nothing can answer. Change it here.
inline constexpr double kTrackSpread = 0.5;

/// A track's two ends.
struct TrackSpan {
    double lo = 0, hi = 0;
};

/// Widen `[lo, hi]` outwards into the span a slider is drawn over.
///
/// Each end moves by `kTrackSpread` of **its own** magnitude, so a range the game prints negative
/// grows away from zero in the same direction it is read in, and at least one step at `dp` — the
/// smallest number the row can express — so a range that is a single value, or one sitting on
/// zero, still comes back with a track to aim at rather than a point. The result is rounded
/// outwards at `dp`, which keeps the ends readable numbers and never rounds the reach away.
///
/// **This is a place to put the mouse, not a statement about what the affix rolls.** The caller
/// holds the published range separately and marks it on the track; nothing here knows what the
/// tiers either side are, and the spread must never be drawn as though it did.
///
/// Crossed input is ordered rather than refused: the bounds behind a track can be mid-edit.
TrackSpan widen_track(double lo, double hi, int dp);

} // namespace ppc::ui
