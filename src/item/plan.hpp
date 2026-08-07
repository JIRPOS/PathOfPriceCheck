#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "data/game_data.hpp"
#include "item/derive.hpp"
#include "item/item.hpp"
#include "item/range_match.hpp"

namespace ppc::item {

/// How an item gets priced. What matters about an item depends entirely on this: a rare is
/// its modifiers, a unique is its name, and a white item is its base plus what a crafter
/// could do with it.
enum class Strategy : uint8_t {
    BaseItem,    ///< the base type itself: influences, item level, fractured mods, implicits
    Modifiers,   ///< a rolled item: every modifier, bounded by the tier it rolled
    Unique,      ///< the named item, with its variable rolls
    Currency,    ///< priced in bulk, not searched  (not implemented yet)
    Gem,         ///< level / quality / alternate quality  (not implemented yet)
    Map,         ///< tier or area, the drop bonuses, implicits and enchants — never the affixes
    Unsupported
};

std::string_view to_string(Strategy s);

/// One modifier turned into a trade stat filter.
struct StatFilter {
    /// Into `Item::mods`, or absent for a filter no single modifier is behind: trade's
    /// `pseudo.*` totals — a map's drop bonuses, which the game prints as properties, and its
    /// count of affixes, which is a fact about the whole item.
    std::optional<size_t> mod_index;
    /// The other modifiers folded into this filter by `merge_same_stat`, also into
    /// `Item::mods`. Two rolls of one stat are searched as their total, but they are still two
    /// affixes, and which two is what the tier display is about.
    std::vector<size_t> merged;
    std::string id;         ///< trade stat id, "explicit.stat_3299347043"
    std::string text;       ///< the wording, for display
    data::ModType type = data::ModType::Explicit;
    bool enabled = false;
    std::optional<double> min, max;
    /// The modifier's own tier range is known — Advanced Mod Descriptions printed one, or the
    /// per-unique data supplied it — so `BoundMode::WithinTiered` had something to gate the
    /// bounds against. Without it all a filter can say is what the roll itself is worth.
    bool tiered = false;
    /// The trade site indexes this stat with the opposite sign; the query builder flips it.
    bool inverted = false;
    int dp = 0;

    /// What this modifier *can* roll, whichever source said so: the affix tier's own range
    /// where Advanced Mod Descriptions printed one, the per-unique record's range otherwise.
    /// Kept apart from `min`/`max` on purpose — those are what the search asks for, and the
    /// two are only equal until the asking is something the user can edit.
    std::optional<double> roll_min, roll_max;

    // From the bundle's per-unique modifier data, on a unique it has a record of.
    /// The unique picks this modifier from a pool rather than always having it, so not every
    /// copy carries it. The one thing worth searching a unique on that a printed range can
    /// never reveal — Ralakesh's Impatience rolls one of three charge modifiers, each 1..1.
    bool pooled = false;
    std::string pool_hint; ///< the source's prose for that pool, when it states one
};

/// A numeric trade filter: `key` is the name in the trade query's filter groups.
struct NumericFilter {
    std::string key;   ///< "ilvl", "quality", "ar", "pdps", …
    std::string label; ///< "Item Level"
    std::optional<double> min, max;
    bool enabled = false;
    int dp = 0;
    std::string note;  ///< why the value is what it is, e.g. "at 20% quality"
};

/// Everything a search for this item would ask for. Purely declarative — building the trade
/// query JSON out of this, and running it, is the next layer up.
struct SearchPlan {
    Strategy strategy = Strategy::Unsupported;
    std::string category; ///< trade `category` option, e.g. "weapon.bow"
    std::string name;     ///< trade `name` term — the unique's name
    std::string type;     ///< trade `type` term — the base type
    std::string discriminator; ///< set when the base's name alone is ambiguous on trade
    /// The trade `rarity` option. Which market is being priced is the plan's call and not the
    /// query builder's: a unique map is planned as a map, and reading it back off the strategy
    /// would search it among the rares. Defaulted rather than left empty, because an empty one
    /// is a search across both markets at once and nothing here ever means that.
    std::string rarity = "nonunique";

    std::optional<bool> corrupted; ///< match exactly; corruption always matters
    bool synthesised = false, fractured = false, mirrored = false;
    std::vector<Influence> influences;

    std::vector<StatFilter> stats;
    std::vector<NumericFilter> numerics;
    /// What was deliberately left out, or could not be honoured. Shown to the user: a silently
    /// dropped filter reads as a successful price check on the wrong item.
    std::vector<std::string> notes;

    bool has_enabled_stats() const;
};

/// The strategy an item gets unless the user overrides it.
Strategy default_strategy(const Item& it);

/// Build the plan. `force` overrides the strategy — a rare with a fractured mod or a good
/// base is often worth more as a base item than as the sum of its rolls, and only the user
/// knows which they meant. `rm` is how wide each modifier's filter is seeded; it is a setting
/// rather than a fact about the item, which is why it arrives from outside.
SearchPlan build_plan(const data::GameData& gd, const Item& it, const Derived& d,
                      std::optional<Strategy> force = std::nullopt, const RangeMatch& rm = {});

} // namespace ppc::item
