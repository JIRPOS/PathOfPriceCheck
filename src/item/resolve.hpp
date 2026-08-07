#pragma once

#include "data/game_data.hpp"
#include "item/item.hpp"

namespace ppc::item {

/// Second pass over a parsed item: resolve its base against the bundle and every modifier
/// against the stat table, merging the lines of hybrid mods the parser could not group.
///
/// `gd` must outlive `it` — the item ends up holding pointers into the bundle.
void resolve_item(const data::GameData& gd, Item& it);

/// The base item's own name with a magic item's affixes stripped, or "" when no span of the
/// printed line is a known base. "Surgeon's Quicksilver Flask of the Cheetah" -> "Quicksilver
/// Flask". `item_class` narrows the answer when the same words name bases in two classes.
std::string strip_magic_affixes(const data::GameData& gd, std::string_view printed,
                                std::string_view item_class);

/// Take one of `it.unique_candidates` as the unique this unidentified item is. `chosen` may be
/// null, which puts the item back to undecided.
///
/// The item still says `Unidentified` and the search still asks for that; what the choice
/// supplies is the one thing the clipboard does not — which unique the base rolled into.
/// Anything not in the candidate list is refused, so a stale pointer from a previous item (or
/// from a bundle the updater has since swapped out) cannot end up being searched for.
void choose_unique(Item& it, const data::BaseType* chosen);

/// The unique named inside `printed`, or "" — the longest run of its words that names one.
/// A Valdo map states its payout as "Foil Hrimsorrow", where the foil is the reward's own
/// variant and only "Hrimsorrow" is a name the trade site will accept.
std::string find_unique_in(const data::GameData& gd, std::string_view printed);

} // namespace ppc::item
