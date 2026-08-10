#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "item/item.hpp"
#include "trade/currency.hpp"
#include "trade/query.hpp"
#include "parse_en.hpp"
#include "util/base64.hpp"

using namespace ppc::trade;
using ppc::item::SearchPlan;
using ppc::item::Strategy;
using json = nlohmann::json;

namespace {

/// A plan is a plain struct, so the query builder can be exercised without a data bundle —
/// which is the point of it living in ppc_core.
SearchPlan rare_plan() {
    SearchPlan p;
    p.strategy = Strategy::Modifiers;
    p.category = "armour.helmet";
    // No `type`: the plan leaves it empty for a rare, and this layer sends what it was given.
    p.options = {{"corrupted", "Corrupted", "false", "no"}};
    ppc::item::StatFilter f;
    f.id = "explicit.stat_3299347043";
    f.enabled = true;
    f.min = 77;
    f.max = 90;
    p.stats.push_back(f);
    return p;
}

json query_of(const SearchPlan& p) { return json::parse(build_query(p))["query"]; }

} // namespace

TEST_CASE("a rare is searched on its modifiers, not on its base") {
    const json q = query_of(rare_plan());
    CHECK(q["status"]["option"] == "securable"); // Instant Buyout, the default
    CHECK(q["filters"]["type_filters"]["filters"]["category"]["option"] == "armour.helmet");
    CHECK(q["filters"]["type_filters"]["filters"]["rarity"]["option"] == "nonunique");
    // Deliberately absent: a rare is bought for its mods and the category already says where
    // those can live. Constraining the base would drop every other helmet with them.
    CHECK_FALSE(q.contains("type"));
    CHECK_FALSE(q.contains("name"));
}

TEST_CASE("a base named under Modifiers is sent — the flask case") {
    // Trade has one category for every flask, so a search that names no base prices a
    // Quicksilver Flask against the Ruby Flasks that share its suffix. The plan decides that;
    // this layer must not drop what it named.
    SearchPlan p = rare_plan();
    p.category = "flask";
    p.type = "Quicksilver Flask";
    const json q = query_of(p);
    CHECK(q["type"] == "Quicksilver Flask");
    CHECK_FALSE(q.contains("name"));

    const json& stats = q["stats"][0]["filters"];
    REQUIRE(stats.size() == 1);
    CHECK(stats[0]["id"] == "explicit.stat_3299347043");
    CHECK(stats[0]["disabled"] == false);
    CHECK(stats[0]["value"]["min"] == 77);
    CHECK(stats[0]["value"]["max"] == 90);
    CHECK(q["filters"]["misc_filters"]["filters"]["corrupted"]["option"] == "false");
}

TEST_CASE("listing status: Instant Buyout by default, the site's own vocabulary otherwise") {
    const SearchPlan p = rare_plan();
    CHECK(kDefaultStatus == "securable");
    CHECK(query_of(p)["status"]["option"] == "securable");
    CHECK(json::parse(build_query(p, "online"))["query"]["status"]["option"] == "online");
    CHECK(json::parse(build_query(p, "any"))["query"]["status"]["option"] == "any");
    // A hand-edited config must not turn every search into "Unknown status type".
    CHECK(json::parse(build_query(p, "nonsense"))["query"]["status"]["option"] == "securable");

    CHECK(valid_status("available"));
    CHECK_FALSE(valid_status("priced"));
    CHECK(status_label("securable") == "Instant Buyout");
    CHECK(status_label("online") == "In Person (Online)");
    CHECK(status_label("made up") == "made up");
    CHECK(status_options().size() == 5);
}

TEST_CASE("how many listings to fetch, and what each choice costs") {
    CHECK(kDefaultResultCount == 20);
    CHECK(result_counts() == std::vector<int>{10, 20, 50, 100});
    CHECK(valid_result_count(50));
    CHECK_FALSE(valid_result_count(30)); // a hand-edited config must not invent a batch size

    // Every ten listings is one more fetch request, and the binding policy is 50 per five
    // minutes. That is the whole reason this is a choice.
    CHECK(fetch_requests(10) == 1);
    CHECK(fetch_requests(20) == 2);
    CHECK(fetch_requests(50) == 5);
    CHECK(fetch_requests(100) == 10);
}

TEST_CASE("only ticked filters are sent") {
    SearchPlan p = rare_plan();
    p.stats[0].enabled = false;
    CHECK(query_of(p)["stats"][0]["filters"].empty());
}

