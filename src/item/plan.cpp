#include "item/plan.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <unordered_map>
#include <utility>

#include "data/stat_normalize.hpp"
#include "item/resolve.hpp"

namespace ppc::item {
namespace {

constexpr std::array<std::string_view, static_cast<size_t>(Strategy::Unsupported) + 1>
    kStrategies{"Base item", "Modifiers", "Unique",    "Currency", "Gem",     "Map",
                "Beast",     "Ultimatum", "Heist",     "Sanctum",  "Logbook", "Unsupported"};

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

void add_option(SearchPlan& p, std::string key, std::string label, std::string option,
                std::string display, bool shown = false) {
    OptionFilter f;
    f.key = std::move(key);
    f.label = std::move(label);
    f.option = std::move(option);
    f.display = std::move(display);
    f.shown = shown;
    p.options.push_back(std::move(f));
}

void add_flag(SearchPlan& p, std::string key, std::string label, bool value, bool shown) {
    add_option(p, std::move(key), std::move(label), value ? "true" : "false", value ? "yes" : "no",
               shown);
}

/// The name the *trade site* files a record under, which is not always the one the client
/// printed. Three names can differ and the order between them matters:
///
/// - `trade_name`, where the site files the item somewhere else entirely — a transfigured gem
///   goes under the skill it alters, and sending what the clipboard printed matches nothing;
/// - `ref_name`, the English name every localised bundle carries beside the printed one,
///   because the trade API's `name`/`type` terms are English whatever language the client is;
/// - `name`, the printed one, which is the same string as `ref_name` on an English bundle and
///   on every bundle published before `refName` existed.
std::string_view wire_name(const data::BaseType* b) {
    if (!b) return {};
    if (!b->trade_name.empty()) return b->trade_name;
    return b->ref_name.empty() ? b->name : b->ref_name;
}

/// The base as trade knows it, or what the client printed when nothing resolved — in which
/// case the search is as good as the bundle allowed, which is what the plan's notes say.
std::string base_wire_name(const Item& it) {
    const std::string_view n = wire_name(it.base);
    return n.empty() ? it.base_name : std::string(n);
}

/// A property the game prints as `Label: value`, turned into the `misc_filters` interval trade
/// indexes it under. These are not rolls and there is no tier behind them — the number is
/// simply what this copy has — so the filter is one-sided, and which side it is open on is what
/// "better" means for that property.
const Property* property_of(const Item& it, data::PropertyKey key) {
    for (const Property& p : it.properties)
        if (p.key == key) return &p;
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
bool property_augmented(const Item& it, data::PropertyKey key) {
    return std::any_of(it.properties.begin(), it.properties.end(),
                       [key](const Property& p) { return p.key == key && p.augmented; });
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

/// At or below this, sockets and links are the ordinary case and asking about them only drops
/// listings; at or above it they are most of what the item is worth. Five is where the game puts
/// the line too — five-linking is the step that costs, and the market prices 4-link and 3-link the
/// same as unlinked.
constexpr int kSocketsWorthAsking = 5;

/// The two numbers a linked item is bought for.
///
/// **Both are always offered and neither is always asked.** A six-socket, six-linked chest priced
/// without them is priced as the wrong item — that was the bug — but imposing them on a three-link
/// rare is the mirror of it, since every listing that would have answered has whatever sockets it
/// happens to have. So the count decides: at five or six it is a row, ticked; below that it goes
/// under the expandable section, where a buyer who *does* mean "and four-linked" can still say so.
///
/// They are separate filters because they are separate questions. Six sockets unlinked and six
/// linked are different items at very different prices, and the trade site asks about them in two
/// fields for the same reason.
void add_sockets(SearchPlan& p, const Item& it) {
    struct Entry {
        const char* key;
        const char* label;
        int count;
    };
    for (const Entry& e : {Entry{"sockets", "Sockets", it.socket_count},
                           Entry{"links", "Links", it.link_count}}) {
        if (e.count <= 0) continue;
        const bool worth = e.count >= kSocketsWorthAsking;
        // A floor and no ceiling, like every other numeric: someone shopping for a five-link
        // takes a six-link, and the same buyer would not thank a filter that ruled it out.
        add_numeric(p, e.key, e.label, static_cast<double>(e.count), worth);
        p.numerics.back().hidden = !worth;
    }
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
                enabled && property_augmented(it, data::PropertyKey::AttacksPerSecond), 2);
    add_numeric(p, "crit", "Critical Strike Chance", it.crit_chance,
                enabled && property_augmented(it, data::PropertyKey::CriticalStrikeChance), 2);
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
            f.enabled = variable || m.added_unique || added_to_copy(m.type) ||
                        (m.type == data::ModType::Implicit && (it.corrupted || it.synthesised));
            break;
        case Strategy::Map:
            // Only the modifiers that are a property of *this* map rather than of the roll a
            // Chaos Orb could redo: what the area itself is (the implicit, which names the
            // boss, the influence or the memory) and what somebody paid to enchant onto it.
            // The affixes are handled entirely by their count — see `map_affix_count`.
            f.enabled = true;
            break;
        case Strategy::Heist:
            // The map argument with the tick left off instead of the whole row. A blueprint's
            // **enchant** is what the run is for and somebody paid to put it there; its other
            // modifiers are the danger it will hold, which is rolled and re-rollable, so they
            // are offered and not imposed — seven ticked hazards ask for one copy in the world.
            f.enabled = m.type == data::ModType::Enchant;
            break;
        case Strategy::Sanctum:
            // A sanctum's affixes are not a roll somebody could redo — the run is already under
            // way and nothing can be applied to it again, which is what "Unmodifiable" on the
            // item means. They are as much a part of what is being bought as its resolve is.
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

/// The filter whose modifier printed `line`, or null. The join is the game's own wording,
/// which is what both sides have: the per-unique data's unlisted pools are stated as the lines
/// the client prints, and so are the modifiers the parser read off the clipboard.
StatFilter* filter_saying(SearchPlan& p, const Item& it, std::string_view line) {
    for (StatFilter& f : p.stats) {
        if (!f.mod_index) continue;
        for (const std::string& l : it.mods[*f.mod_index].lines)
            if (l == line) return &f;
    }
    return nullptr;
}

/// Whether `line` is one of the item's **property** lines, as the game prints it — either
/// "Label: value" or, for the ones the game writes as a sentence, the value alone.
///
/// The other half of `filter_saying`: both answer "is this already on screen", and the per-unique
/// data does not distinguish a property from a modifier. A unique heist contract is the case —
/// its client, area level, heist target and job requirement are listed there as modifiers and
/// printed by the game as properties.
bool printed_as_property(const Item& it, std::string_view line) {
    for (const Property& p : it.properties) {
        if (p.label.empty() ? p.value == line : line == p.label + ": " + p.value) return true;
    }
    return false;
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
            // said rather than silently left out of the search — but only for a modifier the
            // record is *about*. A crafted one is absent from it by definition, and saying so
            // reads as a failure to recognise a modifier that is right there in the list.
            //
            // On the **row** and not in a note underneath: the row is already the statement —
            // it names the modifier and its box is not ticked — and a paragraph repeating that
            // wording costs three lines of panel to say it a second time. This is why.
            if (!f.enabled && !added_to_copy(f.type))
                f.caveat = "not in the modifier data for \"" + um->name +
                           "\", so nothing can tell it from a modifier every copy has";
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

    // An unlisted pool is prose the source never turned into modifiers — "One to three random
    // Synthesis implicit modifiers". Where it names something the item in hand actually has, it
    // is **already a row** in the filter list and the note would be the same wording a second
    // time: Triad Grip's four conversion modifiers are unlisted *and* printed on the item, so
    // between this and the loop above they cost twelve lines of panel to say what four unticked
    // boxes said. Only prose with nothing on screen behind it is worth a note of its own.
    for (const std::string& u : um->unlisted) {
        // On screen as a **property** rather than as a filter, which is the same argument one
        // step over: the source lists a unique heist contract's client, area level, target and
        // job requirement as modifiers, and the game prints all four in the property block. Four
        // notes saying they are not searched, beside four lines already saying what they are.
        if (printed_as_property(it, u)) continue;
        if (StatFilter* f = filter_saying(p, it, u)) {
            f->caveat = "the modifier data states this but does not enumerate it, so nothing "
                        "here knows what it can roll";
            continue;
        }
        p.notes.push_back(
            "the modifier data states but does not enumerate this, so it is not "
            "searched: " +
            u);
    }
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
            // Never across the divide: a hidden filter folded into a shown one would put a
            // modifier the strategy left out into the total of one it did not, and the row's
            // tick would then be sending both.
            // Never across a choice either, and for a sharper version of the same reason: two
            // of a logbook's destinations can grant one stat, or belong to one faction, and
            // summing those gives a number no single destination has — while the whole point of
            // the group is that only one destination is ever being asked about.
            if (stats[j].id != stats[i].id || stats[j].hidden != stats[i].hidden ||
                stats[j].choice != stats[i].choice) {
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
        data::PropertyKey key; ///< the property the game prints
        const char* id;
        const char* text; ///< the trade site's own wording, so the two read alike
    };
    // Every "More" the chisels grant; there is no fifth pseudo stat in /api/trade/data/stats.
    static constexpr Drop kDrops[]{
        {data::PropertyKey::MoreMaps, "pseudo.pseudo_map_more_map_drops", "More Maps: #%"},
        {data::PropertyKey::MoreScarabs, "pseudo.pseudo_map_more_scarab_drops",
         "More Scarabs: #%"},
        {data::PropertyKey::MoreCurrency, "pseudo.pseudo_map_more_currency_drops",
         "More Currency: #%"},
        {data::PropertyKey::MoreDivinationCards, "pseudo.pseudo_map_more_card_drops",
         "More Divination Cards: #%"},
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
        if (const Property* prop = property_of(it, d.key); prop && prop->num)
            pseudo(d.id, d.text, *prop->num, true);

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
    p.type = std::string(wire_name(it.base));
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

/// An itemised beast: the species and the item level, and nothing else the item prints.
///
/// A beast is bought to be released into the menagerie and spent on a beastcrafting recipe, and
/// a recipe names the **species** — a Wild Hellion Alpha — so that is the whole of what one
/// copy has in common with another. The two lines above it are a rare title the game generated
/// for this capture ("Banebite the Malignant"), which no two copies share and no buyer asks
/// for, so the `name` term is deliberately left empty and the species goes in `type`.
///
/// The **monster modifiers** are skipped for the same reason a map's affixes are (`build_plan`):
/// they are the captured monster's own abilities rather than rolls on a base, the bundle has no
/// stat for "Crushing Claws" to match, and a recipe cares about none of them. Left out silently
/// — with no unrecognised-modifier note — because leaving them out is the decision, not a
/// failure to read them.
///
/// The item level is a floor rather than a window: the recipes that care about it want a beast
/// at least that high, and a higher one still answers.
void plan_beast(const Item& it, SearchPlan& p) {
    // The one place a category is not the bundle's answer for the item class. A beast's class is
    // "Stackable Currency", which maps to `currency` and is right for every orb that prints it —
    // but trade files beasts in a category of their own. Measured: `category: currency` returned
    // **0 matches** for a Wild Hellion Alpha and `monster.beast` returned **1602**, with the same
    // type and the same item level. The site accepts either, so the wrong one reads as nobody
    // selling one rather than as an error, which is what made this worth measuring rather than
    // reasoning about.
    p.category = "monster.beast";
    p.type = base_wire_name(it);
    if (it.base && !it.base->trade_disc.empty()) p.discriminator = it.base->trade_disc;
    else if (!it.base)
        p.notes.push_back("\"" + it.base_name +
                          "\" is not a beast in this data bundle, so the search asks for the "
                          "species as the clipboard spelled it");
    add_numeric(p, "ilvl", "Item Level",
                it.item_level ? std::optional<double>(*it.item_level) : std::nullopt, true);
}

/// The `ultimatum_challenge` option ids, in the order `TermList::UltimatumChallenges` lists the
/// wordings the game prints for them. Same shape as the chart shapes and for the same reason:
/// a closed vocabulary from `/api/trade/data/filters`, joined to the client's text, and the id
/// is never derived from the words.
constexpr std::string_view kUltimatumChallengeIds[]{"Exterminate", "Survival", "Defense",
                                                    "Conquer"};
/// The `ultimatum_reward` ids for the three rewards the game states as a wording. The fourth,
/// below, has none: an ultimatum that pays out a unique prints that unique's name on the line.
constexpr std::string_view kUltimatumRewardIds[]{"DoubleCurrency", "DoubleDivCards", "MirrorRare"};
constexpr std::string_view kUltimatumUniqueRewardId = "ExchangeUnique";

/// `Lexicon::index_of`, ignoring case. Only the challenge list needs it: the trade site titles
/// its option "Defeat Waves of Enemies" and the client prints "Defeat waves of enemies", so the
/// English entries are the site's own text and the case is the one thing that differs.
int index_of_ci(const data::Lexicon& lex, data::TermList l, std::string_view s) {
    const std::vector<std::string>& v = lex.list(l);
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i].size() != s.size() || v[i].empty()) continue;
        if (std::equal(v[i].begin(), v[i].end(), s.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                       std::tolower(static_cast<unsigned char>(b));
            }))
            return static_cast<int>(i);
    }
    return -1;
}

/// The item an ultimatum's stake names, without the count the game prints after it: "Divine Orb
/// x8" is a search for Divine Orbs, and how many of them is not something trade indexes.
std::string_view strip_stack_count(std::string_view v) {
    const size_t x = v.rfind(" x");
    if (x == std::string_view::npos || x + 2 >= v.size()) return v;
    for (size_t i = x + 2; i < v.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(v[i]))) return v;
    return v.substr(0, x);
}

/// The name trade files an ultimatum's stake under, or "" when this bundle does not know it.
///
/// The site takes a **known item** here, across the three namespaces its own filter names —
/// uniques, divination cards and currency — and a name it does not know fails the whole search
/// rather than widening it, exactly as a Valdo map's reward does. So nothing unconfirmed is sent.
std::string find_sacrifice(const data::GameData& gd, std::string_view printed) {
    static constexpr data::Namespace kNs[]{data::Namespace::Unique, data::Namespace::DivinationCard,
                                           data::Namespace::Item};
    for (const data::Namespace ns : kNs)
        for (const data::BaseType* b : gd.find_bases(ns, printed))
            return std::string(wire_name(b));
    return {};
}

/// The two modifiers an ultimatum is searched on, and the only two: they are what the trial's
/// difficulty *is*, and on a currency or divination-card ultimatum they are also what says how
/// much is at stake — the sacrificed stack grows with them. Every other line is the shape of the
/// danger rather than a term of the deal, which is what the user is choosing to run or not.
bool ultimatum_stake_mod(const Modifier& m) {
    if (!m.match || !m.match->stat) return false;
    const std::string& ref = m.match->stat->ref;
    return ref == "#% increased Monster Damage" || ref == "#% more Monster Life";
}

/// An Inscribed Ultimatum is a contract, and a search for one asks for the same contract: the
/// trial, the stake, the payout, and the two numbers that say how large the stake is.
///
/// - **The challenge and the reward type** are what the trial is and what it pays, and trade has
///   an option for each. The reward that is a unique has no wording of its own — the line is the
///   unique's name — so that name goes into `ultimatum_output` and the type is `ExchangeUnique`.
/// - **The sacrificed item**, which is the price of entry. Not its count: trade indexes no such
///   number, and the count is already implied by the two modifiers below. The one reward with no
///   nameable stake is the mirror, whose line reads "Mirrorable, Rare Item" — a class of items
///   rather than one, and already fully said by the reward type.
/// - **The area level**, exact rather than a floor, for the same reason a chart's is: an 83 is a
///   different trial from a 78, not a better one.
/// - **Increased Monster Damage and more Monster Life**, exact rather than windowed *whatever the
///   range-match setting says*, because these two are the scale of the deal and not a roll to be
///   beaten: 200% more Monster Life is the ultimatum that stakes eight Divine Orbs, and asking
///   for "at least 120%" prices four of them alongside it.
///
/// Everything else it prints — Choking Miasma, Drought, Shattered Shield — is left out, and
/// silently: they are the trial's hazards, they sit on the item beside the panel, and a note per
/// line would charge the check with failing at something it deliberately did not attempt.
void plan_ultimatum(const data::GameData& gd, const Item& it, SearchPlan& p) {
    const data::Lexicon& lex = gd.lexicon();
    // **No category at all**, which is the second place the bundle's answer for the item class is
    // overridden and the first where the override is to send nothing. "Misc Map Items" maps to
    // `map.fragment`, and that is right for the invitations and splinters that share the class but
    // not for an ultimatum: measured, the same query returned **0 matches** with it and **443**
    // without, everything else identical. Nothing is lost by dropping it — an ultimatum is one
    // base type, so the type term below already says everything a category could.
    p.category.clear();
    p.type = base_wire_name(it);
    if (it.base && !it.base->trade_disc.empty()) p.discriminator = it.base->trade_disc;

    if (const Property* c = property_of(it, data::PropertyKey::Challenge); c && !c->value.empty()) {
        const int i = index_of_ci(lex, data::TermList::UltimatumChallenges, c->value);
        if (i >= 0 && static_cast<size_t>(i) < std::size(kUltimatumChallengeIds))
            add_option(p, "ultimatum_challenge", c->label,
                       std::string(kUltimatumChallengeIds[i]), c->value, true);
        else
            p.notes.push_back("\"" + c->value +
                              "\" is not a challenge the trade site knows, so the search does not "
                              "ask which trial this is");
    }

    const Property* reward = property_of(it, data::PropertyKey::Reward);
    bool mirror_reward = false;
    if (reward && !reward->value.empty()) {
        const int i = lex.index_of(data::TermList::UltimatumRewards, reward->value);
        if (i >= 0 && static_cast<size_t>(i) < std::size(kUltimatumRewardIds)) {
            mirror_reward = kUltimatumRewardIds[i] == "MirrorRare";
            add_option(p, "ultimatum_reward", reward->label,
                       std::string(kUltimatumRewardIds[i]), reward->value, true);
        } else if (const std::string named = find_unique_in(gd, reward->value); !named.empty()) {
            add_option(p, "ultimatum_reward", reward->label,
                       std::string(kUltimatumUniqueRewardId), reward->value, true);
            add_option(p, "ultimatum_output", "Reward Unique", named, named, true);
        } else {
            p.notes.push_back("\"" + reward->value +
                              "\" is neither a reward wording nor a unique in this data bundle, "
                              "so the search does not ask what this ultimatum pays out");
        }
    }

    if (const Property* s = property_of(it, data::PropertyKey::RequiresSacrifice);
        s && !s->value.empty() && !mirror_reward) {
        const std::string_view stake = strip_stack_count(s->value);
        if (const std::string named = find_sacrifice(gd, stake); !named.empty())
            add_option(p, "ultimatum_input", s->label, named, std::string(stake), true);
        else
            p.notes.push_back("\"" + std::string(stake) +
                              "\" is not an item in this data bundle, and the trade site rejects a "
                              "required item it does not know, so the search does not ask what "
                              "this ultimatum costs to run");
    }

    if (const Property* lvl = property_of(it, data::PropertyKey::AreaLevel); lvl && lvl->num)
        add_numeric(p, "area_level", lvl->label, *lvl->num, true, 0, {}, *lvl->num);
}

/// The `heist_*` filter for each rogue job, in the order `TermList::HeistJobs` names them.
constexpr std::string_view kHeistJobKeys[]{
    "heist_lockpicking", "heist_brute_force",         "heist_perception",
    "heist_demolition",  "heist_counter_thaumaturgy", "heist_trap_disarmament",
    "heist_agility",     "heist_deception",           "heist_engineering"};
/// The `heist_objective_value` option ids, in the order `TermList::HeistObjectiveValues` lists
/// the words the game prints for them.
constexpr std::string_view kHeistObjectiveIds[]{"moderate", "high", "precious", "priceless"};

/// The two numbers of a "3/21" value — what there is now and what there is in all. A blueprint
/// states each of its reveal counts this way and a sanctum states its resolve. Absent when the
/// value is not that shape, which is how a client that words it differently degrades: no filter
/// rather than a filter for a number that is not there.
std::optional<std::pair<double, double>> slashed_pair(const Property& p) {
    const size_t slash = p.value.find('/');
    if (slash == std::string::npos || !p.num) return std::nullopt;
    const std::string_view rest = std::string_view(p.value).substr(slash + 1);
    double total = 0;
    // `std::from_chars` and not the C locale, which would read "21" against a decimal comma.
    const auto [end, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), total);
    if (ec != std::errc{} || end == rest.data()) return std::nullopt;
    return std::pair{*p.num, total};
}

