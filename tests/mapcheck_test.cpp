#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include <filesystem>
#include <fstream>

#include "item/resolve.hpp"
#include "mapcheck/filter.hpp"
#include "mapcheck/rate.hpp"
#include "mapcheck/store.hpp"
#include "mapcheck/verdict.hpp"
#include "parse_en.hpp"

using namespace ppc::mapcheck;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> lines(std::initializer_list<const char*> l) {
    return std::vector<std::string>(l.begin(), l.end());
}

/// The committed bundle slice, which carries nine pool entries across all three domains — among
/// them the two-wording `Unwavering`, which is the whole reason the affix key is a set, and the
/// heist `Elite`, whose two unprinted wordings are why a printed line has to be expanded.
std::shared_ptr<ppc::data::GameData> fixture() {
    std::string err;
    auto gd = ppc::data::GameData::open(fs::path(PPC_TEST_DATA_DIR) / "bundle", "en", &err);
    REQUIRE_MESSAGE(gd != nullptr, "opening the fixture bundle failed: " << err);
    return gd;
}

ppc::item::Item resolved(const ppc::data::GameData& gd, const char* file) {
    std::ifstream in(fs::path(PPC_TEST_DATA_DIR) / "items" / file);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    std::optional<ppc::item::Item> it = ppc::item::parse_item_en(ss.str());
    REQUIRE(it.has_value());
    ppc::item::resolve_item(gd, *it);
    return *it;
}

/// A directory of its own per test, removed with it.
struct TempDir {
    fs::path path;
    explicit TempDir(const char* tag)
        : path(fs::temp_directory_path() / ("ppc-mapcheck-" + std::string(tag))) {
        fs::remove_all(path);
    }
    ~TempDir() { fs::remove_all(path); }
};

} // namespace

TEST_CASE("the worst verdict is the one that leads") {
    CHECK(worse_of(Verdict::Safe, Verdict::Deadly) == Verdict::Deadly);
    CHECK(worse_of(Verdict::Dangerous, Verdict::Safe) == Verdict::Dangerous);
    // Unrated is the absence of a decision, so anything at all outranks it.
    CHECK(worse_of(Verdict::Unrated, Verdict::Safe) == Verdict::Safe);
    CHECK(worse_of(Verdict::Unrated, Verdict::Unrated) == Verdict::Unrated);
}

TEST_CASE("a click walks the four states and comes back round") {
    Verdict v = Verdict::Unrated;
    v = next_verdict(v);
    CHECK(v == Verdict::Safe);
    v = next_verdict(v);
    CHECK(v == Verdict::Dangerous);
    v = next_verdict(v);
    CHECK(v == Verdict::Deadly);
    CHECK(next_verdict(v) == Verdict::Unrated);
}

TEST_CASE("one deadly modifier decides the map on its own") {
    // Whatever else is on it, and whether or not it is outnumbered.
    CHECK(assess(Tally{7, 0, 1, 0}) == Outlook::Fatal);
    CHECK(assess(Tally{0, 0, 1, 0}) == Outlook::Fatal);
}

TEST_CASE("the map's outlook is the strongest thing true of its modifiers") {
    CHECK(assess(Tally{}) == Outlook::NoMods);
    // More than half safe.
    CHECK(assess(Tally{4, 1, 0, 2}) == Outlook::Safe);
    CHECK(assess(Tally{3, 3, 0, 0}) == Outlook::Careful); // exactly half is not more than half
    // Half or more unrated, and nothing worse than safe under it.
    CHECK(assess(Tally{2, 0, 0, 4}) == Outlook::Unrated);
    CHECK(assess(Tally{1, 0, 0, 1}) == Outlook::Unrated);
    // The same map with one dangerous modifier is no longer just unread.
    CHECK(assess(Tally{2, 1, 0, 4}) == Outlook::Likely);
    // More safe than dangerous, but not a majority of the map.
    CHECK(assess(Tally{2, 1, 0, 3}) == Outlook::Likely);
    // As many dangerous as safe, or more.
    CHECK(assess(Tally{2, 2, 0, 2}) == Outlook::Careful);
    CHECK(assess(Tally{1, 4, 0, 2}) == Outlook::Careful);
}

TEST_CASE("setting a stat back to unrated takes the row out of the table") {
    Profile p("Softcore");
    CHECK(p.set({"#% increased Monster Damage"}, Verdict::Deadly));
    CHECK(p.rated() == 1);
    CHECK(p.verdict_of({"#% increased Monster Damage"}) == Verdict::Deadly);
    // Nothing changed, so nothing to write.
    CHECK_FALSE(p.set({"#% increased Monster Damage"}, Verdict::Deadly));
    CHECK(p.set({"#% increased Monster Damage"}, Verdict::Unrated));
    CHECK(p.rated() == 0);
    CHECK_FALSE(p.set({"#% increased Monster Damage"}, Verdict::Unrated));
    // A stat nobody has said anything about, which is most of the pool.
    CHECK(p.verdict_of({"Players are Cursed with Enfeeble"}) == Verdict::Unrated);
}