TEST_CASE("a hidden filter is a row, not a search — the tick is the only gate") {
    // `StatFilter::hidden` says where the panel draws the row (behind the expandable section
    // at the foot of the list), and this layer must not read it at all: a hidden filter the
    // user ticked is exactly as much a filter as any other, or the section would be decoration.
    SearchPlan p = rare_plan();
    p.stats[0].hidden = true;
    p.stats[0].enabled = false;
    CHECK(query_of(p)["stats"][0]["filters"].empty());

    p.stats[0].enabled = true;
    const json f = query_of(p)["stats"][0]["filters"];
    REQUIRE(f.size() == 1);
    CHECK(f[0]["id"] == "explicit.stat_3299347043");
    CHECK(f[0]["value"]["min"] == 77);
}

TEST_CASE("an inverted stat flips sign and swaps its bounds") {
    SearchPlan p = rare_plan();
    p.stats[0].inverted = true;
    const json v = query_of(p)["stats"][0]["filters"][0]["value"];
    // The interval turns end for end: what the game prints as 77..90 the site indexes as
    // -90..-77, and a floor becomes a ceiling.
    CHECK(v["min"] == -90);
    CHECK(v["max"] == -77);
}

TEST_CASE("an unbounded filter stays unbounded on the side it never had") {
    SearchPlan p = rare_plan();
    p.stats[0].max.reset();
    const json v = query_of(p)["stats"][0]["filters"][0]["value"];
    CHECK(v.contains("min"));
    CHECK_FALSE(v.contains("max"));

    p.stats[0].inverted = true;
    const json iv = query_of(p)["stats"][0]["filters"][0]["value"];
    CHECK_FALSE(iv.contains("min"));
    CHECK(iv["max"] == -77);
}

TEST_CASE("a unique is searched by name, with the base as its type") {
    SearchPlan p;
    p.strategy = Strategy::Unique;
    p.rarity = "unique"; // the plan's own statement of which market this is
    p.name = "Tabula Rasa";
    p.type = "Simple Robe";
    p.category = "armour.chest";
    const json q = query_of(p);
    CHECK(q["name"] == "Tabula Rasa");
    CHECK(q["type"] == "Simple Robe");
    CHECK(q["filters"]["type_filters"]["filters"]["rarity"]["option"] == "unique");
}

TEST_CASE("a map's filters go in map_filters, and its tier is exact") {
    SearchPlan p;
    p.strategy = Strategy::Map;
    p.category = "map";
    p.type = "Map";
    p.discriminator = "map";
    ppc::item::NumericFilter tier{"map_tier", "Map Tier", 16, 16, true, 0, {}};
    ppc::item::NumericFilter iiq{"map_iiq", "Item Quantity", 104, std::nullopt, true, 0, {}};
    ppc::item::NumericFilter iir{"map_iir", "Item Rarity", 63, std::nullopt, false, 0, {}};
    p.numerics = {tier, iiq, iir};
    const json q = query_of(p);
    const json& f = q["filters"]["map_filters"]["filters"];
    // Exact, not a floor: a tier-16 map is a different area from a tier-14 one, not a better it.
    CHECK(f["map_tier"]["min"] == 16);
    CHECK(f["map_tier"]["max"] == 16);
    CHECK(f["map_iiq"]["min"] == 104);
    CHECK_FALSE(f.contains("map_iir")); // unticked, so never sent
    // "Map" is the base of every tiered map *and* the prefix of every unique map's entry, so
    // the discriminator has to ride on the type here rather than only on a name.
    CHECK(q["type"]["option"] == "Map");
    CHECK(q["type"]["discriminator"] == "map");
}

TEST_CASE("a unique map is searched as a unique, though it is planned as a map") {
    SearchPlan p;
    p.strategy = Strategy::Map;
    p.rarity = "unique";
    p.category = "map";
    p.name = "Olmec's Sanctum";
    p.type = "Map";
    p.discriminator = "map";
    const json q = query_of(p);
    CHECK(q["filters"]["type_filters"]["filters"]["rarity"]["option"] == "unique");
    CHECK(q["name"]["option"] == "Olmec's Sanctum");
    CHECK(q["name"]["discriminator"] == "map");
    CHECK(q["type"]["discriminator"] == "map");
}

TEST_CASE("a gem's level and quality are misc filters, and both are exact") {
    SearchPlan p;
    p.strategy = Strategy::Gem;
    p.category = "gem.activegem";
    p.type = "Raise Zombie";
    p.discriminator = "alt_y"; // a transfigured gem: trade knows it by the skill it alters
    p.options = {{"corrupted", "Corrupted", "false", "no"}};
    p.numerics = {{"gem_level", "Gem Level", 20, 20, true, 0, {}},
                  {"quality", "Quality", 20, 20, true, 0, {}}};
    const json q = query_of(p);

    CHECK(q["type"]["option"] == "Raise Zombie");
    CHECK(q["type"]["discriminator"] == "alt_y");
    const json& f = q["filters"]["misc_filters"]["filters"];
    // Both exact: a level 21 gem is not a better level 20 one, and the corruption flag below
    // is what says which of the two markets is being asked.
    CHECK(f["gem_level"]["min"] == 20);
    CHECK(f["gem_level"]["max"] == 20);
    CHECK(f["quality"]["min"] == 20);
    CHECK(f["quality"]["max"] == 20);
    CHECK(f["corrupted"]["option"] == "false");
    CHECK(q["stats"][0]["filters"].empty()); // a gem has nothing to filter on
}

