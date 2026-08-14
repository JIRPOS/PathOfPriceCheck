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
#include "parse_en.hpp"

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
    std::optional<Item> it = parse_item_en(text);
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

/// The filters the list shows without being expanded — everything the strategy did not put
/// behind `StatFilter::hidden`. What a case means by "the search is these and nothing else".
std::vector<const StatFilter*> shown_stats(const SearchPlan& p) {
    std::vector<const StatFilter*> out;
    for (const StatFilter& f : p.stats)
        if (!f.hidden) out.push_back(&f);
    return out;
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

/// `kRareChest` with a socket block, which the game prints between the requirements and the item
/// level. The captures in the fixtures are all three-socket or fewer, and what is under test is a
/// rule about the count rather than anything else on the item.
std::string chest_with_sockets(std::string_view sockets) {
    std::string text(kRareChest);
    const size_t at = text.find("Item Level:");
    REQUIRE(at != std::string::npos);
    text.insert(at, "Sockets: " + std::string(sockets) + "\n--------\n");
    return text;
}

} // namespace

TEST_CASE("sockets and links are searched when they are worth something, offered when they are not") {
    auto gd = fixture();
    const auto plan_for = [&gd](std::string_view sockets) {
        const Item it = resolved(*gd, chest_with_sockets(sockets));
        const Derived d = derive(gd.get(), it);
        return build_plan(*gd, it, d);
    };

    SUBCASE("a six-link is most of what the item is worth, so it is asked for") {
        const SearchPlan p = plan_for("B-B-B-B-B-B");
        const NumericFilter* sockets = numeric_for(p, "sockets");
        const NumericFilter* links = numeric_for(p, "links");
        REQUIRE(sockets != nullptr);
        REQUIRE(links != nullptr);
        CHECK(sockets->enabled);
        CHECK_FALSE(sockets->hidden);
        CHECK(links->enabled);
        CHECK_FALSE(links->hidden);
        // A floor and no ceiling: a buyer shopping for a five-link takes a six-link.
        CHECK(sockets->min == doctest::Approx(6));
        CHECK_FALSE(sockets->max.has_value());
        CHECK(links->min == doctest::Approx(6));
    }

    SUBCASE("six sockets in two groups is a six-socket item and a three-link one") {
        const SearchPlan p = plan_for("B-B-B G-G-G");
        REQUIRE(numeric_for(p, "sockets") != nullptr);
        REQUIRE(numeric_for(p, "links") != nullptr);
        CHECK(numeric_for(p, "sockets")->min == doctest::Approx(6));
        CHECK(numeric_for(p, "sockets")->enabled);
        // The links are the ordinary case even though the socket count is not, so the two rows
        // part company — which is the whole reason they are two filters.
        CHECK(numeric_for(p, "links")->min == doctest::Approx(3));
        CHECK_FALSE(numeric_for(p, "links")->enabled);
        CHECK(numeric_for(p, "links")->hidden);
    }

    SUBCASE("at four or fewer both are offered rather than imposed") {
        const SearchPlan p = plan_for("B-G-R B");
        for (const char* key : {"sockets", "links"}) {
            const NumericFilter* f = numeric_for(p, key);
            REQUIRE_MESSAGE(f != nullptr, key);
            CHECK_MESSAGE(f->hidden, key);
            CHECK_MESSAGE(!f->enabled, key);
        }
        // Still seeded with what the item has, so ticking one asks the right question.
        CHECK(numeric_for(p, "sockets")->min == doctest::Approx(4));
        CHECK(numeric_for(p, "links")->min == doctest::Approx(3));
    }

    SUBCASE("an item with no socket line is asked nothing about sockets") {
        const Item it = resolved(*gd, kRareChest);
        const Derived d = derive(gd.get(), it);
        const SearchPlan p = build_plan(*gd, it, d);
        CHECK(numeric_for(p, "sockets") == nullptr);
        CHECK(numeric_for(p, "links") == nullptr);
    }
}

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

TEST_CASE("a modifier the unique's record does not have says so on its own row") {
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

    const StatFilter* life = filter_for(p, "explicit.stat_3299347043");
    REQUIRE(life != nullptr);
    CHECK_FALSE(life->enabled);
    // On the row, not under the list: the row already names the modifier and shows its box
    // unticked, and a note repeating that wording is the same sentence twice.
    CHECK(life->caveat.starts_with("not in the modifier data for \"Ralakesh's Impatience\""));
    CHECK(p.notes.empty());
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

    // Prose with nothing on screen behind it: no row of the filter list is about "4 random
    // Charm modifiers", so a note is the only place the app can say it is leaving them out.
    REQUIRE(p.notes.size() == 1);
    CHECK(p.notes.front() ==
          "the modifier data states but does not enumerate this, so it is not searched: "
          "4 random Charm modifiers");
    // The modifier the copy does show is the other half, and it is on its own row instead.
    CHECK(filter_saying(p, "Corrupted Blood")->caveat.starts_with("not in the modifier data"));
}