TEST_CASE("a verdict is about an affix, so sharing one wording is not sharing a decision") {
    const std::vector<std::string> unwavering{"Monsters cannot be Stunned", "#% more Monster Life"};
    const std::vector<std::string> juggernaut{"Monsters cannot be Stunned",
                                              "Monsters' Action Speed cannot be modified to below Base Value",
                                              "Monsters' Movement Speed cannot be modified to below Base Value"};
    Profile p("Hardcore");
    p.set(unwavering, Verdict::Deadly);
    // The other affix grants that same wording and is a different decision, so it is untouched.
    CHECK(p.exact(juggernaut) == Verdict::Unrated);
    CHECK(p.verdict_of(juggernaut) == Verdict::Unrated);
    // A three-line affix is not spoken for by a one-line one that happens to be inside it.
    CHECK(p.exact({"Monsters cannot be Stunned"}) == Verdict::Unrated);

    // The order the wordings arrive in is not part of the key.
    CHECK(p.verdict_of({"#% more Monster Life", "Monsters cannot be Stunned"}) == Verdict::Deadly);

    // A one-wording affix does speak for the affixes that contain it — until they are rated in
    // their own right, when the longer key wins for being the more particular statement.
    p.set({"Monsters cannot be Stunned"}, Verdict::Safe);
    CHECK(p.verdict_of(juggernaut) == Verdict::Safe);
    p.set(juggernaut, Verdict::Deadly);
    CHECK(p.verdict_of(juggernaut) == Verdict::Deadly);
    CHECK(p.verdict_of({"Monsters cannot be Stunned"}) == Verdict::Safe);

    // And it survives the file, which now writes a set per row.
    const Profile back = profile_from_json("Hardcore", profile_to_json(p));
    CHECK(back.verdict_of(juggernaut) == Verdict::Deadly);
    CHECK(back.verdict_of(unwavering) == Verdict::Deadly);
}

TEST_CASE("a table round-trips through its file, bounds and all") {
    Profile p("Hardcore");
    p.set({"Monsters reflect #% of Physical Damage"}, Verdict::Deadly);
    p.set({"#% increased Quantity of Items found in this Area"}, Verdict::Safe);
    // Written by hand or by a later version: no UI produces this today and the reader still
    // has to keep it, or one visit to Settings would throw it away.
    p.put(affix_key({"#% increased Monster Damage"}), Rating{Verdict::Dangerous, 40.0, std::nullopt});

    const Profile back = profile_from_json("Hardcore", profile_to_json(p));
    CHECK(back.name() == "Hardcore");
    CHECK(back.rated() == 3);
    CHECK(back.verdict_of({"Monsters reflect #% of Physical Damage"}) == Verdict::Deadly);
    CHECK(back.verdict_of({"#% increased Quantity of Items found in this Area"}) == Verdict::Safe);
    const Rating* r = back.rating_of({"#% increased Monster Damage"});
    REQUIRE(r != nullptr);
    CHECK(r->verdict == Verdict::Dangerous);
    REQUIRE(r->min.has_value());
    CHECK(*r->min == doctest::Approx(40.0));
    CHECK_FALSE(r->max.has_value());
}

TEST_CASE("a profile file the user broke costs the ratings, never the run") {
    CHECK(profile_from_json("Boom", "{\"verdicts\": {").rated() == 0);
    CHECK(profile_from_json("Boom", "").rated() == 0);
    // A verdict word this version does not know reads as unrated, so the row is simply absent
    // rather than becoming a wrong answer.
    CHECK(profile_from_json("Boom", R"({"verdicts":{"a":"catastrophic"}})").rated() == 0);
}

TEST_CASE("a profile name is made into something that can be a file") {
    CHECK(sanitize_profile_name("Hardcore SSF") == "Hardcore SSF");
    CHECK(sanitize_profile_name("juice/rush") == "juice_rush");
    CHECK(sanitize_profile_name("who?*") == "who__");
    // Windows drops these silently, which would make two names that do not look alike open one
    // file.
    CHECK(sanitize_profile_name("  trailing. ") == "trailing");
    // Still a device name on Windows however it is spelled.
    CHECK(sanitize_profile_name("con") == "_con");
    CHECK(sanitize_profile_name("NUL.json") == "_NUL.json");
    CHECK(sanitize_profile_name("").empty());
    CHECK(sanitize_profile_name(" . ").empty()); // nothing left once the ends are trimmed
    // Substituted rather than dropped: these are two profiles and must stay two files.
    CHECK(sanitize_profile_name("a/b") != sanitize_profile_name("ab"));
    CHECK(sanitize_profile_name(std::string(200, 'x')).size() == kMaxProfileName);
}