TEST_CASE("blight and a Valdo map's payout are map options, not type terms") {
    SearchPlan p;
    p.strategy = Strategy::Map;
    p.category = "map";
    p.type = "Map";
    p.discriminator = "map";
    p.options.push_back({"map_blighted", "Blighted", "true", "yes"});
    const json f = query_of(p)["filters"]["map_filters"]["filters"];
    // A type of "Blighted Map" is accepted by the site and matches nothing at all, so the
    // type stays the base every map shares and the flag says which kind it is.
    CHECK(f["map_blighted"]["option"] == "true");
    CHECK_FALSE(f.contains("map_uberblighted"));

    // Never asked for in the negative: the two are mutually exclusive, so a blighted map's
    // own search already excludes the ravaged ones.
    p.options.pop_back();
    p.options.push_back({"map_uberblighted", "Blight-ravaged", "true", "yes"});
    const json g = query_of(p)["filters"]["map_filters"]["filters"];
    CHECK(g["map_uberblighted"]["option"] == "true");
    CHECK_FALSE(g.contains("map_blighted"));

    // The reward is an option over the unique list — the "Foil " the game prints in front of
    // it is the plan's to strip, and this layer sends the name it was given.
    p.options.pop_back();
    p.options.push_back({"map_completion_reward", "Reward", "Hrimsorrow", "Hrimsorrow"});
    CHECK(query_of(p)["filters"]["map_filters"]["filters"]["map_completion_reward"]["option"] ==
          "Hrimsorrow");
}

TEST_CASE("a modifier the item does not have is asked for as a second, negated group") {
    SearchPlan p;
    p.strategy = Strategy::Map;
    p.category = "map";
    ppc::item::StatFilter present;
    present.id = "explicit.stat_1";
    present.enabled = true;
    ppc::item::StatFilter absent;
    absent.id = "explicit.stat_1095765106";
    absent.enabled = true;
    absent.negated = true;
    p.stats = {present, absent};

    const json stats = query_of(p)["stats"];
    // Two groups, not one: the site spells an absence as a group of type "not", and a Valdo
    // map that does not void the character who dies in it is bought for exactly that.
    REQUIRE(stats.size() == 2);
    CHECK(stats[0]["type"] == "and");
    REQUIRE(stats[0]["filters"].size() == 1);
    CHECK(stats[0]["filters"][0]["id"] == "explicit.stat_1");
    CHECK(stats[1]["type"] == "not");
    REQUIRE(stats[1]["filters"].size() == 1);
    CHECK(stats[1]["filters"][0]["id"] == "explicit.stat_1095765106");
    // No bounds ride along: there is no roll to compare, only presence.
    CHECK_FALSE(stats[1]["filters"][0].contains("value"));

    // And with nothing absent there is no second group at all.
    p.stats = {present};
    CHECK(query_of(p)["stats"].size() == 1);
}

TEST_CASE("a gem the plan could not name is not searched at all") {
    // Every other strategy has modifiers or a category to fall back on. A gem has neither, so
    // the search would be every gem in the game at this level and its cheapest listing would
    // read as this gem's price.
    SearchPlan p;
    p.strategy = Strategy::Gem;
    p.category = "gem.activegem";
    CHECK_FALSE(searchable(p));
    p.type = "Empower Support";
    CHECK(searchable(p));
}

TEST_CASE("a discriminator rides on whichever term was ambiguous") {
    SearchPlan p;
    p.strategy = Strategy::BaseItem;
    p.type = "Maelstr\xc3\xb6m Staff";
    p.discriminator = "warlord";
    const json q = query_of(p);
    CHECK(q["type"]["option"] == "Maelstr\xc3\xb6m Staff");
    CHECK(q["type"]["discriminator"] == "warlord");
}

