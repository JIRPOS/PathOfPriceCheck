#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

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

std::string capture(const char* name) {
    std::ifstream in(fs::path(PPC_TEST_DATA_DIR) / "items" / name, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// Parse and resolve in one step. The items here are written against the committed fixture
/// bundle, which holds a handful of stats and six bases — enough for every code path.
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

const NumericFilter* numeric_for(const SearchPlan& p, std::string_view key) {
    for (const NumericFilter& f : p.numerics)
        if (f.key == key) return &f;
    return nullptr;
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
    CHECK(p.corrupted == false);

    const StatFilter* life = filter_for(p, "explicit.stat_3299347043");
    REQUIRE(life != nullptr);
    CHECK(life->enabled);
    CHECK(life->min == doctest::Approx(42));
    // Without Advanced Mod Descriptions there is no tier to bound the search with.
    CHECK_FALSE(life->tiered);
    CHECK_FALSE(life->max.has_value());

    REQUIRE(filter_for(p, "explicit.stat_3372524247") != nullptr);
    CHECK(filter_for(p, "explicit.stat_3372524247")->min == doctest::Approx(25));

    // The energy shield mod is not in this slice of the bundle: it has to be reported, not
    // dropped — a silently missing filter reads as a successful price check on a worse item.
    CHECK(p.stats.size() == 2);
    CHECK(p.notes.size() == 1);
    CHECK(p.notes.front().starts_with("unrecognised modifier: 120% increased Energy Shield"));
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
    REQUIRE(d.energy_shield_pct.has_value());
    CHECK(*d.energy_shield_pct == doctest::Approx(0.503).epsilon(0.01));

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
    const SearchPlan p = build_plan(*gd, it, d);

    const StatFilter* life = filter_for(p, "explicit.stat_3299347043");
    REQUIRE(life != nullptr);
    CHECK(life->tiered);
    CHECK(life->min == doctest::Approx(80));
    CHECK(life->max == doctest::Approx(89));
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
    const SearchPlan p = build_plan(*gd, it, d);

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
    const SearchPlan p = build_plan(*gd, it, d);

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
    const SearchPlan p = build_plan(*gd, it, d);
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
    const SearchPlan p = build_plan(*gd, it, d);

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
    CHECK(p.fractured);
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
    CHECK(life->min == doctest::Approx(42));
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
    const SearchPlan p = build_plan(*gd, it, d);

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

TEST_CASE("an unidentified unique says so instead of searching for the wrong thing") {
    auto gd = fixture();
    const Item it = resolved(*gd, R"(Item Class: Boots
Rarity: Unique
Goathide Boots
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
    CHECK(p.name.empty());
    CHECK(p.type == "Goathide Boots");
    REQUIRE(p.notes.size() == 1);
    CHECK(p.notes.front().starts_with("unidentified"));
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