TEST_CASE("a search string is terms, not one regex") {
    // The shape the roadmap fixed, and what poe.re writes.
    const std::vector<SearchTerm> t = parse_search(R"("!\d+ e|te of|ents$" pte)");
    REQUIRE(t.size() == 2);
    CHECK(t[0].negated);
    CHECK(t[0].text == R"(\d+ e|te of|ents$)");
    CHECK_FALSE(t[1].negated);
    CHECK(t[1].text == "pte");
}

TEST_CASE("the negation is read inside the quotes or outside them") {
    const std::vector<SearchTerm> a = parse_search(R"(!"no reflect")");
    REQUIRE(a.size() == 1);
    CHECK(a[0].negated);
    CHECK(a[0].text == "no reflect");
    const std::vector<SearchTerm> b = parse_search(R"("!no reflect")");
    REQUIRE(b.size() == 1);
    CHECK(b[0].negated);
    CHECK(b[0].text == "no reflect");
}

TEST_CASE("a quote left open runs to the end, because the string is still being typed") {
    const std::vector<SearchTerm> t = parse_search(R"(pte "m resist)");
    REQUIRE(t.size() == 2);
    CHECK(t[0].text == "pte");
    CHECK(t[1].text == "m resist");
}

TEST_CASE("a search says wanted, unwanted or nothing at all") {
    const SearchFilter f(R"("!ll damage$|reflect" quantity)");
    // A negated term hit: this is the modifier the string exists to refuse.
    CHECK(f.classify(lines({"Monsters reflect 18% of Physical Damage"})) ==
          SearchFilter::Hit::Unwanted);
    CHECK(f.classify(lines({"#% increased Quantity of Items found in this Area"})) ==
          SearchFilter::Hit::Wanted);
    CHECK(f.classify(lines({"Area contains many Totems"})) == SearchFilter::Hit::None);
    // Both sides hit, and the refusal is the stronger statement.
    CHECK(f.classify(lines({"Monsters reflect 18% of Physical Damage",
                            "20% increased Quantity of Items found in this Area"})) ==
          SearchFilter::Hit::Unwanted);
}

TEST_CASE("every term is tested against a line on its own, so an anchor means what it says") {
    const SearchFilter f("damage$");
    // The second line ends in the word; joined into one string it would not, and this is a
    // modifier that prints two lines.
    CHECK(f.classify(lines({"Monsters have +40% Chaos Resistance", "18% increased Damage"})) ==
          SearchFilter::Hit::Wanted);
}

TEST_CASE("a stat that only rolls below zero is matched on the wording the game prints") {
    // The real record: one canonical wording and its inverse, and a range that can never make
    // the canonical one true.
    ppc::data::Stat rec;
    rec.ref = "Players have #% more Defences";
    rec.matchers.push_back({"Players have #% more Defences", false, {}});
    rec.matchers.push_back({"Players have #% less Defences", true, {}});

    ppc::data::PoolMod m;
    m.name = "of Miring";
    m.stats.push_back({"Players have #% more Defences", {}, -30.0, -25.0});

    const ppc::data::Stat* recs[]{&rec};
    const std::vector<std::string> l = matchable_lines(m, recs);
    // Both ends of the range, said the way the game says them.
    CHECK(l[0] == "Players have 25% less Defences");
    CHECK(l[1] == "Players have 30% less Defences");
    CHECK(l[2] == "of Miring");

    // The symptom this was found by: a term written against the printed line.
    CHECK(SearchFilter("\"s def\"").classify(l) == SearchFilter::Hit::Wanted);
    // And what the list has to show, since a placeholder has no sign to read.
    CHECK(display_wording(&rec, m.stats[0]) == "Players have #% less Defences");

    // A range that can produce either wording keeps the canonical one: both are lines the game
    // may print, and that one is the record's identity.
    ppc::data::PoolStat spans{"Players have #% more Defences", {}, -10.0, 10.0};
    CHECK(display_wording(&rec, spans) == "Players have #% more Defences");
    CHECK(printed_wording(&rec, spans.ref, -10.0) == "Players have 10% less Defences");
    CHECK(printed_wording(&rec, spans.ref, 10.0) == "Players have 10% more Defences");
    // No record to consult is the old behaviour, not a crash.
    CHECK(printed_wording(nullptr, spans.ref, -10.0) == "Players have -10% more Defences");
}

