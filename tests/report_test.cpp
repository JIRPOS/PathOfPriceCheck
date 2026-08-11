#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "item/derive.hpp"
#include "item/plan.hpp"
#include "item/resolve.hpp"
#include "parse_en.hpp"
#include "report/report.hpp"
#include "util/base64.hpp"
#include "util/png.hpp"

namespace fs = std::filesystem;
using namespace ppc;

// The two halves of a bug report that leave this machine: the request body, and the picture in
// it. Both are pinned here rather than at the relay because the relay is the thing being
// protected — what it refuses is its business, and what we promise to send is ours.

namespace {

std::shared_ptr<data::GameData> fixture() {
    std::string err;
    auto gd = data::GameData::open(fs::path(PPC_TEST_DATA_DIR) / "bundle", "en", &err);
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

/// Everything a report carries about one captured item, built the way `App` builds it.
struct Priced {
    item::Item it;
    item::Derived derived;
    item::SearchPlan plan;
};

Priced price(const data::GameData& gd, const std::string& text) {
    Priced p;
    std::optional<item::Item> parsed = item::parse_item_en(text);
    REQUIRE(parsed.has_value());
    p.it = std::move(*parsed);
    item::resolve_item(gd, p.it);
    p.derived = item::derive(&gd, p.it);
    p.plan = item::build_plan(gd, p.it, p.derived);
    return p;
}

} // namespace

TEST_CASE("the parse dump names the item, every modifier and what the search would ask") {
    const auto gd = fixture();
    const std::string text = capture("rare-bow-elder.txt");
    const Priced p = price(*gd, text);
    const std::string dump = report::describe(p.it, p.derived, p.plan);

    CHECK(dump.find("== Item ==") != std::string::npos);
    CHECK(dump.find("== Modifiers ==") != std::string::npos);
    CHECK(dump.find("== Search plan ==") != std::string::npos);
    CHECK(dump.find("strategy: ") != std::string::npos);
    // One line per modifier the game printed, whatever became of it. A dump that quietly drops
    // the mod nobody could match is a dump that hides the bug it exists to report.
    for (size_t i = 0; i < p.it.mods.size(); ++i)
        CHECK(dump.find("[" + std::to_string(i) + "] ") != std::string::npos);
}

TEST_CASE("a wording nothing matched says so in words") {
    const auto gd = fixture();
    // The fixture bundle knows a handful of stats, so an invented modifier is guaranteed to
    // reach the branch that matters.
    const std::string text =
        "Item Class: Bows\nRarity: Rare\nDoom Song\nSpine Bow\n--------\nItem Level: 84\n"
        "--------\nGrants Level 40 Nonsense Aura Skill\n";
    const Priced p = price(*gd, text);
    const std::string dump = report::describe(p.it, p.derived, p.plan);
    CHECK(dump.find("NO MATCH") != std::string::npos);
}

TEST_CASE("the body carries exactly the fields the relay reads, and no others") {
    report::Report r;
    r.item = "Item Class: Bows\n--------\nSpine Bow\n";
    r.parse = "== Item ==\n";
    r.comment = "priced as the wrong base";
    r.meta.version = "0.6.0";
    r.meta.os = "Linux";
    r.meta.league = "Standard";
    r.meta.bundle = "2026-08-01";

    const nlohmann::json j = nlohmann::json::parse(report::to_json(r));
    CHECK(j["item"] == r.item);
    CHECK(j["parse"] == r.parse);
    CHECK(j["comment"] == r.comment);
    CHECK(j["meta"]["version"] == "0.6.0");
    CHECK(j["meta"]["bundle"] == "2026-08-01");
    // Nothing else at all. The relay ignores what it does not know, but a field appearing here
    // that nobody agreed to send is exactly the change this test is meant to catch.
    CHECK(j.size() == 4);
    CHECK(j.find("screenshot_png_b64") == j.end());
}

TEST_CASE("an empty comment and an empty meta field are left out rather than sent blank") {
    report::Report r;
    r.item = "x";
    r.meta.version = "0.6.0";
    const nlohmann::json j = nlohmann::json::parse(report::to_json(r));
    CHECK(j.find("comment") == j.end());
    CHECK(j["meta"].size() == 1);
}

TEST_CASE("a screenshot rides as base64 of the PNG bytes and nowhere else") {
    report::Report r;
    r.item = "x";
    r.png = "\x89PNGfake";
    const nlohmann::json j = nlohmann::json::parse(report::to_json(r));
    const auto decoded = base64_decode(j["screenshot_png_b64"].get<std::string>());
    REQUIRE(decoded.has_value());
    CHECK(*decoded == r.png);
}

TEST_CASE("$PPC_REPORT_URL wins, and the default is the deployed relay") {
    CHECK(report::relay_url().find("https://") == 0);
#ifdef _WIN32
    _putenv_s("PPC_REPORT_URL", "https://example.invalid/report");
#else
    setenv("PPC_REPORT_URL", "https://example.invalid/report", 1);
#endif
    CHECK(report::relay_url() == "https://example.invalid/report");
#ifdef _WIN32
    _putenv_s("PPC_REPORT_URL", "");
#else
    unsetenv("PPC_REPORT_URL");
#endif
}

TEST_CASE("the relay's own reason for a refusal is what the user is shown") {
    const report::Outcome o =
        report::read_response(400, R"({"error":"comment is longer than 2000 characters"})", "");
    CHECK_FALSE(o.ok);
    CHECK(o.error == "comment is longer than 2000 characters");
}

TEST_CASE("a transport failure is reported as one, and a success carries the id") {
    const report::Outcome dead = report::read_response(0, "", "could not resolve host");
    CHECK_FALSE(dead.ok);
    CHECK(dead.error.find("could not resolve host") != std::string::npos);

    const report::Outcome ok = report::read_response(200, R"({"ok":true,"id":"a1b2c3d4"})", "");
    CHECK(ok.ok);
    CHECK(ok.id == "a1b2c3d4");
}

TEST_CASE("a 200 with no id is not a success") {
    // The relay only ever answers 200 with an id. Anything else under that status is a proxy,
    // a captive portal or a misconfiguration, and reporting it as sent would be the one lie
    // this dialog must not tell.
    const report::Outcome o = report::read_response(200, "<html>hello</html>", "");
    CHECK_FALSE(o.ok);
    CHECK_FALSE(o.error.empty());
}

TEST_CASE("the encoder writes a PNG a decoder would accept") {
    constexpr int w = 7, h = 5;
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = static_cast<uint8_t>(i * 7);
    const std::vector<uint8_t> png = encode_png(rgba.data(), w, h);
    REQUIRE(png.size() > 8);

    const uint8_t sig[]{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    CHECK(std::equal(std::begin(sig), std::end(sig), png.begin()));
    // IHDR is always the first chunk, and its payload is the size we asked for.
    CHECK(std::string(png.begin() + 12, png.begin() + 16) == "IHDR");
    CHECK(png[16] == 0);
    CHECK(png[19] == w);
    CHECK(png[23] == h);
    CHECK(png[24] == 8); // bits per channel
    CHECK(png[25] == 6); // truecolour with alpha
    CHECK(std::string(png.end() - 8, png.end() - 4) == "IEND");
}

TEST_CASE("a flat image compresses, which is the whole reason deflate is here") {
    // A panel is mostly flat, and it is the difference between a payload the relay accepts and
    // one it refuses at five megabytes.
    constexpr int w = 400, h = 400;
    const std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4, 0x20);
    const std::vector<uint8_t> png = encode_png(rgba.data(), w, h);
    CHECK(png.size() < rgba.size() / 100);
}

TEST_CASE("nothing to encode is nothing, not a malformed file") {
    CHECK(encode_png(nullptr, 4, 4).empty());
    const std::vector<uint8_t> one(4);
    CHECK(encode_png(one.data(), 0, 4).empty());
    CHECK(encode_png(one.data(), 4, -1).empty());
}
