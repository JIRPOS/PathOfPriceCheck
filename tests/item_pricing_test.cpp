#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "item/derive.hpp"
#include "item/plan.hpp"
#include "item/resolve.hpp"

namespace fs = std::filesystem;
using namespace ppc::item;
using ppc::data::GameData;

namespace {

std::shared_ptr<GameData> fixture() {
    std::string err;
    auto gd = GameData::open(fs::path(PPC_TEST_DATA_DIR) / "bundle", "en", &err);
    REQUIRE_MESSAGE(gd != nullptr, "opening the fixture bundle failed: " << err);
    return gd;
}

std::string capture(const char* name, const char* dir = "items") {
    std::ifstream in(fs::path(PPC_TEST_DATA_DIR) / dir / name, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// Parse and resolve in one step. The items here are written against the committed fixture
/// bundle, which holds a handful of stats and bases — enough for every code path.
Item resolved(const GameData& gd, std::string_view text) {
    std::optional<Item> it = parse_item(text);
    REQUIRE(it.has_value());
    resolve_item(gd, *it);
    return *it;
}

const StatFilter* filter_for(const SearchPlan& p, std::string_view id) {
    for (const StatFilter& f : p.stats)
        if (f.id == id) return &f;
    return nullptr;
}

const StatFilter* filter_saying(const SearchPlan& p, std::string_view text) {
    for (const StatFilter& f : p.stats)
        if (f.text.find(text) != std::string::npos) return &f;
    return nullptr;
}

// These point into the plan's own vector, so a plan passed as a temporary leaves every caller
// holding freed memory the moment the full expression ends. glibc hands the bytes back
// unchanged and the checks pass; MSVC's debug heap poisons them and they do not. Deleted
// rather than commented, so the next one is a compile error instead of a Windows-only failure.
const StatFilter* filter_for(SearchPlan&&, std::string_view) = delete;
const StatFilter* filter_saying(SearchPlan&&, std::string_view) = delete;

/// "At least what it rolled and nothing else", for the cases that are about **which side** a
/// bound lands on rather than how wide it opens. A window is symmetric, so the direction rule
/// `seed_bounds` applies to a stat that is better the lower it goes only shows with one side
/// open — which is also what these cases asserted before the width was a setting.
constexpr RangeMatch kFloorOnly{BoundMode::Exact, BoundMode::Unbound};

/// Wide enough that the tier gate is always what decides both bounds, which is how a case
/// about the tier's own range states itself now that the window around the roll is a setting.
constexpr RangeMatch kWholeTier{BoundMode::WithinTiered, BoundMode::WithinTiered, 100, 100};

const NumericFilter* numeric_for(const SearchPlan& p, std::string_view key) {
    for (const NumericFilter& f : p.numerics)
        if (f.key == key) return &f;
    return nullptr;
}

/// What the search asks a `misc_filters` boolean to be, or nothing when it does not ask.
std::optional<bool> flag_of(const SearchPlan& p, std::string_view key) {
    const OptionFilter* f = p.option(key);
    if (!f || !f->enabled) return std::nullopt;
    return f->option == "true";
}

constexpr std::string_view kRareChest = R"(Item Class: Body Armours
Rarity: Rare
Doom Shroud
Vaal Regalia
--------
Energy Shield: 405 (augmented)
--------
Requirements:
Level: 68
Int: 194
--------
Item Level: 84
--------
+42 to maximum Life
+25% to Fire Resistance
120% increased Energy Shield
)";

} // namespace

TEST_CASE("a rare's modifiers become enabled stat filters") {
    auto gd = fixture();
    const Item it = resolved(*gd, kRareChest);
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    CHECK(p.strategy == Strategy::Modifiers);
    CHECK(p.category == "armour.chest");
    // A rare is bought for its mods, so the base is not part of the search.
    CHECK(p.type.empty());
    CHECK(flag_of(p, "corrupted") == false);

    const StatFilter* life = filter_for(p, "explicit.stat_3299347043");
    REQUIRE(life != nullptr);
    CHECK(life->enabled);
    // The default asking is the tier-gated 5% window, and without Advanced Mod Descriptions
    // there is no tier to gate it: 42 opens to 39-45, floored down and ceiled up so a small
    // percentage of a small roll still moves it by a whole point.
    CHECK(life->min == doctest::Approx(39));
    CHECK(life->max == doctest::Approx(45));
    CHECK_FALSE(life->tiered);

    REQUIRE(filter_for(p, "explicit.stat_3372524247") != nullptr);
    CHECK(filter_for(p, "explicit.stat_3372524247")->min == doctest::Approx(23));

    // The energy shield mod is not in this slice of the bundle: it has to be reported, not
    // dropped — a silently missing filter reads as a successful price check on a worse item.
    CHECK(p.stats.size() == 2);
    CHECK(p.notes.size() == 1);
    CHECK(p.notes.front().starts_with("unrecognised modifier: 120% increased Energy Shield"));
}

TEST_CASE("how wide a filter opens around the roll is the user's setting") {
    SUBCASE("unbound fills nothing, exact fills the roll") {
        Bounds b = seed_bounds({BoundMode::Unbound, BoundMode::Unbound}, 86, 77, 90, 0, false);
        CHECK_FALSE(b.min.has_value());
        CHECK_FALSE(b.max.has_value());

        b = seed_bounds({BoundMode::Exact, BoundMode::Exact}, 86, 77, 90, 0, false);
        CHECK(b.min == doctest::Approx(86));
        CHECK(b.max == doctest::Approx(86));
    }

    SUBCASE("a window is rounded outwards, so a small percentage still moves a small roll") {
        const RangeMatch rm{BoundMode::Within, BoundMode::Within};
        Bounds b = seed_bounds(rm, 86, std::nullopt, std::nullopt, 0, false);
        CHECK(b.min == doctest::Approx(81)); // 81.7 floored
        CHECK(b.max == doctest::Approx(91)); // 90.3 ceiled

        // 5% of 20 is exactly 1, and of 1 is a fifth of nothing. Both still move a whole point,
        // which is the only movement the filter can express.
        b = seed_bounds(rm, 20, std::nullopt, std::nullopt, 0, false);
        CHECK(b.min == doctest::Approx(19));
        CHECK(b.max == doctest::Approx(21));
        b = seed_bounds(rm, 1, std::nullopt, std::nullopt, 0, false);
        CHECK(b.min == doctest::Approx(0));
        CHECK(b.max == doctest::Approx(2));

        // …and "a whole point" is the filter's own last digit, not the integer 1.
        b = seed_bounds(rm, 1.79, std::nullopt, std::nullopt, 2, false);
        CHECK(b.min == doctest::Approx(1.70));
        CHECK(b.max == doctest::Approx(1.88));
    }

    SUBCASE("only the tiered modes are gated, and the gate never crosses the roll") {
        Bounds b = seed_bounds({BoundMode::Within, BoundMode::Within}, 86, 77, 90, 0, false);
        CHECK(b.min == doctest::Approx(81));
        CHECK(b.max == doctest::Approx(91)); // past the tier, because nothing said not to

        b = seed_bounds({BoundMode::WithinTiered, BoundMode::WithinTiered}, 86, 77, 90, 0, false);
        CHECK(b.min == doctest::Approx(81));
        CHECK(b.max == doctest::Approx(90));

        // A legacy roll sits outside the range its modifier publishes today. Gating to that
        // would ask for a copy of the item that is not the one in hand.
        b = seed_bounds({BoundMode::WithinTiered, BoundMode::WithinTiered, 100, 100}, 60, 20, 40, 0,
                        false);
        CHECK(b.min == doctest::Approx(20));
        CHECK(b.max == doctest::Approx(60));
    }

    SUBCASE("the minimum is the bound that says at least this good, whichever side that is") {
        Bounds b = seed_bounds(kFloorOnly, 47, std::nullopt, std::nullopt, 0, false);
        CHECK(b.min == doctest::Approx(47));
        CHECK_FALSE(b.max.has_value());

        b = seed_bounds(kFloorOnly, -11, std::nullopt, std::nullopt, 0, true);
        CHECK_FALSE(b.min.has_value());
        CHECK(b.max == doctest::Approx(-11));
    }
}

TEST_CASE("quality and local increases decide the searched defence") {
    auto gd = fixture();
    const Item it = resolved(*gd, kRareChest);
    const Derived d = derive(gd.get(), it);

    // The local increase is found from the wording, so it counts even though the stat itself
    // is not in the bundle.
    CHECK(d.incr_energy_shield == doctest::Approx(120));
    // Quality scales the base's own defence, and the item's +120% then applies to that too:
    // 405 / 2.2 = 184 of base, 184 * 1.2 * 2.2 = 486 at 20% quality.
    CHECK(d.energy_shield_q20 == 486);
    CHECK(d.search_energy_shield == 486);
    // 405 / 2.2 = 184 of a base that rolls 171..197.
    REQUIRE(d.base_pct.has_value());
    CHECK(*d.base_pct == doctest::Approx(0.503).epsilon(0.01));

    const SearchPlan p = build_plan(*gd, it, d);
    const NumericFilter* es = numeric_for(p, "es");
    REQUIRE(es != nullptr);
    CHECK(es->enabled);
    CHECK(es->min == doctest::Approx(486));
    CHECK(es->note.starts_with("normalised to 20% quality"));
    // Quality is only worth filtering on when it is above what a buyer can reach for free.
    CHECK(numeric_for(p, "quality") == nullptr);
    // Item level is offered but not imposed on a mod search.
    REQUIRE(numeric_for(p, "ilvl") != nullptr);
    CHECK_FALSE(numeric_for(p, "ilvl")->enabled);
}

TEST_CASE("weapon DPS matches what the game shows") {
    auto gd = fixture();
    Item it = resolved(*gd, capture("rare-rapier.txt"));
    const Derived d = derive(gd.get(), it);

    CHECK(d.pdps == doctest::Approx(110.1).epsilon(0.001));
    CHECK(d.edps == doctest::Approx(105.6).epsilon(0.001));
    CHECK(d.dps == doctest::Approx(215.7).epsilon(0.001));
    // Quality scales the base's own damage, which the item's +128% then multiplies: base avg
    // 61.5 / 2.28 = 26.97, at 20% quality 26.97 * 1.2 * 2.28 = 73.8 -> 132.1 pdps. The
    // elemental half is untouched by quality. This capture has no added physical damage, so it
    // does not pin down whether a flat local roll is scaled — see `inherent_roll`.
    CHECK(d.incr_physical == doctest::Approx(128));
    CHECK(d.dps_q20 == doctest::Approx(237.7).epsilon(0.001));
    CHECK(d.search_dps == d.dps_q20);
}

TEST_CASE("attack speed and crit chance are searched only where a modifier raised them") {
    auto gd = fixture();
    // This rapier prints "Attacks per Second: 1.79 (augmented)" and "Critical Strike Chance:
    // 5.00%" plain — a modifier moved the one and not the other, and that augmented marker is
    // the only thing in the clipboard that says a property beats the base's own number. Asking
    // for a base's crit chance rules out nothing but the same weapon in another stash.
    const Item it = resolved(*gd, capture("rare-rapier.txt"));
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    const NumericFilter* aps = numeric_for(p, "aps");
    REQUIRE(aps != nullptr);
    CHECK(aps->enabled);
    CHECK(aps->min == doctest::Approx(1.79));

    const NumericFilter* crit = numeric_for(p, "crit");
    REQUIRE(crit != nullptr);
    CHECK_FALSE(crit->enabled);

    // The three DPS numbers are what a weapon is bought on, and they are imposed.
    for (const char* k : {"dps", "pdps", "edps"}) {
        REQUIRE_MESSAGE(numeric_for(p, k) != nullptr, k);
        CHECK_MESSAGE(numeric_for(p, k)->enabled, k);
    }
}

TEST_CASE("a modifier already inside a searched number is not searched again by name") {
    auto gd = fixture();
    // Every damage roll on this rapier is inside the DPS totals the search imposes, and its
    // attack speed roll is inside all three. Asking for both the number and the modifier that
    // produced it rules out every other way of reaching the same DPS — which is the whole
    // reason a buyer searches on DPS.
    const Item it = resolved(*gd, capture("rare-rapier.txt"));
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    for (const char* m : {"increased Physical Damage", "Adds 23 to 42 Fire Damage",
                          "Adds 3 to 50 Lightning Damage", "increased Attack Speed"}) {
        REQUIRE_MESSAGE(filter_saying(p, m) != nullptr, m);
        CHECK_MESSAGE(!filter_saying(p, m)->enabled, m);
    }
    // Global critical strike *multiplier* reads like a weapon number and is inside none of
    // them — trade's `crit` is the weapon's own chance — so both rolls of it stay enabled.
    int mult = 0;
    for (const StatFilter& f : p.stats)
        if (f.text.find("Global Critical Strike Multiplier") != std::string::npos) {
            CHECK(f.enabled);
            ++mult;
        }
    CHECK(mult == 2);
}

TEST_CASE("a fractured roll keeps its filter even where the number it feeds is searched") {
    auto gd = fixture();
    // Fracturing is what survives every craft the buyer will do afterwards, so which modifier
    // reached the DPS is the point of the item rather than an over-constraint on it. The
    // filter is sent in trade's own fractured namespace, which is what makes it a different
    // question from the same wording rolled ordinarily.
    const Item it = resolved(*gd, R"(Item Class: Thrusting One Hand Swords
Rarity: Rare
Sorrow Saw
Wyrmbone Rapier
--------
One Handed Sword
Physical Damage: 25-98 (augmented)
Critical Strike Chance: 5.00%
Attacks per Second: 1.79 (augmented)
--------
Item Level: 67
--------
128% increased Physical Damage (fractured)
15% increased Attack Speed
--------
Fractured Item
)");
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    const StatFilter* phys = filter_saying(p, "increased Physical Damage");
    REQUIRE(phys != nullptr);
    CHECK(phys->type == ppc::data::ModType::Fractured);
    CHECK(phys->id.starts_with("fractured."));
    CHECK(phys->enabled);
    CHECK(flag_of(p, "fractured_item") == true); // and the item-level `misc_filters` flag goes with it

    // The ordinary roll beside it is still unimposed: nothing about it is fixed to this copy.
    REQUIRE(filter_saying(p, "increased Attack Speed") != nullptr);
    CHECK_FALSE(filter_saying(p, "increased Attack Speed")->enabled);
}

TEST_CASE("nothing is unimposed where the derived number is not being asked for") {
    auto gd = fixture();
    // A unique's damage follows from which unique it is, so the DPS filters are offered rather
    // than imposed — and then the modifier behind the number is the only question there is.
    const Item it = resolved(*gd, capture("rare-rapier.txt"));
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d, Strategy::Unique);

