#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ppc::item {

/// How one side of a filter's interval is seeded from the roll in hand.
enum class BoundMode : uint8_t {
    Unbound,      ///< that side is left open — the pre-selector fills nothing
    Exact,        ///< the roll itself
    Within,       ///< the roll widened by a percentage of itself
    WithinTiered, ///< the same, but never past what the affix's own tier can roll
};

struct BoundModeOption {
    BoundMode mode;
    std::string_view id;    ///< what goes in config.json
    std::string_view label; ///< what Settings shows
};

/// The closed list, in the order Settings offers it — widest asking first.
inline constexpr std::array<BoundModeOption, 4> kBoundModes{{
    {BoundMode::Unbound, "unbound", "Unbound"},
    {BoundMode::Exact, "exact", "Exact"},
    {BoundMode::Within, "within", "Within"},
    {BoundMode::WithinTiered, "within_tiered", "Within (tiered)"},
}};

std::string_view bound_mode_id(BoundMode m);
std::string_view bound_mode_label(BoundMode m);
/// The mode `id` names, or `fallback` for an id this build does not know.
BoundMode bound_mode_from_id(std::string_view id, BoundMode fallback);

/// Whether the mode reads the percentage beside it.
constexpr bool uses_pct(BoundMode m) {
    return m == BoundMode::Within || m == BoundMode::WithinTiered;
}

/// How the pre-selector turns a roll into the two bounds a search asks for.
///
/// Tier-gated 5% by default: what a buyer wants is a copy that rolled about what this one did,
/// and a bound outside the affix's own tier asks for a roll that tier cannot produce — so it
/// only drops the listings that answer the question exactly.
struct RangeMatch {
    BoundMode min_mode = BoundMode::WithinTiered;
    BoundMode max_mode = BoundMode::WithinTiered;
    double min_pct = 5.0;
    double max_pct = 5.0;
};

struct Bounds {
    std::optional<double> min, max;
};

/// Seed a filter's interval from the roll in hand.
///
/// `tier_lo`/`tier_hi` are what the modifier can roll where that is known — Advanced Mod
/// Descriptions, or the per-unique data — and absent otherwise, which is what makes
/// `WithinTiered` fall back to `Within`: there is no tier to gate against, not a tier of
/// nothing. `dp` is the filter's own precision, and the window is rounded **outwards** at that
/// last digit: rounding inwards would ask for a roll the item in hand does not have, and
/// flooring is also what makes any non-zero percentage move an integer roll by at least one.
///
/// **`lower_is_better` swaps which mode governs which side**, because "Min" is the bound that
/// says *at least this good* and on a stat the game prints negative that is the upper one. It
/// only shows when the two modes differ: a symmetric window is symmetric either way round.
Bounds seed_bounds(const RangeMatch& rm, double value, std::optional<double> tier_lo,
                   std::optional<double> tier_hi, int dp, bool lower_is_better);

} // namespace ppc::item