/// The parenthetical a heist objective's name ends with — "Ancient Seal (Precious)" — or "".
/// The value is the whole of what trade indexes about a target; the target's own name is not a
/// term the site takes at all.
std::string_view objective_value_of(const Property& p) {
    if (p.value.empty() || p.value.back() != ')') return {};
    const size_t open = p.value.rfind(" (");
    if (open == std::string::npos) return {};
    return std::string_view(p.value).substr(open + 2, p.value.size() - open - 3);
}

/// A heist contract or blueprint: **which run this is**, and what it will cost to make.
///
/// The first iteration of a market nobody here has traded, so it errs towards offering rather
/// than towards deciding — every heist filter the site has is a row, and what separates the
/// ticked from the untitcked is one question: is this a fact about *which item this is*, or is
/// it the variation between two copies of the same one?
///
/// Imposed, because they say which run it is:
/// - **The area**, which is the base type and is what the game names the item after ("Blueprint:
///   Underbelly"). A rare heist item's own name — "Cataclysm Vow" — is generated per copy and is
///   no more searchable than a rare bow's.
/// - **The area level**, exact, on the same reading a chart's and an ultimatum's get: pricing is
///   like for like, and a level 83 run is a different product from a level 77 one.
/// - **What is revealed**, on a blueprint, as a floor: more of the map uncovered is strictly more
///   of what a buyer is paying for. The total beside each count is exact where the site indexes
///   one — a blueprint's wing count varies per copy, so it is part of which item this is rather
///   than an amount of anything — and Total Escape Routes is left out entirely. See below.
/// - **The objective's value** on a contract, which is the parenthetical after the target.
/// - **The enchant**, on a blueprint that has one: "Heist Targets are always Enchanted
///   Armaments" is what the whole run is for, and somebody paid to put it there.
///
/// Offered and left unticked, because they are the roll rather than the item:
/// - **The job levels.** A requirement is a demand on the *buyer's* rogue, not a property of the
///   thing being bought, so it is seeded as a ceiling — copies asking less are strictly more
///   usable — and left off, because a buyer whose rogue is levelled does not care.
/// - **The heist modifiers.** They are the danger the run will hold: rolled, re-rollable, and
///   the map argument exactly, except that the row stays and only the tick goes. A contract
///   carries seven of them and ticking all seven asks for one particular copy in the world.
///
/// Not offered at all, because trade indexes none of them: item quantity, item rarity, alert
/// level reduction, time before lockdown, maximum alive reinforcements. They are on the item
/// beside the panel, and there is no `heist_` filter to put them in.
void plan_heist(const data::GameData& gd, const Item& it, SearchPlan& p) {
    const data::Lexicon& lex = gd.lexicon();
    if (it.base) {
        p.type = base_wire_name(it);
        if (!it.base->trade_disc.empty()) p.discriminator = it.base->trade_disc;
    } else {
        // Deliberately no type rather than the printed one: a magic blueprint's base line still
        // carries its affixes ("Deployed Blueprint: Records Office of Spine-Chilling") and
        // sending that matches nothing, which reads as an empty market. The category is still a
        // real search, and a coarser one is what the note says it is.
        p.notes.push_back("\"" + it.base_type +
                          "\" is not a heist base in this data bundle, so the search is for any "
                          "contract or blueprint rather than for this area");
    }

    if (const Property* lvl = property_of(it, data::PropertyKey::AreaLevel); lvl && lvl->num)
        add_numeric(p, "area_level", lvl->label, *lvl->num, true, 0, {}, *lvl->num);

    // Only what is **revealed**, never the total printed beside it. The site publishes a filter
    // for each total — `heist_max_wings`, `heist_max_escape_routes`, `heist_max_reward_rooms` —
    // and indexes nothing under them. Measured on the Records Office capture: the three totals
    // alone, at the item's own 2, 4 and 13, returned **0 matches**, against **135** for the three
    // revealed counts alone and **252** for neither. They are accepted rather than rejected, so
    // sending one costs the whole search and reads as an empty market — which is why they are
    // not offered as unticked rows either. The numbers are on the item beside the panel.
    // **Total Escape Routes is the one filter here with nothing behind it.** The site publishes
    // `heist_max_escape_routes` and indexes no listing under it, so any bound at all empties the
    // result. Measured one filter at a time on the fully revealed Tunnels capture, everything
    // else identical: `heist_max_wings` at 4 returned **460** and `heist_max_reward_rooms` at 28
    // returned **460** (1040 at a bare `min: 1`), while `heist_max_escape_routes` returned **0**
    // both at the item's own 8 and at `min: 1`. It is accepted rather than rejected, so sending
    // it costs the whole search and reads as an empty market — and it is not offered as an
    // unticked row either, because ticking it would do the same. The number is on the item.
    struct Reveal {
        data::PropertyKey property;
        const char* key;
        const char* total;       ///< "" where the site indexes no total
        const char* total_label;
    };
    static constexpr Reveal kReveals[]{
        {data::PropertyKey::WingsRevealed, "heist_wings", "heist_max_wings", "Total Wings"},
        {data::PropertyKey::EscapeRoutesRevealed, "heist_escape_routes", "", ""},
        {data::PropertyKey::RewardRoomsRevealed, "heist_reward_rooms", "heist_max_reward_rooms",
         "Total Reward Rooms"},
    };
    for (const Reveal& r : kReveals) {
        const Property* prop = property_of(it, r.property);
        if (!prop) continue;
        const std::optional<std::pair<double, double>> both = slashed_pair(*prop);
        if (!both) continue;
        // Revealed is a floor — more of the blueprint uncovered is strictly more of what is
        // being bought. The total is exact, because it is not an amount of anything: a Tunnels
        // blueprint comes with two, three or four wings, and a four-wing one is a different
        // item rather than a better copy of the same one.
        add_numeric(p, r.key, prop->label, both->first, true);
        if (*r.total)
            add_numeric(p, r.total, r.total_label, both->second, true, 0, {}, both->second);
    }

    // A contract's, and only a contract's: a blueprint sends the crew after a wing rather than
    // after a thing, and prints no target line at all.
    if (const Property* t = property_of(it, data::PropertyKey::HeistTarget)) {
        const std::string_view value = objective_value_of(*t);
        const int i = value.empty()
                          ? -1
                          : lex.index_of(data::TermList::HeistObjectiveValues, value);
        if (i >= 0 && static_cast<size_t>(i) < std::size(kHeistObjectiveIds))
            add_option(p, "heist_objective_value", "Objective Value",
                       std::string(kHeistObjectiveIds[i]), std::string(value), true);
        else if (!value.empty())
            p.notes.push_back("\"" + std::string(value) +
                              "\" is not an objective value the trade site knows, so the search "
                              "does not ask what the target is worth");
        // No parenthetical at all is the ordinary shape of a boss contract ("Kill Admiral
        // Darnaw"), not a gap: there is no value to ask about, so nothing is said.
    }

    // One row per job the item demands, seeded as a ceiling and left off. See the note above:
    // a job level is what the run asks of the buyer, and a copy asking less still answers.
    for (const Property& prop : it.properties) {
        if (prop.key != data::PropertyKey::HeistJob || !prop.num) continue;
        const std::vector<std::string>& jobs = lex.list(data::TermList::HeistJobs);
        for (size_t i = 0; i < jobs.size() && i < std::size(kHeistJobKeys); ++i) {
            if (jobs[i].empty() || prop.value.find(jobs[i]) == std::string::npos) continue;
            add_numeric(p, std::string(kHeistJobKeys[i]), jobs[i] + " Level", std::nullopt, false,
                        0, {}, *prop.num);
            break;
        }
    }
}

