#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "item/item.hpp"
#include "ninja/ninja.hpp"

using namespace ppc;

namespace {

std::string read(const std::string& rel) {
    std::ifstream in(std::string(PPC_TEST_DATA_DIR) + "/" + rel, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing fixture: ", rel);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// The fixtures are slices of real responses, so a parse failure here is a payload change
/// rather than a bad test.
ninja::Overview load(const std::string& rel, ninja::Feed feed, const char* type) {
    ninja::Overview ov;
    ov.key = ninja::Key{type, "Allflame"};
    ov.feed = feed;
    REQUIRE(ninja::parse_overview(read("ninja/" + rel), feed, ov));
    return ov;
}

item::Item parse_example(const std::string& file) {
    std::optional<item::Item> it = item::parse_item(read("examples/" + file));
    REQUIRE(it.has_value());
    return *it;
}

} // namespace

TEST_CASE("league slugs follow the site, not the league id") {
    CHECK(ninja::league_slug("Allflame") == "allflame");
    CHECK(ninja::league_slug("Standard") == "standard");
    // Hardcore is a suffix on the site: /economy/allflamehc, never /economy/hardcore-allflame.
    CHECK(ninja::league_slug("Hardcore Allflame") == "allflamehc");
    CHECK(ninja::league_slug("Hardcore") == "hardcore");
}

TEST_CASE("urls name the right feed and escape the league") {
    const ninja::Key currency = ninja::currency_key("Hardcore Allflame");
    CHECK(ninja::url(currency) ==
          "https://poe.ninja/poe1/api/economy/exchange/current/overview"
          "?league=Hardcore%20Allflame&type=Currency");
    CHECK(ninja::url(ninja::Key{"UniqueFlask", "Allflame"}) ==
          "https://poe.ninja/poe1/api/economy/stash/current/item/overview"
          "?league=Allflame&type=UniqueFlask");
    // A type outside the table has no URL rather than a guessed one.
    CHECK(ninja::url(ninja::Key{"Nonsense", "Allflame"}).empty());

    CHECK(ninja::page_url(ninja::Key{"UniqueFlask", "Allflame"}, "rumis-concoction") ==
          "https://poe.ninja/poe1/economy/allflame/unique-flasks/rumis-concoction");
    CHECK(ninja::page_url(ninja::Key{"UniqueFlask", "Allflame"}, "").empty());

    // The league is user-supplied and must not be able to name a path.
    CHECK(ninja::cache_name(ninja::Key{"Currency", "../../etc"}).find('/') == std::string::npos);
}

TEST_CASE("the exchange overview carries the rate every price is converted with") {
    const ninja::Overview ov = load("currency.json", ninja::Feed::Exchange, "Currency");
    // The Divine Orb's own line, not the reciprocal in `core.rates` — that one is rounded to
    // four figures and puts a Divine Orb at 0.9995 divine.
    CHECK(ov.chaos_per_divine == doctest::Approx(201.4));

    const ninja::Line* divine = ov.find_id("divine");
    REQUIRE(divine);
    CHECK(divine->name == "Divine Orb");
    CHECK(divine->details_id == "divine-orb");
    CHECK(divine->icon.rfind("https://web.poecdn.com/", 0) == 0);
    CHECK(divine->chaos == doctest::Approx(201.4));
    CHECK(divine->spark.known);
    CHECK(divine->spark.data.size() == 7);

    // A line whose metadata is missing cannot be named or drawn, and is not a price.
    CHECK(ov.find_id("no-such-currency") == nullptr);
}

TEST_CASE("a price is quoted in what a player would say it in") {
    constexpr double kRate = 201.4;
    CHECK(ninja::quote(1.0, kRate).currency == "chaos");
    CHECK(ninja::quote(200.0, kRate).currency == "chaos");
    // At a divine and above, the site and every trade whisper switch over.
    CHECK(ninja::quote(kRate, kRate).currency == "divine");
    CHECK(ninja::quote(kRate, kRate).amount == doctest::Approx(1.0));
    CHECK(ninja::quote(1000.0, kRate).amount == doctest::Approx(4.97));
    // Rounded to what a price is said in, not to what the payload happens to carry.
    CHECK(ninja::quote(0.46663, kRate).amount == doctest::Approx(0.47));
    CHECK(ninja::quote(94668.0, kRate).amount == doctest::Approx(470.0));
    // No rate: everything stays in chaos rather than being divided by zero.
    CHECK(ninja::quote(5000.0, 0).currency == "chaos");
}

TEST_CASE("links only count from five up") {
    CHECK(ninja::max_links("R-G-B-B-B-B") == 6);
    CHECK(ninja::max_links("R-G-B-B-B") == 5);
    CHECK(ninja::max_links("R-G-B-B") == 0);
    CHECK(ninja::max_links("R-G-B-B B-B") == 0);
    CHECK(ninja::max_links("B B B B B B") == 0);
    CHECK(ninja::max_links("") == 0); // no sockets at all is not a link count
}

TEST_CASE("the currency market leads every lookup that has one") {
    ninja::Query q;
    q.league = "Allflame";
    q.strategy = item::Strategy::Unique;
    q.category = "flask";
    std::vector<ninja::Key> keys = ninja::keys_for(q);
    REQUIRE(keys.size() == 2);
    CHECK(keys[0].type == "Currency"); // the rate, always
    CHECK(keys[1].type == "UniqueFlask");

    q.category = "weapon.bow";
    CHECK(ninja::keys_for(q)[1].type == "UniqueWeapon");
    q.category = "armour.quiver";
    CHECK(ninja::keys_for(q)[1].type == "UniqueArmour");
    q.category = "accessory.belt";
    CHECK(ninja::keys_for(q)[1].type == "UniqueAccessory");
    q.category = "jewel.abyss";
    CHECK(ninja::keys_for(q)[1].type == "UniqueJewel");

    q.strategy = item::Strategy::Gem;
    CHECK(ninja::keys_for(q)[1].type == "SkillGem");

    // A currency item is looked for in the market itself first, so there is nothing else to
    // fetch for it unless its name says otherwise.
    q.strategy = item::Strategy::Currency;
    q.category = "currency";
    q.names = {"Divine Orb"};
    CHECK(ninja::keys_for(q).size() == 1);
    q.names = {"Winged Bestiary Scarab"};
    CHECK(ninja::keys_for(q)[1].type == "Scarab");
    q.category = "card";
    q.names = {"The Doctor"};
    CHECK(ninja::keys_for(q)[1].type == "DivinationCard");

    // Everything rolled is priced as its base, which is the only thing poe.ninja knows how to
    // say about one.
    q.strategy = item::Strategy::Modifiers;
    CHECK(ninja::keys_for(q)[1].type == "BaseType");
    q.strategy = item::Strategy::BaseItem;
    CHECK(ninja::keys_for(q)[1].type == "BaseType");

    // A map is priced on neither, so there is nothing to ask and nothing to draw.
    q.strategy = item::Strategy::Unsupported;
    CHECK(ninja::keys_for(q).empty());
}

TEST_CASE("a rolled item is priced as its base — item level and influences, not its name") {
    const ninja::Overview currency = load("currency.json", ninja::Feed::Exchange, "Currency");
    const ninja::Overview bases = load("base-type.json", ninja::Feed::StashItem, "BaseType");

    SUBCASE("the eldritch influences are not base influences and must not be asked for") {
        // A Twilight Regalia at item level 84 carrying Searing Exarch and Eater of Worlds.
        // Those come from implicits rather than from the base, so poe.ninja does not split
        // bases by them — asking for them would match nothing at all.
        const item::Item it = parse_example("item_6.txt");
        item::SearchPlan plan;
        plan.strategy = item::Strategy::Modifiers;
        plan.category = "armour.chest";
        const ninja::Query q = ninja::query_for(it, plan, "Allflame");
        CHECK(q.influences.empty());
        CHECK(q.item_level == 84);

        const ninja::Reference r = ninja::reference_for(q, {&currency, &bases});
        CHECK(r.state == ninja::Reference::State::Priced);
        CHECK(r.price.amount == doctest::Approx(5.0)); // the uninfluenced ilvl-84 line
        CHECK(r.label == "item level 84");
        // The rare's own modifiers are not this: the trade search below prices those.
        CHECK(!r.note.empty());
        CHECK(r.url == "https://poe.ninja/poe1/economy/allflame/base-types/twilight-regalia-84");
    }
    SUBCASE("an influence is matched exactly, never approximately") {
        ninja::Query q;
        q.strategy = item::Strategy::BaseItem;
        q.league = "Allflame";
        q.base_type = "Twilight Regalia";
        q.item_level = 84;
        q.influences = {"Warlord"};
        const ninja::Reference r = ninja::reference_for(q, {&currency, &bases});
        // 1370 chaos against 5 for the uninfluenced one: falling back would be wrong by two
        // orders of magnitude, so an influence poe.ninja does not list is no price at all.
        CHECK(r.price.amount == doctest::Approx(6.8)); // 1370 chaos, in divine
        CHECK(r.label == "item level 84, Warlord");

        q.influences = {"Crusader", "Shaper"}; // order is ours, not poe.ninja's
        CHECK(ninja::reference_for(q, {&currency, &bases}).label == "item level 84, Shaper/Crusader");

        q.influences = {"Elder", "Hunter"}; // a pairing nothing is listed under
        const ninja::Reference none = ninja::reference_for(q, {&currency, &bases});
        CHECK(none.state == ninja::Reference::State::None);
        CHECK(none.note == "no price for a Elder/Hunter one");
    }
    SUBCASE("the best bracket the item has reached, and nothing below the lowest") {
        ninja::Query q;
        q.strategy = item::Strategy::BaseItem;
        q.league = "Allflame";
        q.base_type = "Infiltrator Mitts";

        q.item_level = 100; // above the top bracket, which is an open end
        CHECK(ninja::reference_for(q, {&currency, &bases}).label == "item level 86");
        q.item_level = 84;
        CHECK(ninja::reference_for(q, {&currency, &bases}).label == "item level 84");
        q.item_level = 82;
        CHECK(ninja::reference_for(q, {&currency, &bases}).label == "item level 82");

        // item_7 is a real capture at item level 78 — under everything poe.ninja publishes,
        // which is a fact about the item rather than a gap in the data.
        q.item_level = 78;
        const ninja::Reference r = ninja::reference_for(q, {&currency, &bases});
        CHECK(r.state == ninja::Reference::State::None);
        CHECK(r.note == "ilvl too low (<82)");
    }
    SUBCASE("a base poe.ninja does not price says so") {
        ninja::Query q;
        q.strategy = item::Strategy::BaseItem;
        q.league = "Allflame";
        q.base_type = "Driftwood Wand";
        q.item_level = 84;
        CHECK(ninja::reference_for(q, {&currency, &bases}).state == ninja::Reference::State::None);
    }
}

TEST_CASE("a unique with one line is priced outright") {
    const ninja::Overview currency = load("currency.json", ninja::Feed::Exchange, "Currency");
    const ninja::Overview flasks = load("unique-flask.json", ninja::Feed::StashItem, "UniqueFlask");

    const item::Item it = parse_example("item_1.txt"); // Rumi's Concoction, Granite Flask
    item::SearchPlan plan;
    plan.strategy = item::Strategy::Unique;
    plan.category = "flask";
    plan.name = it.name;
    const ninja::Query q = ninja::query_for(it, plan, "Allflame");

    const ninja::Reference r = ninja::reference_for(q, {&currency, &flasks});
    CHECK(r.state == ninja::Reference::State::Priced);
    CHECK(r.price.currency == "chaos");
    CHECK(r.price.amount == doctest::Approx(4.0));
    // A Replica is a different item that happens to share the wording, so the name match is
    // exact rather than a prefix.
    CHECK(r.url == "https://poe.ninja/poe1/economy/allflame/unique-flasks/rumis-concoction");
}

TEST_CASE("a unique's variant is read off the modifiers the copy in hand rolled") {
    const ninja::Overview currency = load("currency.json", ninja::Feed::Exchange, "Currency");
    const ninja::Overview armour = load("unique-armour.json", ninja::Feed::StashItem, "UniqueArmour");

    // Ralakesh's Impatience is three lines — Power, Endurance, Frenzy — 805, 133 and 75 chaos,
    // and this copy says outright which it is.
    const item::Item it = parse_example("item_5.txt");
    item::SearchPlan plan;
    plan.strategy = item::Strategy::Unique;
    plan.category = "armour.boots";
    plan.name = it.name;
    const ninja::Query q = ninja::query_for(it, plan, "Allflame");

    const ninja::Reference r = ninja::reference_for(q, {&currency, &armour});
    CHECK(r.state == ninja::Reference::State::Priced);
    CHECK(r.label == "Power");
    CHECK(r.price.currency == "divine");
    CHECK(r.price.amount == doctest::Approx(4.0));
}

TEST_CASE("variants nothing in the clipboard tells apart are reported, never guessed") {
    const ninja::Overview currency = load("currency.json", ninja::Feed::Exchange, "Currency");
    const ninja::Overview acc =
        load("unique-accessory.json", ninja::Feed::StashItem, "UniqueAccessory");

    ninja::Query q;
    q.strategy = item::Strategy::Unique;
    q.league = "Allflame";
    q.category = "accessory.belt";
    q.names = {"Mageblood"};
    q.base_type = "Heavy Belt";
    // Mageblood's variants differ by "Leftmost N Magic Utility Flasks", and poe.ninja publishes
    // no modifiers at all for the dearest of them — so the item saying "4" cannot rule the
    // others out, and a line with nothing to compare must not win by default.
    q.mods = {"+45 to Dexterity", "+22% to Fire Resistance", "+21% to Cold Resistance",
              "Magic Utility Flasks cannot be Used",
              "Leftmost 4 Magic Utility Flasks constantly apply their Flask Effects to you",
              "Magic Utility Flask Effects cannot be removed"};

    const ninja::Reference r = ninja::reference_for(q, {&currency, &acc});
    CHECK(r.state == ninja::Reference::State::Ambiguous);
    CHECK(r.variants.size() == 4);
    // Cheapest first, and the span is what the row states: guessing "5 Flasks" would be a
    // twenty-fold error on a Mageblood that is not one.
    CHECK(r.variants.front().label == "2 Flasks");
    CHECK(r.variants.back().label == "5 Flasks");
    CHECK(r.price.amount < r.high.amount);
    // Both ends in the same currency, or the pair is not a span.
    CHECK(r.price.currency == "divine");
    CHECK(r.high.currency == "divine");
    CHECK(!r.url.empty());
}

TEST_CASE("a gem is priced at the nearest tier poe.ninja publishes") {
    const ninja::Overview currency = load("currency.json", ninja::Feed::Exchange, "Currency");
    const ninja::Overview gems = load("skill-gem.json", ninja::Feed::StashItem, "SkillGem");

    ninja::Query q;
    q.strategy = item::Strategy::Gem;
    q.league = "Allflame";
    q.names = {"Spell Echo Support"};

    SUBCASE("an exact tier is priced as itself") {
        q.gem_level = 20;
        q.gem_quality = 20;
        const ninja::Reference r = ninja::reference_for(q, {&currency, &gems});
        CHECK(r.state == ninja::Reference::State::Priced);
        CHECK(r.label == "20/20");
        CHECK(r.price.amount == doctest::Approx(70.0));
        CHECK(r.note.empty());
    }
    SUBCASE("anything between tiers falls back to the best one it has reached") {
        q.gem_level = 16;
        q.gem_quality = 0;
        const ninja::Reference r = ninja::reference_for(q, {&currency, &gems});
        CHECK(r.state == ninja::Reference::State::Priced);
        CHECK(r.label == "1"); // level 1, no quality — the only tier at or below it
        CHECK(!r.note.empty());
    }
    SUBCASE("corruption is a hard split, not a preference") {
        q.gem_level = 21;
        q.gem_quality = 20;
        q.corrupted = true;
        const ninja::Reference r = ninja::reference_for(q, {&currency, &gems});
        CHECK(r.label == "21/20c");
        CHECK(r.price.currency == "divine"); // 242.8 chaos is over a divine
        CHECK(r.price.amount == doctest::Approx(1.21));
    }
}

TEST_CASE("a currency item is answered out of the market itself") {
    const ninja::Overview currency = load("currency.json", ninja::Feed::Exchange, "Currency");

    const item::Item it = parse_example("item_8.txt"); // a stack of Divine Orbs
    item::SearchPlan plan;
    plan.strategy = item::Strategy::Currency;
    plan.category = "currency";
    const ninja::Query q = ninja::query_for(it, plan, "Allflame");

    const ninja::Reference r = ninja::reference_for(q, {&currency});
    CHECK(r.state == ninja::Reference::State::Priced);
    // Per orb, not per stack: that is what "a divine" means.
    CHECK(r.price.currency == "divine");
    CHECK(r.price.amount == doctest::Approx(1.0));
    CHECK(r.url == "https://poe.ninja/poe1/economy/allflame/currency/divine-orb");
}

TEST_CASE("a league poe.ninja has no economy for says so") {
    // An unknown league answers 200 with a well-formed but empty payload, which is the
    // difference between "no prices here" and "the response was broken".
    ninja::Overview empty;
    empty.key = ninja::currency_key("SSF Allflame");
    REQUIRE(ninja::parse_overview(
        R"({"core":{"items":[],"rates":{},"primary":"chaos"},"lines":[],"items":[]})",
        ninja::Feed::Exchange, empty));
    CHECK(empty.chaos_per_divine == 0);

    ninja::Query q;
    q.strategy = item::Strategy::Currency;
    q.league = "SSF Allflame";
    q.names = {"Divine Orb"};
    const ninja::Reference r = ninja::reference_for(q, {&empty});
    CHECK(r.state == ninja::Reference::State::None);
    CHECK(r.note.find("SSF Allflame") != std::string::npos);
}
