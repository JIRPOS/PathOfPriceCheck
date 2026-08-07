#include "item/plan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

#include "data/stat_normalize.hpp"
#include "item/resolve.hpp"

namespace ppc::item {
namespace {

constexpr std::array<std::string_view, 7> kStrategies{
    "Base item", "Modifiers", "Unique", "Currency", "Gem", "Map", "Unsupported"};

/// How many decimals `v` needs to survive being printed. Rolls are at most hundredths.
int decimals_needed(double v) {
    if (v != std::floor(v * 10) / 10) return 2;
    if (v != std::floor(v)) return 1;
    return 0;
}

/// A mod's text on one line, for a note.
std::string one_line(const Modifier& m) {
    std::string out;
    for (const std::string& l : m.lines) {
        if (!out.empty()) out += " / ";
        out += l;
    }
    return out;
}

void add_numeric(SearchPlan& p, std::string key, std::string label,
                 std::optional<double> min, bool enabled, int dp = 0, std::string note = {},
                 std::optional<double> max = std::nullopt) {
    if (!min && !max) return;
    NumericFilter f;
    f.key = std::move(key);
    f.label = std::move(label);
    f.min = min;
    f.max = max;
    f.enabled = enabled;
    f.dp = dp;
    f.note = std::move(note);
    p.numerics.push_back(std::move(f));
}

void add_flag(SearchPlan& p, std::string key, std::string label, bool value, bool shown) {
    FlagFilter f;
    f.key = std::move(key);
    f.label = std::move(label);
    f.value = value;
    f.shown = shown;
    p.flags.push_back(std::move(f));
}

/// A property the game prints as `Label: value`, turned into the `misc_filters` interval trade
/// indexes it under. These are not rolls and there is no tier behind them — the number is
/// simply what this copy has — so the filter is one-sided, and which side it is open on is what
/// "better" means for that property.
const Property* property_of(const Item& it, std::string_view label) {
    for (const Property& p : it.properties)
        if (p.label == label) return &p;
    return nullptr;
}

/// The 20%-quality note that explains why a filter's number is not the one on the item.
/// Nothing to say at exactly 20%, and above it the item's own number is what is searched.
std::string quality_note(const Item& it) {
    return it.quality.value_or(0) < 20 ? "normalised to 20% quality" : std::string();
}

/// Whether the game printed this property in the augmented blue, i.e. a modifier on *this
/// copy* raised it above what the base gives. It is the only statement the clipboard makes
/// about a property being better than default, and the bundle carries no base crit chance or
/// attack speed to compare against.
bool property_augmented(const Item& it, std::string_view label) {
    return std::any_of(it.properties.begin(), it.properties.end(),
                       [label](const Property& p) { return p.label == label && p.augmented; });
}

void add_defences(SearchPlan& p, const Item& it, const Derived& d, bool enabled) {
    const std::string note = quality_note(it);
    struct Entry {
        const char* key;
        const char* label;
        const std::optional<int>& value;
    };
    for (const Entry& e : std::initializer_list<Entry>{
             {"ar", "Armour", d.search_armour},
             {"ev", "Evasion", d.search_evasion},
             {"es", "Energy Shield", d.search_energy_shield},
             {"ward", "Ward", d.search_ward}}) {
        if (!e.value) continue;
        add_numeric(p, e.key, e.label, static_cast<double>(*e.value), enabled, 0, note);
    }

    // Where the base's own roll sits in its range — `armour_filters.base_defence_percentile`
    // on the trade site, so it is a filter and not a remark under one. It is the base's single
    // roll spread over every defence, which is why there is one of these and not one per
    // defence.
    //
    // Ticked only on a base-item search, where that roll *is* what is being bought. On a
    // modifier search the defence totals above already carry it, and asking the same question
    // twice only drops the listings that answer it once. **Floored, never rounded**: the filter
    // is a minimum, and a 78.6th-percentile item asked for at 79 does not match itself.
    if (d.base_pct)
        add_numeric(p, "base_defence_percentile", "Base Percentile",
                    std::floor(*d.base_pct * 100), p.strategy == Strategy::BaseItem);
}

void add_weapon(SearchPlan& p, const Item& it, const Derived& d, bool enabled) {
    if (!it.is_weapon()) return;
    const std::string note = quality_note(it);
    add_numeric(p, "dps", "Total DPS", d.search_dps, enabled, 1, note);
    add_numeric(p, "pdps", "Physical DPS", d.search_pdps, enabled, 1, note);
    add_numeric(p, "edps", "Elemental DPS", d.search_edps, enabled, 1);
    // Every weapon has these two and on most of them they are the base's own numbers, which
    // asking for would only rule out the same weapon in someone else's stash. What makes one
    // worth searching is a modifier having raised it — and the game says exactly that by
    // printing the value augmented.
    add_numeric(p, "aps", "Attacks per Second", it.attacks_per_second,
                enabled && property_augmented(it, "Attacks per Second"), 2);
    add_numeric(p, "crit", "Critical Strike Chance", it.crit_chance,
                enabled && property_augmented(it, "Critical Strike Chance"), 2);
}

/// The roll a trade filter for this mod is compared against, and the tier's range for it.
///
/// Trade indexes an added-damage mod as the average of its two numbers, which is what the
/// matcher's own `value` is. Every other multi-number wording is indexed on its *first*
/// number — "15% chance to Unnerve Enemies for 4 seconds on Hit" is searched on the 15, and
/// averaging it with the duration asks for a 10% chance.
struct Roll {
    std::optional<double> value, min, max;
};

/// True when trade indexes this mod as the average of its numbers rather than on the first.
bool averaged_roll(const data::StatMatch& m) {
    return m.matcher && m.matcher->string.starts_with("Adds ") && m.rolls.size() > 1;
}

Roll roll_for(const data::StatMatch& m) {
    Roll r;
    if (m.rolls.empty()) return r;
    if (averaged_roll(m)) {
        r.value = m.value;
        if (m.roll_bounds.size() == m.rolls.size()) {
            double lo = 0, hi = 0;
            for (const auto& [blo, bhi] : m.roll_bounds) {
                lo += blo;
                hi += bhi;
            }
            const auto n = static_cast<double>(m.roll_bounds.size());
            r.min = lo / n;
            r.max = hi / n;
        }
        return r;
    }
    r.value = m.rolls.front();
    if (!m.roll_bounds.empty()) {
        r.min = m.roll_bounds.front().first;
        r.max = m.roll_bounds.front().second;
    }
    return r;
}

/// A modifier the player put on *this copy* rather than one the item came with. It costs
/// currency and not every copy has it, so it is worth searching on even for a unique — an
/// instilled "Used when Charges reach full" is most of what a Rumi's Concoction sells for.
/// It is also why the per-unique modifier data never mentions it: that describes the unique,
/// not what was crafted onto one.
bool added_to_copy(data::ModType t) {
    return t == data::ModType::Enchant || t == data::ModType::Crafted ||
           t == data::ModType::Fractured || t == data::ModType::Scourge ||
           t == data::ModType::Veiled || t == data::ModType::Crucible;
}

/// Turn one modifier into a filter. Bounds follow the strategy: a rolled item is searched
/// inside the tier it rolled, everything else is searched at "no worse than this".
///
/// `ranges_printed` is whether *this item* printed a roll range anywhere, i.e. whether the
/// owner has Advanced Mod Descriptions on — which is what makes the absence of one on a given
/// modifier mean something. See the bound rule below.
std::optional<StatFilter> to_filter(const Item& it, size_t index, Strategy s, bool ranges_printed,
                                    const RangeMatch& rm) {
    const Modifier& m = it.mods[index];
    if (!m.match || !m.match->stat) return std::nullopt;
    const data::Stat& stat = *m.match->stat;
    const std::vector<std::string>& ids = stat.trade_ids(m.match->mod_type);
    if (ids.empty()) return std::nullopt;

    StatFilter f;
    f.mod_index = index;
    f.id = ids.front();
    f.text = m.text();
    f.type = m.match->mod_type;
    f.inverted = stat.inverted;

    const Roll roll = roll_for(*m.match);
    // The bundle does not carry a decimal count for every stat, and rounding a roll away is
    // how "0.4% of Physical Attack Damage Leeched as Mana" comes out as a filter for 0.
    f.dp = std::max(stat.dp, decimals_needed(roll.value.value_or(0)));
    const bool has_bounds = roll.min && roll.max;
    // What the affix could have rolled, as against what the search will ask for. The same two
    // numbers on a `Modifiers` plan today, and the reason they are two fields is that they stop
    // being the same the moment the asking is editable.
    f.roll_min = roll.min;
    f.roll_max = roll.max;
    // Advanced Mod Descriptions printed a range wider than a point, so this roll is one of
    // several the affix could have had — which is what "variable" means for a unique.
    const bool variable = has_bounds && *roll.min != *roll.max;

    // Which side an open bound goes on. "No worse than what it rolled" is a *minimum* only
    // when higher is better, and for a mod the game prints negative it is not: an exposure
    // implicit applying -11% to Cold Resistance is better at -13, and a minimum of -11 asks
    // for the weakest copies of it. The bundle's `better` says so for the ten stats that are
    // bad at any sign ("#% increased Damage taken"), and the roll's own sign covers the rest,
    // because the canonical wording already carries the direction — "#% reduced Mana Cost" is
    // stored as a negative increase. The one case this reads wrong is a negative roll of a
    // stat that also rolls positive, i.e. a resistance penalty, where less negative is better;
    // those are drawbacks on uniques and corrupted implicits rather than what a buyer searches.
    const bool lower_is_better = stat.better < 0 || roll.value.value_or(0) < 0;

    // **A number that is not a roll is not a bound.** A fixed modifier says the same thing on
    // every copy of itself, and asking the trade site to compare its number asks it to compare
    // a value it does not index the stat on. Measured, not inferred: the Baran map implicit
    // ("…drops by 20% of its value") returned 0 listings with `min: 20` against 1705 without
    // it, and "Area is influenced by The Elder" — whose number is not even in the clipboard,
    // but a constant the matcher substitutes for the influence — 0 against 10000. The filter
    // stays and only its number goes, so the search asks for the modifier being *present*,
    // which is the only thing a fixed modifier can be asked about.
    //
    // What says a number is fixed is that the game printed **no range beside it** — and that
    // only means anything on an item that printed ranges at all. With Advanced Mod Descriptions
    // off nothing carries one, so reading their absence as "fixed" would strip the bound off
    // every real roll on the item and search a rare for "has a life modifier". A map is the
    // exception and needs no such evidence: none of the numbers a map's implicits and enchants
    // carry is ever a roll.
    //
    // **A tier or a rank is itself a range**, whether or not the modifier rolls one inside it:
    // a different tier is a different number, so "no worse than what this one gave" is a real
    // question. It is also the only thing an eldritch implicit has to say so with — its
    // magnitude comes from the tier of the currency that put it there, so the clipboard prints
    // no range and states the rank instead: `{ Eater of Worlds Implicit Modifier (Lesser) }`.
    const bool ranked = m.tier > 0 || m.rank > 0 || !m.qualifier.empty();
    const bool fixed = !variable && !ranked && (s == Strategy::Map || ranges_printed);

    // How wide the asking is around that roll is the user's setting, not this layer's: see
    // `seed_bounds`. All this decides is whether there is a roll to seed from at all, and what
    // the tier gate is when the item printed one.
    f.tiered = has_bounds;
    if (fixed) {
        // presence only
    } else if (roll.value) {
        const Bounds b = seed_bounds(rm, *roll.value, roll.min, roll.max, f.dp, lower_is_better);
        f.min = b.min;
        f.max = b.max;
    }

    switch (s) {
        case Strategy::Modifiers:
            f.enabled = true;
            break;
        case Strategy::BaseItem:
            // The point of a base-item search is that the rolls do *not* matter — except a
            // fractured one, which the buyer keeps, and an implicit that is not the base's own.
            f.enabled = m.type == data::ModType::Fractured ||
                        (m.type == data::ModType::Implicit && (variable || it.synthesised));
            break;
        case Strategy::Unique:
            // A unique's fixed mods are the same on every copy of it, so filtering on them
            // only costs results. What does matter: a roll a range proves is variable, a mod
            // something *added* to the item ("Foulborn Unique Modifier"), anything the player
            // crafted onto this copy, and an implicit corruption or synthesis could have put
            // there — there is no way to tell an added implicit from the unique's own without
            // per-unique mod data.
            f.enabled = variable || m.added_unique() || added_to_copy(m.type) ||
                        (m.type == data::ModType::Implicit && (it.corrupted || it.synthesised));
            break;
        case Strategy::Map:
            // Only the modifiers that are a property of *this* map rather than of the roll a
            // Chaos Orb could redo: what the area itself is (the implicit, which names the
            // boss, the influence or the memory) and what somebody paid to enchant onto it.
            // The affixes are handled entirely by their count — see `map_affix_count`.
            f.enabled = true;
            break;
        default:
            f.enabled = false;
            break;
    }
    return f;
}

/// What this modifier can roll on this unique, in the terms its filter is compared on: the
/// average of both numbers for an added-damage mod and the first number otherwise, scaled by
/// a catalyst exactly as the clipboard's own roll already was.
Roll unique_range(const data::UniqueModFilter& uf, const Modifier& m, bool averaged) {
    Roll r;
    if (uf.ranges.empty()) return r;
    double lo = uf.ranges.front().first, hi = uf.ranges.front().second;
    if (averaged && uf.ranges.size() > 1) {
        lo = hi = 0;
        for (const auto& [a, b] : uf.ranges) {
            lo += a;
            hi += b;
        }
        const auto n = static_cast<double>(uf.ranges.size());
        lo /= n;
        hi /= n;
    }
    if (m.roll_incr != 0) {
        const int dp = m.match && m.match->stat ? m.match->stat->dp : 0;
        lo = data::incr_roll(lo, m.roll_incr, dp);
        hi = data::incr_roll(hi, m.roll_incr, dp);
    }
    r.min = lo;
    r.max = hi;
    return r;
}

/// Fold the bundle's per-unique modifier data into the plan.
///
/// Without it `Strategy::Unique` can only enable a roll whose printed range proves it is
/// variable, which leaves out the case the dataset exists for: a modifier the unique picks
/// from a pool prints exactly like one every copy has — each of Ralakesh's three charge
/// modifiers rolls 1..1 — and it is the difference between a chaos and a hundred divines. It
/// also supplies the range whatever the user's Advanced Mod Descriptions setting is.
///
/// The join is on the trade id and never on the wording: wordings are shared by two stat
/// records often enough that the ids are the only thing telling those apart. Nothing here
/// ever *disables* a filter — the item's own printed range outranks a record about the
/// unique in general.
void apply_unique_mods(const data::GameData& gd, const Item& it, SearchPlan& p,
                       const RangeMatch& rm) {
    // Unidentified, or a name the bundle does not know; both already have their own note.
    if (p.name.empty()) return;

    const data::UniqueMods* um = gd.find_unique_mods(p.name);
    if (!um) {
        const bool anything_left_out =
            std::any_of(p.stats.begin(), p.stats.end(), [](const StatFilter& f) {
                return !f.enabled && f.type == data::ModType::Explicit;
            });
        if (anything_left_out)
            p.notes.push_back(
                gd.has_unique_mods()
                    ? "no modifier data for \"" + p.name +
                          "\" in this bundle, so a modifier that is one of a pool of "
                          "possibilities cannot be told from a fixed one"
                    : "this data bundle carries no per-unique modifier data, so a modifier "
                      "that is one of a pool of possibilities cannot be told from a fixed one");
        return;
    }

    struct Entry {
        const data::UniqueModFilter* filter;
        const data::UniqueModPool* pool; ///< null for a modifier every copy has
    };
    std::unordered_map<std::string_view, Entry> by_id;
    for (const data::UniqueMod& m : um->fixed)
        for (const data::UniqueModFilter& f : m.filters)
            if (!f.trade_id.empty()) by_id.emplace(f.trade_id, Entry{&f, nullptr});
    // A pool wins over a fixed entry of the same stat: what is being searched for is the copy
    // that rolled it, and the fixed half of the pair is on every copy either way.
    for (const data::UniqueModPool& pool : um->pools)
        for (const data::UniqueMod& m : pool.mods)
            for (const data::UniqueModFilter& f : m.filters)
                if (!f.trade_id.empty()) by_id[f.trade_id] = Entry{&f, &pool};

    for (StatFilter& f : p.stats) {
        if (!f.mod_index) continue; // a pseudo total, which no unique's record is about
        const auto e = by_id.find(f.id);
        if (e == by_id.end()) {
            // Either something added to this copy of the item, or a modifier the source has
            // not caught up with, or one it cannot search. Nothing says it is fixed, so it is
            // reported rather than silently left out of the search — but only for a modifier
            // the record is *about*. A crafted one is absent from it by definition, and saying
            // so reads as a failure to recognise a modifier that is right there in the list.
            if (!f.enabled && !added_to_copy(f.type))
                p.notes.push_back("not in the modifier data for \"" + um->name +
                                  "\", so not searched: " + one_line(it.mods[*f.mod_index]));
            continue;
        }
        const Modifier& m = it.mods[*f.mod_index];
        Roll r = unique_range(*e->second.filter, m, m.match && averaged_roll(*m.match));
        // Only trust a range that describes the roll in front of us. The bundle carries no
        // decimal count for every stat, so a range can arrive a hundred times the roll it
        // bounds ("0.4% of Physical Attack Damage Leeched as Mana" against 40..40) — and a
        // legacy roll genuinely sits outside its own. Either way the bounds are not this
        // item's, and calling a modifier variable on them would be a guess. Pool membership
        // is a fact about the item rather than a number, so it stands regardless.
        const Roll printed = m.match ? roll_for(*m.match) : Roll{};
        if (r.min && r.max && printed.value &&
            (*printed.value < *r.min - 1e-6 || *printed.value > *r.max + 1e-6))
            r = Roll{};
        // The item's own printed range outranks a record about the unique in general, so this
        // only fills a gap the clipboard left.
        if (!f.roll_min && !f.roll_max) {
            f.roll_min = r.min;
            f.roll_max = r.max;
        }
        if (e->second.pool) {
            f.pooled = true;
            f.pool_hint = e->second.pool->hint;
            f.enabled = true;
        } else if (r.min && r.max && *r.min != *r.max) {
            // Fixed for the item, variable in its roll: the same judgement a printed range
            // drives, now made whether or not the game printed one.
            f.enabled = true;
        }
        // And a range is a range whichever source stated it. `to_filter` leaves a modifier
        // unbounded when the clipboard printed no range for it, because it cannot tell a fixed
        // one from an owner with Advanced Mod Descriptions off — but here the record says
        // outright that this one rolls, so the roll is a bound after all.
        if (!f.min && !f.max && printed.value && r.min && r.max && *r.min != *r.max) {
            const bool lower_is_better =
                (m.match->stat && m.match->stat->better < 0) || *printed.value < 0;
            const Bounds b = seed_bounds(rm, *printed.value, r.min, r.max, f.dp, lower_is_better);
            f.min = b.min;
            f.max = b.max;
            f.tiered = true; // the record stated a range, which is what the tiered modes gate on
        }
    }

    for (const std::string& u : um->unlisted)
        p.notes.push_back(
            "the modifier data states but does not enumerate this, so it is not "
            "searched: " +
            u);
}

/// Fold filters that share a trade id into one, summing their bounds.
///
/// An item with "+28 to maximum Life" and "+89 to maximum Life" is indexed by trade as one
/// stat worth 117, so two separate filters would each be compared against that total and the
/// smaller of the two would decide the search on its own. Summing is what the site actually
/// searches on: 104 to 117 for that pair.
void merge_same_stat(std::vector<StatFilter>& stats) {
    for (size_t i = 0; i < stats.size(); ++i) {
        for (size_t j = i + 1; j < stats.size();) {
            if (stats[j].id != stats[i].id) {
                ++j;
                continue;
            }
            StatFilter& into = stats[i];
            const StatFilter& from = stats[j];
            const auto add = [](std::optional<double>& a, const std::optional<double>& b) {
                if (a && b)
                    *a += *b;
                else
                    a.reset(); // one side unbounded makes the total unbounded
            };
            add(into.min, from.min);
            add(into.max, from.max);
            add(into.roll_min, from.roll_min);
            add(into.roll_max, from.roll_max);
            into.tiered = into.tiered && from.tiered;
            into.enabled = into.enabled || from.enabled;
            if (from.pooled && !into.pooled) {
                into.pooled = true;
                into.pool_hint = from.pool_hint;
            }
            into.text += "\n" + from.text;
            if (from.mod_index) into.merged.push_back(*from.mod_index);
            into.merged.insert(into.merged.end(), from.merged.begin(), from.merged.end());
            stats.erase(stats.begin() + static_cast<ptrdiff_t>(j));
        }
    }
}

/// A modifier a map is *searched* on. A map's prefixes and suffixes are deliberately not among
/// them and are not even offered: they are re-rollable with one Chaos Orb, the buyer is choosing
/// how dangerous a map they want rather than which affix it has, and a query naming them would
/// return the one copy in the league that rolled that set. What is left is what a currency
/// cannot change — the implicit, which says whose area this is — and what somebody paid to put
/// there. How many affixes it has still matters, and that goes in as a total; see below.
bool map_searched_mod(const Modifier& m) {
    return m.type == data::ModType::Implicit || m.type == data::ModType::Enchant;
}

/// How many affixes the map rolled, or nothing when the clipboard does not say.
///
/// A rare map takes six, and only corruption can push it to eight — which is most of what an
/// eight-mod map is worth, and the reason trade indexes the count as a pseudo stat at all. One
/// affix can print several lines (a hybrid "Players have 30% less Armour / 40% reduced Chance to
/// Block"), so the continuation lines are what is counted out; and the side of the pool is only
/// ever printed by Advanced Mod Descriptions, so with that off there is no count to give rather
/// than a count of zero.
std::optional<int> map_affix_count(const Item& it) {
    int n = 0;
    bool any_explicit = false;
    for (const Modifier& m : it.mods) {
        if (m.type != data::ModType::Explicit) continue;
        any_explicit = true;
        if (m.continuation) continue;
        if (m.affix == Affix::Prefix || m.affix == Affix::Suffix) ++n;
    }
    if (any_explicit && n == 0) return std::nullopt;
    return n;
}

/// Trade's `pseudo.*` totals for a map, which is everything about one that is not a modifier.
///
/// The four drop bonuses are printed by the game as **properties** — "More Maps: +70%", what a
/// Maven's chisel adds — and the site has no `map_filters` entry for any of them, so a pseudo
/// stat is the only way to ask. The affix count is the same shape: a fact about the whole item
/// with no single modifier behind it, which is why `StatFilter::mod_index` is optional.
void add_map_pseudo(const Item& it, SearchPlan& p) {
    struct Drop {
        const char* label; ///< the property the game prints
        const char* id;
        const char* text; ///< the trade site's own wording, so the two read alike
    };
    // Every "More" the chisels grant; there is no fifth pseudo stat in /api/trade/data/stats.
    static constexpr Drop kDrops[]{
        {"More Maps", "pseudo.pseudo_map_more_map_drops", "More Maps: #%"},
        {"More Scarabs", "pseudo.pseudo_map_more_scarab_drops", "More Scarabs: #%"},
        {"More Currency", "pseudo.pseudo_map_more_currency_drops", "More Currency: #%"},
        {"More Divination Cards", "pseudo.pseudo_map_more_card_drops", "More Divination Cards: #%"},
    };

    const auto pseudo = [&p](const char* id, const char* text, double min, bool enabled) {
        StatFilter f;
        f.id = id;
        f.text = text;
        f.type = data::ModType::Pseudo;
        f.min = min;
        f.enabled = enabled;
        p.stats.push_back(std::move(f));
    };

    for (const Drop& d : kDrops)
        for (const Property& prop : it.properties)
            if (prop.label == d.label && prop.num) pseudo(d.id, d.text, *prop.num, true);

    // Only on a corrupted map: below eight the count is what every rare map of its rarity has,
    // and filtering on it would drop the six-mod maps that are the same item.
    if (!it.corrupted) return;
    const std::optional<int> n = map_affix_count(it);
    if (!n)
        p.notes.emplace_back(
            "how many affixes this map has needs Advanced Mod Descriptions, "
            "so the search does not ask for the count");
    else if (*n > 0) // a corrupted white map has none, which is not something to ask for
        pseudo("pseudo.pseudo_number_of_affix_mods", "# Modifiers", *n, true);
}

/// Stop searching for a modifier the search is already asking about **by its result**.
///
/// A local roll is not something the item has beside its armour — it is part of the armour the
/// item displays, and the same is true of a weapon's damage rolls and of what a flat energy
/// shield prefix does to `es`. Filtering on both the number and the modifier behind it asks one
/// question twice, and the second asking is the brittle half: a flat roll and a local increase
/// reach the same armour by different routes, so naming *this* item's route rules out every
/// other way of arriving at the number the buyer actually wants.
///
/// So the derived value is what is imposed and the modifier behind it is only offered — left in
/// the list, not removed, since it can still be the thing the buyer wants. Conditional on the
/// derived filter being enabled: with nothing asking for the armour (a unique, where the
/// defences are offered rather than imposed) the modifier is all there is to ask about.
///
/// **A fractured roll is the exception and keeps its filter.** It cannot be re-rolled, it is
/// what survives every craft the buyer will do to the item afterwards, and trade indexes it in
/// a namespace of its own (`fractured.stat_…`, which is what `to_filter` already sends) — so
/// unlike every other route to the same armour, *which* modifier it is is the point of buying
/// the item at all.
void unimpose_derived_mods(const Item& it, SearchPlan& p) {
    const auto imposed = [&p](std::string_view key) {
        return std::any_of(p.numerics.begin(), p.numerics.end(),
                           [key](const NumericFilter& n) { return n.enabled && n.key == key; });
    };
    for (StatFilter& f : p.stats) {
        if (!f.enabled || !f.mod_index) continue;
        if (f.type == data::ModType::Fractured) continue;
        for (const std::string_view k : derived_filter_keys(it, it.mods[*f.mod_index]))
            if (imposed(k)) {
                f.enabled = false;
                break;
            }
    }
}

/// A gem is a name, a level and a quality — and deliberately nothing else.
///
/// The lines a gem prints are what the skill does, identical on every copy, so there is nothing
/// to filter on and the name plus those two numbers are the whole search. Both numbers are
/// matched **exactly**, the same reasoning as a map's tier: a level 21 gem is not a better
/// level 20 one, it is what the gem sells as, and the same goes for the quality bracket. A
/// floor would put 21/23 corrupted gems in the results for a 20/20 and price the wrong item.
/// Corruption is already matched exactly for every strategy, and on a gem it is the hard split
/// between the two markets: it is what allows level 21 and quality 23 at all.
///
/// The name is the record's, never the printed one. A Vaal gem prints the base skill and a
/// transfigured gem prints a name trade does not file it under — see `Item::gem_name` and
/// `BaseType::trade_name` — and a `type` term trade does not know matches nothing, which reads
/// as a gem nobody is selling rather than as a search that could not be built. So an unresolved
/// gem gets no search at all and says why; poe.ninja still prices it.
void plan_gem(const Item& it, SearchPlan& p) {
    if (!it.base) {
        const std::string name = "\"" + it.gem_name() + "\" is not in this data bundle";
        p.notes.push_back(it.transfigured
                              ? name + ": trade files a transfigured gem under the skill it "
                                       "alters, and only a data build carrying its printed "
                                       "name can say which one this is"
                              : name);
        return;
    }
    p.type = it.base->trade_name.empty() ? it.base->name : it.base->trade_name;
    p.discriminator = it.base->trade_disc;

    const auto exact = [&p](const char* key, const char* label, std::optional<int> v) {
        if (!v) return;
        add_numeric(p, key, label, static_cast<double>(*v), true, 0, {},
                    static_cast<double>(*v));
    };
    exact("gem_level", "Gem Level", it.gem_level);
    // Always, and at zero as readily as at twenty: an unquality gem is a different thing from a
    // 20% one, and leaving the filter off would price it as whatever the cheapest quality is.
    exact("quality", "Quality", it.quality.value_or(0));
}

/// The property a Valdo's Puzzle Box map states its payout in, or null on any other map. No
/// other map prints one, which is what makes it the marker as well as the thing searched for.
const Property* reward_property(const Item& it) {
    for (const Property& prop : it.properties)
        if (prop.label == "Reward") return &prop;
    return nullptr;
}

/// Whether a character who dies in the map is sent to the Void — the one thing about a Valdo
/// map's modifiers a buyer chooses on, and it is chosen in **both** directions. A map that
/// voids is a different item from one that does not, so the copy in hand decides which
/// question is asked: present, and the search asks for it; absent, and it asks for the
/// absence, which is what `StatFilter::negated` is for. Leaving it open prices the two
/// together, which is the whole of what there was to get wrong here.
void add_void_rule(const data::GameData& gd, const Item& it, SearchPlan& p) {
    static constexpr std::string_view kVoid = "Players who Die in area are sent to the Void";
    const data::Stat* stat = gd.find_stat_by_ref(kVoid);
    if (!stat) return; // an older bundle: the reward is still the search, so say nothing
    const std::vector<std::string>& ids = stat->trade_ids(data::ModType::Explicit);
    if (ids.empty()) return;

    StatFilter f;
    f.id = ids.front();
    f.text = std::string(kVoid);
    f.enabled = true;
    for (size_t i = 0; i < it.mods.size(); ++i) {
        const Modifier& m = it.mods[i];
        if (m.match && m.match->stat && m.match->stat->ref == kVoid) {
            f.mod_index = i;
            f.text = m.text();
            break;
        }
    }
    f.negated = !f.mod_index;
    p.stats.push_back(std::move(f));
}

/// A map is priced on where it goes and what was spent on it, and on nothing else.
///
/// Which area it is comes from the tier where the base line prints one ("Map (Tier 16)" — every
/// ordinary map shares the one base type now, so the tier is the whole of its identity) and from
/// the base's own name where it does not ("Shaper Guardian Map", "Nightmare Map"). A unique map
/// is its name plus that same tier.
///
/// A **Valdo map** is the one shape that is not any of that: it is bought for the unique it
/// pays out, its quantity and pack size come from unique modifiers rather than from an affix
/// roll, and the only thing about those modifiers a buyer picks on is whether dying in the map
/// voids the character. So the reward is imposed, the void rule goes in both directions, and
/// the drop bonuses are offered rather than asked for.
void plan_map(const data::GameData& gd, const Item& it, SearchPlan& p) {
    p.rarity = it.rarity == Rarity::Unique ? "unique" : "nonunique";
    if (it.rarity == Rarity::Unique) {
        p.name = it.unique_entry ? it.unique_entry->name : it.name;
        if (!it.identified)
            p.notes.emplace_back(
                "unidentified: the clipboard does not say which unique map "
                "this is");
    }
    p.type = it.base_name;
    // "Map" is a type on trade *and* the prefix of every unique map's own entry, so it always
    // carries a discriminator; the unique's record repeats it, which is what lets one field
    // serve both terms. It is **load-bearing** rather than a tie-break: a query sending the
    // type as a bare "Map" is accepted and matches nothing at all, which reads as an empty
    // market rather than as a search that could not be built.
    if (const data::BaseType* b = it.rarity == Rarity::Unique ? it.unique_entry : it.base)
        if (!b->trade_disc.empty()) p.discriminator = b->trade_disc;
    if (p.discriminator.empty() && it.map_tier)
        p.notes.emplace_back("\"" + it.base_name +
                             "\" is not a base in this data bundle, and trade matches no map "
                             "without the discriminator its record carries");
    // Blight is a filter and not a type: the base line is the only place the clipboard says so,
    // and `resolve_base` has already pointed the base at the ordinary map it shares with every
    // other one. Never asked for in the negative — the two flags are mutually exclusive, so a
    // blighted map's own search already excludes the ravaged ones and vice versa.
    p.blighted = it.blighted;
    p.blight_ravaged = it.blight_ravaged;

    // Exact, not a floor: a tier-16 map is not a better tier-14 one, it is a different area.
    if (it.map_tier)
        add_numeric(p, "map_tier", "Map Tier", static_cast<double>(*it.map_tier), true, 0, {},
                    static_cast<double>(*it.map_tier));

    // A Valdo map's own numbers come from the unique modifiers it is stamped with rather than
    // from a roll, so they say nothing about which of them a buyer wants; the reward does.
    const Property* reward = reward_property(it);
    if (reward) {
        // The site takes the **unique's own name** here and rejects anything else outright
        // ("Unknown reward output provided", which fails the whole search rather than widening
        // it) — so the "Foil " the game prints in front of the payout has to go, and only a
        // name the bundle confirms is a unique is ever sent.
        p.map_reward = find_unique_in(gd, reward->value);
        if (p.map_reward.empty())
            p.notes.push_back("\"" + reward->value +
                              "\" is not a unique in this data bundle, and the trade site "
                              "rejects a reward it does not know, so the search is for any "
                              "map of this kind");
        add_void_rule(gd, it, p);
    }

    struct Bonus {
        const char* label; ///< the property the game prints
        const char* key;   ///< the trade `map_filters` filter
        bool enabled;
    };
    // Quantity and pack size are what a map is run for; rarity is a preference, and imposing it
    // would drop the cheaper copies of the same map that most buyers are actually after.
    static constexpr Bonus kBonuses[]{
        {"Item Quantity", "map_iiq", true},
        {"Monster Pack Size", "map_packsize", true},
        {"Item Rarity", "map_iir", false},
    };
    for (const Bonus& b : kBonuses)
        for (const Property& prop : it.properties)
            if (prop.label == b.label && prop.num)
                add_numeric(p, b.key, b.label, *prop.num, b.enabled && !reward);
}

/// The `misc_filters` booleans every plan carries, and whether the user is offered a say.
///
/// The rule is one line: **the search asks the item to be what it is**, and it says so out loud
/// only where that is not the ordinary answer. An uncorrupted, unmirrored, identified item is
/// what nearly every check is about, so those three are imposed without a row; a corrupted,
/// mirrored or unidentified one is a different product, and *that* is worth a row, because it is
/// the one a buyer might want to widen back out.
///
/// Two of the five are asked in one direction only. Synthesis and fracturing are evidence about
/// the copy in hand rather than a choice — an ordinary item's search has no reason to rule out
/// the fractured ones, which are strictly more constrained versions of it.
///
/// **`identified` is not asked of a gem or a currency item**, measured rather than assumed:
/// `identified: true` returns 0 listings under `category: gem` and 0 for a Facetor's Lens (10000
/// and 177 without it), because trade indexes the flag only for what can be unidentified. A
/// filter that matches nothing reads as an item nobody is selling. `mirrored: false` is safe
/// everywhere and was checked the same way.
void add_item_flags(const Item& it, SearchPlan& p) {
    if (p.strategy == Strategy::Unsupported) return;
    // Corruption is never incidental: it fixes the item's mods forever and splits the market in
    // two, so it is matched exactly whatever the strategy.
    add_flag(p, "corrupted", "Corrupted", it.corrupted, it.corrupted);
    add_flag(p, "mirrored", "Mirrored", it.mirrored, it.mirrored);
    if (p.strategy == Strategy::BaseItem || p.strategy == Strategy::Modifiers ||
        p.strategy == Strategy::Unique || p.strategy == Strategy::Map)
        add_flag(p, "identified", "Identified", it.identified, !it.identified);
    if (it.synthesised) add_flag(p, "synthesised_item", "Synthesised", true, false);
    if (it.fractured_item) add_flag(p, "fractured_item", "Fractured", true, false);
}

/// The three `misc_filters` intervals that come off a property line rather than off a modifier.
///
/// None of them is a roll, so none has a tier to gate against and none gets a window around it:
/// the number is what this copy has, and all a filter can say is "no worse". Which side that
/// leaves open is the whole of the judgement here, and it differs per property:
///
/// - **Memory Strands** (1–100) are spent to raise the tier of a modifier a craft adds, so more
///   of them is more of the thing being bought — a floor, ticked.
/// - **Intangibility** is the opposite: it is the penalty an item accrues from Allflame crafting,
///   the chance the *next* craft on it comes back with one outcome instead of several. Less is
///   better, so it is a **ceiling** — and it is left unticked, because a buyer who is not going
///   to craft on the item does not care what it has accrued.
/// - **Stored Experience** is what a Facetor's Lens is, and the only thing telling two apart.
void add_property_filters(const Item& it, SearchPlan& p) {
    if (const Property* m = property_of(it, "Memory Strands"); m && m->num)
        add_numeric(p, "memory_level", "Memory Strands", *m->num, true);
    if (const Property* i = property_of(it, "Intangibility"); i && i->num)
        add_numeric(p, "intangibility", "Intangibility", std::nullopt, false, 0, {}, *i->num);
    if (const Property* x = property_of(it, "Stored Experience"); x && x->num)
        add_numeric(p, "stored_experience", "Stored Experience", *x->num, true);
}

} // namespace

std::string_view to_string(Strategy s) { return kStrategies[static_cast<size_t>(s)]; }

bool SearchPlan::has_enabled_stats() const {
    return std::any_of(stats.begin(), stats.end(), [](const StatFilter& f) { return f.enabled; });
}

const FlagFilter* SearchPlan::flag(std::string_view key) const {
    for (const FlagFilter& f : flags)
        if (f.key == key) return &f;
    return nullptr;
}

Strategy default_strategy(const Item& it) {
    // A map is priced on none of the things a rare is, at any rarity it prints: pricing one as
    // gear would search for a chest piece carrying map modifiers.
    if (it.is_map()) return Strategy::Map;
    // A map item splits on whether it prints an **item level**, which is what says whether it
    // is a bulk good or an item. A scarab, an ember, a splinter or a breachstone prints none:
    // every copy is identical, there is nothing to filter on, and they change hands on the
    // in-game currency exchange rather than through a listing — so pricing one as a base type
    // asked poe.ninja a question about crafting bases and the trade site one about item level
    // and influences, and none of that exists here. One that *does* print an item level can
    // carry a rarity and its own quantity/rarity modifiers exactly as a map does, and is sold
    // as an item, so it falls through to the ordinary rules below.
    if (it.is_map_fragment() && !it.item_level) return Strategy::Currency;
    switch (it.rarity) {
        case Rarity::Unique: return Strategy::Unique;
        case Rarity::Normal: return Strategy::BaseItem;
        case Rarity::Magic:
        case Rarity::Rare: return Strategy::Modifiers;
        case Rarity::Currency: return Strategy::Currency;
        case Rarity::DivinationCard: return Strategy::Currency;
        case Rarity::Gem: return Strategy::Gem;
        default: return Strategy::Unsupported;
    }
}

SearchPlan build_plan(const data::GameData& gd, const Item& it, const Derived& d,
                      std::optional<Strategy> force, const RangeMatch& rm) {
    SearchPlan p;
    p.strategy = force.value_or(default_strategy(it));
    p.category = std::string(gd.trade_category_for(it.item_class));
    if (p.category.empty() && !it.item_class.empty())
        p.notes.push_back("item class \"" + it.item_class +
                          "\" maps to no trade category in this data bundle");

    add_item_flags(it, p);
    p.rarity = p.strategy == Strategy::Unique ? "unique" : "nonunique";
    if (p.strategy == Strategy::BaseItem || p.strategy == Strategy::Modifiers)
        p.influences = it.influences;

    switch (p.strategy) {
        case Strategy::Unique:
            // The resolved record's name, not the printed one: a unique can be renamed by
            // what was done to it ("Foulborn Romira's Banquet"), and trade knows the unique.
            p.name = it.unique_entry ? it.unique_entry->name : it.name;
            p.type = it.base_name;
            if (it.unique_entry && !it.unique_entry->trade_disc.empty())
                p.discriminator = it.unique_entry->trade_disc;
            if (!it.identified)
                p.notes.emplace_back(
                    "unidentified: the clipboard does not say which unique this is — picking "
                    "it from the base's uniques is not implemented yet");
            else if (!it.unique_entry)
                p.notes.push_back("\"" + it.name + "\" is not in this data bundle");
            break;
        case Strategy::BaseItem:
            p.type = it.base_name;
            if (it.base && !it.base->trade_disc.empty()) p.discriminator = it.base->trade_disc;
            add_numeric(p, "ilvl", "Item Level",
                        it.item_level ? std::optional<double>(*it.item_level) : std::nullopt, true);
            break;
        case Strategy::Modifiers:
            // Deliberately not constrained to this base: a rare is bought for its mods, and
            // the class is what decides where they can go. A flask is the exception — its base
            // is half of what its mods are worth. "25% increased Movement Speed" is a
            // sought-after suffix on a Quicksilver Flask and nothing on a Ruby one, and the
            // tier decides what a Life Flask recovers; trade files every flask under one
            // category, so the type term is the only place to say which. Only ever a
            // *resolved* base: an unstripped magic name ("Surgeon's Quicksilver Flask of the
            // Cheetah") as the type matches nothing, which reads as nobody selling one.
            if (it.is_flask()) {
                if (it.base) {
                    p.type = it.base_name;
                    if (!it.base->trade_disc.empty()) p.discriminator = it.base->trade_disc;
                } else {
                    p.notes.push_back("\"" + it.base_type +
                                      "\" is not a base in this data bundle, so the search "
                                      "cannot name the flask type and covers every flask");
                }
            }
            add_numeric(p, "ilvl", "Item Level",
                        it.item_level ? std::optional<double>(*it.item_level) : std::nullopt,
                        false);
            break;
        case Strategy::Map: plan_map(gd, it, p); break;
        case Strategy::Gem: plan_gem(it, p); break;
        default:
            // Currency is priced by poe.ninja and by the in-game exchange rather than by a stat
            // query — bulk is what it sells in, and a stat filter has nothing to say about a
            // stack of orbs. So this is not a gap to warn about, it is where the search stops
            // and the reference price is the answer. Only a class nothing prices is worth a note.
            if (p.strategy == Strategy::Unsupported)
                p.notes.push_back("pricing an item of class \"" + it.item_class +
                                  "\" is not implemented yet");
            // **Except the one currency item that is not a bulk good.** A Facetor's Lens carries
            // the experience stored in it, every copy holds a different amount, and that number
            // is the whole of what one is worth — so they are listed individually rather than
            // traded by the stack, and naming the type is all a search needs. Same shape as the
            // map fragment that prints an item level: what says a currency item is not
            // interchangeable is that it prints something no other copy of it does. poe.ninja
            // still prices it in the currency market, which is the floor under the search.
            else if (p.strategy == Strategy::Currency && property_of(it, "Stored Experience")) {
                p.type = it.base ? it.base->name : it.base_name;
                if (it.base && !it.base->trade_disc.empty()) p.discriminator = it.base->trade_disc;
            }
            break;
    }

    if (p.strategy != Strategy::Unsupported) {
        // Whether this owner has Advanced Mod Descriptions on, which is what makes a modifier
        // printing no range evidence that it does not roll one. Asked of the whole item because
        // that is what the setting is a property of; see `to_filter`'s bound rule.
        const bool ranges_printed =
            std::any_of(it.mods.begin(), it.mods.end(),
                        [](const Modifier& m) { return m.match && !m.match->roll_bounds.empty(); });
        for (size_t i = 0; i < it.mods.size(); ++i) {
            // A map's affixes are not filters and are not notes either: they are left out on
            // purpose, in a place where the reader can see them (the item beside the panel),
            // and "unrecognised modifier: Players have 25% less Accuracy Rating" would charge
            // the check with failing at something it deliberately did not attempt.
            if (p.strategy == Strategy::Map && !map_searched_mod(it.mods[i])) continue;
            if (std::optional<StatFilter> f = to_filter(it, i, p.strategy, ranges_printed, rm)) {
                p.stats.push_back(std::move(*f));
                continue;
            }
            const Modifier& m = it.mods[i];
            if (m.match && m.match->stat)
                p.notes.push_back("no " + std::string(data::trade_prefix(m.match->mod_type)) +
                                  " trade id: " + one_line(m));
            else if (m.lines.size() == 1 &&
                     gd.find_stats(data::placeholder_form(m.lines.front())).size() > 1)
                // The matcher refuses to guess between two stats that share a wording and are
                // both searchable; telling them apart needs context this layer does not have
                // yet. Saying so beats searching for whichever came first in the file.
                p.notes.push_back("ambiguous wording, not searched: " + one_line(m));
            else
                p.notes.push_back("unrecognised modifier: " + one_line(m));
        }
        // Before the merge, while every filter still points at the modifier it came from.
        if (p.strategy == Strategy::Unique) apply_unique_mods(gd, it, p, rm);
        if (p.strategy == Strategy::Map) add_map_pseudo(it, p);
        merge_same_stat(p.stats);
    }

    if (p.strategy == Strategy::BaseItem || p.strategy == Strategy::Modifiers ||
        p.strategy == Strategy::Unique) {
        // Above 20% the quality is itself worth something and is not free to change.
        if (it.quality.value_or(0) > 20)
            add_numeric(p, "quality", "Quality", static_cast<double>(*it.quality), true);
        // A unique's defences and damage follow from which unique it is, so filtering on them
        // only drops listings; they are offered, not imposed.
        const bool impose = p.strategy != Strategy::Unique;
        add_defences(p, it, d, impose);
        add_weapon(p, it, d, impose);
        // Last, because it is a question about the numerics that were just added.
        unimpose_derived_mods(it, p);
    }
    // Driven by the properties being printed rather than by the strategy: what carries them is
    // what a crafting mechanic touched, and that is a fact about the copy in hand.
    if (p.strategy != Strategy::Unsupported) add_property_filters(it, p);
    // A gem is nothing but its own effect, so saying this about one is noise.
    if (!it.inherent_lines.empty() && it.rarity != Rarity::Gem)
        p.notes.emplace_back("the base's own effect is not part of the search");
    return p;
}

} // namespace ppc::item