    REQUIRE(numeric_for(p, "pdps") != nullptr);
    CHECK_FALSE(numeric_for(p, "pdps")->enabled);
    // Not enabled either, but for the reason `Strategy::Unique` already had: it is a fixed
    // roll on a named item. What matters is that the pass above did not touch it.
    REQUIRE(filter_saying(p, "increased Physical Damage") != nullptr);
}

TEST_CASE("the base's own roll is a filter of its own, not a remark under the defence") {
    auto gd = fixture();
    const Item it = resolved(*gd, kRareChest);
    const Derived d = derive(gd.get(), it);

    // 50.3rd percentile, floored: the filter is a minimum, and an item asked for at 51 does
    // not match itself.
    const SearchPlan mods = build_plan(*gd, it, d);
    const NumericFilter* pct = numeric_for(mods, "base_defence_percentile");
    REQUIRE(pct != nullptr);
    CHECK(pct->min == doctest::Approx(50));
    CHECK_FALSE(pct->max.has_value());
    // Off on a modifier search: the energy shield filter above already carries the same roll,
    // and asking twice only drops the listings that answer once.
    CHECK_FALSE(pct->enabled);
    // And the defence no longer says it in prose.
    REQUIRE(numeric_for(mods, "es") != nullptr);
    CHECK(numeric_for(mods, "es")->note.find("base roll") == std::string::npos);

    // On a base-item search the roll *is* what is being bought.
    const SearchPlan base = build_plan(*gd, it, d, Strategy::BaseItem);
    REQUIRE(numeric_for(base, "base_defence_percentile") != nullptr);
    CHECK(numeric_for(base, "base_defence_percentile")->enabled);
}

TEST_CASE("Advanced Mod Descriptions bound a mod search by its tier") {
    auto gd = fixture();
    const Item it = resolved(*gd, R"(Item Class: Body Armours
Rarity: Rare
Doom Shroud
Vaal Regalia
--------
Energy Shield: 200
--------
Item Level: 84
--------
{ Prefix Modifier "Athlete's" (Tier: 2) — Life }
+89(80-89) to maximum Life
)");
    const Derived d = derive(gd.get(), it);

    // A window wider than the tier is gated by it on both sides — the affix cannot roll past
    // its own range, so asking for that only drops the copies that answer the question.
    const SearchPlan whole = build_plan(*gd, it, d, std::nullopt, kWholeTier);
    const StatFilter* life = filter_for(whole, "explicit.stat_3299347043");
    REQUIRE(life != nullptr);
    CHECK(life->tiered);
    CHECK(life->min == doctest::Approx(80));
    CHECK(life->max == doctest::Approx(89));

    // And the default 5% window only meets that gate at the top: 89 is what the tier gives.
    const SearchPlan dflt_plan = build_plan(*gd, it, d);
    const StatFilter* dflt = filter_for(dflt_plan, "explicit.stat_3299347043");
    REQUIRE(dflt != nullptr);
    CHECK(dflt->min == doctest::Approx(84));
    CHECK(dflt->max == doctest::Approx(89));
}