std::string_view trim_spaces(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}

/// One boon or affliction, as the trade site indexes it: the stat whose wording is the name the
/// item printed under a `Has `. Null when this bundle has no such stat, or has one it cannot
/// search — which is what the caller says out loud rather than dropping.
const data::Stat* sanctum_effect_stat(const data::GameData& gd, std::string_view name) {
    const std::string_view prefix = gd.lexicon().term(data::Term::SanctumEffectPrefix);
    if (prefix.empty() || name.empty()) return nullptr;
    const data::Stat* s = gd.find_stat_by_ref(std::string(prefix) + std::string(name));
    return s && s->has(data::ModType::Sanctum) ? s : nullptr;
}

/// Turn the comma-separated names under `Minor Boons:` and `Major Afflictions:` into one stat
/// filter each. They are not modifiers — the game prints them as a property and the item's mod
/// block holds the affixes instead — so they are built here rather than by `to_filter`.
void add_sanctum_effects(const data::GameData& gd, const Item& it, SearchPlan& p) {
    for (const Property& prop : it.properties) {
        if (prop.key != data::PropertyKey::Boons && prop.key != data::PropertyKey::Afflictions)
            continue;
        for (std::string_view rest = prop.value; !rest.empty();) {
            const size_t comma = rest.find(',');
            const std::string_view name = trim_spaces(rest.substr(0, comma));
            rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
            if (name.empty()) continue;
            const data::Stat* s = sanctum_effect_stat(gd, name);
            if (!s) {
                p.notes.push_back("\"" + std::string(name) +
                                  "\" is not a sanctum boon or affliction the trade site knows in "
                                  "this data bundle, so the search does not ask for it");
                continue;
            }
            StatFilter f;
            f.id = s->trade_ids(data::ModType::Sanctum).front();
            // The stat's own wording, which is what the site's filter is called. The item beside
            // the panel is where minor and major are told apart.
            f.text = s->ref;
            f.type = data::ModType::Sanctum;
            f.enabled = true;
            p.stats.push_back(std::move(f));
        }
    }
}

