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
    /// The pool namespace this base's modifiers are generated from — `Mods.Domain`, an integer
    /// the game has no published name for past the first few. A base has exactly one; the
    /// domains are mutually exclusive, so this is what says a chart rolls from a different pool
    /// than the map it is sailed from without compiling in a list of names.
    ///
    /// **0 means the record does not say**, which is not the same as "no modifiers": a unique,
    /// anything the build could not match to game data, and every bundle published before the
    /// field existed all read as 0 — and so does trade's one "Map" entry, whose game row is a
    /// stand-in for all 491 of them and states the wrong domain outright. Ask
    /// `GameData::mod_domain_for` rather than this field, and it will fall back to the item
    /// class, which is exact for that case.
    int mod_domain = 0;
    std::string category;    ///< craftable.category, e.g. "Rings"
    std::string trade_disc;  ///< discriminator when name/type alone is ambiguous
    /// The trade `type` term, where it is not the display name. Only gems have one: trade
    /// files a transfigured gem under the skill it alters — "Raise Zombie of Falling" is
    /// `Raise Zombie` with the `alt_y` discriminator — and a search naming what the clipboard
    /// printed matches nothing. Empty everywhere else, and on every bundle published before
    /// the field existed, so nothing may depend on it being there.
    std::string trade_name;
    /// The game's own `Metadata/Items/...` path for this base, and the only key GGG's
    /// currency-exchange feed states an item by — that feed publishes no names at all. Empty
    /// on a unique, on anything the build could not match to game data, and on every bundle
    /// published before the field existed, which is why nothing may depend on it being there.
    std::string metadata_id;
    /// True when this item has **ever** appeared in a currency-exchange market. A fact about
    /// the item, unlike the hourly digest `exchange/` reads, which can only say whether one
    /// traded in the last hour — and for a thin item (a Weeping Essence of Greed) no trades in
    /// a given hour is the normal case. Without this the app cannot tell "not traded on the
    /// exchange" from "nobody traded one this hour", and since poe.ninja prices neither, the
    /// check comes back saying nothing at all.
    ///
    /// False both for an item that does not trade there and on a bundle published before the
    /// flag existed, so it is only an answer once `GameData::has_exchange_flags()` is true.
    bool exchange = false;
    /// Where GGG's CDN serves this item's picture, `Art/2DItems/Armours/Gloves/Hrimsorrow.png`
    /// — see `item_image_url`. **Uniques only**: a unique is not a base type in the game's data
    /// but a name, a base and a mod list put together when the item drops, so the picture is
    /// the one thing about it no other record can be asked for.
    ///
    /// Empty for 110 of trade's uniques (the sanctum relics, the Harbinger pieces, a few the
    /// client's word list has renamed) and on every bundle published before the field existed.
    /// Unlike the exchange flag that needs no bundle-level signal beside it: nothing here reads
    /// an absent picture as a claim about the item, it just draws the name instead.
    std::string art;
    int w = 0, h = 0;
    int drop_level = 0;
    bool corrupted = false;
    std::string unique_base; ///< for Namespace::Unique, the base it rolls on
    /// Defence ranges as [min, max]; the pair present is what tells same-named bases apart.
    std::optional<std::pair<int, int>> armour, evasion, energy_shield, ward;
};

/// One stat a unique's modifier grants, as the per-unique modifier data states it.
struct UniqueModFilter {
    /// The canonical '#'-placeholder wording. Present to render: when `trade_id` is empty
    /// this may be the client's wording only, so it is not guaranteed to resolve to a stat.
    std::string ref;
    /// The ready-to-use stat hash, to be sent verbatim. Empty means the modifier is real but
    /// not searchable — its wording resolves to two trade ids, or to none.
    std::string trade_id;
    /// One [min, max] per '#' the wording covers — two for "Adds # to # Fire Damage", one
    /// otherwise. Already in displayed units, so these compare directly against a roll read
    /// off the clipboard.
    std::vector<std::pair<double, double>> ranges;
};

/// One modifier a unique can carry, with every stat it grants.
struct UniqueMod {
    std::string mod;      ///< GGG's own mod id; stable across patches, for debugging
    bool implicit = false;
    std::vector<UniqueModFilter> filters;
};