TEST_CASE("a modifier that is better the lower it goes is bounded from above") {
    auto gd = fixture();
    // An eldritch implicit: its magnitude comes from the currency tier that put it there, so
    // the clipboard prints no range and all the filter has is the roll. -11 as a *minimum*
    // asks for every weaker copy of it; the buyer wants -11 or more.
    const Item it = resolved(*gd, R"(Item Class: Body Armours
Rarity: Rare
Doom Shroud
Vaal Regalia
--------
Energy Shield: 200
--------
Item Level: 84
--------
{ Eater of Worlds Implicit Modifier (Lesser) }
Inflict Cold Exposure on Hit, applying -11% to Cold Resistance
--------
{ Suffix Modifier "of the Sky" (Tier: 1) — Elemental, Cold, Resistance }
+47(46-48)% to Cold Resistance
)");
    const Derived d = derive(gd.get(), it);
    // Asked for as a floor and nothing else, which is the only shape the direction shows in:
    // the "Minimum" setting is the bound that says *at least this good*, and on a modifier the
    // game prints negative that is the upper one.
    const SearchPlan p = build_plan(*gd, it, d, std::nullopt, kFloorOnly);

    const StatFilter* exposure = filter_for(p, "implicit.stat_3005701891");
    REQUIRE(exposure != nullptr);
    CHECK_FALSE(exposure->min.has_value());
    CHECK(exposure->max == doctest::Approx(-11));
    // A resistance is the ordinary direction, so the same setting fills the other side.
    const StatFilter* cold = filter_for(p, "explicit.stat_4220027924");
    REQUIRE(cold != nullptr);
    CHECK(cold->min == doctest::Approx(47));
    CHECK_FALSE(cold->max.has_value());

    // Both sides open onto a window and the direction stops showing: -11 widens outwards to
    // -12..-10 rather than inwards, which is what taking the slack off the magnitude buys.
    const SearchPlan both_plan = build_plan(*gd, it, d);
    const StatFilter* both = filter_for(both_plan, "implicit.stat_3005701891");
    REQUIRE(both != nullptr);
    CHECK(both->min == doctest::Approx(-12));
    CHECK(both->max == doctest::Approx(-10));
}

TEST_CASE("an added-damage mod is searched on its average, tier bounds included") {
    auto gd = fixture();
    const Item it = resolved(*gd, R"(Item Class: Body Armours
Rarity: Rare
Doom Shroud
Vaal Regalia
--------
Energy Shield: 200
--------
Item Level: 84
--------
{ Prefix Modifier "Flaring" (Tier: 3) — Damage, Physical }
Adds 5(4-6) to 12(10-14) Physical Damage
)");
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d, std::nullopt, kWholeTier);

    REQUIRE(p.stats.size() == 1);
    const StatFilter& f = p.stats.front();
    // Trade indexes the pair as its average, so the tier's range is the average of the two
    // ranges — not the widest number either of them could be.
    CHECK(f.min == doctest::Approx(7));
    CHECK(f.max == doctest::Approx(10));
    CHECK(f.tiered);
}

TEST_CASE("one affix printed as two lines is searched as two stats") {
    auto gd = fixture();
    // "Urchin's" is one prefix, but armour and life are separate stats on trade. The armour
    // half is not in this slice of the bundle, which is what makes the split visible: the
    // life half must still resolve.
    const Item it = resolved(*gd, R"(Item Class: Body Armours
Rarity: Rare
Doom Shroud
Vaal Regalia
--------
Energy Shield: 200
--------
Item Level: 84
--------
{ Prefix Modifier "Urchin's" (Tier: 2) — Life, Defences }
+34(33-48) to Armour
+42(40-45) to maximum Life
)");

    REQUIRE(it.mods.size() == 2);
    CHECK(it.mods[0].affix_name == "Urchin's");
    CHECK(it.mods[1].affix_name == "Urchin's");
    // Only the first part prints the affix line; both carry its name.
    CHECK_FALSE(it.mods[0].continuation);
    CHECK(it.mods[1].continuation);
    CHECK_FALSE(it.mods[0].match.has_value());
    REQUIRE(it.mods[1].match.has_value());
    CHECK(it.mods[1].match->stat->ref == "# to maximum Life");
}

TEST_CASE("two rolls of one stat are searched as their total") {
    auto gd = fixture();
    const Item it = resolved(*gd, R"(Item Class: Body Armours
Rarity: Rare
Doom Shroud
Vaal Regalia
--------
Energy Shield: 200
--------
Item Level: 84
--------
{ Prefix Modifier "Urchin's" (Tier: 2) — Life, Defences }
+28(24-28) to maximum Life
{ Prefix Modifier "Athlete's" (Tier: 2) — Life }
+89(80-89) to maximum Life
)");
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d, std::nullopt, kWholeTier);

    // Trade indexes the item's total life, so two filters would each be compared against 117
    // and the weaker one would decide the search on its own.
    REQUIRE(p.stats.size() == 1);
    CHECK(p.stats.front().min == doctest::Approx(104));
    CHECK(p.stats.front().max == doctest::Approx(117));
    CHECK(p.stats.front().tiered);
}

TEST_CASE("a catalyst's scaling is applied to the roll and to its tier") {
    auto gd = fixture();
    // Catalyst quality scales the mods it applies to, and the clipboard prints the *unscaled*
    // roll and range: the tooltip for this ring reads 30%, not the 25 written here.
    const Item it = resolved(*gd, R"(Item Class: Rings
Rarity: Rare
Doom Coil
Two-Stone Ring
--------
Quality (Critical Modifiers): +20% (augmented)
--------
Item Level: 84
--------
{ Prefix Modifier "Sunfire" (Tier: 1) )"
                                     "\xe2\x80\x94"
                                     R"( Elemental, Fire, Resistance )"
                                     "\xe2\x80\x94"
                                     R"( 20% Increased }
+25(15-25)% to Fire Resistance
)");
    REQUIRE(it.mods.size() == 1);
    CHECK(it.mods.front().roll_incr == doctest::Approx(20));

    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d, std::nullopt, kWholeTier);
    const StatFilter* res = filter_for(p, "explicit.stat_3372524247");
    REQUIRE(res != nullptr);
    CHECK(res->min == doctest::Approx(18));
    CHECK(res->max == doctest::Approx(30));
    CHECK(res->tiered);
    // Not legacy: the scaled roll has to be compared against the scaled tier, or every
    // catalysed mod looks like it rolled above its own range.
    CHECK_FALSE(it.mods.front().match->legacy);
    CHECK(it.mods.front().match->value == doctest::Approx(30));
}

TEST_CASE("a white item is searched as a base") {
    auto gd = fixture();
    const Item it = resolved(*gd, R"(Item Class: Rings
Rarity: Normal
Two-Stone Ring
--------
Item Level: 84
--------
+25(20-30)% to Fire Resistance (implicit)
--------
Elder Item
)");
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d, std::nullopt, kFloorOnly);

    CHECK(p.strategy == Strategy::BaseItem);
    CHECK(p.category == "accessory.ring");
    CHECK(p.type == "Two-Stone Ring");
    CHECK(p.influences == std::vector<Influence>{Influence::Elder});
    REQUIRE(numeric_for(p, "ilvl") != nullptr);
    CHECK(numeric_for(p, "ilvl")->enabled);
    CHECK(numeric_for(p, "ilvl")->min == doctest::Approx(84));

    // A variable implicit is part of what the base is worth, so it is searched at "at least
    // this much" — not bounded by its tier the way a rare's mods are.
    const StatFilter* res = filter_for(p, "implicit.stat_3372524247");
    REQUIRE(res != nullptr);
    CHECK(res->enabled);
    CHECK(res->min == doctest::Approx(25));
    CHECK_FALSE(res->max.has_value());
}

TEST_CASE("a rare can be searched as its base instead, and then its rolls stop mattering") {
    auto gd = fixture();
    const Item it = resolved(*gd, R"(Item Class: Body Armours
Rarity: Rare
Doom Shroud
Vaal Regalia
--------
Energy Shield: 405 (augmented)
--------
Item Level: 86
--------
+42 to maximum Life
--------
Fractured Item
)");
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d, Strategy::BaseItem);

    CHECK(p.type == "Vaal Regalia");
    CHECK(flag_of(p, "fractured_item") == true);
    REQUIRE(numeric_for(p, "ilvl") != nullptr);
    CHECK(numeric_for(p, "ilvl")->enabled);
    const StatFilter* life = filter_for(p, "explicit.stat_3299347043");
    REQUIRE(life != nullptr);
    CHECK_FALSE(life->enabled);
}