/// An itemised sanctum: **how far this run has got, and what it is carrying**.
///
/// Everything a buyer of one is choosing between is on the item as a number or a name, and all
/// of it is imposed, because none of it is a roll to be beaten — a run is bought to be finished
/// and its state is the product:
/// - **The floor**, which is the base type: an Archives run and a Vaults run are different items.
/// - **The area level**, exact, the reading a chart's, an ultimatum's and a heist item's already
///   get — a level 83 sanctum is a different product from a level 78 one, not a better copy.
/// - **Resolve**, as a floor: it is the whole of how much run is left to survive.
/// - **Inspiration** and **Aureus**, as floors, for the same reason. More of either is strictly
///   more of what is being bought.
/// - **Every boon and affliction**, each its own `sanctum.sanctum_effect_…` stat.
/// - **The affixes** — "The Merchant has 10 additional Choices", "18 additional Rooms are
///   revealed on the Sanctum Map" — through the ordinary mod path, which files them under the
///   `sanctum` namespace because the parser typed them that way.
///
/// **Maximum Resolve is the one row left unticked**, seeded from the number beside the current
/// one and open on the right. It is not a fact about the run's state so much as about the
/// character that started it, and the item already says everything about resolve that a buyer
/// is choosing on through the current value.
void plan_sanctum(const data::GameData& gd, const Item& it, SearchPlan& p) {
    if (it.base) {
        p.type = base_wire_name(it);
        if (!it.base->trade_disc.empty()) p.discriminator = it.base->trade_disc;
    } else {
        p.notes.push_back("\"" + it.base_type +
                          "\" is not a sanctum in this data bundle, so the search is for any "
                          "research item rather than for this floor");
    }

    if (const Property* lvl = property_of(it, data::PropertyKey::AreaLevel); lvl && lvl->num)
        add_numeric(p, "area_level", lvl->label, *lvl->num, true, 0, {}, *lvl->num);

    if (const Property* r = property_of(it, data::PropertyKey::Resolve)) {
        // "299/300": what is left, and what the run started with. Only the first is a floor.
        if (const std::optional<std::pair<double, double>> both = slashed_pair(*r)) {
            add_numeric(p, "sanctum_resolve", r->label, both->first, true);
            add_numeric(p, "sanctum_max_resolve", "Maximum Resolve", both->second, false);
        } else if (r->num) {
            add_numeric(p, "sanctum_resolve", r->label, *r->num, true);
        }
    }
    if (const Property* i = property_of(it, data::PropertyKey::Inspiration); i && i->num)
        add_numeric(p, "sanctum_inspiration", i->label, *i->num, true);
    // `sanctum_gold` on the wire; "Aureus" is what both the item and the site's own row call it.
    if (const Property* a = property_of(it, data::PropertyKey::Aureus); a && a->num)
        add_numeric(p, "sanctum_gold", a->label, *a->num, true);

    add_sanctum_effects(gd, it, p);
}

