#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ppc::data {

enum class Namespace : uint8_t { Item, Unique, Gem, DivinationCard, CapturedBeast, Area };

std::string_view to_string(Namespace ns);
std::optional<Namespace> namespace_from_string(std::string_view s);

/// The trade API's stat groups. These double as the mod types the parser reports, because
/// which group a hash lives in *is* what distinguishes an explicit roll from an implicit one.
enum class ModType : uint8_t {
    Explicit, Implicit, Fractured, Enchant, Crafted, Veiled, Pseudo, Scourge,
    Crucible, Sanctum, Delve, Ultimatum, Imbued, Mercenary, Count
};

std::string_view trade_prefix(ModType t); ///< "explicit", "implicit", ...
std::optional<ModType> mod_type_from_prefix(std::string_view s);

struct StatMatcher {
    std::string string;             ///< the wording, '#'-placeholder form
    bool negate = false;            ///< this wording is the inverse; flip the roll's sign
    std::optional<double> value;    ///< implied roll for a wording that shows no number
};

struct Stat {
    std::string ref;                ///< canonical wording; the record's identity
    int better = 1;                 ///< +1 higher is better, -1 lower is, 0 not comparable
    int dp = 0;                     ///< decimal places
    bool inverted = false;          ///< trade indexes this with the opposite sign
    std::vector<StatMatcher> matchers;
    /// Trade ids per mod type. Empty means the stat is not searchable in that namespace.
    std::vector<std::string> ids[static_cast<size_t>(ModType::Count)];

    bool has(ModType t) const { return !ids[static_cast<size_t>(t)].empty(); }
    const std::vector<std::string>& trade_ids(ModType t) const {
        return ids[static_cast<size_t>(t)];
    }
    /// The matcher whose wording produced `normalized`, or nullptr.
    const StatMatcher* matcher_for(std::string_view normalized) const;
};

struct BaseType {
    std::string name;
    std::string ref_name;
    Namespace ns = Namespace::Item;
    std::string category;    ///< craftable.category, e.g. "Rings"
    std::string trade_disc;  ///< discriminator when name/type alone is ambiguous
    int w = 0, h = 0;
    int drop_level = 0;
    bool corrupted = false;
    std::string unique_base; ///< for Namespace::Unique, the base it rolls on
    /// Defence ranges as [min, max]; the pair present is what tells same-named bases apart.
    std::optional<std::pair<int, int>> armour, evasion, energy_shield, ward;
};

struct ItemClass {
    std::string item_class;     ///< as printed by the clipboard, e.g. "Rings"
    std::string id;             ///< the game's internal class id
    std::string trade_category; ///< trade `category` option; empty when unmapped
};

} // namespace ppc::data