TEST_CASE("a fractured mod still matters when the base is what is being sold") {
    auto gd = fixture();
    const Item it = resolved(*gd, R"(Item Class: Body Armours
Rarity: Rare
Doom Shroud
Vaal Regalia
--------
Energy Shield: 405 (augmented)
--------
Item Level: 86
--------
+42 to maximum Life (fractured)
+25% to Fire Resistance
--------
Fractured Item
)");
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d, Strategy::BaseItem);

    const StatFilter* life = filter_for(p, "fractured.stat_3299347043");
    REQUIRE(life != nullptr);
    CHECK(life->enabled);
    CHECK(life->min == doctest::Approx(39));
    CHECK_FALSE(filter_for(p, "explicit.stat_3372524247")->enabled);
}

TEST_CASE("a unique is its name, and only its variable rolls are filters") {
    auto gd = fixture();
    const Item it = resolved(*gd, R"(Item Class: Boots
Rarity: Unique
Abberath's Hooves
Goathide Boots
--------
Evasion Rating: 30
--------
Item Level: 60
--------
+42 to maximum Life
+25(20-30)% to Fire Resistance
)");
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d, std::nullopt, kFloorOnly);

    CHECK(p.strategy == Strategy::Unique);
    CHECK(p.name == "Abberath's Hooves");
    CHECK(p.type == "Goathide Boots");
    CHECK(it.unique_entry != nullptr);
    // The only thing left out is whether the fixed-looking life roll is fixed at all: several
    // uniques pick a modifier from a pool, and no data we have says which.
    REQUIRE(p.notes.size() == 1);
    CHECK(p.notes.front().find("pool of possibilities") != std::string::npos);

    // Every copy of the unique has the same life roll, so filtering on it only costs results.
    const StatFilter* life = filter_for(p, "explicit.stat_3299347043");
    REQUIRE(life != nullptr);
    CHECK_FALSE(life->enabled);

    // The resistance rolled inside a range, so it is what separates a good copy from a bad one.
    const StatFilter* res = filter_for(p, "explicit.stat_3372524247");
    REQUIRE(res != nullptr);
    CHECK(res->enabled);
    CHECK(res->min == doctest::Approx(25));
    CHECK_FALSE(res->max.has_value());
}

TEST_CASE("a unique's pooled modifier is searched even though it printed no range") {
    auto gd = fixture();
    // Ralakesh's Impatience rolls one of three charge modifiers, each 1..1. The clipboard
    // prints it exactly like the four modifiers every copy has, and it is the only thing
    // about the item worth searching on.
    const Item it = resolved(*gd, R"(Item Class: Boots
Rarity: Unique
Ralakesh's Impatience
Riveted Boots
--------
Armour: 65
Energy Shield: 14
--------
Item Level: 70
--------
+20% to Cold Resistance
+20% to Chaos Resistance
20% increased Movement Speed
Corrupted Blood cannot be inflicted on you
Count as having maximum number of Frenzy Charges
)");
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    CHECK(p.strategy == Strategy::Unique);
    CHECK(p.name == "Ralakesh's Impatience");

    const StatFilter* frenzy = filter_for(p, "explicit.stat_2046300872");
    REQUIRE(frenzy != nullptr);
    CHECK(frenzy->enabled);
    CHECK(frenzy->pooled);
    CHECK(frenzy->pool_hint == "Random charge modifier");

    // A modifier every copy has is still not worth filtering on — but its roll is variable,
    // so it is enabled at what it rolled, and the data supplies the range the clipboard did
    // not print.
    const StatFilter* cold = filter_for(p, "explicit.stat_4220027924");
    REQUIRE(cold != nullptr);
    CHECK_FALSE(cold->pooled);
    CHECK(cold->enabled);
    CHECK(cold->min == doctest::Approx(19));
    CHECK(cold->max == doctest::Approx(21));
    CHECK(cold->roll_min == doctest::Approx(15));
    CHECK(cold->roll_max == doctest::Approx(25));

    // Fixed on the item and fixed in its roll: nothing to search for.
    const StatFilter* blood = filter_for(p, "explicit.stat_1658498488");
    REQUIRE(blood != nullptr);
    CHECK_FALSE(blood->enabled);
    CHECK_FALSE(blood->pooled);

    // And with the data in hand there is nothing left to warn about.
    CHECK(p.notes.empty());
}

TEST_CASE("an enchant on a unique is searched, not reported as missing from its data") {
    auto gd = fixture();
    // The enchant is crafted onto this copy, so the per-unique data has nothing to say about
    // it by design — and saying "not in the modifier data" about a modifier that is sitting
    // right there in the filter list reads as a failure to recognise it.
    const Item it = resolved(*gd, capture("item_1.txt", "examples"));
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    CHECK(p.strategy == Strategy::Unique);
    CHECK(p.name == "Rumi's Concoction");

    const StatFilter* instilled = filter_for(p, "enchant.stat_3287581721");
    REQUIRE(instilled != nullptr);
    CHECK(instilled->enabled); // most of what the flask sells for
    CHECK(instilled->type == ppc::data::ModType::Enchant);

    // Fixed for the unique, variable in their roll, so both are searched at what they rolled.
    const StatFilter* block = filter_for(p, "explicit.stat_2519106214");
    REQUIRE(block != nullptr);
    CHECK(block->enabled);
    // 12 is the top of what the record says this rolls, so the window is gated there.
    CHECK(block->min == doctest::Approx(11));
    CHECK(block->max == doctest::Approx(12));
    CHECK(block->roll_min == doctest::Approx(8));
    CHECK(block->roll_max == doctest::Approx(12));
    REQUIRE(filter_for(p, "explicit.stat_215754572") != nullptr);
    CHECK(filter_for(p, "explicit.stat_215754572")->enabled);

    for (const std::string& n : p.notes)
        CHECK_MESSAGE(n.find("not in the modifier data") == std::string::npos, n);
}

TEST_CASE("a range that does not contain the roll is not this item's range") {
    auto gd = fixture();
    // 40 is outside the 15..25 the data says this modifier rolls: either a legacy copy, or a
    // record whose decimal point sits elsewhere than the clipboard's. Either way the bounds
    // describe something other than the item in hand, and calling the roll variable on them
    // would be a guess.
    const Item it = resolved(*gd, R"(Item Class: Boots
Rarity: Unique
Ralakesh's Impatience
Riveted Boots
--------
Item Level: 70
--------
+40% to Cold Resistance
)");
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    const StatFilter* cold = filter_for(p, "explicit.stat_4220027924");
    REQUIRE(cold != nullptr);
    CHECK_FALSE(cold->roll_min.has_value());
    CHECK_FALSE(cold->roll_max.has_value());
    CHECK_FALSE(cold->enabled);
}

TEST_CASE("a modifier the unique's record does not have is reported, not dropped") {
    auto gd = fixture();
    // Nothing says this one is fixed, so it cannot be left out of the search in silence:
    // it is either something added to this copy or a modifier the source has not caught up
    // with, and both are exactly what a buyer would be searching for.
    const Item it = resolved(*gd, R"(Item Class: Boots
Rarity: Unique
Ralakesh's Impatience
Riveted Boots
--------
Item Level: 70
--------
20% increased Movement Speed
+42 to maximum Life
)");
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    CHECK_FALSE(filter_for(p, "explicit.stat_3299347043")->enabled);
    REQUIRE(p.notes.size() == 1);
    CHECK(p.notes.front() ==
          "not in the modifier data for \"Ralakesh's Impatience\", so not searched: "
          "+42 to maximum Life");
}

TEST_CASE("a pool the data states but does not enumerate is named rather than implied away") {
    auto gd = fixture();
    const Item it = resolved(*gd, R"(Item Class: Jewels
Rarity: Unique
That Which Was Taken
Crimson Jewel
--------
Item Level: 70
--------
Corrupted Blood cannot be inflicted on you
)");
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    REQUIRE(p.notes.size() == 2);
    // The one modifier this copy shows is not in the record either, so both notes fire: what
    // the item has that the data does not know, and what the data knows it cannot enumerate.
    CHECK(p.notes[0].starts_with("not in the modifier data"));
    CHECK(p.notes[1] ==
          "the modifier data states but does not enumerate this, so it is not searched: "
          "4 random Charm modifiers");
}

TEST_CASE("an unidentified unique on a base with one unique is that unique") {
    auto gd = fixture();
    // Riveted Boots roll into Ralakesh's Impatience and nothing else, so the name is not a
    // guess: reading it off the base is the only thing the clipboard leaves to be worked out.
    const Item it = resolved(*gd, R"(Item Class: Boots
Rarity: Unique
Riveted Boots
--------
Evasion Rating: 30
--------
Item Level: 84
--------
Unidentified
)");
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    CHECK_FALSE(it.identified);
    CHECK_FALSE(it.needs_unique_choice());
    REQUIRE(it.unique_candidates.size() == 1);
    REQUIRE(it.unique_entry != nullptr);
    CHECK(p.name == "Ralakesh's Impatience");
    CHECK(p.type == "Riveted Boots");
    CHECK(p.rarity == "unique");
    // The flag the item states about itself is asked for, and offered as a row: an unidentified
    // copy is a different product from the identified ones beside it.
    CHECK(flag_of(p, "identified") == std::optional<bool>(false));
    REQUIRE(p.option("identified") != nullptr);
    CHECK(p.option("identified")->shown);
    // The item level is the one number an unidentified copy carries, and it bounds what the
    // item can still turn out to have rolled — a floor, ticked.
    REQUIRE(numeric_for(p, "ilvl") != nullptr);
    CHECK(numeric_for(p, "ilvl")->enabled);
    CHECK(numeric_for(p, "ilvl")->min == doctest::Approx(84));
    CHECK_FALSE(numeric_for(p, "ilvl")->max.has_value());
    // Said out loud: nothing on the item printed this name.
    CHECK(std::any_of(p.notes.begin(), p.notes.end(), [](const std::string& n) {
        return n.find("only unique that drops") != std::string::npos;
    }));
}