/// The pseudo stat a logbook's faction or area name is searched under — `Has Logbook Faction:
/// Druids of the Broken Circle`, which is the site's own wording for it. Null when this bundle
/// has no such stat, which the caller says out loud rather than dropping: the area list grows
/// with the league and a name nobody has published a stat for is a real gap.
const data::Stat* logbook_stat(const data::GameData& gd, data::Term prefix,
                               std::string_view name) {
    const std::string_view p = gd.lexicon().term(prefix);
    if (p.empty() || name.empty()) return nullptr;
    const data::Stat* s = gd.find_stat_by_ref(std::string(p) + std::string(name));
    return s && s->has(data::ModType::Pseudo) ? s : nullptr;
}

/// An Expedition Logbook: **which of its destinations is being priced**, and what the whole
/// book is worth wherever it goes.
///
/// A logbook is up to three items in one. Each destination names an area, the faction whose
/// land it is and the implicits that apply there; the player travels to exactly one of them,
/// and the faction is what decides what that is worth — which is why a split logbook, two of
/// whose destinations share a valuable faction, is a thing people pay for. Nothing in the
/// clipboard says which destination the reader has in mind, so the plan does not guess: the
/// destinations are `SearchPlan::choices` and the panel asks.
///
/// Inside a destination, only the **faction** is ticked. Where it goes and what it grants there
/// are offered — a buyer picking a faction is rarely picking an area with it, and an implicit
/// is one of two or three numbers that came with the area rather than something anybody chose.
///
/// What the book grants wherever it goes:
/// - **The area level**, a floor and ticked. Unlike a map's tier or a sanctum's floor, a higher
///   one is not a different product but strictly more of the same one, and a buyer at 80 takes
///   an 83.
/// - **The item level**, offered. It bounds what the affixes can be crafted to and is a
///   question about crafting the book rather than about running it.
/// - **Quantity, rarity and pack size**, offered and unticked — **decided, not deferred**, and
///   the one place this parts company with the map strategy it borrows the keys from. A map's
///   quantity and pack size are the whole of what it is run for and are ticked; a logbook's are
///   a second-order bonus on top of the artifacts, which the *destination* decides. They also
///   only exist on a magic or rare book, so ticking them would search the same logbook two
///   different ways depending on whether it had rolled affixes at all. Nobody has measured that
///   the site indexes them for this category either, and a filter it accepts and indexes nothing
///   under empties the search exactly as `heist_max_escape_routes` does.
///
/// The **affixes it prints below the destinations** are the map argument, and get the map's
/// answer: they apply wherever it goes, they are re-rollable — a logbook is craftable, which is
/// most of what the affix crafting market is — and a query naming them finds the one copy in
/// the league that rolled that set. So they are `hidden`, offered under the section at the foot
/// of the list rather than dropped on the floor.
void plan_logbook(const data::GameData& gd, const Item& it, SearchPlan& p) {
    // The category is the whole search on its own — one base type is filed under `logbook` —
    // so the type term is sent only where the bundle resolved it, never off the printed line.
    // A magic logbook's line is "Buffered Expedition Logbook", and that as a type matches
    // nothing, which reads as nobody selling one.
    if (it.base) {
        p.type = base_wire_name(it);
        if (!it.base->trade_disc.empty()) p.discriminator = it.base->trade_disc;
    }

    if (const Property* lvl = property_of(it, data::PropertyKey::AreaLevel); lvl && lvl->num)
        add_numeric(p, "area_level", lvl->label, *lvl->num, true);
    add_numeric(p, "ilvl", "Item Level",
                it.item_level ? std::optional<double>(*it.item_level) : std::nullopt, false);
    static constexpr std::pair<data::PropertyKey, const char*> kBonuses[]{
        {data::PropertyKey::ItemQuantity, "map_iiq"},
        {data::PropertyKey::MonsterPackSize, "map_packsize"},
        {data::PropertyKey::ItemRarity, "map_iir"},
    };
    for (const auto& [key, filter] : kBonuses)
        if (const Property* prop = property_of(it, key); prop && prop->num)
            add_numeric(p, filter, prop->label, *prop->num, false);

    for (size_t d = 0; d < it.logbook_areas.size(); ++d) {
        const LogbookArea& dest = it.logbook_areas[d];
        ChoiceGroup g;
        g.label = dest.faction;
        g.note = dest.area;
        p.choices.push_back(std::move(g));

        const auto pseudo = [&](data::Term prefix, std::string_view name, bool primary) {
            const data::Stat* s = logbook_stat(gd, prefix, name);
            if (!s) {
                p.notes.push_back("\"" + std::string(name) +
                                  "\" is not a logbook faction or area the trade site knows in "
                                  "this data bundle, so the search does not ask for it");
                return;
            }
            StatFilter f;
            f.id = s->trade_ids(data::ModType::Pseudo).front();
            // The stat's own wording rather than the bare name: it is what the site's filter is
            // called, and "Druids of the Broken Circle" on a row of its own says nothing about
            // being a faction. The item beside the panel is where the block itself is read.
            f.text = s->ref;
            f.type = data::ModType::Pseudo;
            f.choice = d;
            f.choice_primary = primary;
            // **Presence, not a count.** The pseudo stat takes a value — how many of the
            // destinations belong to that faction — and it is not what decides the price: a
            // logbook with two Druids destinations is still bought for a Druids run. Bounding
            // it would drop every single-destination copy of the same thing.
            p.stats.push_back(std::move(f));
        };
        pseudo(data::Term::LogbookFactionPrefix, dest.faction, true);
        pseudo(data::Term::LogbookAreaPrefix, dest.area, false);
    }
}

/// Whether modifier `index` came out of one of the logbook's destination blocks. Everything
/// else it prints is an affix of the book itself, which applies wherever it goes.
bool logbook_destination_mod(const Item& it, size_t index) {
    return std::any_of(it.logbook_areas.begin(), it.logbook_areas.end(),
                       [index](const LogbookArea& a) {
                           return std::find(a.mods.begin(), a.mods.end(), index) != a.mods.end();
                       });
}

