#include "item/plan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

#include "data/stat_normalize.hpp"

namespace ppc::item {
namespace {

constexpr std::array<std::string_view, 6> kStrategies{
    "Base item", "Modifiers", "Unique", "Currency", "Gem", "Unsupported"};

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
                 std::optional<double> min, bool enabled, int dp = 0, std::string note = {}) {
    if (!min) return;
    NumericFilter f;
    f.key = std::move(key);
    f.label = std::move(label);
    f.min = min;
    f.enabled = enabled;
    f.dp = dp;
    f.note = std::move(note);
    p.numerics.push_back(std::move(f));
}

/// The 20%-quality note that explains why a filter's number is not the one on the item.
/// Nothing to say at exactly 20%, and above it the item's own number is what is searched.
std::string quality_note(const Item& it) {
    return it.quality.value_or(0) < 20 ? "normalised to 20% quality" : std::string();
}

void add_defences(SearchPlan& p, const Item& it, const Derived& d, bool enabled) {
    const std::string note = quality_note(it);
    struct Entry {
        const char* key;
        const char* label;
        const std::optional<int>& value;
    };
    // The percentile is the base's one roll, so it belongs to the item and is stated once,
    // on whichever defence the item lists first.
    bool said_percentile = false;
    for (const Entry& e : std::initializer_list<Entry>{
             {"ar", "Armour", d.search_armour},
             {"ev", "Evasion", d.search_evasion},
             {"es", "Energy Shield", d.search_energy_shield},
             {"ward", "Ward", d.search_ward}}) {
        if (!e.value) continue;
        std::string n = note;
        if (d.base_pct && !said_percentile) {
            if (!n.empty()) n += ", ";
            n += "base roll " + std::to_string(static_cast<int>(*d.base_pct * 100 + 0.5)) + "%";
            said_percentile = true;
        }
        add_numeric(p, e.key, e.label, static_cast<double>(*e.value), enabled, 0, std::move(n));
    }
}

void add_weapon(SearchPlan& p, const Item& it, const Derived& d, bool enabled) {
    if (!it.is_weapon()) return;
    const std::string note = quality_note(it);
    add_numeric(p, "dps", "Total DPS", d.search_dps, enabled, 1, note);
    add_numeric(p, "pdps", "Physical DPS", d.search_pdps, enabled, 1, note);
    add_numeric(p, "edps", "Elemental DPS", d.search_edps, enabled, 1);
    // Both are on plenty of items without mattering; the user enables them when they do.
    add_numeric(p, "aps", "Attacks per Second", it.attacks_per_second, false, 2);
    add_numeric(p, "crit", "Critical Strike Chance", it.crit_chance, false, 2);
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
std::optional<StatFilter> to_filter(const Item& it, size_t index, Strategy s) {
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

    if (s == Strategy::Modifiers && has_bounds) {
        f.min = roll.min;
        f.max = roll.max;
        f.tiered = true;
    } else if (roll.value) {
        (lower_is_better ? f.max : f.min) = roll.value;
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
void apply_unique_mods(const data::GameData& gd, const Item& it, SearchPlan& p) {
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
        const auto e = by_id.find(f.id);
        if (e == by_id.end()) {
            // Either something added to this copy of the item, or a modifier the source has
            // not caught up with, or one it cannot search. Nothing says it is fixed, so it is
            // reported rather than silently left out of the search — but only for a modifier
            // the record is *about*. A crafted one is absent from it by definition, and saying
            // so reads as a failure to recognise a modifier that is right there in the list.
            if (!f.enabled && !added_to_copy(f.type))
                p.notes.push_back("not in the modifier data for \"" + um->name +
                                  "\", so not searched: " + one_line(it.mods[f.mod_index]));
            continue;
        }
        const Modifier& m = it.mods[f.mod_index];
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
        f.unique_min = r.min;
        f.unique_max = r.max;
        if (e->second.pool) {
            f.pooled = true;
            f.pool_hint = e->second.pool->hint;
            f.enabled = true;
        } else if (r.min && r.max && *r.min != *r.max) {
            // Fixed for the item, variable in its roll: the same judgement a printed range
            // drives, now made whether or not the game printed one.
            f.enabled = true;
        }
    }

    for (const std::string& u : um->unlisted)
        p.notes.push_back("the modifier data states but does not enumerate this, so it is not "
                          "searched: " + u);
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
                if (a && b) *a += *b;
                else a.reset(); // one side unbounded makes the total unbounded
            };
            add(into.min, from.min);
            add(into.max, from.max);
            add(into.unique_min, from.unique_min);
            add(into.unique_max, from.unique_max);
            into.tiered = into.tiered && from.tiered;
            into.enabled = into.enabled || from.enabled;
            if (from.pooled && !into.pooled) {
                into.pooled = true;
                into.pool_hint = from.pool_hint;
            }
            into.text += "\n" + from.text;
            into.merged.push_back(from.mod_index);
            into.merged.insert(into.merged.end(), from.merged.begin(), from.merged.end());
            stats.erase(stats.begin() + static_cast<ptrdiff_t>(j));
        }
    }
}

} // namespace

