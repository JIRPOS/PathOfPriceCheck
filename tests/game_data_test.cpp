#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include "data/game_data.hpp"
#include "data/stat_normalize.hpp"

namespace fs = std::filesystem;
using namespace ppc::data;

namespace {

/// A committed slice of a real bundle — small enough to read in a diff, real enough that
/// the record shapes are the ones the builder actually emits.
std::shared_ptr<GameData> fixture() {
    std::string err;
    auto gd = GameData::open(fs::path(PPC_TEST_DATA_DIR) / "bundle", "en", &err);
    REQUIRE_MESSAGE(gd != nullptr, "opening the fixture bundle failed: " << err);
    return gd;
}

} // namespace

TEST_CASE("open reports what is missing rather than crashing") {
    std::string err;
    CHECK(GameData::open(fs::path(PPC_TEST_DATA_DIR) / "no-such-bundle", "en", &err) == nullptr);
    CHECK_FALSE(err.empty());
}

TEST_CASE("a plain stat resolves to its trade hash") {
    auto gd = fixture();
    const Stat* s = gd->find_stat("# to maximum Life", ModType::Explicit);
    REQUIRE(s != nullptr);
    CHECK(s->ref == "# to maximum Life");
    CHECK(s->trade_ids(ModType::Explicit) ==
          std::vector<std::string>{"explicit.stat_3299347043"});
    // The same underlying stat is indexed separately per namespace.
    CHECK(s->trade_ids(ModType::Implicit) ==
          std::vector<std::string>{"implicit.stat_3299347043"});
}

TEST_CASE("mod type picks between namespaces of one wording") {
    auto gd = fixture();
    const Stat* e = gd->find_stat("#% increased Physical Damage", ModType::Explicit);
    REQUIRE(e != nullptr);
    CHECK(e->trade_ids(ModType::Explicit).front() == "explicit.stat_1509134228");
    CHECK(e->trade_ids(ModType::Crafted).front() == "crafted.stat_1509134228");
}

TEST_CASE("negate and fixed-value wordings reach the same record") {
    auto gd = fixture();
    const Stat* inc = gd->find_stat("#% increased Physical Damage", ModType::Explicit);
    const Stat* red = gd->find_stat("#% reduced Physical Damage", ModType::Explicit);
    const Stat* none = gd->find_stat("No Physical Damage", ModType::Explicit);
    REQUIRE(inc != nullptr);
    CHECK(red == inc);
    CHECK(none == inc);

    // ...and each wording carries how it should be interpreted.
    const StatMatcher* m_red = inc->matcher_for("#% reduced Physical Damage");
    REQUIRE(m_red != nullptr);
    CHECK(m_red->negate);

    const StatMatcher* m_none = inc->matcher_for("No Physical Damage");
    REQUIRE(m_none != nullptr);
    REQUIRE(m_none->value.has_value());
    CHECK(*m_none->value == doctest::Approx(-100.0));

    const StatMatcher* m_inc = inc->matcher_for("#% increased Physical Damage");
    REQUIRE(m_inc != nullptr);
    CHECK_FALSE(m_inc->negate);
    CHECK_FALSE(m_inc->value.has_value());
}

TEST_CASE("a wording the bundle does not have returns null") {
    auto gd = fixture();
    CHECK(gd->find_stat("#% increased Nonsense", ModType::Explicit) == nullptr);
    CHECK(gd->find_stats("#% increased Nonsense").empty());
}

TEST_CASE("asking for a namespace the stat is not searchable in returns null") {
    auto gd = fixture();
    // maximum Life is not an enchantment.
    CHECK(gd->find_stat("# to maximum Life", ModType::Enchant) == nullptr);
}

TEST_CASE("lookup by canonical wording") {
    auto gd = fixture();
    const Stat* s = gd->find_stat_by_ref("# to maximum Life");
    REQUIRE(s != nullptr);
    CHECK(s->ref == "# to maximum Life");
    CHECK(gd->find_stat_by_ref("nothing at all") == nullptr);
}

TEST_CASE("base types carry what disambiguates them") {
    auto gd = fixture();
    const auto rings = gd->find_bases(Namespace::Item, "Two-Stone Ring");
    REQUIRE_FALSE(rings.empty());
    CHECK(rings.front()->category == "Rings");

    // Two-Toned Boots is the family that can only be told apart by which defences it rolls.
    const auto boots = gd->find_bases(Namespace::Item, "Two-Toned Boots");
    REQUIRE_FALSE(boots.empty());
    CHECK(boots.front()->evasion.has_value());
    CHECK(boots.front()->energy_shield.has_value());
    CHECK_FALSE(boots.front()->armour.has_value());
}