TEST_CASE("numeric filters land in the group the API files them under") {
    SearchPlan p;
    p.strategy = Strategy::BaseItem;
    p.type = "Vaal Regalia";
    const auto add = [&p](const char* key, double min, bool on) {
        ppc::item::NumericFilter f;
        f.key = key;
        f.min = min;
        f.enabled = on;
        p.numerics.push_back(f);
    };
    add("ilvl", 84, true);
    add("es", 300, true);
    add("pdps", 400, true);
    // Where the base's roll sits in its range is an armour filter on the site, not a weapon
    // one, and not a number the item's own defence carries.
    add("base_defence_percentile", 78, true);
    add("quality", 23, false); // untouched by the user, so not sent
    // Their own group on the site, and the API rejects a filter filed anywhere else.
    add("sockets", 6, true);
    add("links", 6, true);
    const json filters = query_of(p)["filters"];
    CHECK(filters["misc_filters"]["filters"]["ilvl"]["min"] == 84);
    CHECK(filters["armour_filters"]["filters"]["es"]["min"] == 300);
    CHECK(filters["armour_filters"]["filters"]["base_defence_percentile"]["min"] == 78);
    CHECK(filters["weapon_filters"]["filters"]["pdps"]["min"] == 400);
    // Integers, not the floats every other group takes: the site answers `6.0` here with
    // "Socket min must be an integer" and runs no search.
    CHECK(filters["socket_filters"]["filters"]["sockets"]["min"] == 6);
    CHECK(filters["socket_filters"]["filters"]["sockets"]["min"].is_number_integer());
    CHECK(filters["socket_filters"]["filters"]["links"]["min"].is_number_integer());
    CHECK(filters["misc_filters"]["filters"]["ilvl"]["min"].is_number_float());
    CHECK_FALSE(filters["misc_filters"]["filters"].contains("quality"));
}

TEST_CASE("a hidden socket filter is a row, not a search") {
    SearchPlan p = rare_plan();
    ppc::item::NumericFilter f;
    f.key = "links";
    f.min = 3;
    f.hidden = true;
    f.enabled = false; // what the plan does with a three-link
    p.numerics.push_back(f);
    CHECK_FALSE(query_of(p)["filters"].contains("socket_filters"));

    // The flag says where the row is drawn and nothing else: ticking it sends it, and this layer
    // never reads the flag at all.
    p.numerics.back().enabled = true;
    CHECK(query_of(p)["filters"]["socket_filters"]["filters"]["links"]["min"] == 3);
}

TEST_CASE("influences become misc booleans; the eldritch pair has none") {
    SearchPlan p = rare_plan();
    p.influences = {ppc::item::Influence::Shaper, ppc::item::Influence::EaterOfWorlds};
    const json misc = query_of(p)["filters"]["misc_filters"]["filters"];
    CHECK(misc["shaper_item"]["option"] == "true");
    CHECK(misc.size() == 2); // shaper_item and corrupted; nothing invented for the Eater
}

TEST_CASE("only the strategies with a stat query behind them are searchable") {
    SearchPlan p;
    for (const Strategy s : {Strategy::BaseItem, Strategy::Modifiers, Strategy::Unique}) {
        p.strategy = s;
        CHECK(searchable(p));
    }
    for (const Strategy s : {Strategy::Currency, Strategy::Gem, Strategy::Unsupported}) {
        p.strategy = s;
        CHECK_FALSE(searchable(p));
    }
}

TEST_CASE("urls") {
    CHECK(search_url("Rise of the Abyssal") ==
          "https://www.pathofexile.com/api/trade/search/Rise%20of%20the%20Abyssal");
    CHECK(fetch_url({"aa", "bb"}, "xYz") ==
          "https://www.pathofexile.com/api/trade/fetch/aa,bb?query=xYz");
    CHECK(web_url("Standard", "xYz") == "https://www.pathofexile.com/trade/search/Standard/xYz");
    // The browser button spends no API call: the site takes the query itself in ?q=.
    CHECK(web_url_for_query("Standard", "{\"a\":1}") ==
          "https://www.pathofexile.com/trade/search/Standard?q=%7B%22a%22%3A1%7D");
}

TEST_CASE("search and fetch responses, as the live API returns them") {
    SearchResults r;
    REQUIRE(parse_search(R"({"id":"yYgqeZVrtR","complexity":6,"result":["aa","bb"],"total":112})",
                         r));
    CHECK(r.query_id == "yYgqeZVrtR");
    CHECK(r.total == 112);
    CHECK(r.hashes.size() == 2);
    CHECK_FALSE(parse_search("not json", r));
    CHECK_FALSE(parse_search(R"({"error":{"code":2,"message":"nope"}})", r));

    std::vector<Listing> ls;
    REQUIRE(parse_fetch(R"({"result":[
      {"id":"aa","listing":{"indexed":"2025-07-24T00:35:34Z",
        "price":{"type":"~price","amount":10,"currency":"chaos"},"fee":3520,
        "account":{"name":"PARKSABRAD#0065"},"whisper":"@AuraBottt Hi"}},
      null,
      {"id":"cc","listing":{"indexed":"2025-07-24T00:35:34Z","account":{"name":"NoPrice#1"}}}
    ]})",
                        ls));
    REQUIRE(ls.size() == 2); // the null is a listing that went while the search was running
    CHECK(ls[0].account == "PARKSABRAD#0065");
    CHECK(ls[0].amount == 10);
    CHECK(ls[0].currency == "chaos");
    CHECK(ls[0].priced);
    CHECK(ls[0].indexed_at == 1753317334);
    // The gold fee is a sibling of "price", not a field of it.
    CHECK(ls[0].fee == 3520);
    // A tab with no price note lists the item without offering it at a number.
    CHECK_FALSE(ls[1].priced);
    CHECK(ls[1].fee == 0);
}