/// Hand every destination's implicits to the destination they came from, once the ordinary mod
/// pass has turned them into filters.
///
/// Two things happen here that could not happen in `to_filter`, which sees one modifier at a
/// time and nothing about the block it was printed in.
///
/// The **upper bound goes**. Trade indexes an item's implicits as one total per stat, and all
/// three destinations feed the same total — the rare capture grants "increased number of
/// Explosives" twice, at 14% and 16%, and the site sees 30%. A floor still matches under that,
/// since the total can only exceed the destination's own roll; a ceiling seeded from one
/// destination's roll asks the other two not to exist. Which side is the floor is the stat's
/// own `better`, the same question `to_filter` asks when it decides which way an open bound
/// faces.
///
/// And the row **joins its group**, which is what keeps `merge_same_stat` from folding two
/// destinations' rolls of one stat into a sum belonging to neither.
void group_logbook_mods(const Item& it, SearchPlan& p) {
    for (StatFilter& f : p.stats) {
        if (!f.mod_index || f.hidden || f.choice) continue;
        for (size_t d = 0; d < it.logbook_areas.size(); ++d) {
            const std::vector<size_t>& mods = it.logbook_areas[d].mods;
            if (std::find(mods.begin(), mods.end(), *f.mod_index) == mods.end()) continue;
            f.choice = d;
            f.enabled = false;
            const data::StatMatch& m = *it.mods[*f.mod_index].match;
            if (m.stat && m.stat->better < 0) f.min.reset();
            else f.max.reset();
            break;
        }
    }
}

/// The property a Valdo's Puzzle Box map states its payout in, or null on any other map. No
/// other map prints one, which is what makes it the marker as well as the thing searched for.
const Property* reward_property(const Item& it) {
    if (const Property* prop = property_of(it, data::PropertyKey::Reward)) return prop;
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

/// The key an area's own record is filed under, from the name the game prints.
///
/// Trade files a chart under **the area it covers**, as a `type` option carrying the `chart`
/// discriminator and the area's internal id for a value: `SeafloorRidges`, not "Seafloor
/// Ridges". The bundle carries those records, but only under that id — there is no display name
/// on them — so the printed name has to be turned back into one. Apostrophes go, spaces and
/// hyphens are word breaks, and every word is capitalised: "Brine King's Domain" is
/// `BrineKingsDomain` and "Clam-infested Shelf" is `ClamInfestedShelf`.
///
/// **This is only ever a lookup key.** A record has to come back under it, carrying the
/// discriminator that proves it is an area rather than some other base, or nothing is sent — so
/// a convention that turns out to be wrong costs a coarser search (the chart's own base type),
/// never a wrong one.
std::string chart_area_key(std::string_view printed) {
    std::string out;
    bool at_word_start = true;
    for (const char c : printed) {
        if (c == '\'') continue;
        if (c == ' ' || c == '-') {
            at_word_start = true;
            continue;
        }
        out += at_word_start ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        at_word_start = false;
    }
    return out;
}

const data::BaseType* find_chart_area(const data::GameData& gd, std::string_view printed) {
    if (printed.empty()) return nullptr;
    for (const data::BaseType* b : gd.find_bases(data::Namespace::Item, chart_area_key(printed)))
        if (b->trade_disc == "chart") return b;
    return nullptr;
}

/// The `chart_shape` option ids, from `map_filters` in `/api/trade/data/filters`. A closed
/// vocabulary — five shapes, numbered — so it is a table rather than something fetched, and the
/// game prints the option's own text, which is what makes the join possible at all. Sending the
/// text instead answers `{"code":2,"message":"Invalid chart shape"}` and fails the whole search.
std::string chart_shape_id(const data::Lexicon& lex, std::string_view printed) {
    const int i = lex.index_of(data::TermList::ChartShapes, printed);
    return i < 0 ? std::string() : std::to_string(i + 1);
}

/// A chart is a map by another name, and the strategy it shares says why: its prefixes and
/// suffixes are the danger the buyer is choosing among rather than the thing they are buying, so
/// they are left out here exactly as a map's are. What is left is what a currency cannot redo.
///
/// - **Which area it covers**, which the game prints as the leading prose line of the property
///   block and trade takes as the type. A chart whose area the bundle cannot name falls back to
///   its own base type ("Coral Reef Chart"), which is a real search and simply a coarser one.
/// - **The area's level**, exact rather than a floor, and for the same reason a map's tier is:
///   a level 83 area is a different area from a level 78 one, not a better one.
/// - **The shape**, which is what says how the chart joins to the ones around it on the voyage.
/// - **The sulphur it yields**, alongside the quantity and pack size every map already asks for.
/// - **Its voyage modifier**, which is an implicit and so is already enabled by the map strategy
///   — including on a chart that has not been sailed yet and prints only the promise of one
///   ("Voyage Modifier will be revealed once Charted"), which is itself a searchable stat.
void plan_chart(const data::GameData& gd, const Item& it, SearchPlan& p) {
    if (const data::BaseType* area = find_chart_area(gd, it.type_line)) {
        p.type = std::string(wire_name(area));
        p.discriminator = area->trade_disc;
    } else {
        p.type = base_wire_name(it);
        if (it.base && !it.base->trade_disc.empty()) p.discriminator = it.base->trade_disc;
        if (!it.type_line.empty())
            p.notes.push_back("\"" + it.type_line +
                              "\" is not an area in this data bundle, so the search is for any "
                              "chart of this kind rather than for this one's area");
    }

    if (const Property* lvl = property_of(it, data::PropertyKey::AreaLevel); lvl && lvl->num)
        add_numeric(p, "area_level", "Area Level", *lvl->num, true, 0, {}, *lvl->num);
    if (const Property* shape = property_of(it, data::PropertyKey::ChartShape);
        shape && !shape->value.empty()) {
        if (std::string id = chart_shape_id(gd.lexicon(), shape->value); !id.empty())
            add_option(p, "chart_shape", "Chart Shape", std::move(id), shape->value, true);
        else
            p.notes.push_back("\"" + shape->value +
                              "\" is not a chart shape the trade site knows, so the search does "
                              "not ask for the shape");
    }
    // The league's own currency, so the same reasoning as a map's quantity rather than as its
    // rarity: it is what the area is run for, and a copy yielding less of it is worth less.
    if (const Property* s = property_of(it, data::PropertyKey::Sulphur); s && s->num)
        add_numeric(p, "chart_sulphur", "Dead Man's Sulphur", *s->num, true);
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
        p.name = it.unique_entry ? std::string(wire_name(it.unique_entry)) : it.name;
        if (!it.identified)
            p.notes.emplace_back(
                "unidentified: the clipboard does not say which unique map "
                "this is");
    }
    const Property* reward = nullptr;
    if (it.is_chart()) {
        plan_chart(gd, it, p);
    } else {
        p.type = base_wire_name(it);
        // "Map" is a type on trade *and* the prefix of every unique map's own entry, so it
        // always carries a discriminator; the unique's record repeats it, which is what lets one
        // field serve both terms. It is **load-bearing** rather than a tie-break: a query sending
        // the type as a bare "Map" is accepted and matches nothing at all, which reads as an
        // empty market rather than as a search that could not be built.
        if (const data::BaseType* b = it.rarity == Rarity::Unique ? it.unique_entry : it.base)
            if (!b->trade_disc.empty()) p.discriminator = b->trade_disc;
        if (p.discriminator.empty() && it.map_tier)
            p.notes.emplace_back("\"" + it.base_name +
                                 "\" is not a base in this data bundle, and trade matches no map "
                                 "without the discriminator its record carries");
        // Blight is a filter and not a type: the base line is the only place the clipboard says
        // so, and `resolve_base` has already pointed the base at the ordinary map it shares with
        // every other one. Never asked for in the negative — the two flags are mutually
        // exclusive, so a blighted map's own search already excludes the ravaged ones.
        if (it.blighted) add_flag(p, "map_blighted", "Blighted", true, false);
        if (it.blight_ravaged) add_flag(p, "map_uberblighted", "Blight-ravaged", true, false);

        // Exact, not a floor: a tier-16 map is not a better tier-14 one, it is a different area.
        if (it.map_tier)
            add_numeric(p, "map_tier", "Map Tier", static_cast<double>(*it.map_tier), true, 0, {},
                        static_cast<double>(*it.map_tier));

        // A Valdo map's own numbers come from the unique modifiers it is stamped with rather
        // than from a roll, so they say nothing about which of them a buyer wants; the reward
        // does.
        reward = reward_property(it);
        if (reward) {
            // The site takes the **unique's own name** here and rejects anything else outright
            // ("Unknown reward output provided", which fails the whole search rather than
            // widening it) — so the "Foil " the game prints in front of the payout has to go,
            // and only a name the bundle confirms is a unique is ever sent.
            const std::string named = find_unique_in(gd, reward->value);
            if (named.empty())
                p.notes.push_back("\"" + reward->value +
                                  "\" is not a unique in this data bundle, and the trade site "
                                  "rejects a reward it does not know, so the search is for any "
                                  "map of this kind");
            else
                add_option(p, "map_completion_reward", "Reward", named, named);
            add_void_rule(gd, it, p);
        }
    }

    struct Bonus {
        data::PropertyKey property; ///< what the game printed
        const char* key;            ///< the trade `map_filters` filter
        bool enabled;
    };
    // Quantity and pack size are what a map is run for; rarity is a preference, and imposing it
    // would drop the cheaper copies of the same map that most buyers are actually after.
    static constexpr Bonus kBonuses[]{
        {data::PropertyKey::ItemQuantity, "map_iiq", true},
        {data::PropertyKey::MonsterPackSize, "map_packsize", true},
        {data::PropertyKey::ItemRarity, "map_iir", false},
    };
    // The row is labelled with what the client printed, which is the wording the reader has
    // in front of them in the game.
    for (const Bonus& b : kBonuses)
        if (const Property* prop = property_of(it, b.property); prop && prop->num)
            add_numeric(p, b.key, prop->label, *prop->num, b.enabled && !reward);
}

