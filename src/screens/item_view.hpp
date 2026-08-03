#pragma once

#include "fonts.hpp"
#include "item/derive.hpp"
#include "item/item.hpp"

namespace ppc {

/// Draw the item the way the game's own tooltip does: rarity-coloured name plate, grey
/// property labels with white values, blue modifiers, red corruption. Presentation only —
/// nothing here parses, resolves or prices.
void draw_item_tooltip(const item::Item& it, const Fonts& fonts);

/// The numbers the game leaves the player to work out: DPS, defences at 20% quality, and
/// where the base's own roll sits in its range.
void draw_derived_numbers(const item::Item& it, const item::Derived& d, const Fonts& fonts);

} // namespace ppc