TEST_CASE("uniques record the base they roll on") {
    auto gd = fixture();
    const auto u = gd->find_bases(Namespace::Unique, "Abberath's Hooves");
    REQUIRE_FALSE(u.empty());
    CHECK(u.front()->unique_base == "Goathide Boots");
}

TEST_CASE("a base answers with the uniques that drop on it") {
    auto gd = fixture();
    CHECK(gd->has_unique_bases());
    // The lookup an unidentified unique is read with: it prints its base and nothing else.
    const auto gloves = gd->find_uniques_on_base("Goathide Gloves");
    REQUIRE(gloves.size() == 2);
    CHECK(gloves[0]->name == "Hrimsorrow");
    CHECK(gloves[1]->name == "Hrimburn");
    for (const BaseType* u : gloves) CHECK(u->ns == Namespace::Unique);

    // One is the ordinary case and is what lets the app take the answer itself.
    const auto boots = gd->find_uniques_on_base("Riveted Boots");
    REQUIRE(boots.size() == 1);
    CHECK(boots.front()->name == "Ralakesh's Impatience");

    // A base nothing drops on, and the base's own record, which lives under a different key.
    CHECK(gd->find_uniques_on_base("Two-Toned Boots").empty());
    CHECK(gd->find_uniques_on_base("Hrimsorrow").empty());
}

TEST_CASE("a unique carries the path its artwork is served at") {
    auto gd = fixture();
    const auto u = gd->find_bases(Namespace::Unique, "Ralakesh's Impatience");
    REQUIRE_FALSE(u.empty());
    CHECK(u.front()->art == "Art/2DItems/Armours/Boots/RalakeshsImpatience.png");
    // The size is the base's, since a unique is not a base type and has none of its own.
    CHECK(item_image_url(u.front()->art, 2, 2) ==
          "https://web.poecdn.com/image/Art/2DItems/Armours/Boots/RalakeshsImpatience.png"
          "?w=2&h=2&scale=1");
    // Unknown size is the same picture unscaled, never a URL with empty parameters in it.
    CHECK(item_image_url(u.front()->art) ==
          "https://web.poecdn.com/image/Art/2DItems/Armours/Boots/RalakeshsImpatience.png");

    // Two uniques sharing one picture is what the game data says, not a join gone wrong.
    CHECK(gd->find_bases(Namespace::Unique, "Hrimburn").front()->art ==
          gd->find_bases(Namespace::Unique, "Hrimsorrow").front()->art);

    // No picture is drawn for anything else, and a missing path is never turned into a URL —
    // that would be a 404 fetched once a frame.
    CHECK(gd->find_bases(Namespace::Item, "Riveted Boots").front()->art.empty());
    CHECK(item_image_url("", 2, 2).empty());
}

TEST_CASE("the namespace is part of the key") {
    auto gd = fixture();
    // A unique name must not resolve as a plain base.
    CHECK(gd->find_bases(Namespace::Item, "Abberath's Hooves").empty());
}

TEST_CASE("a unique's modifiers say which are fixed and which come from a pool") {
    auto gd = fixture();
    REQUIRE(gd->has_unique_mods());
    const UniqueMods* u = gd->find_unique_mods("Ralakesh's Impatience");
    REQUIRE(u != nullptr);
    CHECK(u->base == "Riveted Boots");

    // Four modifiers every copy has, and one of three charge modifiers — each rolling 1..1,
    // which is the whole reason this dataset exists: no printed range could reveal it.
    CHECK(u->fixed.size() == 4);
    REQUIRE(u->pools.size() == 1);
    CHECK(u->pools.front().hint == "Random charge modifier");
    CHECK(u->pools.front().mods.size() == 3);
    // The source states no count for this pool, which means "at least one, unknown".
    CHECK_FALSE(u->pools.front().count.has_value());

    REQUIRE_FALSE(u->pools.front().mods.front().filters.empty());
    const UniqueModFilter& f = u->pools.front().mods.front().filters.front();
    CHECK(f.trade_id == "explicit.stat_1090017486");
    CHECK(f.ref == "Count as having maximum number of Endurance Charges");
    REQUIRE(f.ranges.size() == 1);
    CHECK(f.ranges.front().second == doctest::Approx(1));
}