TEST_CASE("an unidentified unique on a base with several is a question, not a search") {
    auto gd = fixture();
    // Goathide Gloves roll into both Hrimsorrow and Hrimburn, which share nothing but the base.
    const Item it = resolved(*gd, capture("unique-unidentified-gloves.txt"));
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    CHECK(it.unique_candidates.size() == 2);
    CHECK(it.needs_unique_choice());
    CHECK(it.unique_entry == nullptr);
    // No name to ask for, and the plan says which question is unanswered rather than searching
    // the base among every unique that drops on it.
    CHECK(p.name.empty());
    CHECK(p.type == "Goathide Gloves");
    CHECK(std::any_of(p.notes.begin(), p.notes.end(), [](const std::string& n) {
        return n.find("2 uniques drop on") != std::string::npos;
    }));

    SUBCASE("choosing one searches for it, still unidentified") {
        Item chosen = it;
        choose_unique(chosen, chosen.unique_candidates[1]);
        const SearchPlan cp = build_plan(*gd, chosen, d);
        CHECK_FALSE(chosen.needs_unique_choice());
        CHECK(cp.name == chosen.unique_candidates[1]->name);
        CHECK(flag_of(cp, "identified") == std::optional<bool>(false));
        CHECK(numeric_for(cp, "ilvl")->min == doctest::Approx(84));
    }

    SUBCASE("nothing outside the candidates can be chosen") {
        Item chosen = it;
        // A record from the previous item, or from a bundle the updater has swapped out.
        choose_unique(chosen, gd->find_bases(ppc::data::Namespace::Unique, "Hrimsorrow").front());
        CHECK(chosen.unique_entry != nullptr); // that one *is* a candidate
        choose_unique(chosen, gd->find_bases(ppc::data::Namespace::Unique,
                                             "Ralakesh's Impatience").front());
        CHECK(chosen.unique_entry->name == "Hrimsorrow"); // refused, and the choice stands
        choose_unique(chosen, nullptr);
        CHECK(chosen.unique_entry == nullptr);
    }
}

TEST_CASE("an identified unique is not read off its base") {
    auto gd = fixture();
    // The candidate list is only ever about the gap an unidentified item leaves: an identified
    // one names itself, and offering it a choice between the base's uniques would be a way to
    // search for the wrong one.
    const Item it = resolved(*gd, R"(Item Class: Gloves
Rarity: Unique
Hrimsorrow
Goathide Gloves
--------
Item Level: 84
)");
    CHECK(it.unique_candidates.empty());
    CHECK_FALSE(it.needs_unique_choice());
    REQUIRE(it.unique_entry != nullptr);
    CHECK(it.unique_entry->name == "Hrimsorrow");
    // And no item-level filter: which copy of an identified unique this is has nothing to do
    // with what it dropped at.
    const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
    CHECK(numeric_for(p, "ilvl") == nullptr);
}

TEST_CASE("a magic item's base is found under its affixes") {
    auto gd = fixture();
    CHECK(strip_magic_affixes(*gd, "Surgeon's Two-Stone Ring of the Cheetah", "Rings") ==
          "Two-Stone Ring");
    // Nothing in the bundle matches, and inventing a base is worse than admitting it.
    CHECK(strip_magic_affixes(*gd, "Surgeon's Sapphire Flask of Heat", "Utility Flasks").empty());

    const Item it = resolved(*gd, R"(Item Class: Rings
Rarity: Magic
Surgeon's Two-Stone Ring of the Cheetah
--------
Item Level: 84
--------
+42 to maximum Life
)");
    CHECK(it.base_type == "Surgeon's Two-Stone Ring of the Cheetah");
    CHECK(it.base_name == "Two-Stone Ring");
    CHECK(it.base != nullptr);
}

TEST_CASE("a magic flask searches its affixes and says nothing else") {
    auto gd = fixture();
    const Item it = resolved(*gd, capture("item_12.txt", "examples"));
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d, std::nullopt, kWholeTier);

    CHECK(p.strategy == Strategy::Modifiers);
    CHECK(p.category == "flask");
    // Unlike every other rare or magic item, a flask names its base: trade has one category
    // for all of them, and the same suffix is worth one thing on a Silver Flask and nothing
    // on a Ruby one.
    CHECK(p.type == "Silver Flask");

    // The Surgeon's prefix. Its wording used to reach two stat records — the game renders this
    // stat both as a chance and as the 100% "Gain a Flask Charge…", and trade hashes each — so
    // the matcher refused to guess and the prefix went unsearched on every crit-charge flask.
    const StatFilter* surgeons = filter_for(p, "explicit.stat_3738001379");
    REQUIRE(surgeons != nullptr);
    CHECK(surgeons->enabled);
    CHECK(surgeons->min == doctest::Approx(31));
    CHECK(surgeons->max == doctest::Approx(35));

    // The suffix is stored against the canonical "increased" wording: the roll it printed is
    // "reduced", and only the sign carries that.
    const StatFilter* owl = filter_for(p, "explicit.stat_4265534424");
    REQUIRE(owl != nullptr);
    CHECK(owl->min == doctest::Approx(-65));
    CHECK(owl->max == doctest::Approx(-60));

    REQUIRE(filter_for(p, "enchant.stat_3287581721") != nullptr);
    // The flask's own effect is properties, so there is nothing left to warn about. Before the
    // parser knew that, "Lasts 6 Seconds" and three more lines each came back an unrecognised mod.
    CHECK(p.notes.empty());
}

TEST_CASE("a flask whose base the bundle does not know says so instead of searching a name") {
    auto gd = fixture();
    // Quicksilver Flask is not in the fixture bundle, so the affixes are never stripped and
    // `base_name` is still the whole magic name. Sending that as the trade type would match
    // nothing at all, which reads as "no such item is for sale".
    const Item it = resolved(*gd, capture("item_2.txt", "examples"));
    const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

    CHECK(p.strategy == Strategy::Modifiers);
    CHECK(p.type.empty());
    bool said = false;
    for (const std::string& n : p.notes)
        if (n.find("cannot name the flask type") != std::string::npos) said = true;
    CHECK(said);
}

TEST_CASE("a rare flask names its base too — that is where its modifiers are worth something") {
    auto gd = fixture();
    const Item it = resolved(*gd, R"(Item Class: Utility Flasks
Rarity: Rare
Cataclysm Cure
Granite Flask
--------
Lasts 4.50 Seconds
Consumes 30 of 60 Charges on use
Currently has 0 Charges
+1500 to Armour
--------
Item Level: 84
--------
35% chance to gain a Flask Charge when you deal a Critical Strike
)");
    const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

    CHECK(p.strategy == Strategy::Modifiers);
    CHECK(p.type == "Granite Flask");
    // The rare's own name is randomly generated, so it is never part of the search.
    CHECK(p.name.empty());
}

TEST_CASE("a map item is a bulk good or an item, and the item level is what says which") {
    // No item level: every copy is identical, there is nothing to filter on, and it changes
    // hands on the in-game currency exchange rather than through a listing.
    for (const char* f : {"fragment-scarab.txt", "fragment-allflame-ember.txt",
                          "fragment-phoenix.txt", "fragment-mavens-writ.txt"}) {
        std::optional<Item> it = parse_item(capture(f));
        REQUIRE_MESSAGE(it.has_value(), f);
        CHECK_MESSAGE(!it->item_level.has_value(), f);
        CHECK_MESSAGE(default_strategy(*it) == Strategy::Currency, f);
    }

    // One that prints an item level can also carry a rarity and its own quantity/rarity
    // modifiers, exactly as a map does, so it goes down the ordinary path for its rarity.
    const std::optional<Item> inv = parse_item(capture("invitation-writhing.txt"));
    REQUIRE(inv.has_value());
    CHECK(inv->item_level == 83);
    CHECK(default_strategy(*inv) == Strategy::BaseItem);

    // Same class, same absence of a rarity worth the name — a rare one is priced on what it
    // rolled, which is the whole reason the item level is the split.
    Item rare = *inv;
    rare.rarity = Rarity::Rare;
    CHECK(default_strategy(rare) == Strategy::Modifiers);
    rare.item_level.reset();
    CHECK(default_strategy(rare) == Strategy::Currency);
}

