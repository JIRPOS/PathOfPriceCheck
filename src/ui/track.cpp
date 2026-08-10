#include "ui/track.hpp"

#include <algorithm>
#include <cmath>

namespace ppc::ui {

TrackSpan widen_track(double lo, double hi, int dp) {
    if (hi < lo) std::swap(lo, hi);
    // Clamped first, so the arithmetic below starts from a number it can be exact about.
    lo = std::clamp(lo, -kRangeLimit, kRangeLimit);
    hi = std::clamp(hi, -kRangeLimit, kRangeLimit);

    const double step = std::pow(10.0, -dp);
    const double p = std::pow(10.0, dp);
    const double out_lo = lo - std::max(std::abs(lo) * kTrackSpread, step);
    const double out_hi = hi + std::max(std::abs(hi) * kTrackSpread, step);
    return {std::clamp(std::floor(out_lo * p) / p, -kRangeLimit, kRangeLimit),
            std::clamp(std::ceil(out_hi * p) / p, -kRangeLimit, kRangeLimit)};
}

} // namespace ppc::ui
