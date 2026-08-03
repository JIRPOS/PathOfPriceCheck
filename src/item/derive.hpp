#pragma once

#include <optional>

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
    std::optional<double> armour_pct, evasion_pct, energy_shield_pct, ward_pct;

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

} // namespace ppc::item