TEST_CASE("timestamps and the age they are shown as") {
    CHECK(parse_iso8601_utc("1970-01-01T00:00:00Z") == 0);
    CHECK(parse_iso8601_utc("2026-08-05T18:21:50Z") == 1785954110);
    CHECK(parse_iso8601_utc("") == 0);
    CHECK(parse_iso8601_utc("yesterday") == 0);

    const int64_t now = 1785954110;
    CHECK(age_text(now - 5, now) == "now");
    CHECK(age_text(now - 300, now) == "5m");
    CHECK(age_text(now - 7200, now) == "2h");
    CHECK(age_text(now - 3 * 86400, now) == "3d");
    CHECK(age_text(0, now) == "?");
}

TEST_CASE("prices print as the seller wrote them") {
    CHECK(price_text(10) == "10");
    CHECK(price_text(1.5) == "1.5");
    CHECK(price_text(0.25) == "0.25");
}

TEST_CASE("base64 round-trips, and refuses rather than guessing") {
    using ppc::base64_decode;
    using ppc::base64_encode;
    for (const std::string s : {"", "f", "fo", "foo", "foob", "Item Class: Belts\r\nRarity: Unique"})
        CHECK(base64_decode(base64_encode(s)).value_or("<failed>") == s);
    CHECK(base64_encode("foo") == "Zm9v");
    CHECK(base64_encode("fo") == "Zm8=");
    // Wrapped payloads decode; anything else outside the alphabet is a corrupt payload, and a
    // half-decoded item would surface as one that mysteriously half-parsed.
    CHECK(base64_decode("Zm9v\n Zm9v").value_or("") == "foofoo");
    CHECK_FALSE(base64_decode("Zm9v!").has_value());
    CHECK_FALSE(base64_decode("Zm9=v").has_value()); // digits after the padding
    CHECK_FALSE(base64_decode("Z").has_value());     // one digit encodes nothing
}

TEST_CASE("the mod-type markers the site's item text leaves off are put back") {
    // A real fetch response (Hydrascale Boots, Allflame), cut to what this turns on. The site
    // renders the clipboard format but writes no "(implicit)", "(crafted)" or "(fractured)" —
    // the fractured mod is simply the first line of the explicit block, and the payload's
    // `domain` is the only thing that says so.
    const std::string text =
        "Item Class: Boots\r\nRarity: Rare\r\nHate Pace\r\nHydrascale Boots\r\n--------\r\n"
        "Item Level: 71\r\n--------\r\n"
        "+460 to Accuracy Rating\r\n--------\r\n"
        "59% increased Armour and Evasion\r\n"
        "14% increased Life Regeneration rate\r\n"
        "13% increased Movement Speed\r\n--------\r\nFractured Item\r\n";
    const std::string body = R"({"result":[{"id":"aa","listing":{"account":{"name":"A#1"}},
      "item":{"typeLine":"Hydrascale Boots",
        "implicitMods":[{"description":"+460 to Accuracy Rating","domain":"implicit"}],
        "explicitMods":[
          {"description":"14% increased Life Regeneration rate","domain":"explicit"},
          {"description":"13% increased Movement Speed","flags":{"crafted":true},
           "domain":"crafted"},
          {"description":"59% increased Armour and Evasion","flags":{"fractured":true},
           "domain":"fractured"}],
        "extended":{"text":")" + ppc::base64_encode(text) + R"("}}}]})";

    std::vector<Listing> ls;
    REQUIRE(parse_fetch(body, ls));
    REQUIRE(ls.size() == 1);
    const std::optional<ppc::item::Item> it = ppc::item::parse_item_en(ls[0].item_text);
    REQUIRE(it);
    CHECK(it->fractured_item);
    REQUIRE(it->mods_of(ppc::data::ModType::Fractured).size() == 1);
    CHECK(it->mods_of(ppc::data::ModType::Fractured).front()->lines.front() ==
          "59% increased Armour and Evasion");
    REQUIRE(it->mods_of(ppc::data::ModType::Crafted).size() == 1);
    CHECK(it->mods_of(ppc::data::ModType::Crafted).front()->lines.front() ==
          "13% increased Movement Speed");
    REQUIRE(it->mods_of(ppc::data::ModType::Implicit).size() == 1);
    CHECK(it->mods_of(ppc::data::ModType::Explicit).size() == 1);

    // The site does mark an enchant, so a marked line must not be marked twice.
    const std::string enchanted =
        "Item Class: Amulets\r\nRarity: Rare\r\nDeath Medallion\r\nSand Spitter Talisman\r\n"
        "--------\r\n12% increased Movement Speed (enchant)\r\n--------\r\n"
        "+20 to all Attributes\r\n";
    const std::string body2 = R"({"result":[{"id":"aa","listing":{"account":{"name":"A#1"}},
      "item":{"typeLine":"Sand Spitter Talisman",
        "enchantMods":[{"description":"12% increased Movement Speed","domain":"enchant"}],
        "explicitMods":[{"description":"+20 to all Attributes","domain":"explicit"}],
        "extended":{"text":")" + ppc::base64_encode(enchanted) + R"("}}}]})";
    std::vector<Listing> ls2;
    REQUIRE(parse_fetch(body2, ls2));
    REQUIRE(ls2.size() == 1);
    CHECK(ls2[0].item_text == enchanted);
}

