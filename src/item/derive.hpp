#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "data/game_data.hpp"
#include "item/item.hpp"

namespace ppc::item {

/// Numbers the game shows the player only indirectly, and the ones a search needs.
///
/// Quality inflates a weapon's physical damage and an armour piece's defences, so two
/// otherwise identical items list different values. Everything a buyer compares is therefore
/// normalised to 20% quality — the quality they will bring it to — except on an item already
/// past 20%, where the number in front of them is the real one.
struct Derived {
    std::optional<double> pdps, edps, cdps, dps; ///< as the item is now
    std::optional<double> pdps_q20, dps_q20;

    std::optional<int> armour_q20, evasion_q20, energy_shield_q20, ward_q20;

    /// Where the base's own defence roll sits in the base type's range, 0..1. Bases roll their
    /// defences, which is what makes two identical-looking rares worth different amounts.
    ///
    /// One number per item, not per defence: a base rolls *one* value and spreads it across the
    /// defences it has, so an armour/energy-shield hybrid whose two percentiles disagree is
    /// reporting rounding, not two different rolls. Summed on both sides — the item's recovered
    /// inherent values against the sum of the base's ranges. Absent unless every defence the
    /// item displays has a published range, since a partial sum is not comparable to a full one.
    std::optional<double> base_pct;

    /// What a search should ask for: the 20%-quality value, or the item's own past 20%.
    std::optional<int> search_armour, search_evasion, search_energy_shield, search_ward;
    std::optional<double> search_pdps, search_edps, search_dps;

    /// Sum of the item's *local* increases, i.e. the ones already folded into the properties
    /// above. Quality adds to these rather than multiplying them, which is the whole reason
    /// they have to be known before anything can be normalised.
    double incr_armour = 0, incr_evasion = 0, incr_energy_shield = 0, incr_ward = 0;
    double incr_physical = 0;
};

/// `gd` is only needed for base-defence ranges (the percentiles); pass null without a bundle.
Derived derive(const data::GameData* gd, const Item& it);

/// The `NumericFilter::key`s whose value this modifier is **already inside** — "ar", "pdps",
/// "aps" and the rest. A local roll is not a thing the item has beside its armour, it is part
/// of the armour the item displays, so a search filtering on both asks the same question twice.
///
/// Locality is the whole of it and is decided here rather than in the data: "20% increased
/// Attack Speed" is the weapon's own only on a weapon, and "#% increased Energy Shield" is the
/// item's own only where the item displays energy shield. Empty for the modifiers that feed no
/// derived number, which is most of them.
std::vector<std::string_view> derived_filter_keys(const Item& it, const Modifier& m);

} // namespace ppc::item
