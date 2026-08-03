#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "data/stat_normalize.hpp"

using namespace ppc::data;
using json = nlohmann::json;

// The whole point of this file: prove the C++ normalizer reproduces the builder's Python
// one. They join the same two datasets on the same key, and a divergence does not crash —
// it silently fails to match a mod, so the price check returns a confident wrong answer.
TEST_CASE("conformance vectors from the data release") {
    const std::string path =
        std::string(PPC_TEST_DATA_DIR) + "/stat-normalization-vectors.ndjson";
    std::ifstream in(path);
    REQUIRE_MESSAGE(in.good(), "missing conformance vectors: " << path);

    size_t checked = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const json v = json::parse(line, nullptr, false);
        REQUIRE_FALSE(v.is_discarded());

        const auto input = v.at("line").get<std::string>();
        const auto want_generic = v.at("generic").get<std::string>();
        const auto want_cands = v.at("candidates").get<std::vector<std::string>>();

        INFO("line: " << input);
        CHECK(placeholder_form(input) == want_generic);
        CHECK(candidates(input) == want_cands); // order matters
        ++checked;
    }
    MESSAGE("checked " << checked << " vectors");
    CHECK(checked > 300);
}

TEST_CASE("the sign belongs to the number") {
    CHECK(placeholder_form("+42 to maximum Life") == "# to maximum Life");
    CHECK(placeholder_form("-20% to Fire Resistance") == "#% to Fire Resistance");
    const auto toks = scan_numbers("+42 to maximum Life");
    REQUIRE(toks.size() == 1);
    CHECK(toks[0].value == doctest::Approx(42.0));
}

TEST_CASE("the lookbehind keeps ranges from splitting") {
    const auto toks = scan_numbers("Grants 1-30 Life per Enemy Hit");
    REQUIRE(toks.size() == 2);
    CHECK(toks[0].value == doctest::Approx(1.0));
    CHECK(toks[1].value == doctest::Approx(30.0)); // not -30
    CHECK(placeholder_form("Grants 1-30 Life per Enemy Hit") ==
          "Grants #-# Life per Enemy Hit");
}

TEST_CASE("advanced mod description ranges are absorbed") {
    const auto toks = scan_numbers("Adds 5(4-6) to 12(10-14) Physical Damage");
    REQUIRE(toks.size() == 2);
    CHECK(toks[0].bound_min == doctest::Approx(4.0));
    CHECK(toks[0].bound_max == doctest::Approx(6.0));
    CHECK(toks[1].bound_min == doctest::Approx(10.0));
    CHECK(toks[1].bound_max == doctest::Approx(14.0));
    CHECK(placeholder_form("Adds 5(4-6) to 12(10-14) Physical Damage") ==
          "Adds # to # Physical Damage");
}

TEST_CASE("a negative lower bound does not split at its own sign") {
    const auto toks = scan_numbers("+5(-20-10)% to something");
    REQUIRE(toks.size() == 1);
    CHECK(toks[0].numeric_bounds);
    CHECK(toks[0].bound_min == doctest::Approx(-20.0));
    CHECK(toks[0].bound_max == doctest::Approx(10.0));
}

TEST_CASE("decimals are counted") {
    const auto toks = scan_numbers("0.5% of Physical Attack Damage Leeched as Life");
    REQUIRE(toks.size() == 1);
    CHECK(toks[0].decimals == 1);
    CHECK(toks[0].value == doctest::Approx(0.5));
}

TEST_CASE("candidates are ordered most generic first") {
    CHECK(candidates("Adds 5 to 12 Physical Damage") ==
          std::vector<std::string>{"Adds # to # Physical Damage", "Adds # to 12 Physical Damage",
                                   "Adds 5 to # Physical Damage", "Adds 5 to 12 Physical Damage"});
}

TEST_CASE("a wording with no numbers yields itself once") {
    CHECK(candidates("No Physical Damage") == std::vector<std::string>{"No Physical Damage"});
}

TEST_CASE("empty parens are stripped before scanning") {
    CHECK(placeholder_form("Something ()odd") == "Something odd");
}

TEST_CASE("more than four numbers does not overflow the mask table") {
    // Only the first four are placeheld; the rest stay literal. This must not read past the
    // mask table or shift by 32.
    const std::string line = "1 2 3 4 5 6 7 of something";
    const auto cs = candidates(line);
    CHECK_FALSE(cs.empty());
    CHECK(cs.back() == line);
}