TEST_CASE("a term hitting one wording is about the affix printing it, and about nothing else") {
    ppc::data::PoolMod m;
    m.name = "Protected";
    m.stats.push_back({"+#% Monster Elemental Resistances", {}, 55.0, 55.0});
    m.stats.push_back({"#% more Maps found in Area", {}, 35.0, 35.0});

    // The term names one of the two lines, and what it decides is the modifier that prints it.
    const SearchFilter f("\"ter e\"");
    CHECK(f.classify(matchable_lines(m, nullptr)) == SearchFilter::Hit::Wanted);
    // A term naming the affix is about all of it, which is why the name is in scope too.
    CHECK(SearchFilter("protected").classify(matchable_lines(m, nullptr)) ==
          SearchFilter::Hit::Wanted);

    // And the containment that matters: the proposal keys on the affix's whole set, so accepting
    // it says nothing about the *other* affixes granting one of these wordings. Asking per stat
    // instead wrote `#% more Maps found in Area` on its own, and a key that short is one the
    // propagation rule then reads onto every affix containing it.
    Profile p("Softcore");
    p.set(pool_key_refs(m), Verdict::Safe);
    CHECK(p.verdict_of(pool_key_refs(m)) == Verdict::Safe);
    CHECK(p.verdict_of({"#% more Maps found in Area"}) == Verdict::Unrated);
    CHECK(p.verdict_of({"#% more Maps found in Area", "#% increased Monster Damage"}) ==
          Verdict::Unrated);
}

TEST_CASE("a term inside a long alternation is still the pattern it looks like") {
    // The whole of a real string, with `\d+ e` as one alternative of a negated term. The
    // alternation is regex, not text: quoting groups the spaces and escapes nothing.
    const SearchFilter f(
        R"("!\d+ e|te of|m resistances$|ents$|r, f|ter e|ll damage$|from$|t reg|s def|h tem" pte)");
    REQUIRE(f.size() == 2);
    CHECK(f.classify(lines({"Rare Monsters have Elemental Thorns reflecting 1500 Elemental "
                            "Damage"})) == SearchFilter::Hit::Unwanted);
    CHECK(f.classify(lines({"Monsters cannot be Leeched from"})) == SearchFilter::Hit::Unwanted);
    CHECK(f.classify(lines({"Area is inhabited by Skeletons"})) == SearchFilter::Hit::None);
}

TEST_CASE("a term is a real regex, whatever the string around it is") {
    // The syntax holds patterns apart; it does not replace them. Both of these are lifted from a
    // string a player actually keeps, and both mean exactly what they look like.
    // Quoted, because the space inside it is part of the pattern and an unquoted one would be
    // the AND — which is exactly why the string this came from quotes it.
    const SearchFilter d(R"("\d+ e")");
    REQUIRE(d.size() == 1);
    // Both wordings are the published pool's own, rendered at the top of their range.
    CHECK(d.classify(lines({"Rare Monsters have Elemental Thorns reflecting 1500 Elemental Damage"})) ==
          SearchFilter::Hit::Wanted);
    CHECK(d.classify(lines({"Monsters gain 3 Endurance Charge every 20 seconds"})) ==
          SearchFilter::Hit::Wanted);
    // A number that is not followed by a space and an `e` is not this pattern, whatever else it
    // has in common with it.
    CHECK(d.classify(lines({"20% increased Monster Damage"})) == SearchFilter::Hit::None);
    CHECK(d.classify(lines({"Monsters are Hexproof"})) == SearchFilter::Hit::None);

    const SearchFilter a(R"("ll damage$")");
    CHECK(a.classify(lines({"Monsters have 100% chance to Suppress Spell Damage"})) ==
          SearchFilter::Hit::Wanted);
    // Anchored, and the anchor is the point of writing it this way: the same words with anything
    // after them are a different modifier, and this is how a search string tells them apart.
    CHECK(a.classify(lines({"Monsters have 100% chance to Suppress Spell Damage from Hits"})) ==
          SearchFilter::Hit::None);
}