TEST_CASE("a listing carries the seller's item as clipboard text") {
    // What the API sends: item.extended.text, base64 of the item in clipboard format, CRLF and
    // all. That is what lets a listing go through the same parser as a real copy — bar the mod
    // markers it drops, which `parse_fetch` puts back (above).
    const std::string text = "Item Class: Belts\r\nRarity: Unique\r\nGraven's Secret\r\n";
    const std::string body = R"({"result":[{"id":"aa","listing":{"account":{"name":"A#1"}},
      "item":{"name":"Graven's Secret","extended":{"text":")" +
                             ppc::base64_encode(text) + R"("}}}]})";
    std::vector<Listing> ls;
    REQUIRE(parse_fetch(body, ls));
    REQUIRE(ls.size() == 1);
    CHECK(ls[0].item_text == text);

    // A listing with no item block is still a listing; it just has no tooltip behind it.
    std::vector<Listing> bare;
    REQUIRE(parse_fetch(R"({"result":[{"id":"aa","listing":{"account":{"name":"A#1"}}}]})", bare));
    REQUIRE(bare.size() == 1);
    CHECK(bare[0].item_text.empty());
}

TEST_CASE("a listing fee is grouped the way the site prints it") {
    CHECK(gold_text(3520) == "3,520");
    CHECK(gold_text(0) == "0");
    CHECK(gold_text(999) == "999");
    CHECK(gold_text(1000) == "1,000");
    CHECK(gold_text(1234567) == "1,234,567");
}

TEST_CASE("the static currency data keeps only what can be drawn") {
    const std::vector<CurrencyEntry> c = parse_static_currencies(R"J({"result":[
      {"id":"Currency","entries":[
        {"id":"chaos","text":"Chaos Orb","image":"/gen/image/abc/CurrencyRerollRare.png"},
        {"id":"forbidden-tome-level-68","text":"Forbidden Tome (Level 68)"}]},
      {"id":"Fragments","entries":[
        {"id":"dusk","text":"Sacrifice at Dusk","image":"/gen/image/def/Vaal04.png"}]}]})J");
    REQUIRE(c.size() == 2); // the entry with no image is not a symbol we can show
    CHECK(c[0].id == "chaos");
    CHECK(c[0].text == "Chaos Orb");
    CHECK(c[1].id == "dusk");
    CHECK(parse_static_currencies("{}").empty());
}

TEST_CASE("misc flags are sent as options, and an unticked one is not sent at all") {
    SearchPlan p = rare_plan();
    p.options = {{"corrupted", "Corrupted", "true", "yes", true, true},
                 {"mirrored", "Mirrored", "false", "no"},
                 {"identified", "Identified", "false", "no", false, true}};
    const json misc = query_of(p)["filters"]["misc_filters"]["filters"];

    CHECK(misc["corrupted"]["option"] == "true");
    CHECK(misc["mirrored"]["option"] == "false");
    // Whether a flag has a row is the panel's business; unticking it is what stops the asking.
    CHECK_FALSE(misc.contains("identified"));
}

TEST_CASE("the three misc properties are filed under misc_filters, one-sided as seeded") {
    SearchPlan p = rare_plan();
    p.numerics = {{"memory_level", "Memory Strands", 43, std::nullopt, true, 0, {}},
                  {"intangibility", "Intangibility", std::nullopt, 8, true, 0, {}},
                  {"stored_experience", "Stored Experience", 42420246, std::nullopt, true, 0, {}}};
    const json misc = query_of(p)["filters"]["misc_filters"]["filters"];

    CHECK(misc["memory_level"]["min"] == 43);
    CHECK_FALSE(misc["memory_level"].contains("max"));
    CHECK(misc["intangibility"]["max"] == 8);
    CHECK_FALSE(misc["intangibility"].contains("min"));
    CHECK(misc["stored_experience"]["min"] == 42420246);
}