/// The `misc_filters` booleans every plan carries, and whether the user is offered a say.
///
/// The rule is one line: **the search asks the item to be what it is**, and it says so out loud
/// only where that is not the ordinary answer. An uncorrupted, unmirrored, unmutated, identified
/// item is what nearly every check is about, so those four are imposed without a row; a
/// corrupted, mirrored, foulborn or unidentified one is a different product, and *that* is worth
/// a row, because it is the one a buyer might want to widen back out.
///
/// Two of the six are asked in one direction only. Synthesis and fracturing are evidence about
/// the copy in hand rather than a choice — an ordinary item's search has no reason to rule out
/// the fractured ones, which are strictly more constrained versions of it.
///
/// **`identified` is not asked of a gem or a currency item**, measured rather than assumed:
/// `identified: true` returns 0 listings under `category: gem` and 0 for a Facetor's Lens (10000
/// and 177 without it), because trade indexes the flag only for what can be unidentified. A
/// filter that matches nothing reads as an item nobody is selling. `mirrored: false` and
/// `mutated: false` are safe everywhere and were checked the same way — only a unique can be
/// foulborn, but asking a gem, a currency item, a rare or a map not to be one narrows nothing
/// (655/655 gems, 1299 Facetor's Lenses either way).
void add_item_flags(const Item& it, SearchPlan& p) {
    if (p.strategy == Strategy::Unsupported) return;
    // Corruption is never incidental: it fixes the item's mods forever and splits the market in
    // two, so it is matched exactly whatever the strategy.
    add_flag(p, "corrupted", "Corrupted", it.corrupted, it.corrupted);
    add_flag(p, "mirrored", "Mirrored", it.mirrored, it.mirrored);
    // Foulborn — the site's own key for it is `mutated` — is the same shape and the same
    // argument: Chayula's mutation is a different item at a different price, and a search
    // that leaves it open prices the two together. Measured on Tulfall: 3855 listings in
    // all, 1896 of them not foulborn and 1960 foulborn, the mutated ones cheaper.
    add_flag(p, "mutated", "Foulborn", it.foulborn, it.foulborn);
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
    if (const Property* m = property_of(it, data::PropertyKey::MemoryStrands); m && m->num)
        add_numeric(p, "memory_level", "Memory Strands", *m->num, true);
    if (const Property* i = property_of(it, data::PropertyKey::Intangibility); i && i->num)
        add_numeric(p, "intangibility", "Intangibility", std::nullopt, false, 0, {}, *i->num);
    if (const Property* x = property_of(it, data::PropertyKey::StoredExperience); x && x->num)
        add_numeric(p, "stored_experience", "Stored Experience", *x->num, true);
}

/// What a search for an **unidentified** unique can ask, and what it has to say about the gap.
///
/// The clipboard prints no name line at all, so the base is the whole of what such an item
/// states about itself and the bundle's base → uniques index is what turns that back into a
/// name (`resolve_item`). One candidate is not a guess and is already taken; several is a
/// question only the user can settle, and until they do the search has no name to ask for —
/// which is a different search, not a worse one, so it is said out loud rather than run
/// silently as "some unique of this base".
///
/// **The item level is the one number an unidentified copy carries that matters**, and it is
/// what the rolls it can still turn out to have are bounded by — so it is a floor and it is
/// ticked, the same reading a base item's is given. Everything else about the item is behind
/// the identification; the `Unidentified` flag itself is already on every plan (`add_flag`).
void plan_unidentified(const data::GameData& gd, const Item& it, SearchPlan& p) {
    add_numeric(p, "ilvl", "Item Level",
                it.item_level ? std::optional<double>(*it.item_level) : std::nullopt, true);

    if (p.name.empty()) {
        if (!gd.has_unique_bases())
            p.notes.emplace_back("unidentified: this data bundle carries no index of which "
                                 "uniques drop on a base, so which one this is cannot be "
                                 "worked out — the search asks only for an unidentified "
                                 "unique of this base");
        else if (it.unique_candidates.empty())
            p.notes.push_back("unidentified: no unique in this data bundle drops on \"" +
                              it.base_name + "\", so the search asks only for an unidentified "
                              "unique of this base");
        else
            p.notes.push_back("unidentified: " + std::to_string(it.unique_candidates.size()) +
                              " uniques drop on \"" + it.base_name +
                              "\" — pick which one this is to search for it by name");
        return;
    }
    // Taken rather than chosen: worth saying, because nothing on the item says this name and
    // the panel is otherwise showing a search for a unique the clipboard never mentioned.
    if (it.unique_candidates.size() == 1)
        p.notes.push_back("unidentified: \"" + p.name + "\" is the only unique that drops on \"" +
                          it.base_name + "\", so that is what the search asks for");
    // The trade search asks for an unidentified copy (`add_item_flags`); poe.ninja does not
    // split a unique's price by that, and an unidentified one is a different product — the
    // gamble on the rolls rather than the rolls.
    p.notes.emplace_back("a reference price is what identified copies sell for; an "
                         "unidentified one is priced on what it might roll");
}

} // namespace

std::string_view to_string(Strategy s) { return kStrategies[static_cast<size_t>(s)]; }

bool SearchPlan::has_enabled_stats() const {
    return std::any_of(stats.begin(), stats.end(), [](const StatFilter& f) { return f.enabled; });
}

const OptionFilter* SearchPlan::option(std::string_view key) const {
    for (const OptionFilter& f : options)
        if (f.key == key) return &f;
    return nullptr;
}

void SearchPlan::select_choice(size_t i) {
    if (i >= choices.size()) return;
    choice = i;
    // Every grouped row is decided here, both the ones being turned off and the one being
    // turned on, so there is no state to get out of step: a row is enabled exactly when it is
    // the live group's primary. Rows with no group are none of this function's business.
    for (StatFilter& f : stats)
        if (f.choice) f.enabled = *f.choice == i && f.choice_primary;
}