/// A group of modifiers the unique picks from rather than always having. This is the fact
/// nothing else has: a pooled modifier prints exactly like a fixed one, and it is the
/// difference between a common copy of the unique and the one worth searching for.
struct UniqueModPool {
    std::string hint; ///< the source's own prose, "Two or Three random aura modifiers"
    /// How many of `mods` actually roll. Absent for half the pools the source describes;
    /// that means "at least one, unknown", never "all of them".
    std::optional<std::pair<int, int>> count;
    bool implicit = false;
    std::vector<UniqueMod> mods;
};

/// What one unique can roll. Absent for a unique the source does not cover — 43 of trade's
/// names have no record, and a league launch outruns the source by days.
struct UniqueMods {
    std::string name, base;
    std::vector<UniqueMod> fixed;  ///< every copy of the item has these
    std::vector<UniqueModPool> pools;
    /// A pool stated in prose but never enumerated ("One to three random Synthesis implicit
    /// modifiers"). Nothing to search; it exists so the app can say what it is leaving out
    /// instead of implying the item has nothing more.
    std::vector<std::string> unlisted;
};

/// Where GGG's CDN serves `BaseType::art`, sized to an item that occupies `w`×`h` inventory
/// cells. Empty in, empty out — an item the bundle has no picture for is named rather than
/// drawn, never given a guessed URL, which would be a 404 fetched once per frame.
///
/// The size is the same request the game's own tooltip makes and halves the download; both are
/// dropped when the caller does not know them, since the unscaled image is the same picture.
std::string item_image_url(std::string_view art, int w = 0, int h = 0);

struct ItemClass {
    std::string item_class;     ///< as printed by the clipboard, e.g. "Rings"
    std::string id;             ///< the game's internal class id
    std::string trade_category; ///< trade `category` option; empty when unmapped
    /// The mod domain every base of this class agrees on, or 0 where they do not — a class
    /// holding genuinely different things (Jewels covers two) can answer for a base and never
    /// for itself. It exists for the records whose own domain is missing, which is why `Maps`
    /// carries one: all 511 map bases are domain 5 while the "Map" trade lists them under is a
    /// stand-in row that says 43.
    int mod_domain = 0;
};

/// One stat a pooled modifier grants, as the mod-pool dataset states it — the same shape a
/// resolved `Stat` has, minus everything only a printed roll can answer.
struct PoolStat {
    /// The canonical '#'-placeholder wording, and the key this entry is indexed under. Present
    /// to render and to match a regex against; it resolves to a `Stat` only where `trade_id`
    /// is set, since the rest are wordings trade indexes under no hash at all.
    std::string ref;
    /// The ready-to-use stat hash. Empty means the modifier is real but not searchable, which
    /// is the ordinary case here and costs the entry nothing: a pool is rated, not searched.
    std::string trade_id;
    /// The lowest tier's floor and the highest tier's ceiling, in displayed units — what a
    /// wording has to be rendered with before a regex written against printed item text can be
    /// tested against it. Absent together for a wording that prints no number at all.
    std::optional<double> min, max;
};

/// One modifier a mod domain's pool can spawn — one *wording-set*, not one roll: the tiers of
/// an affix all print the same wordings and a verdict attaches to a wording, so they collapse.
///
/// The pool **describes and never gates**. It is what spawns naturally, which is strictly less
/// than what an item can print: an essence, a craft, a veiled mod or Harvest all put modifiers
/// on an item whose weights would never have produced them, and the published list is trimmed
/// by naming conventions besides. A printed modifier no entry covers is normal, not an error.
struct PoolMod {
    int domain = 0;
    /// `Mods.GenerationType` — 1 prefix, 2 suffix, 5 a Vaal corruption implicit, and the rest
    /// named by number because the game publishes no name for them either.
    int gen = 0;
    int tiers = 0;            ///< how many mod rows print these wordings
    std::string name;         ///< the affix name, as Advanced Mod Descriptions prints it
    std::vector<std::string> mods; ///< GGG's own mod ids; provenance, for the debug log
    std::vector<PoolStat> stats;   ///< one per wording, in the order the modifier prints them
};

} // namespace ppc::data