TEST_CASE("a unique the data does not cover is null, not an error") {
    auto gd = fixture();
    // 43 of trade's unique names have no record and a new league outruns the source by days.
    CHECK(gd->find_unique_mods("Abberath's Hooves") == nullptr);
    CHECK(gd->find_unique_mods("") == nullptr);
}

TEST_CASE("a pool stated only in prose is kept, so the app can say what it is leaving out") {
    auto gd = fixture();
    const UniqueMods* u = gd->find_unique_mods("That Which Was Taken");
    REQUIRE(u != nullptr);
    CHECK(u->fixed.empty());
    CHECK(u->pools.empty());
    CHECK(u->unlisted == std::vector<std::string>{"4 random Charm modifiers"});
}

TEST_CASE("the attribution travels with the bundle") {
    auto gd = fixture();
    CHECK(gd->unique_mods_attribution() == "poewiki.net, CC BY-NC 3.0");
}

TEST_CASE("Item Class maps to a trade category") {
    auto gd = fixture();
    CHECK(gd->trade_category_for("Rings") == "accessory.ring");
    CHECK(gd->trade_category_for("Body Armours") == "armour.chest");
    // An unknown class is not an error; it just does not constrain the search.
    CHECK(gd->trade_category_for("Nonexistent Class").empty());
}

TEST_CASE("a pool answers for a whole mod domain, not for an item") {
    auto gd = fixture();
    CHECK(gd->has_mod_pools());
    const std::span<const PoolMod* const> maps = gd->mod_pool(5);
    REQUIRE(maps.size() == 4);
    // File order, which is the order the game's own table holds the modifiers in.
    CHECK(maps.front()->name == "Ceremonial");
    CHECK(maps.front()->gen == 1);
    // Every row behind the wording, tiers and side-area twin alike: it is provenance.
    CHECK(maps.front()->tiers == 4);
    CHECK(maps.front()->mods.front() == "MapTotems");
    // Three pools, one file: the domain is what separates them, and asking for one of them
    // never brings back another's entries even where the wording is the same string.
    CHECK(gd->mod_pool(39).size() == 2);
    CHECK(gd->mod_pool(22).size() == 7);
    // A domain the bundle publishes no pool for is empty, which is not the same answer as a
    // bundle that has no pools at all — `has_mod_pools()` is what tells those apart.
    CHECK(gd->mod_pool(1).empty());
}

TEST_CASE("a pooled modifier carries the span of its tiers, or no bounds at all") {
    auto gd = fixture();
    const std::vector<const PoolMod*> hinder =
        gd->find_pool_mods(5, "Monsters have #% chance to Hinder on Hit with Spells");
    REQUIRE(hinder.size() == 1);
    REQUIRE(hinder.front()->stats.size() == 1);
    const PoolStat& s = hinder.front()->stats.front();
    CHECK(s.trade_id == "explicit.stat_962720646");
    CHECK(s.min == doctest::Approx(100));
    CHECK(s.max == doctest::Approx(100));

    // A wording that prints no number has no bounds, which the reader must not confuse with
    // bounds it failed to read.
    const std::vector<const PoolMod*> totems = gd->find_pool_mods(5, "Area contains many Totems");
    REQUIRE(totems.size() == 1);
    CHECK_FALSE(totems.front()->stats.front().min.has_value());
}

TEST_CASE("the domain is part of what a pool lookup asks for") {
    auto gd = fixture();
    // A map and a chart word this modifier identically and are separate pools. Answering with
    // both would offer a chart's affix for a map, which is what the domain keeps apart.
    const std::string_view wording = "Monsters have #% chance to Hinder on Hit with Spells";
    REQUIRE(gd->find_pool_mods(5, wording).size() == 1);
    REQUIRE(gd->find_pool_mods(39, wording).size() == 1);
    CHECK(gd->find_pool_mods(5, wording).front()->domain == 5);
    CHECK(gd->find_pool_mods(39, wording).front()->domain == 39);
    // A wording no entry in that domain prints. Normal, never an error: the pool describes
    // what spawns naturally and an item can print more than that.
    CHECK(gd->find_pool_mods(5, "# to maximum Life").empty());
}