TEST_CASE("a term that is not a valid regex hits nothing rather than becoming its own text") {
    // An unterminated group, which is what a pattern looks like halfway through being typed.
    // Read as literal text it would find the first line here, and this box would then be two
    // search languages with nothing on screen saying which one a term had got.
    const SearchFilter f(R"("Damage (")");
    REQUIRE(f.size() == 1);
    CHECK(f.classify(lines({"Monsters deal 30% extra Damage (Fire)"})) == SearchFilter::Hit::None);
    CHECK(f.classify(lines({"Monsters deal 30% extra Damage"})) == SearchFilter::Hit::None);
    // It still counts as a term, so filtering it shows nothing rather than showing everything.
    CHECK_FALSE(f.matches(lines({"Monsters deal 30% extra Damage (Fire)"})));
    // Escaped, it is the pattern the player meant, and the same string works again.
    CHECK(SearchFilter(R"("Damage \(")").classify(lines({"Monsters deal 30% extra Damage (Fire)"})) ==
          SearchFilter::Hit::Wanted);
}

TEST_CASE("filtering ANDs the terms, the way the game's own search box does") {
    const SearchFilter f("monster damage");
    CHECK(f.matches(lines({"#% increased Monster Damage"})));
    // Both words, not either: this box is also where somebody types two plain words.
    CHECK_FALSE(f.matches(lines({"#% increased Monster Life"})));
    CHECK_FALSE(f.matches(lines({"#% increased Damage taken"})));
    // A negated term hides rather than marks, which is what it means in a search box.
    const SearchFilter g("damage !monster");
    CHECK(g.matches(lines({"#% increased Damage taken"})));
    CHECK_FALSE(g.matches(lines({"#% increased Monster Damage"})));
}

TEST_CASE("proposing asks each term on its own, because one modifier cannot satisfy two") {
    // Two wanted terms naming two different modifiers, which is how these strings are written.
    const SearchFilter f("quantity pack");
    CHECK(f.classify(lines({"#% increased Quantity of Items found in this Area"})) ==
          SearchFilter::Hit::Wanted);
    CHECK(f.classify(lines({"#% increased Monster pack size"})) == SearchFilter::Hit::Wanted);
    // ANDed, as filtering does it, neither of them would be proposed at all.
    CHECK_FALSE(f.matches(lines({"#% increased Quantity of Items found in this Area"})));
}

TEST_CASE("a term asking about the item is set aside instead of emptying the list") {
    // What a real map string carries besides its modifier terms. Left in, the AND would make
    // the whole list vanish and never say why.
    const SearchFilter f("ilvl:84 monster");
    CHECK(f.size() == 1);
    CHECK(f.set_aside() == 1);
    CHECK(f.matches(lines({"#% increased Monster Damage"})));

    // A search that is nothing but those reads as an empty box, not as one matching nothing.
    const SearchFilter g("\"rarity: rare\" ts:.+");
    CHECK(g.empty());
    CHECK(g.set_aside() == 2);
}

TEST_CASE("a keyword is only recognised by its colon, never by the word") {
    // Every one of these is real modifier text in the published pool, and a keyword list would
    // have swallowed the searches most worth typing.
    CHECK_FALSE(asks_about_item("currency"));
    CHECK_FALSE(asks_about_item("corrupted"));
    CHECK_FALSE(asks_about_item("rarity"));
    CHECK(asks_about_item("rarity:"));
    CHECK(asks_about_item("item level: 78"));
    // A colon that is part of a pattern is not a keyword.
    CHECK_FALSE(asks_about_item("(?:a|b)"));
    CHECK_FALSE(asks_about_item("[a-z]:"));
    CHECK_FALSE(asks_about_item(":nope"));
    CHECK(SearchFilter("currency").matches(lines({"#% more Currency found in Area"})));
}

TEST_CASE("an empty search mentions nothing") {
    const SearchFilter f("   ");
    CHECK(f.empty());
    CHECK(f.classify(lines({"anything at all"})) == SearchFilter::Hit::None);
}

TEST_CASE("a wording is rendered before a term written against printed text meets it") {
    CHECK(render_wording("#% increased Monster Damage", 40.0) == "40% increased Monster Damage");
    CHECK(render_wording("Adds # to # Fire Damage", 12.0) == "Adds 12 to 12 Fire Damage");
    CHECK(render_wording("+#% Monster Chaos Resistance", 3.5) == "+3.50% Monster Chaos Resistance");
    // No bound is the wordings that print no number, and they are left exactly as they are.
    CHECK(render_wording("Area contains many Totems", std::nullopt) == "Area contains many Totems");
}

TEST_CASE("a pool entry is matched on its rendered wordings and on its affix name") {
    ppc::data::PoolMod m;
    m.domain = 5;
    m.gen = 2;
    m.name = "of Impedance";
    m.stats.push_back({"Monsters have #% chance to Hinder on Hit with Spells", "explicit.stat_1",
                       100.0, 100.0});
    const std::vector<std::string> l = matchable_lines(m, nullptr);
    REQUIRE(l.size() == 2);
    CHECK(l[0] == "Monsters have 100% chance to Hinder on Hit with Spells");
    CHECK(l[1] == "of Impedance");
    // The name is a line of the tooltip the game's own search reads, so a term naming one has
    // to be able to hit.
    CHECK(SearchFilter("te of").classify(l) == SearchFilter::Hit::Wanted);
    // And the number is there to be matched, which a placeholder could never be.
    CHECK(SearchFilter(R"(\d+% chance)").classify(l) == SearchFilter::Hit::Wanted);
}

TEST_CASE("what a map printed is keyed on the pool entry covering it") {
    const auto gd = fixture();
    // The one-wording affix keys as itself, since the domain-5 entry grants nothing else.
    CHECK(pool_refs_for({"Monsters have #% chance to Hinder on Hit with Spells"}, kMapDomain,
                        gd.get()) ==
          std::vector<std::string>{"Monsters have #% chance to Hinder on Hit with Spells"});

    // The case the expansion exists for: an affix grants two wordings and the item printed one
    // of them, so the printed line alone is not the key a rating in Settings was written under.
    const std::vector<std::string> unwavering =
        pool_refs_for({"Monsters cannot be Stunned"}, kChartDomain, gd.get());
    CHECK(unwavering ==
          std::vector<std::string>{"#% more Monster Life", "Monsters cannot be Stunned"});

    // Nothing in the pool covers it, which is normal and never a gate: the wording stands as its
    // own key and is rated on the spot like anything else.
    CHECK(pool_refs_for({"Players are Cursed with Enfeeble"}, kMapDomain, gd.get()) ==
          std::vector<std::string>{"Players are Cursed with Enfeeble"});
    // And with no pool to ask, the printed wordings are the answer rather than an empty one.
    CHECK(pool_refs_for({"Monsters cannot be Stunned"}, kChartDomain, nullptr) ==
          std::vector<std::string>{"Monsters cannot be Stunned"});
}

TEST_CASE("a map's rolled affixes are rated, its implicits are printed and left alone") {
    const auto gd = fixture();
    const ppc::item::Item map = resolved(*gd, "map-magic-t16.txt");
    REQUIRE(is_rateable_item(map, gd.get()));

    const TempDir tmp("rate");
    Store store;
    store.open(tmp.path, {}, "");

    std::vector<Row> rows = rate(map, store, gd.get());
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].rateable());
    // The record's `ref`, not the `Monsters Hinder on Hit with Spells` the item printed: a
    // wording is language-dependent and a key is not.
    CHECK(rows[0].refs ==
          std::vector<std::string>{"Monsters have #% chance to Hinder on Hit with Spells"});
    CHECK(rows[0].verdict == Verdict::Unrated);
    CHECK(assess(tally(rows)) == Outlook::Unrated);

    // Rated in Settings, off the pool entry, and read back off the map.
    const std::vector<const ppc::data::PoolMod*> pool = [&] {
        std::vector<const ppc::data::PoolMod*> out;
        for (const ppc::data::PoolMod* m : gd->mod_pool(kMapDomain))
            if (m->name == "of Impedance") out.push_back(m);
        return out;
    }();
    REQUIRE(pool.size() == 1);
    store.set(pool_key_refs(*pool[0]), Verdict::Deadly);

    rows = rate(map, store, gd.get());
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].verdict == Verdict::Deadly);
    CHECK(assess(tally(rows)) == Outlook::Fatal);
}

