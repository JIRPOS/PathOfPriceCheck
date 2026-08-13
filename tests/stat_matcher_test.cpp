#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "data/stat_matcher.hpp"

namespace fs = std::filesystem;
using namespace ppc::data;

namespace {

std::shared_ptr<GameData> fixture() {
    std::string err;
    auto gd = GameData::open(fs::path(PPC_TEST_DATA_DIR) / "bundle", "en", &err);
    REQUIRE_MESSAGE(gd != nullptr, "opening the fixture bundle failed: " << err);
    return gd;
}

std::optional<StatMatch> match(const GameData& gd, std::vector<std::string> lines,
                               ModType t = ModType::Explicit, double incr = 0) {
    return match_stat(gd, lines, 0, MatchContext{t, incr});
}

} // namespace

TEST_CASE("a plain roll resolves to its hash and value") {
    auto gd = fixture();
    const auto m = match(*gd, {"+42 to maximum Life"});
    REQUIRE(m.has_value());
    CHECK(m->stat->ref == "# to maximum Life");
    CHECK(m->value == doctest::Approx(42.0));
    CHECK(m->lines_consumed == 1);
    CHECK_FALSE(m->negated);
    CHECK(m->stat->trade_ids(ModType::Explicit).front() == "explicit.stat_3299347043");
}

TEST_CASE("a negate wording is stored in the canonical direction") {
    auto gd = fixture();
    const auto inc = match(*gd, {"23% increased Physical Damage"});
    const auto red = match(*gd, {"23% reduced Physical Damage"});
    REQUIRE(inc.has_value());
    REQUIRE(red.has_value());

    CHECK(inc->value == doctest::Approx(23.0));
    CHECK_FALSE(inc->negated);
    // Same stat, opposite sign — so summing an increase and a reduction cancels correctly.
    CHECK(red->stat == inc->stat);
    CHECK(red->negated);
    CHECK(red->value == doctest::Approx(-23.0));
}

TEST_CASE("a range the game prints high to low still comes out ordered") {
    auto gd = fixture();
    // An inverse wording's range is printed descending, and negating it leaves it that way:
    // -60..-65 is a trade filter wanting at least -60 and at most -65, i.e. nothing.
    const auto red = match(*gd, {"23(30-20)% reduced Physical Damage"});
    REQUIRE(red.has_value());
    REQUIRE(red->roll_bounds.size() == 1);
    CHECK(red->roll_bounds.front().first == doctest::Approx(-30.0));
    CHECK(red->roll_bounds.front().second == doctest::Approx(-20.0));

    const auto inc = match(*gd, {"23(20-30)% increased Physical Damage"});
    REQUIRE(inc.has_value());
    CHECK(inc->roll_bounds.front().first == doctest::Approx(20.0));
    CHECK(inc->roll_bounds.front().second == doctest::Approx(30.0));
}

TEST_CASE("a wording with no number carries its implied roll") {
    auto gd = fixture();
    const auto m = match(*gd, {"No Physical Damage"});
    REQUIRE(m.has_value());
    CHECK(m->value == doctest::Approx(-100.0));
    CHECK(m->stat->ref == "#% increased Physical Damage");
}

TEST_CASE("a two-number mod is filtered on its average") {
    auto gd = fixture();
    const auto m = match(*gd, {"Adds 5 to 12 Physical Damage"});
    REQUIRE(m.has_value());
    CHECK(m->rolls == std::vector<double>{5.0, 12.0});
    CHECK(m->value == doctest::Approx(8.5));
}

TEST_CASE("advanced mod description bounds become the filter range") {
    auto gd = fixture();
    const auto m = match(*gd, {"Adds 5(4-6) to 12(10-14) Physical Damage"});
    REQUIRE(m.has_value());
    CHECK(m->value == doctest::Approx(8.5));
    REQUIRE(m->min.has_value());
    REQUIRE(m->max.has_value());
    CHECK(*m->min == doctest::Approx(4.0));
    CHECK(*m->max == doctest::Approx(14.0));
    CHECK_FALSE(m->legacy);
}