std::string_view to_string(Strategy s) { return kStrategies[static_cast<size_t>(s)]; }

bool SearchPlan::has_enabled_stats() const {
    return std::any_of(stats.begin(), stats.end(), [](const StatFilter& f) { return f.enabled; });
}

Strategy default_strategy(const Item& it) {
    // A map is not priced on its modifiers, and trade's own map filters are not built: pricing
    // one as a rare would search for gear carrying map mods.
    if (it.item_class == "Maps") return Strategy::Unsupported;
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
                      std::optional<Strategy> force) {
    SearchPlan p;
    p.strategy = force.value_or(default_strategy(it));
    p.category = std::string(gd.trade_category_for(it.item_class));
    if (p.category.empty() && !it.item_class.empty())
        p.notes.push_back("item class \"" + it.item_class +
                          "\" maps to no trade category in this data bundle");

    // Corruption is never incidental: it fixes the item's mods forever and splits the market
    // in two, so it is matched exactly whatever the strategy.
    p.corrupted = it.corrupted;
    p.mirrored = it.mirrored;
    p.synthesised = it.synthesised;
    p.fractured = it.fractured_item;
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
            add_numeric(p, "ilvl", "Item Level", it.item_level ? std::optional<double>(*it.item_level)
                                                              : std::nullopt,
                        true);
            break;
        case Strategy::Modifiers:
            // Deliberately not constrained to this base: a rare is bought for its mods, and
            // the class is what decides where they can go.
            add_numeric(p, "ilvl", "Item Level",
                        it.item_level ? std::optional<double>(*it.item_level) : std::nullopt,
                        false);
            break;
        default:
            // Currency and gems are priced by poe.ninja rather than by a stat query — bulk is
            // what they sell in, and a stat filter has nothing to say about a stack of orbs.
            // So this is not a gap to warn about, it is where the search stops and the
            // reference price is the answer. Only a class nothing prices is worth a note.
            if (p.strategy == Strategy::Unsupported)
                p.notes.push_back("pricing an item of class \"" + it.item_class +
                                  "\" is not implemented yet");
            break;
    }

    if (p.strategy != Strategy::Unsupported) {
        for (size_t i = 0; i < it.mods.size(); ++i) {
            if (std::optional<StatFilter> f = to_filter(it, i, p.strategy)) {
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
        if (p.strategy == Strategy::Unique) apply_unique_mods(gd, it, p);
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
    }
    // A gem is nothing but its own effect, so saying this about one is noise.
    if (!it.inherent_lines.empty() && it.rarity != Rarity::Gem)
        p.notes.emplace_back("the base's own effect is not part of the search");
    return p;
}

} // namespace ppc::item