TEST_CASE("what the in-game exchange trades in bulk has to resolve to a base to be found") {
    auto gd = fixture();
    // The exchange states every item by its metadata path and carries no names at all, so the
    // *only* way an item is looked up there is through the base record `resolve_base` found.
    // An essence already went down the ordinary path; a divination card is a namespace of its
    // own and used to fall through it, so a card had no base, no metadata id and no price.
    for (const char* f : {"currency-essence.txt", "card-blazing-fire.txt"}) {
        const Item it = resolved(*gd, capture(f));
        REQUIRE_MESSAGE(it.base != nullptr, f);
        CHECK_MESSAGE(!it.base->metadata_id.empty(), f);
    }

    const Item card = resolved(*gd, capture("card-blazing-fire.txt"));
    CHECK(card.rarity == Rarity::DivinationCard);
    CHECK(card.base->metadata_id == "Metadata/Items/DivinationCards/DivinationCardTheBlazingFire");
    // Still not something a stat query can ask about: the exchange is the whole answer.
    const SearchPlan p = build_plan(*gd, card, derive(gd.get(), card));
    CHECK(p.strategy == Strategy::Currency);
    CHECK(p.category == "card");
}

TEST_CASE("the bundle says which items trade on the exchange, whatever the hour did") {
    auto gd = fixture();
    // The hourly feed can only say whether one traded in the last hour, and for a thin item
    // (a Weeping Essence of Greed) an hour with no trade is the normal case rather than an
    // answer. This flag is the standing fact underneath it, and it is what the panel keys the
    // "traded here, no trades in the past hour" line and the absence of a search off.
    REQUIRE(gd->has_exchange_flags());
    for (const char* f : {"currency-essence.txt", "card-blazing-fire.txt"}) {
        const Item it = resolved(*gd, capture(f));
        REQUIRE_MESSAGE(it.base != nullptr, f);
        CHECK_MESSAGE(it.base->exchange, f);
    }
    // And a rolled item is not sold there at all, so the flag is a discriminator rather than
    // something every record happens to carry.
    const Item boots = resolved(*gd, capture("item_1.txt", "examples"));
    REQUIRE(boots.base != nullptr);
    CHECK_FALSE(boots.base->exchange);
}

