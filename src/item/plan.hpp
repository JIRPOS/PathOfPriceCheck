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
    Currency,    ///< priced in bulk on poe.ninja and the in-game exchange, never searched
    Gem,         ///< the skill's name, its level and its quality — never what the skill does
    Map,         ///< tier or area, the drop bonuses, implicits and enchants — never the affixes
    Beast,       ///< the species and its item level — never the monster modifiers or the title
    Ultimatum,   ///< the trial, its stake and its payout, plus the two mods that scale the stake
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
    /// Ask for the modifier being **absent** rather than present — a `not` stat group instead
    /// of the `and` one. A filter for a modifier the item in hand does not have, which is only
    /// ever worth asking where its absence is itself the thing being bought: a Valdo map that
    /// does not void the character who dies in it. Carries no bounds; there is no roll.
    bool negated = false;
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

    /// Why this row is what it is, shown **on hover** rather than as a line of its own: the
    /// per-unique data does not cover this modifier, or covers it only as prose it never
    /// enumerates. It used to be a note under the filter list, and a note repeats a wording
    /// that is already on screen one row above — Triad Grip's four conversion modifiers cost
    /// twelve lines of panel saying what four unticked boxes had already said.
    std::string caveat;
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

/// A filter the trade site takes as an **option** rather than as an interval: the booleans
/// (corrupted, mirrored, foulborn, identified, blighted), and the closed vocabularies (a
/// chart's shape, a Valdo map's payout). One shape for all of them, because the wire form is the
/// same — an `{"option": …}` under one of the filter groups — and the only thing that differs is
/// where the string comes from.
///
/// **`shown` is the whole point of the struct.** Most of these are answered by the item without
/// the user having anything to decide: an uncorrupted, unmirrored, unmutated, identified rare is
/// the ordinary case, and four rows saying so would push the modifiers — the thing being read —
/// off the panel. So they are imposed silently. It is the *unusual* value that is worth a row,
/// because that is the one a buyer might want to relax: a mirrored item cannot be crafted on, an
/// unidentified one is a different product, and a corrupted or foulborn one is a different market.
struct OptionFilter {
    std::string key;     ///< the filter's name: "corrupted", "chart_shape", "map_blighted"
    std::string label;   ///< "Corrupted", "Chart Shape"
    std::string option;  ///< what the site takes: "true"/"false", "1", "Hrimsorrow"
    std::string display; ///< what the panel shows: "yes"/"no", "End"
    bool enabled = true;
    bool shown = false; ///< offered as a row the user can untick, rather than imposed silently
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

    /// Every option filter, in the order they are offered. Corruption, mirroring and
    /// identification are on every plan whether or not they have a row; synthesis, fracturing
    /// and blight are only ever asked for in the positive, because their absence is not what a
    /// buyer of an ordinary item is choosing.
    std::vector<OptionFilter> options;
    /// Blight, which the site asks about with a `map_filters` flag rather than with a type.
    /// Only ever set true, and never both: the two are mutually exclusive on the site as well
    /// as in the game, and an ordinary map's search leaves them open rather than asking for
    /// their absence.
    std::vector<Influence> influences;

    std::vector<StatFilter> stats;
    std::vector<NumericFilter> numerics;
    /// What was deliberately left out, or could not be honoured. Shown to the user: a silently
    /// dropped filter reads as a successful price check on the wrong item.
    std::vector<std::string> notes;

    bool has_enabled_stats() const;
    /// The option filter under `key`, or null. Also the answer to "is this asked at all".
    const OptionFilter* option(std::string_view key) const;
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