TEST_CASE("a chart's options and intervals go under map_filters, beside a map's") {
    SearchPlan p;
    p.strategy = Strategy::Map;
    p.category = "chart";
    p.type = "SeafloorRidges";
    p.discriminator = "chart";
    p.options = {{"corrupted", "Corrupted", "false", "no"},
                 {"chart_shape", "Chart Shape", "1", "End", true, true}};
    p.numerics = {{"area_level", "Area Level", 83, 83, true, 0, {}},
                  {"chart_sulphur", "Dead Man's Sulphur", 45, std::nullopt, true, 0, {}}};
    const json q = query_of(p);

    CHECK(q["type"]["option"] == "SeafloorRidges");
    CHECK(q["type"]["discriminator"] == "chart");
    const json& map = q["filters"]["map_filters"]["filters"];
    CHECK(map["chart_shape"]["option"] == "1");
    CHECK(map["area_level"]["min"] == 83);
    CHECK(map["area_level"]["max"] == 83);
    CHECK(map["chart_sulphur"]["min"] == 45);
    // The item's own booleans stay where they belong; only the map/chart ones move.
    CHECK(q["filters"]["misc_filters"]["filters"]["corrupted"]["option"] == "false");
    CHECK_FALSE(map.contains("corrupted"));
}

TEST_CASE("an ultimatum's four options go in a group of their own, and it sends no category") {
    SearchPlan p;
    p.strategy = Strategy::Ultimatum;
    p.type = "Inscribed Ultimatum";
    // Deliberately empty: the plan drops the bundle's answer for "Misc Map Items" because
    // `map.fragment` matched nothing, and the type term is the whole of what is left to say.
    p.category.clear();
    p.options = {{"corrupted", "Corrupted", "false", "no"},
                 {"ultimatum_challenge", "Challenge", "Conquer", "Stand in the Stone Circles",
                  true, true},
                 {"ultimatum_reward", "Reward", "ExchangeUnique", "Mageblood", true, true},
                 {"ultimatum_output", "Reward Unique", "Mageblood", "Mageblood", true, true},
                 {"ultimatum_input", "Requires Sacrifice", "Martyr of Innocence",
                  "Martyr of Innocence", true, true}};
    p.numerics = {{"area_level", "Area Level", 83, 83, true}};

    const json q = query_of(p);
    CHECK(q["type"] == "Inscribed Ultimatum");
    CHECK_FALSE(q["filters"]["type_filters"]["filters"].contains("category"));
    const json& u = q["filters"]["ultimatum_filters"]["filters"];
    CHECK(u["ultimatum_challenge"]["option"] == "Conquer");
    CHECK(u["ultimatum_reward"]["option"] == "ExchangeUnique");
    CHECK(u["ultimatum_output"]["option"] == "Mageblood");
    CHECK(u["ultimatum_input"]["option"] == "Martyr of Innocence");
    // The corruption boolean is not one of them, and the area level is the map group's — the
    // site files "Area Level" under Map/Chart filters whatever kind of item asks about it.
    CHECK(q["filters"]["misc_filters"]["filters"].contains("corrupted"));
    CHECK(q["filters"]["map_filters"]["filters"]["area_level"]["min"] == 83);

    SUBCASE("a plan that could fill none of them in is not a search") {
        // One base type, so the type term alone is every ultimatum in the league — the cheapest
        // listing among them would read as this contract's price.
        p.options = {{"corrupted", "Corrupted", "false", "no"}};
        CHECK_FALSE(searchable(p));
    }
    SUBCASE("and one that filled in any of them is") { CHECK(searchable(p)); }
}

