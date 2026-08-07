#include "item/range_match.hpp"

#include <algorithm>
#include <cmath>

namespace ppc::item {
namespace {

/// The value rounded to `dp` decimals, away from the roll. The epsilon absorbs the noise of
/// `value * pct / 100` — without it a window that lands exactly on a representable value half
/// the time moves by one digit and half the time by two.
double floor_to(double v, int dp) {
    const double s = std::pow(10.0, dp);
    return std::floor(v * s + 1e-9) / s;
}

double ceil_to(double v, int dp) {
    const double s = std::pow(10.0, dp);
    return std::ceil(v * s - 1e-9) / s;
}

/// One side of the interval. `gate` is the tier's own end, which never crosses the roll: a
/// legacy roll sits outside the range its modifier publishes today, and gating it to that would
/// ask for a copy of the item that is not this one.
std::optional<double> bound(BoundMode mode, double pct, double value, std::optional<double> gate,
                            int dp, bool upper) {
    switch (mode) {
        case BoundMode::Unbound: return std::nullopt;
        case BoundMode::Exact: return value;
        case BoundMode::Within:
        case BoundMode::WithinTiered: break;
    }
    // Off the magnitude, so a negative roll widens outwards like a positive one: -11 at 5% is
    // -11.55 to -10.45, not the -11.55 to -10.45 read backwards.
    const double slack = std::abs(value) * pct / 100.0;
    double v = upper ? ceil_to(value + slack, dp) : floor_to(value - slack, dp);
    if (mode == BoundMode::WithinTiered && gate)
        v = upper ? std::max(value, std::min(v, *gate)) : std::min(value, std::max(v, *gate));
    return v;
}

} // namespace

std::string_view bound_mode_id(BoundMode m) {
    for (const BoundModeOption& o : kBoundModes)
        if (o.mode == m) return o.id;
    return {};
}

std::string_view bound_mode_label(BoundMode m) {
    for (const BoundModeOption& o : kBoundModes)
        if (o.mode == m) return o.label;
    return {};
}

BoundMode bound_mode_from_id(std::string_view id, BoundMode fallback) {
    for (const BoundModeOption& o : kBoundModes)
        if (o.id == id) return o.mode;
    return fallback;
}

Bounds seed_bounds(const RangeMatch& rm, double value, std::optional<double> tier_lo,
                   std::optional<double> tier_hi, int dp, bool lower_is_better) {
    const BoundMode lo_mode = lower_is_better ? rm.max_mode : rm.min_mode;
    const BoundMode hi_mode = lower_is_better ? rm.min_mode : rm.max_mode;
    const double lo_pct = lower_is_better ? rm.max_pct : rm.min_pct;
    const double hi_pct = lower_is_better ? rm.min_pct : rm.max_pct;

    Bounds b;
    b.min = bound(lo_mode, lo_pct, value, tier_lo, dp, false);
    b.max = bound(hi_mode, hi_pct, value, tier_hi, dp, true);
    return b;
}

} // namespace ppc::item