TEST_CASE("a heist contract is rated from the heist pool, not from the map's") {
    const auto gd = fixture();
    const ppc::item::Item contract = resolved(*gd, "heist-contract-rare-mansion-advanced.txt");
    // "Contract: Mansion" is not a base in the slice, so this is also the fallback: the bundle
    // could not say, the clipboard could, and a contract is a heist area either way.
    REQUIRE(contract.base == nullptr);
    CHECK(map_domain_of(contract, gd.get()) == kHeistDomain);
    REQUIRE(is_rateable_item(contract, gd.get()));

    const TempDir tmp("heist");
    Store store;
    store.open(tmp.path, {}, "");

    // Advanced Mod Descriptions is on in this capture, so the six affixes are six rows. Only
    // `Elite` and `of Flames` resolve against the slice; the rest are drawn and cannot be rated,
    // which is what an unresolved wording is supposed to do rather than vanish.
    const std::vector<Row> rows = rate(contract, store, gd.get());
    REQUIRE(rows.size() == 6);
    CHECK(std::count_if(rows.begin(), rows.end(), [](const Row& r) { return r.rateable(); }) == 2);

    // The expansion, and the reason it exists here: the affix also raises alert level and delays
    // lockdown, the contract prints neither — it folds them into `Alert Level Reduction: +34%` —
    // and a verdict set in Settings is keyed on all three.
    CHECK(rows[0].refs ==
          std::vector<std::string>{
              "#% increased time before Lockdown", "#% more raising of Alert Level",
              "Patrol Packs have #% increased chance to be replaced by an Elite Patrol Pack"});

    const std::vector<PoolGroup> groups = pool_groups(*gd);
    const auto elite = std::find_if(groups.begin(), groups.end(), [](const PoolGroup& g) {
        return g.mod->domain == kHeistDomain && g.mod->name == "Elite";
    });
    REQUIRE(elite != groups.end());
    store.set(elite->refs, Verdict::Deadly);
    CHECK(rate(contract, store, gd.get())[0].verdict == Verdict::Deadly);
    CHECK(assess(tally(rate(contract, store, gd.get()))) == Outlook::Fatal);
}

TEST_CASE("a heist item the bundle knows the base of is placed by the bundle") {
    const auto gd = fixture();
    // "Contract: Tunnels" is in the slice and its record states domain 22, so nothing here
    // depends on the item class at all.
    const ppc::item::Item contract = resolved(*gd, "heist-contract-rare-tunnels.txt");
    REQUIRE(contract.base != nullptr);
    CHECK(contract.base->mod_domain == kHeistDomain);
    CHECK(map_domain_of(contract, gd.get()) == kHeistDomain);
    CHECK(is_rateable_item(contract, gd.get()));

    const ppc::item::Item blueprint = resolved(*gd, "heist-blueprint-rare-tunnels-full.txt");
    CHECK(map_domain_of(blueprint, gd.get()) == kHeistDomain);
    CHECK(is_rateable_item(blueprint, gd.get()));
}

