#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ppc::data {

/// One numeric token plus the advanced-mod-description range that may follow it.
/// `+25(20-30)%` yields value 25, bounds 20..30, spanning `+25(20-30)`.
struct NumberToken {
    size_t begin = 0;
    size_t value_end = 0; ///< end of the number itself, excluding any "(...)"
    size_t end = 0;       ///< end of the whole token, including "(...)"
    double value = 0;
    int decimals = 0;
    bool has_bounds = false;
    bool numeric_bounds = false;
    double bound_min = 0;
    double bound_max = 0;
};

/// Remove every literal "()" — GGG emits them occasionally. Must run before scanning.
std::string strip_empty_parens(std::string_view line);

/// Remove every Advanced Mod Descriptions range whose roll is a *name*.
///
/// A modifier can roll over a list instead of over an interval — "Maximum number of
/// Sentinels of Purity (Animated Weapons-Holy Armaments) is Doubled" rolls over the minion
/// skill gems, and the parenthesis is the first and last of that list exactly as "(50-100)"
/// is the first and last of an interval. `scan_numbers` cannot see it: there is no numeric
/// token in front of it to carry the bounds. The name itself is the roll and the trade
/// wording spells it out, so the range is dropped rather than placeheld.
///
/// Only a group whose two halves are both non-numeric, and that does not follow a number,
/// qualifies — a numeric token's own range belongs to that token.
std::string strip_named_ranges(std::string_view line);

/// Every numeric token, in order.
///
/// A token starts at a '+', '-' or digit that is *not* preceded by a digit or ')'. That
/// lookbehind is load-bearing: without it "1-30" scans as 1 and -30 rather than 1 and 30.
/// The sign belongs to the token, which is why "+42 to maximum Life" normalizes to
/// "# to maximum Life" with no leading '+'.
std::vector<NumberToken> scan_numbers(std::string_view line);

/// Render one candidate. Bit i of `keep` set means token i stays '#'; a cleared bit is
/// replaced by the token's literal source text with its bounds dropped. A kept token whose
/// bounds are non-numeric keeps them verbatim.
std::string apply_candidate(std::string_view line, std::span<const NumberToken> tokens,
                            uint32_t keep);

/// Lookup candidates, most generic first, duplicates removed, raw line last.
///
/// The order is load-bearing: the generic form wins wherever the data has it, and the
/// literal forms exist only for wordings that do not generalise. A line carrying a named
/// range is enumerated twice, as printed and then stripped, so a wording that really does
/// contain a parenthesis still wins as itself.
std::vector<std::string> candidates(std::string_view line);

/// The fully generic candidate — every number replaced by '#'. This is the join key the
/// builder uses between GGG's trade text and the game's own stat descriptions.
std::string placeholder_form(std::string_view line);

} // namespace ppc::data