TEST_CASE("a pooled modifier printing two wordings carries one entry per wording") {
    auto gd = fixture();
    const std::vector<const PoolMod*> found = gd->find_pool_mods(39, "Monsters cannot be Stunned");
    REQUIRE(found.size() == 1);
    REQUIRE(found.front()->stats.size() == 2);
    // Only one of the two prints a number, and only one is searchable: "#% more Monster Life"
    // is a wording trade indexes under two hashes, so the build refuses to pick one rather
    // than filtering on the wrong stat.
    CHECK(found.front()->stats[0].trade_id == "explicit.stat_1041951480");
    CHECK(found.front()->stats[1].ref == "#% more Monster Life");
    CHECK(found.front()->stats[1].trade_id.empty());
    CHECK(found.front()->stats[1].min == doctest::Approx(10));
    // It is still an entry: a pool is rated, not searched.
    CHECK(gd->find_pool_mods(39, "#% more Monster Life").size() == 1);
}

TEST_CASE("a corruption implicit is filed in the implicit namespace") {
    auto gd = fixture();
    const std::vector<const PoolMod*> iiq = gd->find_pool_mods(5, "#% Item Quantity");
    REQUIRE(iiq.size() == 1);
    CHECK(iiq.front()->gen == 5);
    CHECK(iiq.front()->stats.front().trade_id == "implicit.stat_2023217031");
    // Nothing names this one: only the affixes carry an affix name.
    CHECK(iiq.front()->name.empty());
}

TEST_CASE("which pool an item rolls from is the base's answer, then its class's") {
    auto gd = fixture();
    const std::vector<const BaseType*> rings = gd->find_bases(Namespace::Item, "Two-Stone Ring");
    REQUIRE_FALSE(rings.empty());
    CHECK(rings.front()->mod_domain == 1);
    CHECK(gd->mod_domain_for(rings.front(), "Rings") == 1);

    // The case the fallback exists for: trade lists all 491 maps under one entry whose game
    // row is a stand-in sitting with the stackable currency, so the record states no domain
    // and the class is what knows a map rolls from 5.
    const std::vector<const BaseType*> maps = gd->find_bases(Namespace::Item, "Map");
    REQUIRE_FALSE(maps.empty());
    CHECK(maps.front()->mod_domain == 0);
    CHECK(gd->mod_domain_for(maps.front(), "Maps") == 5);

    // A chart's own record answers, and its class agrees.
    const std::vector<const BaseType*> chart =
        gd->find_bases(Namespace::Item, "Coral Reef Chart");
    REQUIRE_FALSE(chart.empty());
    CHECK(gd->mod_domain_for(chart.front(), "Chart") == 39);

    // Neither says: a unique carries no domain and Jewels holds two, so nothing is claimed.
    CHECK(gd->mod_domain_for(nullptr, "Jewels") == 0);
    CHECK(gd->mod_domain_for(nullptr, "Nonexistent Class") == 0);
}

TEST_CASE("a base can be named by its reference name") {
    auto gd = fixture();
    // How the app names a record the clipboard did not print — the blighted-map redirect is
    // the one that matters, since "Map" is a reference name and not what a translated client
    // would have shown.
    const std::vector<const BaseType*> maps = gd->find_bases_by_ref(Namespace::Item, "Map");
    REQUIRE_FALSE(maps.empty());
    CHECK(maps.front()->name == "Map");
    CHECK(gd->find_bases_by_ref(Namespace::Item, "Not A Base").empty());
    // The namespace is part of the key here as it is for the name index.
    CHECK(gd->find_bases_by_ref(Namespace::Unique, "Map").empty());
}

TEST_CASE("a bundle with no lexicon reads its items in English") {
    auto gd = fixture();
    // Every bundle published so far is this one's shape, which is why the English table is
    // compiled in rather than required from the bundle.
    CHECK_FALSE(gd->has_lexicon());
    CHECK(gd->lexicon().language() == "en");
    CHECK(gd->lexicon().term(Term::RarityLabel) == "Rarity");
    // And it declares which languages it has assets for, which is what Settings offers.
    REQUIRE_FALSE(gd->languages().empty());
    CHECK(gd->languages().front() == "en");
}

TEST_CASE("normalizer output feeds straight into lookup") {
    auto gd = fixture();
    // The end-to-end path the parser will take: clipboard line -> candidates -> stat.
    const Stat* found = nullptr;
    for (const std::string& c : candidates("+42 to maximum Life")) {
        if ((found = gd->find_stat(c, ModType::Explicit))) break;
    }
    REQUIRE(found != nullptr);
    CHECK(found->trade_ids(ModType::Explicit).front() == "explicit.stat_3299347043");
}
