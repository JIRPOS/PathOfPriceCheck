#pragma once

#include <string>
#include <string_view>

#include "fonts.hpp"
#include "item/derive.hpp"
#include "item/item.hpp"

namespace ppc {

/// Drop the Advanced Mod Descriptions range the game glues to a roll: "+86(77-90) to maximum
/// Energy Shield" reads as "+86 to maximum Energy Shield". Display only — the range is what
/// the modifier's hover tooltip and the filter's own bounds are for.
std::string strip_roll_ranges(std::string_view line);

/// Draw the item the way the game's own tooltip does: rarity-coloured name plate, grey
/// property labels with white values, blue modifiers, red corruption. Presentation only —
/// nothing here parses, resolves or prices.
///
/// `d` supplies the numbers the game leaves the player to work out — DPS, the base's
/// percentile — which are printed in small grey under the property block they summarise.
/// `lex` is the vocabulary the item was parsed with: an info line is taken apart during
/// parsing and rebuilt here, so the words between its pieces come from the same table.
void draw_item_tooltip(const item::Item& it, const item::Derived& d, const Fonts& fonts,
                       const data::Lexicon& lex);

} // namespace ppc