TEST_CASE("an affix two pools both grant is one row, because it is one decision") {
    const auto gd = fixture();
    const std::vector<PoolGroup> groups = pool_groups(*gd);

    // The slice holds nine entries in seven groups: `of Impedance` is in it twice — the map's and
    // the chart's, identically worded — and `Area has patches of Burning Ground` three times, of
    // which the map's and one contract's are the same set. The store keys on the ref set with no
    // domain in it, so entries sharing one can never hold different verdicts and drawing them
    // apart is drawing one decision twice.
    CHECK(groups.size() == 7);
    const auto flames = std::find_if(groups.begin(), groups.end(), [](const PoolGroup& g) {
        return g.refs == std::vector<std::string>{"Area has patches of Burning Ground"};
    });
    REQUIRE(flames != groups.end());
    // Across two *different* pools this time, and the map's still leads.
    CHECK(flames->all.size() == 2);
    CHECK(flames->mod->domain == kMapDomain);
    CHECK(flames->all[1]->domain == kHeistDomain);
    // The contract entry granting the same wording *plus* the alert-level pair is not folded in:
    // it is a bigger affix, and one decision about it is not the same decision.
    CHECK(std::count_if(groups.begin(), groups.end(), [](const PoolGroup& g) {
              return g.mod->domain == kHeistDomain;
          }) == 2);

    const auto imp = std::find_if(groups.begin(), groups.end(), [](const PoolGroup& g) {
        return g.mod->name == "of Impedance";
    });
    REQUIRE(imp != groups.end());
    CHECK(imp->all.size() == 2);
    // The map's entry leads, because `kDomains` is asked in order and a map is what the reader
    // is nearly always deciding about.
    CHECK(imp->mod->domain == kMapDomain);
    CHECK(imp->all[1]->domain == kChartDomain);

    // Rating the row writes the one key both entries share.
    Profile p("Softcore");
    p.set(imp->refs, Verdict::Deadly);
    CHECK(p.verdict_of(pool_key_refs(*imp->all[0])) == Verdict::Deadly);
    CHECK(p.verdict_of(pool_key_refs(*imp->all[1])) == Verdict::Deadly);

    // And a term is asked about every entry's rendering, not only the one on show: the two
    // pools roll different ranges and a number a term names may be in either.
    const std::vector<std::string> lines = group_lines(*imp, gd.get());
    CHECK(SearchFilter("hinder").matches(lines));
    CHECK(std::count(lines.begin(), lines.end(), "of Impedance") == 1); // unioned, not repeated
}

TEST_CASE("an implicit is a row like any other, because some of them roll") {
    const auto gd = fixture();
    const ppc::item::Item map = resolved(*gd, "map-rare-t16-corrupted.txt");

    const TempDir tmp("implicit");
    Store store;
    store.open(tmp.path, {}, "");

    std::vector<Row> rows = rate(map, store, gd.get());
    REQUIRE(!rows.empty());
    // First, because the item prints it first, and rateable, which is the change: an implicit
    // that can be rated in Settings and not here is the worse half of both rules.
    REQUIRE(rows[0].mod() != nullptr);
    CHECK(rows[0].mod()->type == ppc::data::ModType::Implicit);
    REQUIRE(rows[0].rateable());

    const std::vector<std::string> refs = rows[0].refs;
    store.set(refs, Verdict::Safe);
    rows = rate(map, store, gd.get());
    CHECK(rows[0].verdict == Verdict::Safe);
}

TEST_CASE("a first run is given a profile, so a map check always has somewhere to rate into") {
    const TempDir tmp("seed");
    Store s;
    s.open(tmp.path, {}, "");
    REQUIRE(s.names().size() == 1);
    CHECK(s.names()[0] == kDefaultProfile);
    CHECK(s.current() == kDefaultProfile);
    // Written, not just held: the popup's first click has to land somewhere on disk.
    CHECK(fs::exists(tmp.path / "Default.json"));
}

TEST_CASE("a new profile reaches the disk before anything else can be lost") {
    const TempDir tmp("create");
    Store s;
    s.open(tmp.path, {}, "");

    REQUIRE(s.create("Hardcore"));
    CHECK(s.current() == "Hardcore");
    // No flush, no tick: the file is there the moment the dialog closes.
    CHECK(fs::exists(tmp.path / "Hardcore.json"));
    // A name already taken, and one that sanitises to nothing.
    CHECK_FALSE(s.create("Hardcore"));
    CHECK_FALSE(s.create("  "));
}

