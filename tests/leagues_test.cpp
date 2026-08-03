#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fstream>

#include "leagues.hpp"

namespace fs = std::filesystem;
using namespace ppc;

namespace {

/// Trimmed from a live GET of /api/trade/data/leagues: the eight pc leagues, then the same
/// eight ids repeated for xbox and sony. Embedded rather than kept as a fixture file so the
/// test has no cwd dependency.
constexpr const char* kPayload = R"({"result":[
{"id":"Allflame","realm":"pc","text":"Allflame"},
{"id":"Hardcore Allflame","realm":"pc","text":"Hardcore Allflame"},
{"id":"Ruthless Allflame","realm":"pc","text":"Ruthless Allflame"},
{"id":"HC Ruthless Allflame","realm":"pc","text":"HC Ruthless Allflame"},
{"id":"Standard","realm":"pc","text":"Standard"},
{"id":"Hardcore","realm":"pc","text":"Hardcore"},
{"id":"Ruthless","realm":"pc","text":"Ruthless"},
{"id":"Hardcore Ruthless","realm":"pc","text":"Hardcore Ruthless"},
{"id":"Allflame","realm":"xbox","text":"Allflame"},
{"id":"Standard","realm":"xbox","text":"Standard"},
{"id":"Hardcore","realm":"xbox","text":"Hardcore"},
{"id":"Allflame","realm":"sony","text":"Allflame"},
{"id":"Standard","realm":"sony","text":"Standard"}]})";

fs::path scratch(const char* name) {
    const fs::path p = fs::temp_directory_path() / "ppc-leagues-test" / name;
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::remove(p, ec);
    return p;
}

void write(const fs::path& p, const char* text) { std::ofstream(p) << text; }

} // namespace

TEST_CASE("parse_leagues filters by realm and preserves API order") {
    const auto pc = parse_leagues(kPayload, "pc");
    REQUIRE(pc.size() == 8);
    CHECK(pc.front() == "Allflame"); // the challenge league sorts first
    CHECK(pc[4] == "Standard");
    CHECK(pc.back() == "Hardcore Ruthless");
}

TEST_CASE("ids repeated across realms are not duplicated") {
    const auto pc = parse_leagues(kPayload, "pc");
    CHECK(std::count(pc.begin(), pc.end(), std::string("Standard")) == 1);
    CHECK(std::count(pc.begin(), pc.end(), std::string("Allflame")) == 1);
}

TEST_CASE("other realms are selectable") {
    CHECK(parse_leagues(kPayload, "xbox").size() == 3);
    CHECK(parse_leagues(kPayload, "sony").size() == 2);
    CHECK(parse_leagues(kPayload, "poe2").empty());
}

TEST_CASE("malformed payloads yield an empty list and never throw") {
    CHECK(parse_leagues("").empty());
    CHECK(parse_leagues("{}").empty());
    CHECK(parse_leagues("[]").empty());
    CHECK(parse_leagues(R"({"result":"nope"})").empty());
    CHECK(parse_leagues(R"({"result":[{"id":"X"}]})").empty()); // no realm
    CHECK(parse_leagues(R"({"result":[{"realm":"pc"}]})").empty()); // no id
    CHECK(parse_leagues(R"({"result":[{"id":"","realm":"pc"}]})").empty()); // empty id
    CHECK(parse_leagues(R"({"result":[{"id":123,"realm":"pc"}]})").empty()); // wrong type
    CHECK(parse_leagues(R"({"result":[{"id":"Allflame","realm":"pc")").empty()); // truncated
}

TEST_CASE("fallback list is non-empty and permanent-only") {
    const auto& f = fallback_leagues();
    REQUIRE_FALSE(f.empty());
    CHECK(std::find(f.begin(), f.end(), std::string("Standard")) != f.end());
    CHECK(std::find(f.begin(), f.end(), std::string("Hardcore")) != f.end());
}

TEST_CASE("cache round-trips") {
    const fs::path p = scratch("roundtrip.json");
    const LeagueList in{{"Allflame", "Standard", "Hardcore"}, 1754150400};
    REQUIRE(league_cache::store(p, in));

    const auto out = league_cache::load(p);
    REQUIRE(out.has_value());
    CHECK(out->ids == in.ids);
    CHECK(out->fetched_at == in.fetched_at);
}

TEST_CASE("cache rejects what it cannot trust") {
    CHECK_FALSE(league_cache::load(scratch("absent.json")).has_value());

    const fs::path garbage = scratch("garbage.json");
    write(garbage, "garbage");
    CHECK_FALSE(league_cache::load(garbage).has_value());

    const fs::path future = scratch("future.json");
    write(future, R"({"version":99,"realm":"pc","fetched_at":1,"leagues":["Standard"]})");
    CHECK_FALSE(league_cache::load(future).has_value());

    // Written for a different realm — must not be served as pc data.
    const fs::path realm = scratch("realm.json");
    write(realm, R"({"version":1,"realm":"xbox","fetched_at":1,"leagues":["Standard"]})");
    CHECK_FALSE(league_cache::load(realm).has_value());

    // An empty list is useless and would leave the dropdown blank.
    const fs::path empty = scratch("empty.json");
    write(empty, R"({"version":1,"realm":"pc","fetched_at":1,"leagues":[]})");
    CHECK_FALSE(league_cache::load(empty).has_value());
}

TEST_CASE("freshness boundaries") {
    constexpr int64_t now = 1'000'000'000;
    CHECK(league_cache::fresh({{"Standard"}, now - kLeagueTtlSeconds + 1}, now));
    CHECK_FALSE(league_cache::fresh({{"Standard"}, now - kLeagueTtlSeconds}, now));
    CHECK_FALSE(league_cache::fresh({{"Standard"}, now - kLeagueTtlSeconds - 1}, now));
    CHECK_FALSE(league_cache::fresh({{"Standard"}, 0}, now));       // never fetched
    CHECK_FALSE(league_cache::fresh({{"Standard"}, now + 3600}, now)); // clock rollback
}