TEST_CASE("a heist item's filters go in heist_filters, and its area level does not") {
    SearchPlan p;
    p.strategy = Strategy::Heist;
    p.category = "heistmission.blueprint";
    p.type = "Blueprint: Tunnels";
    p.options = {{"heist_objective_value", "Objective Value", "priceless", "Priceless", true,
                  true}};
    p.numerics = {{"area_level", "Area Level", 83, 83, true},
                  {"heist_wings", "Wings Revealed", 4, std::nullopt, true},
                  {"heist_max_wings", "Total Wings", 4, 4, true},
                  // Seeded as a ceiling and unticked, so it must not be sent at all.
                  {"heist_brute_force", "Brute Force Level", std::nullopt, 5, false}};

    const json q = query_of(p);
    const json& h = q["filters"]["heist_filters"]["filters"];
    CHECK(h["heist_objective_value"]["option"] == "priceless");
    CHECK(h["heist_wings"]["min"] == 4);
    CHECK_FALSE(h["heist_wings"].contains("max"));
    CHECK(h["heist_max_wings"]["max"] == 4);
    CHECK_FALSE(h.contains("heist_brute_force"));
    // "Area Level" is a heist item's, but the site files that filter under Map/Chart whatever
    // kind of item is asking — so the `heist_` prefix is the whole of the group rule.
    CHECK(q["filters"]["map_filters"]["filters"]["area_level"]["min"] == 83);
    CHECK_FALSE(h.contains("area_level"));

    SUBCASE("the category alone is a search when the bundle could not name the wing") {
        // Unlike a gem, whose type term is the only question it has: a heist item is told apart
        // by filters that live outside it, so "some blueprint at this level, this much revealed"
        // is coarser rather than useless.
        p.type.clear();
        CHECK(searchable(p));
        p.category.clear();
        CHECK_FALSE(searchable(p));
    }
}

TEST_CASE("a sanctum's filters go in sanctum_filters, and its area level does not") {
    SearchPlan p;
    p.strategy = Strategy::Sanctum;
    p.category = "sanctum.research";
    p.type = "Sanctum Vaults Research";
    p.numerics = {{"area_level", "Area Level", 83, 83, true},
                  {"sanctum_resolve", "Resolve", 299, std::nullopt, true},
                  // Seeded open on the right and left unticked, so it must not be sent.
                  {"sanctum_max_resolve", "Maximum Resolve", 300, std::nullopt, false},
                  {"sanctum_inspiration", "Inspiration", 0, std::nullopt, true},
                  {"sanctum_gold", "Aureus", 399, std::nullopt, true}};

    const json q = query_of(p);
    const json& s = q["filters"]["sanctum_filters"]["filters"];
    CHECK(s["sanctum_resolve"]["min"] == 299);
    CHECK_FALSE(s["sanctum_resolve"].contains("max"));
    CHECK(s["sanctum_inspiration"]["min"] == 0);
    CHECK(s["sanctum_gold"]["min"] == 399);
    CHECK_FALSE(s.contains("sanctum_max_resolve"));
    // The same exception a heist item's gets: the site files Area Level under Map/Chart
    // whichever kind of item is asking.
    CHECK(q["filters"]["map_filters"]["filters"]["area_level"]["max"] == 83);
    CHECK_FALSE(s.contains("area_level"));

    SUBCASE("the category alone is a search when the bundle could not name the floor") {
        p.type.clear();
        CHECK(searchable(p));
        p.category.clear();
        CHECK_FALSE(searchable(p));
    }
}

TEST_CASE("a logbook sends one destination of several, and the others are simply unticked") {
    // The exclusion is entirely `SearchPlan::select_choice` keeping the other groups off. This
    // layer reads `enabled` and knows nothing about the grouping, exactly as it knows nothing
    // about `hidden` — which is the guarantee worth stating on the wire rather than in the plan.
    SearchPlan p;
    p.strategy = Strategy::Logbook;
    p.category = "logbook";
    p.type = "Expedition Logbook";
    p.choices = {{"Druids of the Broken Circle", "Volcanic Island"},
                 {"Order of the Chalice", "Sarn Slums"}};
    const auto add = [&p](const char* id, size_t group, bool primary) {
        ppc::item::StatFilter f;
        f.id = id;
        f.type = ppc::data::ModType::Pseudo;
        f.choice = group;
        f.choice_primary = primary;
        p.stats.push_back(f);
    };
    add("pseudo.pseudo_logbook_faction_druids", 0, true);
    add("pseudo.pseudo_logbook_area_volcano", 0, false);
    add("pseudo.pseudo_logbook_faction_order", 1, true);
    add("pseudo.pseudo_logbook_area_sarn_slums", 1, false);
    p.numerics = {{"area_level", "Area Level", 80.0, std::nullopt, true}};

    CHECK(searchable(p));
    p.select_choice(0);
    json q = query_of(p);
    REQUIRE(q["stats"].size() == 1);
    REQUIRE(q["stats"][0]["filters"].size() == 1);
    CHECK(q["stats"][0]["filters"][0]["id"] == "pseudo.pseudo_logbook_faction_druids");
    // Presence: the pseudo stat takes a count and the search deliberately does not send one.
    CHECK_FALSE(q["stats"][0]["filters"][0].contains("value"));
    CHECK(q["filters"]["map_filters"]["filters"]["area_level"]["min"] == 80.0);

    // The other destination, and nothing of the first one left behind.
    p.select_choice(1);
    q = query_of(p);
    REQUIRE(q["stats"][0]["filters"].size() == 1);
    CHECK(q["stats"][0]["filters"][0]["id"] == "pseudo.pseudo_logbook_faction_order");
}