TEST_CASE("an unlisted pool the item does print is the row, not a note under it") {
    auto gd = fixture();
    // Triad Grip's four conversion modifiers are unlisted in the record *and* printed on the
    // item, so they used to be said twice each — once as "not in the modifier data" and once as
    // "states but does not enumerate" — and twelve lines of panel went on four unticked boxes.
    // The fixture's stand-in for that shape is Rumi's, whose enchant the record does not carry.
    const Item it = resolved(*gd, R"(Item Class: Utility Flasks
Rarity: Unique
Rumi's Concoction
Granite Flask
--------
Lasts 4.00 Seconds
Consumes 30 of 60 Charges on use
Currently has 60 Charges
+3000 to Armour
--------
Requirements:
Level: 27
--------
Item Level: 70
--------
Used when Charges reach full (enchant)
--------
16% Chance to Block Attack Damage during Effect
9% Chance to Block Spell Damage during Effect
)");
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    // An enchant is `added_to_copy`: absent from a record about the unique by definition, so it
    // gets neither a note nor a caveat — saying so reads as failing to recognise it.
    const StatFilter* enchant = filter_saying(p, "Used when Charges reach full");
    REQUIRE(enchant != nullptr);
    CHECK(enchant->caveat.empty());
    // Nothing about the modifier data is said underneath the list; the flask's own effect,
    // which has no row anywhere, still is.
    CHECK(std::none_of(p.notes.begin(), p.notes.end(), [](const std::string& n) {
        return n.find("modifier data") != std::string::npos;
    }));
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

TEST_CASE("a quality unidentified unique is still read off its base") {
    auto gd = fixture();
    // Quality prints the base line as "Superior Riveted Boots", because nothing else has named
    // the item yet. Stripped only at Normal rarity, that word reached every lookup: no base, no
    // candidate uniques, no name to search for and no reference price either.
    const Item it = resolved(*gd, R"(Item Class: Boots
Rarity: Unique
Superior Riveted Boots
--------
Quality: +20% (augmented)
Evasion Rating: 36
--------
Item Level: 84
--------
Unidentified
)");
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    CHECK(it.quality == 20);
    CHECK(it.base_type == "Riveted Boots");
    REQUIRE(it.base != nullptr);
    REQUIRE(it.unique_entry != nullptr);
    CHECK(p.name == "Ralakesh's Impatience");
    CHECK(p.type == "Riveted Boots");
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

TEST_CASE("a unique that drops on two bases is two candidates, and two records") {
    auto gd = fixture();
    // Stormblood drops on both the Sapphire and the Topaz Flask under one name. The bundle
    // used to carry one record per name, so the Topaz Flask answered with Vessel of Vinktar
    // alone — one candidate, which is taken as the name rather than asked about, and an
    // unidentified Topaz Flask was priced as somebody else's unique.
    const Item it = resolved(*gd, R"(Item Class: Utility Flasks
Rarity: Unique
Topaz Flask
--------
Lasts 4.00 Seconds
Consumes 20 of 50 Charges on use
Currently has 0 Charges
--------
Item Level: 85
--------
Unidentified
)");
    REQUIRE(it.unique_candidates.size() == 2);
    CHECK(it.needs_unique_choice());
    CHECK(build_plan(*gd, it, derive(gd.get(), it)).name.empty());

    SUBCASE("the identified one resolves to the record for the base it is on") {
        // Both records answer to "Stormblood", so the base is what tells them apart — and the
        // search is sent for this flask rather than for the cold one of the same name.
        const Item id = resolved(*gd, capture("unique-flask-stormblood-topaz.txt"));
        REQUIRE(id.unique_entry != nullptr);
        CHECK(id.unique_entry->unique_base == "Topaz Flask");
        const SearchPlan p = build_plan(*gd, id, derive(gd.get(), id));
        CHECK(p.name == "Stormblood");
        CHECK(p.type == "Topaz Flask");
    }

    SUBCASE("and the other base's copy is the other record") {
        const Item other = resolved(*gd, R"(Item Class: Utility Flasks
Rarity: Unique
Stormblood
Sapphire Flask
--------
Item Level: 85
)");
        REQUIRE(other.unique_entry != nullptr);
        CHECK(other.unique_entry->unique_base == "Sapphire Flask");
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
    CHECK(strip_magic_affixes(*gd, "Surgeon's Quicksilver Flask of Heat", "Utility Flasks")
              .empty());

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
        std::optional<Item> it = parse_item_en(capture(f));
        REQUIRE_MESSAGE(it.has_value(), f);
        CHECK_MESSAGE(!it->item_level.has_value(), f);
        CHECK_MESSAGE(default_strategy(*it) == Strategy::Currency, f);
    }

    // One that prints an item level can also carry a rarity and its own quantity/rarity
    // modifiers, exactly as a map does, so it goes down the ordinary path for its rarity.
    const std::optional<Item> inv = parse_item_en(capture("invitation-writhing.txt"));
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

TEST_CASE("a beast is bought for its species and its item level, and for nothing else") {
    auto gd = fixture();

    SUBCASE("the species is the base, and it lives in a namespace of its own") {
        // Looking one up among the ordinary bases finds nothing at all, so before this the
        // plan had no type and the search would have been every beast in the league.
        const Item it = resolved(*gd, capture("beast-hellion-alpha.txt"));
        REQUIRE(it.base != nullptr);
        CHECK(it.base->name == "Wild Hellion Alpha");
        CHECK(it.base->ns == ppc::data::Namespace::CapturedBeast);
    }

    SUBCASE("the strategy is decided by the taxonomy, not by the rarity or the class") {
        // "Stackable Currency" is the class every orb shares and "Rare" is what the monster
        // modifiers made it. Planned as a rare, a Wild Hellion Alpha would search "Extra Life"
        // as an affix on a currency item, which matches nothing.
        const Item it = resolved(*gd, capture("beast-hellion-alpha.txt"));
        CHECK(default_strategy(it) == Strategy::Beast);

        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.strategy == Strategy::Beast);
        CHECK(p.type == "Wild Hellion Alpha");
        // The title is one capture's own and no two copies share it, so it is not asked for.
        CHECK(p.name.empty());
        // Not the bundle's answer for "Stackable Currency", which is `currency` and is right
        // for every orb printing that class. Measured on this capture: `currency` returned 0
        // matches and `monster.beast` returned 1602, same type and same item level.
        CHECK(p.category == "monster.beast");

        const NumericFilter* ilvl = numeric_for(p, "ilvl");
        REQUIRE(ilvl != nullptr);
        CHECK(ilvl->enabled);
        CHECK(ilvl->min == 83);
        // A floor, not a window: a recipe wanting an item level wants at least that much, and
        // a higher beast still answers.
        CHECK_FALSE(ilvl->max.has_value());
    }

    SUBCASE("the monster modifiers are left out silently, exactly as a map's affixes are") {
        // They are the captured monster's own abilities rather than rolls on a base, the
        // bundle has no stat for "Satyr Storm" to match, and no beastcrafting recipe asks for
        // one. Leaving them out is the decision, so "unrecognised modifier: Evasive" — five of
        // them on this capture — would charge the check with failing at what it did on purpose.
        const Item it = resolved(*gd, capture("beast-farric-goliath.txt"));
        REQUIRE(it.mods.size() == 5);
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.stats.empty());
        CHECK(p.notes.empty());
    }

    SUBCASE("a species the bundle does not know still searches, and says what it did") {
        // The beast list grows every league and a bundle behind the game is the ordinary case.
        // The clipboard's own spelling is the best term left, unlike a magic item's base line,
        // which carries affixes and would match nothing — so the search goes ahead with a note.
        const Item it = resolved(*gd, capture("beast-porcupine-goliath.txt"));
        CHECK(it.base == nullptr);
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.type == "Porcupine Goliath");
        REQUIRE(p.notes.size() == 1);
        CHECK(p.notes.front().find("is not a beast in this data bundle") != std::string::npos);
    }
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

        // Not one of those eight is part of the search, and not one is a note either: they are
        // left out deliberately, and "unrecognised modifier" would charge the check with
        // failing at it. They are `hidden` rather than absent — a row behind the expandable
        // section, unticked, so that a buyer who does want to name one can.
        bool any_hidden = false;
        for (const StatFilter& f : p.stats)
            if (f.type == ppc::data::ModType::Explicit) {
                CHECK(f.hidden);
                CHECK_FALSE(f.enabled);
                any_hidden = true;
            }
        CHECK(any_hidden);
        CHECK(p.notes.empty());
    }

    SUBCASE("a map with no implicit is its tier and its affix count, and nothing else") {
        const Item it = resolved(*gd, capture("map-rare-8mod-corrupted.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        // One filter the search is actually made of. The affixes are all behind it, hidden.
        const std::vector<const StatFilter*> shown = shown_stats(p);
        REQUIRE(shown.size() == 1);
        CHECK(shown[0]->id == "pseudo.pseudo_number_of_affix_mods");
        CHECK(shown[0]->min == 8);
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
        // Its own modifiers are the same on every copy; the name already says them, so none of
        // them is part of the search. They are still offered, hidden and unticked, on the same
        // terms as a rare map's affixes — nothing in the list, nothing in the query.
        CHECK(shown_stats(p).empty());
        for (const StatFilter& f : p.stats) {
            CHECK(f.hidden);
            CHECK_FALSE(f.enabled);
        }
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
    CHECK(shown_stats(p).size() == 2);
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
        const std::vector<const StatFilter*> shown = shown_stats(p);
        REQUIRE(shown.size() == 1);
        CHECK(shown[0]->id == "explicit.stat_1095765106");
        CHECK(shown[0]->enabled);
        CHECK_FALSE(shown[0]->negated);
        CHECK(shown[0]->mod_index.has_value());

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

        for (const char* key : {"corrupted", "mirrored", "mutated", "identified"}) {
            const OptionFilter* f = p.option(key);
            REQUIRE_MESSAGE(f != nullptr, key);
            CHECK(f->enabled);
            CHECK_FALSE(f->shown);
        }
        CHECK(flag_of(p, "corrupted") == false);
        CHECK(flag_of(p, "mirrored") == false);
        CHECK(flag_of(p, "mutated") == false);
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

    SUBCASE("a foulborn unique asks for the mutation and offers the row") {
        Item it = resolved(*gd, kRareChest);
        it.foulborn = true;
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        const OptionFilter* f = p.option("mutated");
        REQUIRE(f != nullptr);
        CHECK(f->label == "Foulborn"); // the site's key is `mutated`; the game's word is not
        CHECK(f->option == "true");
        CHECK(f->enabled);
        CHECK(f->shown); // foulborn copies are their own market, and priced apart from the rest
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
        // are left out of the search exactly as a map's are — and left out silently, in front
        // of the reader. Hidden rather than dropped: unticked, behind the expandable section.
        const StatFilter* life = filter_saying(p, "Monster Life");
        REQUIRE(life != nullptr);
        CHECK(life->hidden);
        CHECK_FALSE(life->enabled);
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

TEST_CASE("an ultimatum is searched on the deal it offers, not on the danger it describes") {
    auto gd = fixture();

    /// What the search asks an `ultimatum_filters` option for, or nothing when it does not ask.
    const auto asked = [](const SearchPlan& p, std::string_view key) {
        const OptionFilter* f = p.option(key);
        return f && f->enabled ? f->option : std::string();
    };

    SUBCASE("the challenge picks the strategy, and the trade category is dropped outright") {
        const Item it = resolved(*gd, capture("ultimatum-currency-divine-x8.txt"));
        CHECK(default_strategy(it) == Strategy::Ultimatum);

        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.type == "Inscribed Ultimatum");
        CHECK(p.name.empty());
        // The bundle *does* map "Misc Map Items", and to `map.fragment`, which is right for the
        // invitations and splinters sharing the class. Measured on this capture: 0 matches with
        // it and 443 without, everything else identical. The type term says the rest.
        CHECK(gd->trade_category_for(it.item_class) == "map.fragment");
        CHECK(p.category.empty());
        CHECK(p.notes.empty());
    }

    SUBCASE("the challenge and the reward are sent as trade's own option ids") {
        // The game prints the option's own text for the challenge and its own wording for the
        // reward; neither id can be derived from either, so both are joined through a table.
        const Item it = resolved(*gd, capture("ultimatum-mirror.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(asked(p, "ultimatum_challenge") == "Conquer");
        CHECK(asked(p, "ultimatum_reward") == "MirrorRare");
        // "Mirrorable, Rare Item" names a class of items rather than one, and the reward type
        // has already said so — so there is nothing to ask and nothing to apologise for.
        CHECK(p.option("ultimatum_input") == nullptr);
        CHECK(p.notes.empty());
    }

    SUBCASE("all four trials join, and each to the id trade publishes for it") {
        // The whole vocabulary, one capture apiece, because the ids are nothing like the words
        // and a table with an entry out of order fails as a search for the wrong trial rather
        // than as an error. "Defense" is the altar and "Survival" is the bare "Survive".
        const std::pair<const char*, const char*> kTrials[]{
            {"ultimatum-divination.txt", "Exterminate"},
            {"ultimatum-challenge-survive.txt", "Survival"},
            {"ultimatum-challenge-altar.txt", "Defense"},
            {"ultimatum-mirror.txt", "Conquer"}};
        for (const auto& [file, id] : kTrials) {
            const Item it = resolved(*gd, capture(file));
            const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
            CHECK_MESSAGE(asked(p, "ultimatum_challenge") == id, file);
        }
    }

    SUBCASE("a divination card is as nameable a stake as an orb or a unique") {
        // The third of the namespaces the site's `knownItem` filter names, and the one whose
        // positive case the deliberately-absent Dialla's Subjugation cannot cover.
        const Item it = resolved(*gd, capture("ultimatum-challenge-altar.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(asked(p, "ultimatum_input") == "Blind Venture");
        CHECK(asked(p, "ultimatum_reward") == "DoubleDivCards");
        // No stake-scaling modifiers on this one at all, which is an ordinary ultimatum and
        // not a plan that failed to read them.
        CHECK(p.stats.empty());
        CHECK(p.notes.empty());
    }

    SUBCASE("a lower-case challenge still joins to its option") {
        // The trade site titles the option "Defeat Waves of Enemies" and the client prints
        // "Defeat waves of enemies". The case is the only thing that differs, and matching on
        // it exactly would leave the search asking for every trial at this area level.
        const Item it = resolved(*gd, capture("ultimatum-divination.txt"));
        CHECK(it.properties.front().value == "Defeat waves of enemies");
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(asked(p, "ultimatum_challenge") == "Exterminate");
        CHECK(asked(p, "ultimatum_reward") == "DoubleDivCards");
    }

    SUBCASE("a unique reward is a reward type and the unique's own name") {
        const Item it = resolved(*gd, capture("ultimatum-mageblood.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(asked(p, "ultimatum_reward") == "ExchangeUnique");
        CHECK(asked(p, "ultimatum_output") == "Mageblood");
        // A unique can be staked as readily as paid out, so the stake is looked up across the
        // three namespaces the site's own filter names rather than in the currency alone.
        CHECK(asked(p, "ultimatum_input") == "Martyr of Innocence");
        CHECK(p.notes.empty());
    }

    SUBCASE("the stake is the item, never how many of it") {
        // Trade indexes no count, and the count is already implied by the two modifiers below:
        // an ultimatum staking eight Divine Orbs is the one at 200% more Monster Life.
        const Item it = resolved(*gd, capture("ultimatum-currency-divine-x8.txt"));
        CHECK(it.properties[2].value == "Divine Orb x8");
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(asked(p, "ultimatum_input") == "Divine Orb");
    }

    SUBCASE("the area level is exact, and so are the two modifiers that scale the stake") {
        const Item it = resolved(*gd, capture("ultimatum-currency-divine-x8.txt"));
        // Wide enough that anything not forced exact would come out as a window.
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it), std::nullopt, kWholeTier);

        const NumericFilter* lvl = numeric_for(p, "area_level");
        REQUIRE(lvl != nullptr);
        CHECK(lvl->enabled);
        CHECK(lvl->min == 83);
        CHECK(lvl->max == 83);

        REQUIRE(p.stats.size() == 2);
        for (const StatFilter& f : p.stats) {
            CHECK(f.enabled);
            REQUIRE(f.min.has_value());
            CHECK(f.min == f.max);
        }
        CHECK(p.stats[0].min == 30);
        CHECK(p.stats[1].min == 120);
    }

    SUBCASE("the hazards are left out silently, exactly as a map's affixes are") {
        // Eleven of the thirteen lines are the shape of the danger rather than a term of the
        // deal, the bundle has no stat for "Shattered Shield", and a note apiece would charge
        // the check with failing at eleven things it left out on purpose.
        const Item it = resolved(*gd, capture("ultimatum-mageblood.txt"));
        REQUIRE(it.mods.size() == 13);
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.stats.size() == 2);
        CHECK(p.notes.empty());
    }

    SUBCASE("a stake the bundle cannot confirm is left off, and said out loud") {
        // The trade site fails the whole search on a required item it does not know, so an
        // unconfirmed name is never sent — the rest of the contract is still a real search.
        const Item it = resolved(*gd, capture("ultimatum-divination.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.option("ultimatum_input") == nullptr);
        REQUIRE(p.notes.size() == 1);
        CHECK(p.notes.front().find("Dialla's Subjugation") != std::string::npos);
        // Still a real search: the trial, the payout and the area level are the rest of it.
        CHECK(p.option("ultimatum_challenge") != nullptr);
    }

    SUBCASE("every filter it does ask for is offered rather than imposed") {
        // The user is choosing which half of the contract to relax, so all of them have a row.
        const Item it = resolved(*gd, capture("ultimatum-mageblood.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        int shown = 0;
        for (const OptionFilter& f : p.options)
            if (f.key.starts_with("ultimatum_")) {
                CHECK(f.enabled);
                CHECK(f.shown);
                ++shown;
            }
        CHECK(shown == 4);
    }
}

TEST_CASE("a heist item is searched on the run it opens, not on the danger it rolled") {
    auto gd = fixture();

    SUBCASE("a magic or rare heist item gets its own strategy; a unique one does not") {
        // The rarity switch would plan a rare contract as a rare and search its seven hazards
        // as if somebody were buying them, and ask for none of the filters the site indexes it
        // on. A unique contract is the opposite case: it is bought for its name, and the name
        // fixes everything else about it — so it stays with the unique strategy.
        CHECK(default_strategy(resolved(*gd, capture("heist-contract-rare-tunnels.txt"))) ==
              Strategy::Heist);
        CHECK(default_strategy(
                  resolved(*gd, capture("heist-blueprint-magic-records-office.txt"))) ==
              Strategy::Heist);
        CHECK(default_strategy(resolved(*gd, capture("heist-contract-unique-slaver-king.txt"))) ==
              Strategy::Unique);
    }

    SUBCASE("the area is the type, and contracts and blueprints are separate categories") {
        const Item c = resolved(*gd, capture("heist-contract-rare-tunnels.txt"));
        const SearchPlan cp = build_plan(*gd, c, derive(gd.get(), c));
        CHECK(cp.type == "Contract: Tunnels");
        CHECK(cp.category == "heistmission.contract");
        // The generated name is one copy's own, exactly as a rare bow's is.
        CHECK(cp.name.empty());

        // Same area, same wing, different item and a different market.
        const Item b = resolved(*gd, capture("heist-blueprint-rare-tunnels-full.txt"));
        const SearchPlan bp = build_plan(*gd, b, derive(gd.get(), b));
        CHECK(bp.type == "Blueprint: Tunnels");
        CHECK(bp.category == "heistmission.blueprint");
    }

    SUBCASE("a magic blueprint is searched by the base under its affixes") {
        // "Deployed Blueprint: Records Office of Spine-Chilling" as a type matches nothing.
        const Item it = resolved(*gd, capture("heist-blueprint-magic-records-office.txt"));
        CHECK(it.base_type == "Deployed Blueprint: Records Office of Spine-Chilling");
        CHECK(build_plan(*gd, it, derive(gd.get(), it)).type == "Blueprint: Records Office");
    }

    SUBCASE("what is revealed is a floor and what there is of it is exact") {
        const Item it = resolved(*gd, capture("heist-blueprint-magic-records-office.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        const NumericFilter* wings = numeric_for(p, "heist_wings");
        REQUIRE(wings != nullptr);
        CHECK(wings->enabled);
        CHECK(wings->min == 1);
        CHECK_FALSE(wings->max.has_value());

        // A Records Office blueprint comes with two, three or four wings, so the total is part
        // of which item this is rather than an amount of anything.
        const NumericFilter* total = numeric_for(p, "heist_max_wings");
        REQUIRE(total != nullptr);
        CHECK(total->enabled);
        CHECK(total->min == 2);
        CHECK(total->max == 2);
        REQUIRE(numeric_for(p, "heist_max_reward_rooms") != nullptr);
        CHECK(numeric_for(p, "heist_max_reward_rooms")->min == 13);
    }

    SUBCASE("Total Escape Routes is never asked for, because nothing is indexed under it") {
        // The site publishes `heist_max_escape_routes` and accepts it, and no listing carries a
        // value for it — so any bound empties the result and it reads as nobody selling one.
        // Measured one filter at a time on the fully revealed Tunnels capture: `heist_max_wings`
        // at 4 returned 460 and `heist_max_reward_rooms` at 28 returned 460, while
        // `heist_max_escape_routes` returned 0 both at the item's own 8 and at a bare min of 1.
        const Item it = resolved(*gd, capture("heist-blueprint-rare-tunnels-full.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(numeric_for(p, "heist_max_escape_routes") == nullptr);
        // Its revealed count is asked for, though — that one is indexed like the other two.
        REQUIRE(numeric_for(p, "heist_escape_routes") != nullptr);
        CHECK(numeric_for(p, "heist_escape_routes")->min == 8);
    }

    SUBCASE("the objective's value is offered as the parenthetical, and a boss contract has none") {
        // The value follows from whatever target the copy rolled, so it is a row rather than a
        // demand: it is drawn with what the item says, and left unticked.
        const auto offered = [&](const char* file) {
            const Item it = resolved(*gd, capture(file));
            const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
            const OptionFilter* f = p.option("heist_objective_value");
            if (!f) return std::string();
            CHECK(f->shown);
            CHECK(!f->enabled);
            return f->option;
        };
        CHECK(offered("heist-contract-rare-tunnels.txt") == "high");
        CHECK(offered("heist-contract-rare-laboratory.txt") == "priceless");
        CHECK(offered("heist-contract-rare-underbelly.txt") == "precious");
        // A blueprint sends the crew after a wing rather than after a thing, so it prints no
        // target line at all and there is nothing to ask.
        CHECK(offered("heist-blueprint-rare-tunnels-full.txt").empty());
    }

    SUBCASE("a job level is a floor and is asked for at the level the item demands") {
        // What the run costs to open: a rogue short of the requirement cannot run this copy at
        // all, and a copy asking for less is a cheaper product rather than a better one.
        const Item it = resolved(*gd, capture("heist-blueprint-rare-underbelly.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        for (const char* key : {"heist_brute_force", "heist_agility", "heist_deception"}) {
            const NumericFilter* f = numeric_for(p, key);
            REQUIRE_MESSAGE(f != nullptr, key);
            CHECK_MESSAGE(f->enabled, key);
            CHECK_MESSAGE(!f->max.has_value(), key);
        }
        CHECK(numeric_for(p, "heist_brute_force")->min == 4);
        CHECK(numeric_for(p, "heist_agility")->min == 3);
        CHECK(numeric_for(p, "heist_deception")->min == 1);
        // The six it does not demand are not rows at all.
        CHECK(numeric_for(p, "heist_engineering") == nullptr);
        CHECK(numeric_for(p, "heist_lockpicking") == nullptr);
    }

    SUBCASE("the enchant is imposed and the hazards are only offered") {
        // "Heist Targets are always Enchanted Armaments" is what the whole run is for and
        // somebody paid to put it there. The rest is the danger it will hold — rolled and
        // re-rollable — and seven ticked hazards ask for one particular copy in the world.
        const Item it = resolved(*gd, capture("heist-blueprint-rare-tunnels-full.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        const StatFilter* ench = filter_saying(p, "Heist Targets are always Enchanted Armaments");
        REQUIRE(ench != nullptr);
        CHECK(ench->type == ppc::data::ModType::Enchant);
        CHECK(ench->enabled);
        for (const StatFilter& f : p.stats)
            if (f.type != ppc::data::ModType::Enchant) CHECK_MESSAGE(!f.enabled, f.text);
    }

    SUBCASE("an area the bundle does not know is still a search, and says what it is not") {
        // The heist wings grow with the league and a bundle behind the game is the ordinary
        // case. Unlike a beast, the clipboard's own spelling is not a usable fallback — a magic
        // blueprint's base line carries its affixes — so the type is dropped and the category,
        // the area level and the reveal counts are what the search has left.
        const Item it = resolved(*gd, capture("heist-contract-rare-underbelly.txt"));
        CHECK(it.base == nullptr);
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.type.empty());
        CHECK(p.category == "heistmission.contract");
        REQUIRE(p.option("heist_objective_value") != nullptr);
        CHECK(p.option("heist_objective_value")->option == "precious");
        CHECK(p.notes.front().find("is not a heist base in this data bundle") !=
              std::string::npos);
    }

    SUBCASE("a unique contract's properties are not four notes about not searching them") {
        // The per-unique modifier data lists this contract's client, area level, heist target
        // and job requirement as modifiers it never enumerates, and the game prints all four as
        // properties. Four notes saying they are not searched, beside four lines already on
        // screen saying what they are.
        const Item it = resolved(*gd, capture("heist-contract-unique-slaver-king.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.strategy == Strategy::Unique);
        CHECK(p.name == "Contract: The Slaver King");
        CHECK(p.type == "Vigilante Contract");
        CHECK(p.notes.empty());
    }
}

TEST_CASE("an itemised sanctum is searched on the state of the run") {
    auto gd = fixture();

    SUBCASE("it gets its own strategy rather than being read as a white base item") {
        // "Rarity: Normal" is all the game has to print on that line, and planning it as a base
        // item searched for an empty Sanctum Vaults Research at this item level — every run in
        // the league, and none of what tells two of them apart.
        const Item it = resolved(*gd, capture("sanctum-vaults-rooms.txt"));
        CHECK(it.rarity == Rarity::Normal);
        CHECK(default_strategy(it) == Strategy::Sanctum);
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.type == "Sanctum Vaults Research");
        CHECK(p.category == "sanctum.research");
        CHECK(numeric_for(p, "ilvl") == nullptr);
    }

    SUBCASE("resolve, inspiration and aureus are floors; the area level is exact") {
        const Item it = resolved(*gd, capture("sanctum-vaults-rooms.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));

        const NumericFilter* lvl = numeric_for(p, "area_level");
        REQUIRE(lvl != nullptr);
        CHECK(lvl->enabled);
        CHECK(lvl->min == 83);
        CHECK(lvl->max == 83);

        for (const auto& [key, min] : std::vector<std::pair<const char*, double>>{
                 {"sanctum_resolve", 328}, {"sanctum_inspiration", 30}, {"sanctum_gold", 419}}) {
            const NumericFilter* f = numeric_for(p, key);
            REQUIRE_MESSAGE(f != nullptr, key);
            CHECK_MESSAGE(f->enabled, key);
            CHECK_MESSAGE(f->min == min, key);
            CHECK_MESSAGE(!f->max.has_value(), key);
        }
    }

    SUBCASE("resolve is two numbers, and only the current one is asked for") {
        // "299/300" — what is left of the run, and what the character it started on could take.
        // The maximum is offered open on the right and left unticked: it says more about the
        // build that opened the sanctum than about how much run is left to sell.
        const Item it = resolved(*gd, capture("sanctum-vaults-partial-resolve.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(numeric_for(p, "sanctum_resolve")->min == 299);
        const NumericFilter* max = numeric_for(p, "sanctum_max_resolve");
        REQUIRE(max != nullptr);
        CHECK_FALSE(max->enabled);
        CHECK(max->min == 300);
        CHECK_FALSE(max->max.has_value());
    }

    SUBCASE("every boon and affliction is its own stat filter") {
        const Item it = resolved(*gd, capture("sanctum-vaults-major-boon.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        // Major and minor alike: which of the two it is stays on the item beside the panel, and
        // the stat the site indexes is the effect's own name either way.
        for (const char* name : {"Has Gold Coin", "Has Weakened Flesh", "Has Sharpened Arrowhead"}) {
            const StatFilter* f = filter_saying(p, name);
            REQUIRE_MESSAGE(f != nullptr, name);
            CHECK_MESSAGE(f->enabled, name);
            CHECK_MESSAGE(f->type == ppc::data::ModType::Sanctum, name);
            CHECK_MESSAGE(f->id.starts_with("sanctum.sanctum_effect_"), name);
            // Not a modifier — the game prints these as a property — so nothing points back.
            CHECK_MESSAGE(!f->mod_index.has_value(), name);
        }
    }

    SUBCASE("the affixes are searched in the sanctum namespace and nowhere else") {
        // Every one of these stats is indexed under `sanctum.` alone. With the parser's default
        // mod type they resolved to nothing at all and came back as unrecognised modifiers.
        const Item it = resolved(*gd, capture("sanctum-vaults-major-boon.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        const StatFilter* choices = filter_saying(p, "The Merchant has 10 additional Choices");
        REQUIRE(choices != nullptr);
        CHECK(choices->enabled);
        CHECK(choices->id == "sanctum.stat_290775436");
        // The inverse wording: the stat is stored as an increase and the site indexes it that
        // way, so 40% reduced is a bound on the negative side.
        const StatFilter* prices = filter_saying(p, "40% reduced Merchant Prices");
        REQUIRE(prices != nullptr);
        CHECK(prices->id == "sanctum.stat_3096446459");
        CHECK(prices->max.value_or(0) < 0);
        CHECK(p.notes.empty());
    }

    SUBCASE("a boon the bundle cannot name is left out and said out loud") {
        // The effect list grows with the league, exactly as the beast list does. "Has Red
        // Smoke" is deliberately absent from the test bundle so the degradation has a case.
        const Item it = resolved(*gd, capture("sanctum-vaults-partial-resolve.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(filter_saying(p, "Has Red Smoke") == nullptr);
        REQUIRE(filter_saying(p, "Has Empty Trove") != nullptr);
        CHECK(std::any_of(p.notes.begin(), p.notes.end(), [](const std::string& n) {
            return n.find("\"Red Smoke\" is not a sanctum boon or affliction") !=
                   std::string::npos;
        }));
    }
}

TEST_CASE("an Expedition Logbook is priced on one of its destinations at a time") {
    const std::shared_ptr<GameData> gd = fixture();

    SUBCASE("the strategy, the category and the type the category already implies") {
        const Item it = resolved(*gd, capture("logbook-normal-three-areas.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.strategy == Strategy::Logbook);
        CHECK(p.category == "logbook");
        CHECK(p.type == "Expedition Logbook");
        CHECK(p.rarity == "nonunique");
    }

    SUBCASE("one group per destination, and exactly one of them live") {
        const Item it = resolved(*gd, capture("logbook-normal-three-areas.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        REQUIRE(p.choices.size() == 3);
        CHECK(p.choices[0].label == "Druids of the Broken Circle");
        CHECK(p.choices[0].note == "Scrublands");
        CHECK(p.choices[2].label == "Order of the Chalice");
        CHECK(p.choice == 0);
        // Every stat on this plan belongs to a destination — a Normal logbook has no affixes —
        // and the only ticked one is the live group's faction.
        std::vector<std::string> enabled;
        for (const StatFilter& f : p.stats) {
            REQUIRE(f.choice.has_value());
            if (f.enabled) enabled.push_back(f.id);
        }
        CHECK(enabled == std::vector<std::string>{"pseudo.pseudo_logbook_faction_druids"});
    }

    SUBCASE("the faction is ticked; where it goes and what it grants there are offered") {
        const Item it = resolved(*gd, capture("logbook-normal-three-areas.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        std::vector<std::pair<std::string, bool>> live;
        for (const StatFilter& f : p.stats)
            if (f.choice == 0) live.emplace_back(f.text, f.enabled);
        REQUIRE(live.size() == 4);
        CHECK(live[0] == std::pair<std::string, bool>{
                             "Has Logbook Faction: Druids of the Broken Circle", true});
        CHECK(live[1] ==
              std::pair<std::string, bool>{"Has Logbook Area: Scrublands", false});
        CHECK(live[2].first == "32% increased quantity of Artifacts dropped by Monsters");
        CHECK_FALSE(live[2].second);
    }

    SUBCASE("the faction filter is presence, never a count") {
        // The pseudo stat takes one — how many destinations belong to that faction — and it is
        // not what decides the price. A logbook with two Druids destinations is still bought
        // for a Druids run, and bounding it drops every single-destination copy of the same
        // thing. The rare capture has exactly that pair.
        const Item it = resolved(*gd, capture("logbook-rare-ancient-lands.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        for (const StatFilter& f : p.stats)
            if (f.id.starts_with("pseudo.pseudo_logbook_faction")) {
                CHECK_FALSE(f.min.has_value());
                CHECK_FALSE(f.max.has_value());
            }
    }

    SUBCASE("two destinations of one faction stay two alternatives") {
        // Both are Druids, so both rows carry the same trade id — and `merge_same_stat` would
        // fold them into one row standing for a choice that is not a choice.
        const Item it = resolved(*gd, capture("logbook-rare-ancient-lands.txt"));
        SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        REQUIRE(p.choices.size() == 3);
        CHECK(p.choices[0].note == "Volcanic Island");
        CHECK(p.choices[1].note == "Battleground Graves");
        CHECK(p.choices[0].label == p.choices[1].label);
        size_t druids = 0;
        for (const StatFilter& f : p.stats)
            if (f.id == "pseudo.pseudo_logbook_faction_druids") ++druids;
        CHECK(druids == 2);

        // And switching group ticks the new faction and unticks everything of the old one.
        p.select_choice(1);
        CHECK(p.choice == 1);
        for (const StatFilter& f : p.stats)
            CHECK(f.enabled == (f.choice == 1 && f.choice_primary));
    }

    SUBCASE("a destination's implicit is a floor, because trade totals them across the book") {
        // Volcanic Island grants 14% increased number of Explosives and Battleground Graves
        // grants 16%, and the site indexes the item's implicits as one total per stat — so a
        // ceiling seeded from one destination's roll asks the other two not to exist.
        const Item it = resolved(*gd, capture("logbook-rare-ancient-lands.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        std::vector<double> explosives;
        for (const StatFilter& f : p.stats)
            if (f.text.find("increased number of Explosives") != std::string::npos) {
                REQUIRE(f.min.has_value());
                CHECK_FALSE(f.max.has_value());
                explosives.push_back(*f.min);
            }
        REQUIRE(explosives.size() == 2);
        CHECK(explosives[0] < explosives[1]);
    }

    SUBCASE("the book's own affixes are hidden, exactly as a map's are") {
        const Item it = resolved(*gd, capture("logbook-rare-ancient-lands.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        for (const StatFilter& f : p.stats) CHECK(f.hidden != f.choice.has_value());
        size_t hidden = 0;
        for (const StatFilter& f : p.stats)
            if (f.hidden) {
                CHECK_FALSE(f.enabled);
                ++hidden;
            }
        // Three of the five, and the two missing ones are a data gap rather than this rule:
        // "+25% Monster Chaos Resistance" and "+40% Monster Elemental Resistances" are
        // published with the leading sign inside the matcher ("+#% Monster Chaos Resistance"),
        // and `placeholder_form` replaces the sign along with the digits — so nothing the
        // clipboard prints can reach them. 34 of the bundle's 15,148 matchers are shaped that
        // way and none of them is a logbook's. Fix that and this becomes 5.
        CHECK(hidden == 3);
        // And not one of them is a note: they were left out on purpose, and the reader has
        // them on the item card beside the panel.
        CHECK(p.notes.empty());
    }

    SUBCASE("the area level is a floor and ticked; everything else about the book is offered") {
        const Item it = resolved(*gd, capture("logbook-rare-ancient-lands.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        const NumericFilter* lvl = numeric_for(p, "area_level");
        REQUIRE(lvl != nullptr);
        CHECK(lvl->min == 80);
        CHECK_FALSE(lvl->max.has_value()); // an 83 answers a search for an 80
        CHECK(lvl->enabled);
        // Quantity and pack size are ticked on a map and deliberately are not here, which is the
        // one place this strategy parts company with the keys it borrows: a map is run for them,
        // and a logbook's are a second-order bonus on top of the artifacts the destination
        // decides. They also only exist on a magic or rare book, so ticking them would search
        // the same logbook two ways depending on whether it had rolled affixes at all.
        for (const char* key : {"ilvl", "map_iiq", "map_packsize", "map_iir"}) {
            const NumericFilter* f = numeric_for(p, key);
            REQUIRE_MESSAGE(f != nullptr, key);
            CHECK_FALSE(f->enabled);
        }
        CHECK(numeric_for(p, "map_iiq")->min == 61);
    }

    SUBCASE("a magic logbook is searched as the base, never as the name the affix decorated") {
        const Item it = resolved(*gd, capture("logbook-magic-buffered.txt"));
        const SearchPlan p = build_plan(*gd, it, derive(gd.get(), it));
        CHECK(p.type == "Expedition Logbook");
        REQUIRE(p.choices.size() == 2);
        CHECK(p.choices[0].note == "Bluffs");
        // Three implicits on that destination, not the two the other captures print.
        size_t rows = 0;
        for (const StatFilter& f : p.stats)
            if (f.choice == 0) ++rows;
        CHECK(rows == 5); // the faction, the area, and three implicits
    }
}

TEST_CASE("a modifier that rolls over a list of names resolves as one modifier") {
    auto gd = fixture();
    // The Dark Monarch doubles the limit of one minion skill gem out of sixteen, and with
    // Advanced Mod Descriptions on the game prints that pool the way it prints a numeric
    // range: the roll is the gem named in the wording, the parenthesis is the first and last
    // of the list. Left in, neither line matched anything and both came back unrecognised.
    const Item it = resolved(*gd, capture("unique-dark-monarch.txt"));
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    CHECK(p.strategy == Strategy::Unique);
    CHECK(p.name == "The Dark Monarch");
    for (const std::string& n : p.notes)
        CHECK(n.find("unrecognised modifier") == std::string::npos);

    // Two clipboard lines, one stat, and the option the item rolled is in the trade id.
    const StatFilter* doubled = filter_for(p, "explicit.stat_56473917|10");
    REQUIRE(doubled != nullptr);
    CHECK(doubled->text.find("(Animated Weapons-Holy Armaments)") != std::string::npos);
    CHECK(doubled->text.find("Cannot have Minions other than") != std::string::npos);

    // The range is the pool, not a number: nothing about it is a bound.
    CHECK_FALSE(doubled->min.has_value());
    CHECK_FALSE(doubled->max.has_value());
    CHECK_FALSE(doubled->roll_min.has_value());
    CHECK_FALSE(doubled->roll_max.has_value());

    // And it is the one thing about this copy worth searching for: sixteen minion types roll
    // here, so the modifier is pooled, ticked, and carries no "not in the modifier data".
    CHECK(doubled->pooled);
    CHECK(doubled->enabled);
    CHECK(doubled->caveat.empty());

    // And the numeric range on the same item is untouched by the stripping.
    const StatFilter* es = filter_for(p, "explicit.stat_4052037485");
    REQUIRE(es != nullptr);
    CHECK(es->roll_min == doctest::Approx(50));
    CHECK(es->roll_max == doctest::Approx(100));
}

TEST_CASE("a named range needs no space in front of it, and the pool can be every gem") {
    auto gd = fixture();
    // The other shape of the same thing: Replica Dragonfang's Flight raises one skill gem out
    // of every skill gem there is, so the wording carries a number *and* a name, and the game
    // prints the pool tight against the name — "Storm Burst(Fireball-Mana-Infused Staff)".
    const Item it = resolved(*gd, capture("unique-replica-dragonfangs-flight.txt"));
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    CHECK(p.name == "Replica Dragonfang's Flight");
    for (const std::string& n : p.notes)
        CHECK(n.find("unrecognised modifier") == std::string::npos);

    // The gem the copy rolled is the trade filter; the "+3" is not a bound, because the game
    // printed no range for it — the parenthesis belongs to the gem, not to the level.
    const StatFilter* gem = filter_for(p, "explicit.indexable_skill_160");
    REQUIRE(gem != nullptr);
    CHECK(gem->text.find("Storm Burst(Fireball-Mana-Infused Staff)") != std::string::npos);
    CHECK_FALSE(gem->min.has_value());
    CHECK_FALSE(gem->max.has_value());
    // 287 gems can roll here, and which one it is is the item's whole price.
    CHECK(gem->pooled);
    CHECK(gem->enabled);
    CHECK(gem->caveat.empty());

    // The numeric ranges on the same item are untouched, the descending one included: the
    // wording is the inverse, so the roll is stored negative and the window follows it down.
    const StatFilter* attr = filter_for(p, "explicit.stat_752930724");
    REQUIRE(attr != nullptr);
    CHECK(attr->min == doctest::Approx(-6));
    CHECK(attr->max == doctest::Approx(-5));

    // And both corruption implicits under the one marker are still two modifiers.
    CHECK(filter_for(p, "implicit.stat_4139681126") != nullptr);
    CHECK(filter_for(p, "implicit.stat_656461285") != nullptr);
}

TEST_CASE("a modifier that enumerates its alternatives is one modifier, however long") {
    auto gd = fixture();
    // Bound Fate's modifier is seven clipboard lines: the promise, then the six things it
    // can be. The join cap was four, so it could never be built, and all seven lines came
    // back as unrecognised modifiers of their own.
    const Item it = resolved(*gd, capture("unique-bound-fate.txt"));
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    CHECK(p.name == "Bound Fate");
    for (const std::string& n : p.notes)
        CHECK(n.find("unrecognised modifier") == std::string::npos);

    const StatFilter* fate = filter_for(p, "explicit.stat_2501671832");
    REQUIRE(fate != nullptr);
    CHECK(fate->text.find("Every 5 seconds") != std::string::npos);
    CHECK(fate->text.find("Damage of Hits against you is Lucky") != std::string::npos);

    // Every copy has it and it rolls 1..1, so it is correctly left unticked — and, unlike
    // before, without claiming the unique's own modifier data has never heard of it.
    CHECK_FALSE(fate->enabled);
    CHECK_FALSE(fate->pooled);
    CHECK(fate->caveat.empty());

    // Four filters and no more: the six alternatives are inside the one above, not beside it.
    CHECK(p.stats.size() == 5);
    CHECK(filter_for(p, "explicit.stat_3261801346") != nullptr); // Dexterity
    CHECK(filter_for(p, "explicit.stat_328541901") != nullptr);  // Intelligence
    CHECK(filter_for(p, "explicit.stat_3299347043") != nullptr); // maximum Life
    CHECK(filter_for(p, "implicit.stat_2511217560") != nullptr); // the belt's implicit
}

TEST_CASE("a synthesised base resolves under the name beneath its prefix") {
    auto gd = fixture();
    // The client prints a synthesised weapon or armour's type line as "Synthesised <base>", and
    // no bundle carries a base under that whole name — only "Void Sceptre" itself. Reported as
    // Nebulis coming back with no base record at all.
    const Item it = resolved(*gd, capture("unique-nebulis-synthesised.txt"));

    CHECK(it.synthesised);
    REQUIRE(it.base != nullptr);
    CHECK(it.base->name == "Void Sceptre");
    REQUIRE(it.unique_entry != nullptr);
    CHECK(it.unique_entry->name == "Nebulis");

    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);
    CHECK(p.name == "Nebulis");
    // The base's own name, not the printed line: trade's type filter takes "Void Sceptre" and
    // the "Synthesised Item" checkbox separately, which `synthesised_item` above already ticks.
    CHECK(p.type == "Void Sceptre");
    for (const std::string& n : p.notes)
        CHECK(n.find("unrecognised modifier") == std::string::npos);
}

TEST_CASE("a modifier with no roll at all is not read past its em-dash unscalable suffix") {
    auto gd = fixture();
    // A Heist Contract's own boolean affixes print no number, and Advanced Mod Descriptions
    // marks them "\xe2\x80\x94 Unscalable Value" rather than the numeric "(unscalable value)"
    // parenthetical — a spelling nothing stripped, so all four affixes came back unrecognised.
    const Item it = resolved(*gd, capture("heist-contract-rare-smugglers-den.txt"));
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    for (const std::string& n : p.notes)
        CHECK(n.find("unrecognised modifier") == std::string::npos);

    CHECK(filter_for(p, "explicit.stat_4154059009") != nullptr); // Monsters are Hexproof
    CHECK(filter_for(p, "explicit.stat_4056408881") != nullptr); // Reward Rooms increased Monsters
    CHECK(filter_for(p, "explicit.stat_3350803563") != nullptr); // Monsters Poison on Hit
    CHECK(filter_for(p, "explicit.stat_616993076") != nullptr);  // The Ring takes no Cut
}

TEST_CASE("Heist Gear's own boilerplate lines are not unrecognised modifiers") {
    auto gd = fixture();
    // "Any Heist member can equip this item." sits above the requirements and "Can only be
    // equipped to Heist members." sits at the bottom — neither carries a roll, and before the
    // needle both came back as unrecognised modifiers of their own.
    const Item it = resolved(*gd, capture("heist-gear-rare-oblivion.txt"));
    const Derived d = derive(gd.get(), it);
    const SearchPlan p = build_plan(*gd, it, d);

    CHECK(p.strategy == Strategy::Modifiers);
    CHECK(p.category == "heistequipment.heistweapon");
    for (const std::string& n : p.notes) {
        CHECK(n.find("Heist member") == std::string::npos);
        CHECK(n.find("equipped") == std::string::npos);
    }

    CHECK(filter_for(p, "implicit.stat_2162876159") != nullptr); // Projectile Attack Damage
    CHECK(filter_for(p, "explicit.stat_2162876159") != nullptr); // the Poacher's prefix
    CHECK(filter_for(p, "explicit.stat_2697534676") != nullptr); // the Buzzing prefix
    CHECK(filter_for(p, "explicit.stat_4193390599") != nullptr); // Grants Level 10 Purity of Ice

    // "of Personality"'s reduced Hiring Fee is real and still unmatched: the bundle only names
    // job-specific variants ("... for Deception Jobs", "... of Rogues"), not a bare one, and
    // that gap is in the data build rather than in anything this layer decides.
    const bool hiring_fee_noted =
        std::any_of(p.notes.begin(), p.notes.end(), [](const std::string& n) {
            return n.find("reduced Hiring Fee") != std::string::npos;
        });
    CHECK(hiring_fee_noted);
}