TEST_CASE("a roll outside its stated bounds is flagged legacy") {
    auto gd = fixture();
    // Pre-nerf roll: above the range the mod can currently roll.
    const auto m = match(*gd, {"+60(20-30)% to Fire Resistance"});
    REQUIRE(m.has_value());
    CHECK(m->value == doctest::Approx(60.0));
    CHECK(m->legacy);
    REQUIRE(m->max.has_value());
    CHECK(*m->max == doctest::Approx(60.0)); // widened to admit the actual roll
}

TEST_CASE("reminder text between lines is skipped") {
    auto gd = fixture();
    const auto m = match(*gd, {"+42 to maximum Life", "(this is reminder text)"});
    REQUIRE(m.has_value());
    CHECK(m->value == doctest::Approx(42.0));
    CHECK(is_reminder_text("(anything at all)"));
    CHECK(is_reminder_text("  (padded)  "));
    CHECK_FALSE(is_reminder_text("+42 to maximum Life"));
    CHECK_FALSE(is_reminder_text("(partial"));
}

TEST_CASE("an unscalable suffix is stripped and recorded") {
    auto gd = fixture();
    const auto m = match(*gd, {"+42 to maximum Life (unscalable value)"});
    REQUIRE(m.has_value());
    CHECK(m->unscalable);
    CHECK(m->value == doctest::Approx(42.0));
}

TEST_CASE("a modifier with no roll at all gets the em-dash unscalable suffix instead") {
    // A Heist Contract's own boolean effects: "Monsters are Hexproof \xe2\x80\x94 Unscalable
    // Value" is the em-dash, title-case form the numeric "(unscalable value)" parenthetical
    // never covered — both spellings mean the same thing and are looked up from the same
    // lexicon list, see UnscalableSuffixes.
    auto gd = fixture();
    const auto m = match(*gd, {"Monsters are Hexproof \xe2\x80\x94 Unscalable Value"});
    REQUIRE(m.has_value());
    CHECK(m->stat->ref == "Monsters are Hexproof");
    CHECK(m->unscalable);
}

TEST_CASE("Wine's plain-hyphen clipboard fallback is accepted for the same suffix") {
    // The same fallback risk parse.cpp's info-line tags carry: right after a copy PoE has
    // often rendered only CF_TEXT, and every em dash arrives as a plain hyphen.
    auto gd = fixture();
    const auto m = match(*gd, {"Monsters are Hexproof - Unscalable Value"});
    REQUIRE(m.has_value());
    CHECK(m->stat->ref == "Monsters are Hexproof");
    CHECK(m->unscalable);
}

TEST_CASE("roll_incr scales the roll, but never an unscalable one") {
    auto gd = fixture();
    const auto scaled = match(*gd, {"+100 to maximum Life"}, ModType::Explicit, 20.0);
    REQUIRE(scaled.has_value());
    CHECK(scaled->value == doctest::Approx(120.0));

    const auto fixed = match(*gd, {"+100 to maximum Life (unscalable value)"},
                             ModType::Explicit, 20.0);
    REQUIRE(fixed.has_value());
    CHECK(fixed->value == doctest::Approx(100.0));
}

TEST_CASE("incr_roll truncates the way the game does") {
    CHECK(incr_roll(10.0, 15.0, 0) == doctest::Approx(11.0)); // 11.5 -> 11, not 12
    CHECK(incr_roll(10.0, 15.0, 2) == doctest::Approx(11.5));
}

TEST_CASE("the wrong mod type does not match") {
    auto gd = fixture();
    CHECK_FALSE(match(*gd, {"+42 to maximum Life"}, ModType::Enchant).has_value());
    CHECK(match(*gd, {"+42 to maximum Life"}, ModType::Implicit).has_value());
}

TEST_CASE("an unknown wording matches nothing") {
    auto gd = fixture();
    CHECK_FALSE(match(*gd, {"+42 to maximum Nonsense"}).has_value());
    CHECK_FALSE(match(*gd, {""}).has_value());
    CHECK_FALSE(match_stat(*gd, std::vector<std::string>{}, 0, MatchContext{}).has_value());
}

TEST_CASE("matching starts at the requested line") {
    auto gd = fixture();
    const std::vector<std::string> lines{"unrelated junk", "+42 to maximum Life"};
    const auto m = match_stat(*gd, lines, 1, MatchContext{});
    REQUIRE(m.has_value());
    CHECK(m->value == doctest::Approx(42.0));
}