TEST_CASE("a map is priced on where it goes and what was spent on it, never on its affixes") {
    auto gd = fixture();

    SUBCASE("a corrupted tier-16 rare") {
        const Item it = resolved(*gd, capture("map-rare-t16-corrupted.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        CHECK(p.strategy == Strategy::Map);
        CHECK(p.category == "map");
        CHECK(p.rarity == "nonunique");
        // Every ordinary map shares one base, so the type says almost nothing and the tier
        // says the rest. Exact on both ends: a tier-16 map is a different area, not a better one.
        CHECK(p.type == "Map");
        CHECK(p.discriminator == "map");
        const NumericFilter* tier = numeric_for(p, "map_tier");
        REQUIRE(tier != nullptr);
        CHECK(tier->enabled);
        CHECK(tier->min == 16);
        CHECK(tier->max == 16);

        // Quantity and pack size are what the map is run for; rarity is a preference, and
        // imposing it would drop the cheaper copies of the same map.
        REQUIRE(numeric_for(p, "map_iiq") != nullptr);
        CHECK(numeric_for(p, "map_iiq")->enabled);
        CHECK(numeric_for(p, "map_iiq")->min == 104);
        REQUIRE(numeric_for(p, "map_packsize") != nullptr);
        CHECK(numeric_for(p, "map_packsize")->enabled);
        REQUIRE(numeric_for(p, "map_iir") != nullptr);
        CHECK_FALSE(numeric_for(p, "map_iir")->enabled);

        // Four prefixes and four suffixes: eight, which only corruption allows and which is
        // most of what this map is worth. Trade indexes it as a total, not as the affixes.
        const StatFilter* count = filter_for(p, "pseudo.pseudo_number_of_affix_mods");
        REQUIRE(count != nullptr);
        CHECK(count->enabled);
        CHECK(count->min == 8);
        CHECK_FALSE(count->mod_index.has_value());

        // Not one of those eight is a filter, and not one is a note either: they are left out
        // deliberately, and "unrecognised modifier" would charge the check with failing at it.
        for (const StatFilter& f : p.stats)
            CHECK(f.type != ppc::data::ModType::Explicit);
        CHECK(p.notes.empty());
    }

    SUBCASE("a map with no implicit is its tier and its affix count, and nothing else") {
        const Item it = resolved(*gd, capture("map-rare-8mod-corrupted.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        REQUIRE(p.stats.size() == 1);
        CHECK(p.stats[0].id == "pseudo.pseudo_number_of_affix_mods");
        CHECK(p.stats[0].min == 8);
        CHECK(p.notes.empty());
    }

    SUBCASE("a fixed number is not a bound, only the modifier's presence is") {
        // The number in "…drops by 20% of its value" says 20 on every Baran map, and the one
        // behind "Area is influenced by The Elder" is not in the clipboard at all — it is the
        // constant the matcher substitutes for the influence. Neither is what trade indexes
        // the stat on: asking for them returned 0 listings against 1705 and 10000.
        const Item it = resolved(*gd, capture("map-rare-t16-corrupted.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        const StatFilter* baran = filter_for(p, "implicit.stat_2563183002|1");
        REQUIRE(baran != nullptr);
        CHECK(baran->enabled); // the filter stays; only its number goes
        CHECK_FALSE(baran->min.has_value());
        CHECK_FALSE(baran->max.has_value());
    }

    SUBCASE("an uncorrupted map is not searched on its affix count") {
        // Six is what every rare map has, so filtering on it would drop the identical ones.
        const Item it = resolved(*gd, capture("map-rare-guardian.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(filter_for(p, "pseudo.pseudo_number_of_affix_mods") == nullptr);
        // No tier printed, so the base's own name is the whole of what says which area it is.
        CHECK(p.type == "Shaper Guardian Map");
        CHECK(numeric_for(p, "map_tier") == nullptr);
        // The implicit is the one modifier a currency cannot re-roll, so it is searched on.
        const StatFilter* shaper = filter_for(p, "implicit.stat_1792283443|1");
        REQUIRE(shaper != nullptr);
        CHECK(shaper->enabled);
    }

    SUBCASE("the drop bonuses a chisel adds are pseudo stats, not properties trade knows") {
        const Item it = resolved(*gd, capture("map-rare-more-drops.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        for (const auto& [id, value] : {std::pair{"pseudo.pseudo_map_more_map_drops", 70.0},
                                        std::pair{"pseudo.pseudo_map_more_scarab_drops", 53.0}}) {
            const StatFilter* f = filter_for(p, id);
            REQUIRE_MESSAGE(f != nullptr, id);
            CHECK(f->enabled);
            CHECK(f->min == value);
            CHECK_FALSE(f->mod_index.has_value());
        }
        // The two the map does not have are not asked for at all.
        CHECK(filter_for(p, "pseudo.pseudo_map_more_currency_drops") == nullptr);
        CHECK(filter_for(p, "pseudo.pseudo_map_more_card_drops") == nullptr);
    }

    SUBCASE("a unique map is its name and its tier") {
        const Item it = resolved(*gd, capture("map-unique-olmecs.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.strategy == Strategy::Map);
        CHECK(p.rarity == "unique");
        CHECK(p.name == "Olmec's Sanctum");
        CHECK(p.type == "Map");
        CHECK(p.discriminator == "map");
        REQUIRE(numeric_for(p, "map_tier") != nullptr);
        CHECK(numeric_for(p, "map_tier")->min == 16);
        // Its own modifiers are the same on every copy; the name already says them.
        CHECK(p.stats.empty());
    }

    SUBCASE("a white map is its tier and nothing else") {
        const Item it = resolved(*gd, capture("map-normal-t4.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.strategy == Strategy::Map);
        CHECK(p.stats.empty());
        REQUIRE(numeric_for(p, "map_tier") != nullptr);
        CHECK(numeric_for(p, "map_tier")->min == 4);
        CHECK(numeric_for(p, "map_iiq") == nullptr);
    }

    SUBCASE("without Advanced Mod Descriptions there is no affix count to give") {
        Item it = resolved(*gd, capture("map-rare-t16-corrupted.txt"));
        for (Modifier& m : it.mods) m.affix = Affix::Unknown;
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(filter_for(p, "pseudo.pseudo_number_of_affix_mods") == nullptr);
        CHECK(p.notes.size() == 1); // said, rather than silently counted as zero
    }
}

TEST_CASE("blight is a filter on the ordinary map base, not a type of its own") {
    auto gd = fixture();
    const Item it = resolved(*gd, capture("map-blighted.txt"));
    const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

    // The clipboard's "Blighted Map" is not a base in any bundle and not a type the trade site
    // matches anything under — measured, 0 listings against 1398 for the Map base plus the
    // flag. So the base resolves to the one every map shares and the flag says which it is.
    CHECK(it.blighted);
    CHECK_FALSE(it.blight_ravaged);
    CHECK(it.base_type == "Blighted Map"); // what was printed, which is what the panel draws
    CHECK(it.base_name == "Map");
    CHECK(p.type == "Map");
    CHECK(p.discriminator == "map");
    CHECK(flag_of(p, "map_blighted") == true);
    CHECK(p.option("map_uberblighted") == nullptr);

    REQUIRE(numeric_for(p, "map_tier") != nullptr);
    CHECK(numeric_for(p, "map_tier")->min == 12);
    CHECK(numeric_for(p, "map_tier")->max == 12);
    // Its implicit is searched like any map's, and neither half of it goes unrecognised.
    CHECK(p.stats.size() == 2);
    CHECK(p.notes.empty());
}

TEST_CASE("a Valdo map is bought for its reward and for whether dying in it voids you") {
    auto gd = fixture();
    const std::string text = capture("map-valdo.txt");

    SUBCASE("the reward is the unique's own name, not the foil the game prints") {
        const Item it = resolved(*gd, text);
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.strategy == Strategy::Map);
        CHECK(p.type == "Valdo Map");
        // "Foil Hrimsorrow" is rejected by the site outright — the option is over the unique
        // list, and an unknown one fails the whole search rather than widening it.
        CHECK(p.option("map_completion_reward")->option == "Hrimsorrow");

        // This copy voids, so the search asks for that modifier and for nothing else.
        REQUIRE(p.stats.size() == 1);
        CHECK(p.stats[0].id == "explicit.stat_1095765106");
        CHECK(p.stats[0].enabled);
        CHECK_FALSE(p.stats[0].negated);
        CHECK(p.stats[0].mod_index.has_value());

        // The quantity and pack size come from unique modifiers rather than from a roll, so
        // they say nothing about which Valdo map a buyer wants: offered, never imposed.
        REQUIRE(numeric_for(p, "map_iiq") != nullptr);
        CHECK_FALSE(numeric_for(p, "map_iiq")->enabled);
        CHECK_FALSE(numeric_for(p, "map_packsize")->enabled);
        CHECK(p.notes.empty());
    }

    SUBCASE("a map that does not void asks for the absence, not for nothing") {
        // Leaving it open would price the two kinds together, and they are different items.
        static constexpr std::string_view kVoidBlock =
            "{ Unique Modifier }\n"
            "Players who Die in area are sent to the Void\n"
            "(Characters sent to Void Leagues are no longer playable, and cannot be restored "
            "for any reason)\n";
        std::string safe = text;
        const size_t at = safe.find(kVoidBlock);
        REQUIRE(at != std::string::npos);
        safe.erase(at, kVoidBlock.size());

        const Item it = resolved(*gd, safe);
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        REQUIRE(p.stats.size() == 1);
        CHECK(p.stats[0].id == "explicit.stat_1095765106");
        CHECK(p.stats[0].enabled);
        CHECK(p.stats[0].negated);
        CHECK_FALSE(p.stats[0].mod_index.has_value());
    }

    SUBCASE("a reward the bundle cannot name is said, never guessed at") {
        static constexpr std::string_view kReward = "Reward: Foil Hrimsorrow";
        std::string unknown = text;
        const size_t at = unknown.find(kReward);
        REQUIRE(at != std::string::npos);
        unknown.replace(at, kReward.size(), "Reward: Foil Nothingsorrow");

        const Item it = resolved(*gd, unknown);
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.option("map_completion_reward") == nullptr);
        REQUIRE(p.notes.size() == 1);
        CHECK(p.notes[0].find("Foil Nothingsorrow") != std::string::npos);
    }
}

TEST_CASE("a number with no roll range behind it is not a bound") {
    auto gd = fixture();

    SUBCASE("with Advanced Mod Descriptions on, no range means the modifier does not roll") {
        // The suffix prints its range and the enchant does not, on the same item — so the
        // absence on the enchant is the game saying that one is the same on every copy.
        const Item it = resolved(*gd, R"(Item Class: Utility Flasks
Rarity: Magic
Granite Flask of the Sky
--------
Lasts 4.50 Seconds
Consumes 30 of 60 Charges on use
--------
Item Level: 84
--------
{ Enchant Modifier }
Used when Charges reach full
--------
{ Suffix Modifier "of the Sky" (Tier: 1) — Elemental, Cold, Resistance }
+47(46-48)% to Cold Resistance
)");
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it), std::nullopt, kWholeTier);
        const StatFilter* enchant = filter_for(p, "enchant.stat_3287581721");
        REQUIRE(enchant != nullptr);
        CHECK(enchant->enabled);
        CHECK_FALSE(enchant->min.has_value());
        const StatFilter* cold = filter_for(p, "explicit.stat_4220027924");
        REQUIRE(cold != nullptr);
        CHECK(cold->min == doctest::Approx(46));
        CHECK(cold->max == doctest::Approx(48));
    }

    SUBCASE("a tier is itself a range, printed or not") {
        // A different tier is a different number, so "no worse than what this one gave" is a
        // real question even where the tier holds one value. Same for a rank, and for the
        // qualifier an eldritch implicit prints in place of a range it has no way to state.
        const Item it = resolved(*gd, R"(Item Class: Body Armours
Rarity: Rare
Doom Shroud
Vaal Regalia
--------
Energy Shield: 200
--------
Item Level: 84
--------
{ Prefix Modifier "Urchin's" (Tier: 3) — Life }
+42 to maximum Life
{ Suffix Modifier "of the Sky" (Tier: 1) — Elemental, Cold, Resistance }
+47(46-48)% to Cold Resistance
)");
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it), std::nullopt, kFloorOnly);
        const StatFilter* life = filter_for(p, "explicit.stat_3299347043");
        REQUIRE(life != nullptr);
        CHECK(life->min == doctest::Approx(42));
    }

    SUBCASE("with them off, no range means nothing and every roll keeps its floor") {
        // Nothing on the item prints a range, so their absence is evidence of the setting
        // rather than of a fixed modifier — and stripping the bounds here would search a rare
        // for "has a life modifier".
        const Item it = resolved(*gd, kRareChest);
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it), std::nullopt, kFloorOnly);
        const StatFilter* life = filter_for(p, "explicit.stat_3299347043");
        REQUIRE(life != nullptr);
        CHECK(life->min == doctest::Approx(42));
        CHECK_FALSE(life->tiered);
    }
}

TEST_CASE("a gem is searched as its name, its level and its quality, and nothing else") {
    auto gd = fixture();

    SUBCASE("an ordinary support gem") {
        const Item it = resolved(*gd, capture("gem-support-empower.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        CHECK(p.strategy == Strategy::Gem);
        CHECK(p.category == "gem.supportgem");
        CHECK(p.rarity == "nonunique");
        CHECK(p.type == "Empower Support");
        CHECK(p.discriminator.empty());
        CHECK(flag_of(p, "corrupted") == false);

        // Exact on both ends, the same reasoning as a map's tier: a level 3 Empower is not a
        // better level 2 one, it is what the gem sells as. A floor would show its price here.
        const NumericFilter* level = numeric_for(p, "gem_level");
        REQUIRE(level != nullptr);
        CHECK(level->enabled);
        CHECK(level->min == 2);
        CHECK(level->max == 2);
        // At zero as readily as at twenty: an unquality gem is a different thing from a 20%
        // one, and no filter at all would price it as whichever quality is cheapest.
        const NumericFilter* quality = numeric_for(p, "quality");
        REQUIRE(quality != nullptr);
        CHECK(quality->enabled);
        CHECK(quality->min == 0);
        CHECK(quality->max == 0);

        // What the skill does is on every copy of it. There is nothing here to filter on and
        // nothing to warn about either.
        CHECK(p.stats.empty());
        CHECK(p.notes.empty());
    }

    SUBCASE("quality is the gem's own, and the level is not the requirement under it") {
        const Item it = resolved(*gd, capture("gem-tornado-shot.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.category == "gem.activegem");
        REQUIRE(numeric_for(p, "gem_level") != nullptr);
        CHECK(numeric_for(p, "gem_level")->min == 1); // not the level 28 to socket it
        REQUIRE(numeric_for(p, "quality") != nullptr);
        CHECK(numeric_for(p, "quality")->min == 7);
    }

    SUBCASE("a Vaal gem is searched as its Vaal skill, which its name line never prints") {
        const Item it = resolved(*gd, capture("gem-vaal-blight.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        CHECK(p.type == "Vaal Blight"); // the header said "Blight", which is another gem
        CHECK(p.discriminator.empty());
        CHECK(flag_of(p, "corrupted") == true); // what lets it reach level 21 and 23% quality at all
        CHECK(numeric_for(p, "gem_level")->min == 1);
        CHECK(p.notes.empty());
    }

    SUBCASE("a transfigured gem is searched under the skill it alters, plus a discriminator") {
        const Item it = resolved(*gd, capture("gem-transfigured-raise-zombie.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        // The name the clipboard prints is not a type trade answers to: "Raise Zombie of
        // Falling" is `Raise Zombie` with `alt_y`, and the plain type matches only the
        // unaltered gem — a different, far cheaper item rather than an empty market.
        CHECK(p.type == "Raise Zombie");
        CHECK(p.discriminator == "alt_y");
        CHECK(p.notes.empty());
    }

    SUBCASE("a gem the bundle cannot name is not searched for something else") {
        Item it = resolved(*gd, capture("gem-support-empower.txt"));
        it.base = nullptr; // a bundle published before the gem existed
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        CHECK(p.type.empty());
        CHECK(p.numerics.empty());
        REQUIRE(p.notes.size() == 1);
        CHECK(p.notes.front().find("Empower Support") != std::string::npos);
    }
}

TEST_CASE("the misc_filters booleans ask the item to be what it is, and only say so when that "
          "is not the ordinary answer") {
    const std::shared_ptr<GameData> gd = fixture();

    SUBCASE("an ordinary rare imposes all three without spending a row on any") {
        const Item it = resolved(*gd, kRareChest);
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        for (const char* key : {"corrupted", "mirrored", "identified"}) {
            const OptionFilter* f = p.option(key);
            REQUIRE_MESSAGE(f != nullptr, key);
            CHECK(f->enabled);
            CHECK_FALSE(f->shown);
        }
        CHECK(flag_of(p, "corrupted") == false);
        CHECK(flag_of(p, "mirrored") == false);
        // It is identified, so that is what the search asks for.
        CHECK(flag_of(p, "identified") == true);
    }

    SUBCASE("an unidentified item asks for that, and offers the row") {
        const Item it = resolved(*gd, R"(Item Class: Body Armours
Rarity: Rare
Doom Shroud
Vaal Regalia
--------
Energy Shield: 200
--------
Item Level: 84
--------
Unidentified
)");
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        const OptionFilter* f = p.option("identified");
        REQUIRE(f != nullptr);
        CHECK(f->option == "false");
        CHECK(f->enabled);
        CHECK(f->shown); // an unidentified copy is a different product; the buyer may widen it
    }

    SUBCASE("a mirrored item is the only one that offers the mirrored row") {
        Item it = resolved(*gd, kRareChest);
        it.mirrored = true;
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        const OptionFilter* f = p.option("mirrored");
        REQUIRE(f != nullptr);
        CHECK(f->option == "true");
        CHECK(f->enabled);
        CHECK(f->shown);
    }

    SUBCASE("a gem is never asked to be identified") {
        const Item it = resolved(*gd, capture("gem-support-empower.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        // Measured, not assumed: `identified: true` under `category: gem` returns 0 listings
        // against 10000 without it, because trade indexes the flag only for what can be
        // unidentified. A filter matching nothing reads as a gem nobody is selling.
        CHECK(p.option("identified") == nullptr);
        CHECK(p.option("mirrored") != nullptr); // this one is safe everywhere, and was checked
    }
}

TEST_CASE("the three misc properties are filtered on the side that makes them better") {
    const std::shared_ptr<GameData> gd = fixture();

    SUBCASE("memory strands are a floor, ticked") {
        const Item it = resolved(*gd, capture("memory-strands-boots.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        const NumericFilter* f = numeric_for(p, "memory_level");
        REQUIRE(f != nullptr);
        CHECK(f->enabled);
        CHECK(f->min == 43);
        CHECK_FALSE(f->max.has_value()); // more of them is more of what is being bought
    }

    SUBCASE("intangibility is a ceiling, unticked") {
        // Captured from a listing rather than from the game, which is why the property label
        // carries the site's keyword-link markup — the parser is what drops it.
        const Item it = resolved(*gd, capture("listing-intangibility-ring.txt"));
        REQUIRE_FALSE(it.properties.empty());
        CHECK(it.properties.front().label == "Intangibility");

        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        const NumericFilter* f = numeric_for(p, "intangibility");
        REQUIRE(f != nullptr);
        CHECK_FALSE(f->enabled); // a buyer who will not craft on it does not care
        CHECK_FALSE(f->min.has_value());
        CHECK(f->max == 8); // it is the penalty accrued from crafting: less is better
    }

    SUBCASE("a Facetor's Lens is the one currency item a search can tell apart") {
        const Item it = resolved(*gd, capture("currency-facetors-lens.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        CHECK(p.strategy == Strategy::Currency); // still priced by poe.ninja as well
        CHECK(p.type == "Facetor's Lens");       // …but named, which is what makes it searchable
        const NumericFilter* f = numeric_for(p, "stored_experience");
        REQUIRE(f != nullptr);
        CHECK(f->enabled);
        CHECK(f->min == 42420246);
        // Nothing about a lens can be unidentified, and trade does not index the flag for one.
        CHECK(p.option("identified") == nullptr);
    }

    SUBCASE("a currency item with nothing to tell two copies apart is still not searched") {
        const Item it = resolved(*gd, capture("currency-chaos-stack.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.type.empty());
        CHECK(numeric_for(p, "stored_experience") == nullptr);
    }
}

TEST_CASE("a chart is a map: the area it covers, its shape, and its voyage modifier") {
    const std::shared_ptr<GameData> gd = fixture();

    SUBCASE("the area is the type, and none of the danger is searched") {
        const Item it = resolved(*gd, capture("chart-rare-seafloor-ridges.txt"));
        REQUIRE(it.is_chart());
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        CHECK(p.strategy == Strategy::Map); // a chart shares it rather than getting one of its own
        CHECK(p.category == "chart");
        CHECK(p.rarity == "nonunique");
        // Trade files a chart under the area, by its internal id and with a discriminator —
        // "Seafloor Ridges" is not a type the site answers to.
        CHECK(p.type == "SeafloorRidges");
        CHECK(p.discriminator == "chart");

        // Exact, the same reasoning as a map's tier: a level 83 area is a different area.
        const NumericFilter* lvl = numeric_for(p, "area_level");
        REQUIRE(lvl != nullptr);
        CHECK(lvl->enabled);
        CHECK(lvl->min == 83);
        CHECK(lvl->max == 83);

        const OptionFilter* shape = p.option("chart_shape");
        REQUIRE(shape != nullptr);
        // The site takes the shape's number and answers "Invalid chart shape" to its own text.
        CHECK(shape->option == "1");
        CHECK(shape->display == "End");
        CHECK(shape->enabled);
        CHECK(shape->shown);

        // The promise of a voyage modifier is itself the searchable implicit.
        const StatFilter* voyage = filter_saying(p, "Voyage Modifier");
        REQUIRE(voyage != nullptr);
        CHECK(voyage->enabled);
        CHECK(voyage->type == ppc::data::ModType::Implicit);

        // The affixes are the danger a buyer is choosing among, not the thing bought, so they
        // are left out exactly as a map's are — and left out silently, in front of the reader.
        CHECK(filter_saying(p, "Monster Life") == nullptr);
        CHECK(p.notes.empty());

        // Quantity and pack size ride the map path unchanged; rarity is offered, not imposed.
        REQUIRE(numeric_for(p, "map_iiq") != nullptr);
        CHECK(numeric_for(p, "map_iiq")->enabled);
        CHECK(numeric_for(p, "map_packsize")->enabled);
        CHECK_FALSE(numeric_for(p, "map_iir")->enabled);
        CHECK(numeric_for(p, "map_tier") == nullptr); // a chart prints none
    }

    SUBCASE("the league's own currency is asked for like quantity, not like rarity") {
        const Item it = resolved(*gd, capture("listing-chart-sulphur.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        const NumericFilter* s = numeric_for(p, "chart_sulphur");
        REQUIRE(s != nullptr);
        CHECK(s->enabled);
        CHECK(s->min == 45);
        CHECK_FALSE(s->max.has_value());
    }

    SUBCASE("an unidentified chart searches for that, and still for its area and shape") {
        const Item it = resolved(*gd, capture("listing-chart-unidentified.txt"));
        CHECK_FALSE(it.identified);
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        CHECK(p.type == "SeafloorRidges");
        CHECK(p.option("chart_shape")->option == "1");
        CHECK(flag_of(p, "identified") == false);
        CHECK(p.option("identified")->shown);
    }

    SUBCASE("an area the bundle cannot name falls back to the chart's own base type") {
        Item it = resolved(*gd, capture("chart-rare-seafloor-ridges.txt"));
        it.type_line = "Trench Of Nowhere"; // a bundle published before the area existed
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        CHECK(p.type == "Coral Reef Chart");
        CHECK(p.discriminator.empty());
        REQUIRE_FALSE(p.notes.empty());
        CHECK(p.notes.front().find("Trench Of Nowhere") != std::string::npos);
    }
}
