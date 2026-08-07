#include "item/derive.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "data/stat_normalize.hpp"

namespace ppc::item {
namespace {

/// The defences a wording contributes to. Locality is not in the data, so it is decided here:
/// these wordings are local exactly when the item displays the property they name, which is
/// why every use below is guarded on the property being present.
struct LocalDefence {
    std::string_view form;
    bool ar = false, ev = false, es = false, ward = false;
    bool flat = false;
};

constexpr std::array<LocalDefence, 15> kLocalDefences{{
    {"#% increased Armour", true},
    {"#% increased Evasion Rating", false, true},
    {"#% increased Energy Shield", false, false, true},
    {"#% increased Ward", false, false, false, true},
    {"#% increased Armour and Evasion", true, true},
    {"#% increased Armour and Energy Shield", true, false, true},
    {"#% increased Evasion and Energy Shield", false, true, true},
    {"#% increased Armour, Evasion and Energy Shield", true, true, true},
    {"# to Armour", true, false, false, false, true},
    {"# to Evasion Rating", false, true, false, false, true},
    {"# to maximum Energy Shield", false, false, true, false, true},
    {"# to Ward", false, false, false, true, true},
    {"# to Armour and Evasion Rating", true, true, false, false, true},
    {"# to Armour and Energy Shield", true, false, true, false, true},
    {"# to Evasion Rating and Energy Shield", false, true, true, false, true},
}};

constexpr std::string_view kLocalPhysical = "#% increased Physical Damage";
constexpr std::string_view kFlatPhysical = "Adds # to # Physical Damage";

/// The wordings the game folds into a weapon's own properties, as against the ones that only
/// act on the character holding it. "#% increased Elemental Damage" is deliberately absent:
/// it never touches the elemental damage the weapon displays, so it is not inside `edps`.
constexpr std::string_view kFlatElemental[]{"Adds # to # Fire Damage", "Adds # to # Cold Damage",
                                            "Adds # to # Lightning Damage"};
constexpr std::string_view kFlatChaos = "Adds # to # Chaos Damage";
constexpr std::string_view kLocalAttackSpeed = "#% increased Attack Speed";
constexpr std::string_view kLocalCrit = "#% increased Critical Strike Chance";

/// A mod's roll, from the matcher when it resolved and from the text when it did not — the
/// numbers here decide a search's bounds, so they must not depend on a bundle being loaded.
std::optional<double> roll_of(const Modifier& m, const std::string& line) {
    if (m.match && !m.match->rolls.empty()) return m.match->value;
    const std::vector<data::NumberToken> toks = data::scan_numbers(data::strip_empty_parens(line));
    if (toks.empty()) return std::nullopt;
    double sum = 0;
    for (const data::NumberToken& t : toks) sum += t.value;
    return sum / static_cast<double>(toks.size());
}

struct Locals {
    double ar = 0, ev = 0, es = 0, ward = 0;                     ///< percent increases
    double flat_ar = 0, flat_ev = 0, flat_es = 0, flat_ward = 0;  ///< flat adds
    double phys = 0;      ///< percent increased physical damage
    double flat_phys = 0; ///< added physical damage, averaged the way the property is
};

Locals sum_locals(const Item& it) {
    Locals l;
    for (const Modifier& m : it.mods) {
        // An enchant or a scourge mod is printed on the item but is not folded into its
        // defence properties in the same way; only affixes and implicits are.
        for (const std::string& line : m.lines) {
            const std::string form = data::placeholder_form(line);
            if (it.is_weapon() && (form == kLocalPhysical || form == kFlatPhysical)) {
                if (const std::optional<double> v = roll_of(m, line))
                    (form == kLocalPhysical ? l.phys : l.flat_phys) += *v;
                continue;
            }
            for (const LocalDefence& d : kLocalDefences) {
                if (d.form != form) continue;
                const std::optional<double> v = roll_of(m, line);
                if (!v) break;
                if (d.flat) {
                    if (d.ar && it.armour) l.flat_ar += *v;
                    if (d.ev && it.evasion) l.flat_ev += *v;
                    if (d.es && it.energy_shield) l.flat_es += *v;
                    if (d.ward && it.ward) l.flat_ward += *v;
                } else {
                    if (d.ar && it.armour) l.ar += *v;
                    if (d.ev && it.evasion) l.ev += *v;
                    if (d.es && it.energy_shield) l.es += *v;
                    if (d.ward && it.ward) l.ward += *v;
                }
                break;
            }
        }
    }
    return l;
}

/// The base's own roll, recovered from what the item displays.
///
/// **Quality scales the base's inherent value, not the flat local modifiers**, and the item's
/// local increases then apply to the sum:
/// `displayed = (base * (1 + q/100) + flat) * (1 + incr/100)`.
/// One rule for a weapon's physical damage and for a defence — the only difference is which
/// modifiers are the local ones.
///
/// Folding quality into the same bucket as the increases (the form the reference tools use, and
/// what "quality is additive with increased physical damage" describes) recovers a base ~8% too
/// high on a 20% quality chest with a flat energy shield prefix: the Rift Carapace capture comes
/// out at 316.8 against a Twilight Regalia range of 262..302, and its percentile is lost. This
/// form puts it at 293.3, inside the range. That capture is the only real evidence either way, so
/// a before/after quality capture of one item carrying a flat local roll would settle it.
double inherent_roll(double displayed, int quality, double incr, double flat) {
    return (displayed / (1.0 + incr / 100.0) - flat) / (1.0 + quality / 100.0);
}

double at_q20(double displayed, int quality, double incr, double flat) {
    return (inherent_roll(displayed, quality, incr, flat) * 1.2 + flat) * (1.0 + incr / 100.0);
}

std::optional<double> percentile(double base, double lo, double hi) {
    if (hi <= lo) return std::nullopt;
    const double pct = (base - lo) / (hi - lo);
    // A base that lands outside its own range means a local modifier was missed or the bundle
    // disagrees with the client. Rounding puts a perfect roll a hair outside, so allow that
    // much and no more: no number at all beats a confident 0%.
    if (pct < -0.05 || pct > 1.05) return std::nullopt;
    return std::clamp(pct, 0.0, 1.0);
}

} // namespace

Derived derive(const data::GameData* gd, const Item& it) {
    Derived d;
    const int q = it.quality.value_or(0);
    const Locals l = sum_locals(it);
    d.incr_armour = l.ar;
    d.incr_evasion = l.ev;
    d.incr_energy_shield = l.es;
    d.incr_ward = l.ward;
    d.incr_physical = l.phys;

    if (it.attacks_per_second) {
        const double aps = *it.attacks_per_second;
        double total = 0;
        if (it.physical) {
            // Added physical damage is a flat local roll, exactly like a flat defence one, so
            // quality does not scale it — hence the average being un-done and redone here.
            d.pdps = it.physical->avg() * aps;
            d.pdps_q20 = at_q20(it.physical->avg(), q, l.phys, l.flat_phys) * aps;
            total += *d.pdps;
        }
        if (!it.elemental.empty()) {
            double sum = 0;
            for (const DamageRange& e : it.elemental) sum += e.avg();
            d.edps = sum * aps;
            total += *d.edps;
        }
        if (it.chaos) {
            d.cdps = it.chaos->avg() * aps;
            total += *d.cdps;
        }
        if (it.physical || d.edps || d.cdps) {
            d.dps = total;
            // Quality only scales physical damage, so the rest of the total carries over.
            d.dps_q20 = total - d.pdps.value_or(0) + d.pdps_q20.value_or(0);
        }
        d.search_pdps = q > 20 ? d.pdps : d.pdps_q20;
        d.search_dps = q > 20 ? d.dps : d.dps_q20;
        d.search_edps = d.edps;
    }

    const data::BaseType* b = it.base;
    struct Defence {
        const std::optional<int>& value;
        double incr, flat;
        const std::optional<std::pair<int, int>>* range;
        std::optional<int>& q20;
        std::optional<int>& search;
    };
    const std::array<Defence, 4> defences{{
        {it.armour, l.ar, l.flat_ar, b ? &b->armour : nullptr, d.armour_q20, d.search_armour},
        {it.evasion, l.ev, l.flat_ev, b ? &b->evasion : nullptr, d.evasion_q20, d.search_evasion},
        {it.energy_shield, l.es, l.flat_es, b ? &b->energy_shield : nullptr, d.energy_shield_q20,
         d.search_energy_shield},
        {it.ward, l.ward, l.flat_ward, b ? &b->ward : nullptr, d.ward_q20, d.search_ward},
    }};
    // One roll, spread over every defence the base has, so the percentile is one sum against
    // another. A defence with no published range makes the sums incomparable, not partial.
    double base_sum = 0, range_lo = 0, range_hi = 0;
    bool ranges_complete = gd != nullptr && b != nullptr;
    for (const Defence& def : defences) {
        if (!def.value) continue;
        def.q20 = static_cast<int>(std::lround(at_q20(*def.value, q, def.incr, def.flat)));
        def.search = q > 20 ? *def.value : *def.q20;
        if (!def.range || !*def.range) {
            ranges_complete = false;
            continue;
        }
        base_sum += inherent_roll(*def.value, q, def.incr, def.flat);
        range_lo += (*def.range)->first;
        range_hi += (*def.range)->second;
    }
    if (ranges_complete && it.has_defences()) d.base_pct = percentile(base_sum, range_lo, range_hi);
    return d;
}

std::vector<std::string_view> derived_filter_keys(const Item& it, const Modifier& m) {
    std::vector<std::string_view> out;
    const auto add = [&out](std::initializer_list<std::string_view> keys) {
        for (const std::string_view k : keys)
            if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
    };
    for (const std::string& line : m.lines) {
        const std::string form = data::placeholder_form(line);
        if (it.is_weapon()) {
            // Every damage roll is inside the total as well as inside its own half of it, and
            // attack speed multiplies all three: the properties they scale are what the game
            // prints, and the DPS numbers are those properties divided out again.
            if (form == kLocalPhysical || form == kFlatPhysical) {
                add({"pdps", "dps"});
                continue;
            }
            if (std::find(std::begin(kFlatElemental), std::end(kFlatElemental), form) !=
                std::end(kFlatElemental)) {
                add({"edps", "dps"});
                continue;
            }
            if (form == kFlatChaos) {
                add({"dps"}); // trade has no chaos DPS filter; it is inside the total
                continue;
            }
            if (form == kLocalAttackSpeed) {
                add({"aps", "pdps", "edps", "dps"});
                continue;
            }
            if (form == kLocalCrit) {
                add({"crit"});
                continue;
            }
        }
        // The same guard `sum_locals` uses: these wordings are local exactly when the item
        // displays the property they name. **Not** the base percentile — that is recovered by
        // taking these back *out* of the displayed value, so it is the one derived number a
        // local roll is not inside.
        for (const LocalDefence& d : kLocalDefences) {
            if (d.form != form) continue;
            if (d.ar && it.armour) add({"ar"});
            if (d.ev && it.evasion) add({"ev"});
            if (d.es && it.energy_shield) add({"es"});
            if (d.ward && it.ward) add({"ward"});
            break;
        }
    }
    return out;
}

} // namespace ppc::item