Strategy default_strategy(const Item& it) {
    // A map is priced on none of the things a rare is, at any rarity it prints: pricing one as
    // gear would search for a chest piece carrying map modifiers. A **chart** is the same item
    // in all the ways that matter — an area with rolled danger, bought for where it goes — so it
    // shares the strategy rather than getting one of its own; the extras it needs are three
    // filters inside `plan_map`.
    if (it.is_map() || it.is_chart()) return Strategy::Map;
    // A beast reads as a rare — it has a title, an item level and rolled modifiers — and every
    // one of those is the wrong thing to price it on. What a buyer of one wants is the species,
    // because the species is what the crafting recipe names; the monster modifiers are the
    // monster's own and no recipe asks for them. Ahead of the rarity switch below, which would
    // otherwise plan a Wild Hellion Alpha as a rare and search "Extra Life" as an affix.
    if (it.is_beast()) return Strategy::Beast;
    // Ahead of the fragment rule below, which an ultimatum would otherwise fall into: it prints
    // no item level, so it would be planned as a bulk good and handed to a reference price that
    // does not exist. Two copies of one base differ in everything a buyer cares about.
    if (it.is_ultimatum()) return Strategy::Ultimatum;
    // A heist item **at any rarity but unique**. Its affixes are real, so the rarity switch
    // below would plan a rare contract as a rare and search seven heist hazards as if they were
    // what somebody was buying — and none of the reveal counts, job levels or objective values
    // that are the whole of how the site indexes one. A *unique* contract is left to the unique
    // strategy: it is bought for its name, and everything else about it is fixed by that name.
    if (it.is_heist() && it.rarity != Rarity::Unique) return Strategy::Heist;
    // A sanctum prints "Rarity: Normal" and would otherwise be planned as a base item — a search
    // for an empty Sanctum Vaults Research at this item level, which is every run in the league
    // and none of what tells two apart. Its affixes are real, and so is everything else on it.
    if (it.is_sanctum()) return Strategy::Sanctum;
    // A logbook prints Normal, Magic or Rare and is none of those readings. Its affixes are real
    // and re-rollable, its base is the one base in the category, and what a buyer is choosing
    // between is which of its up-to-three destinations they mean — a question the rarity switch
    // below has nowhere to put, and which would leave the faction as an unmatchable line of
    // prose and the price as whatever a logbook of that item level goes for.
    if (it.is_logbook()) return Strategy::Logbook;
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
    // Not for a beast or an ultimatum: both override the category outright — one to a category of
    // its own, one to none — so a bundle that could not map their item class would leave a note
    // about a gap that was about to be filled.
    if (p.category.empty() && !it.item_class.empty() && p.strategy != Strategy::Beast &&
        p.strategy != Strategy::Ultimatum)
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
            p.name = it.unique_entry ? std::string(wire_name(it.unique_entry)) : it.name;
            p.type = base_wire_name(it);
            if (it.unique_entry && !it.unique_entry->trade_disc.empty())
                p.discriminator = it.unique_entry->trade_disc;
            if (!it.identified) plan_unidentified(gd, it, p);
            else if (!it.unique_entry)
                p.notes.push_back("\"" + it.name + "\" is not in this data bundle");
            break;
        case Strategy::BaseItem:
            p.type = base_wire_name(it);
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
                    p.type = base_wire_name(it);
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
        case Strategy::Beast:
            // The species and the item level, and deliberately nothing else. The title is one
            // player's copy rather than a thing to search for, and the monster modifiers are
            // not affixes — see `plan_beast`'s note and the skip below.
            plan_beast(it, p);
            break;
        case Strategy::Ultimatum: plan_ultimatum(gd, it, p); break;
        case Strategy::Heist: plan_heist(gd, it, p); break;
        case Strategy::Sanctum: plan_sanctum(gd, it, p); break;
        case Strategy::Logbook: plan_logbook(gd, it, p); break;
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
            else if (p.strategy == Strategy::Currency && property_of(it, data::PropertyKey::StoredExperience)) {
                p.type = base_wire_name(it);
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
            // Modifiers the strategy does not search, and did not use to offer at all:
            //
            // - **A map's affixes**, which are re-rollable with one Chaos Orb and which a query
            //   naming would answer with the single copy in the league that rolled that set.
            // - **A beast's monster modifiers**, which are not affixes.
            // - **An ultimatum's hazards** — only the two that scale the stake are the deal;
            //   see `ultimatum_stake_mod`.
            //
            // They now get a filter, marked `hidden`, which puts them behind the expandable
            // section at the foot of the list rather than on the floor. Unticked, so the search
            // is exactly what it was; ticked, they are ordinary filters. Every one of these is
            // occasionally the whole question, and there was no way to ask it here before.
            // - **A logbook's own affixes**, on the map argument exactly: they apply wherever
            //   the book goes, a Chaos Orb redoes them, and what is being bought is where it
            //   goes. Its destinations' implicits are not among them — those belong to a
            //   destination, and `group_logbook_mods` files each under the one it came from.
            const bool hidden =
                (p.strategy == Strategy::Map && !map_searched_mod(it.mods[i])) ||
                p.strategy == Strategy::Beast ||
                (p.strategy == Strategy::Ultimatum && !ultimatum_stake_mod(it.mods[i])) ||
                (p.strategy == Strategy::Logbook && !logbook_destination_mod(it, i));
            if (std::optional<StatFilter> f = to_filter(it, i, p.strategy, ranges_printed, rm)) {
                if (hidden) {
                    f->hidden = true;
                    f->enabled = false;
                } else if (p.strategy == Strategy::Ultimatum) {
                    // Exact, and deliberately not what the range-match setting asked for: these
                    // two are the size of the deal, not a roll to be beaten. Set here rather
                    // than inside `to_filter` because it is the strategy that makes it true, and
                    // `to_filter`'s job is to read the modifier.
                    if (const Roll roll = roll_for(*it.mods[i].match); roll.value) {
                        f->min = *roll.value;
                        f->max = *roll.value;
                    }
                    f->tiered = false;
                    f->enabled = true;
                }
                p.stats.push_back(std::move(*f));
                continue;
            }
            // A modifier the strategy was never going to search is not one it failed at, so it
            // gets no note either — "unrecognised modifier: Players have 25% less Accuracy
            // Rating" on a map charges the check with something it deliberately did not attempt.
            // The reader can see it on the item beside the panel, which is where it belongs.
            if (hidden) continue;
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
        // Before the merge as well, and for the same reason `apply_unique_mods` is: it reads
        // `mod_index`, and merging is what makes one filter stand for several modifiers. It is
        // also what tells the merge which rows are alternatives rather than repeats.
        if (p.strategy == Strategy::Logbook) group_logbook_mods(it, p);
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
        // Not gated on `impose`: a six-link is worth more than the unique printed on it, so this
        // is the one number a unique *is* searched on. It follows the socket count, not the
        // strategy.
        add_sockets(p, it);
        // Last, because it is a question about the numerics that were just added.
        unimpose_derived_mods(it, p);
    }
    // Driven by the properties being printed rather than by the strategy: what carries them is
    // what a crafting mechanic touched, and that is a fact about the copy in hand.
    if (p.strategy != Strategy::Unsupported) add_property_filters(it, p);
    // A gem is nothing but its own effect, so saying this about one is noise.
    if (!it.inherent_lines.empty() && it.rarity != Rarity::Gem)
        p.notes.emplace_back("the base's own effect is not part of the search");

    // The first destination is live until the reader says otherwise, and it is the *first* on
    // purpose: nothing here can rank the four factions, that ranking changes with the league and
    // with what the player is farming, and a default dressed up as an answer would be read as
    // one. The panel puts the alternatives in the game's own order and the choice is one click.
    if (!p.choices.empty()) p.select_choice(0);

    // Last of all, and in one place rather than at each of the dozen sites that set a bound:
    // the seed is *whatever this function came out with*, so a per-row reset can only ever
    // disagree with the plan if it is recorded somewhere the plan can still be changed after.
    for (StatFilter& f : p.stats) {
        f.seed_min = f.min;
        f.seed_max = f.max;
    }
    for (NumericFilter& f : p.numerics) {
        f.seed_min = f.min;
        f.seed_max = f.max;
    }
    return p;
}

} // namespace ppc::item
