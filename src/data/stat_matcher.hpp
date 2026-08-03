#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "data/game_data.hpp"
#include "data/types.hpp"

namespace ppc::data {

struct MatchContext {
    ModType mod_type = ModType::Explicit;
    /// Advanced Mod Descriptions can report a percentage the roll is scaled by.
    double roll_incr = 0;
};

struct StatMatch {
    const Stat* stat = nullptr;
    const StatMatcher* matcher = nullptr;
    ModType mod_type = ModType::Explicit;

    std::vector<double> rolls;      ///< one per '#' that survived in the matched wording
    double value = 0;               ///< the filter roll; the mean for a two-number mod
    std::optional<double> min, max; ///< bounds, sign-corrected and ordered

    bool negated = false;   ///< the matched wording was the inverse one
    bool legacy = false;    ///< the roll sits outside current bounds; needs a Divine Orb
    bool unscalable = false;
    size_t lines_consumed = 1; ///< a hybrid mod spans more than one clipboard line
};

/// True for a line that is entirely parenthesised reminder text, which the game prints
/// between a mod and its continuation.
bool is_reminder_text(std::string_view line);

/// Match `lines[start..]` as one modifier.
///
/// Tries progressively longer '\n' joins so a hybrid such as
/// "Adds # to # Physical Damage\n#% increased Attack Speed" resolves as a single stat, and
/// for each join walks the normalizer's candidates most-generic-first. A candidate only
/// wins if the stat it resolves to is actually searchable as `ctx.mod_type`.
std::optional<StatMatch> match_stat(const GameData& gd, std::span<const std::string> lines,
                                    size_t start, const MatchContext& ctx);

/// trunc((v + v*p/100) * 10^dp) / 10^dp — the game's own rounding for scaled rolls.
double incr_roll(double v, double percent, int dp);

} // namespace ppc::data