TEST_CASE("a rating waits for the throttle and lands on a flush") {
    const TempDir tmp("throttle");
    Store s;
    s.open(tmp.path, {}, "");
    REQUIRE(s.create("Softcore"));

    s.set({"#% increased Monster Damage"}, Verdict::Deadly);
    CHECK(s.dirty());
    s.tick(); // nothing like long enough to have expired
    CHECK(s.dirty());
    s.flush();
    CHECK_FALSE(s.dirty());

    Store back;
    back.open(tmp.path, {"Softcore"}, "Softcore");
    CHECK(back.verdict_of({"#% increased Monster Damage"}) == Verdict::Deadly);
}

TEST_CASE("a store nothing has been opened on takes no rating and says nothing") {
    // Not a state the application can reach — `open` seeds a profile — but the guard is what
    // makes that true rather than assumed, and a rating with nowhere to go must not be counted
    // as something to write.
    Store s;
    s.set({"#% increased Monster Damage"}, Verdict::Deadly);
    CHECK_FALSE(s.dirty());
    CHECK(s.verdict_of({"#% increased Monster Damage"}) == Verdict::Unrated);
}

TEST_CASE("a new profile can start as a copy of one that already has opinions") {
    const TempDir tmp("copy");
    Store s;
    s.open(tmp.path, {}, "");
    REQUIRE(s.create("Base"));
    s.set({"Monsters reflect #% of Physical Damage"}, Verdict::Deadly);
    s.flush();

    REQUIRE(s.create("Derived", "Base"));
    CHECK(s.verdict_of({"Monsters reflect #% of Physical Damage"}) == Verdict::Deadly);
    // Copies, not shares: the two tables part company from here.
    s.set({"Monsters reflect #% of Physical Damage"}, Verdict::Dangerous);
    s.flush();
    s.select("Base");
    CHECK(s.verdict_of({"Monsters reflect #% of Physical Damage"}) == Verdict::Deadly);
}

TEST_CASE("the directory is the authority and the config list is the order") {
    const TempDir tmp("reconcile");
    Store seed;
    seed.open(tmp.path, {}, "");
    REQUIRE(seed.create("Alpha"));
    REQUIRE(seed.create("Beta"));
    // Dropped in by hand, which is how a table gets shared between two machines.
    { std::ofstream(tmp.path / "Zulu.json") << R"({"verdicts":{"a":"safe"}})"; }

    Store s;
    // The config remembers an order and one profile whose file has since gone. "Default" is
    // there because the seed run above started on an empty directory.
    s.open(tmp.path, {"Beta", "Alpha", "Ghost"}, "Beta");
    REQUIRE(s.names().size() == 4);
    CHECK(s.names()[0] == "Beta");   // the config's order, for the ones it still has
    CHECK(s.names()[1] == "Alpha");
    CHECK(s.names()[2] == "Default"); // found on disk, appended in name order
    CHECK(s.names()[3] == "Zulu");
    CHECK(s.current() == "Beta");
    s.select("Zulu");
    CHECK(s.verdict_of({"a"}) == Verdict::Safe);
}

TEST_CASE("a selection the config no longer has falls back rather than rating nothing") {
    const TempDir tmp("select");
    Store seed;
    seed.open(tmp.path, {}, "");
    REQUIRE(seed.create("Only"));

    Store s;
    s.open(tmp.path, {"Only"}, "Deleted");
    CHECK(s.current() == "Only");   // the config's first, since what it asked for is gone
    // And a selection of something that is not there leaves the current one alone.
    s.select("Deleted");
    CHECK(s.current() == "Only");
}

TEST_CASE("deleting a profile takes its file with it and lands the selection nearby") {
    const TempDir tmp("remove");
    Store s;
    s.open(tmp.path, {}, "");
    REQUIRE(s.create("One"));
    REQUIRE(s.create("Two"));
    REQUIRE(s.create("Three"));
    s.select("Two");
    s.set({"#% increased Monster Damage"}, Verdict::Deadly);

    CHECK(s.remove("Two"));
    CHECK(fs::exists(tmp.path / "One.json"));
    CHECK_FALSE(fs::exists(tmp.path / "Two.json"));
    // The neighbour, so deleting down a list leaves the selection under the hand.
    CHECK(s.current() == "Three");
    // And the rating that was still buffered must not write the file back out.
    s.flush();
    CHECK_FALSE(fs::exists(tmp.path / "Two.json"));
    CHECK_FALSE(s.remove("Two"));

    CHECK(s.remove("Three"));
    CHECK(s.remove("One"));
    CHECK(s.remove(kDefaultProfile));
    // Deleting the last one puts the default back rather than leaving the feature with nowhere
    // to write — the same reason `open` seeds it.
    REQUIRE(s.names().size() == 1);
    CHECK(s.names()[0] == kDefaultProfile);
    CHECK(s.current() == kDefaultProfile);
}
