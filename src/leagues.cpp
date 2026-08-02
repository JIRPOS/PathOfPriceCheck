#include "leagues.hpp"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

#include "paths.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ppc {
namespace {

/// Bump to invalidate every cache written by an older layout.
constexpr int kCacheVersion = 1;

} // namespace

std::vector<std::string> parse_leagues(std::string_view body, std::string_view realm) {
    std::vector<std::string> out;
    json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return out;
    auto it = j.find("result");
    if (it == j.end() || !it->is_array()) return out;

    for (const json& e : *it) {
        if (!e.is_object()) continue;
        const auto id = e.find("id");
        const auto rl = e.find("realm");
        if (id == e.end() || !id->is_string()) continue;
        if (rl == e.end() || !rl->is_string() || rl->get<std::string>() != realm) continue;
        std::string s = id->get<std::string>();
        if (s.empty()) continue;
        // Linear scan, not a set: the list is a couple of dozen entries and API order is
        // load-bearing (the current challenge league sorts first).
        if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(std::move(s));
    }
    return out;
}

const std::vector<std::string>& fallback_leagues() {
    // Permanent leagues only — they always exist, so a first run with no network still has
    // something pickable. Challenge leagues are deliberately absent: naming one here would
    // go stale every few months.
    static const std::vector<std::string> kFallback{"Standard", "Hardcore", "Ruthless",
                                                    "Hardcore Ruthless"};
    return kFallback;
}

namespace league_cache {

fs::path file() { return cache_dir() / "leagues.json"; }

std::optional<LeagueList> load(const fs::path& p) {
    std::ifstream in(p);
    if (!in) return std::nullopt;
    json j = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    if (j.value("version", -1) != kCacheVersion) return std::nullopt;
    if (j.value("realm", std::string()) != kRealm) return std::nullopt;

    const auto ids = j.find("leagues");
    if (ids == j.end() || !ids->is_array()) return std::nullopt;

    LeagueList l;
    l.fetched_at = j.value("fetched_at", int64_t{0});
    for (const json& e : *ids)
        if (e.is_string()) l.ids.push_back(e.get<std::string>());
    if (l.ids.empty()) return std::nullopt;
    return l;
}

bool store(const fs::path& p, const LeagueList& l) {
    ensure_dir(p.parent_path());
    json j;
    j["version"] = kCacheVersion;
    j["realm"] = std::string(kRealm);
    j["fetched_at"] = l.fetched_at;
    j["leagues"] = l.ids;
    std::ofstream out(p);
    if (!out) return false;
    out << j.dump(2) << "\n";
    return out.good();
}

bool fresh(const LeagueList& l, int64_t now_s, int64_t ttl_s) {
    if (l.fetched_at <= 0 || l.fetched_at > now_s) return false;
    return now_s - l.fetched_at < ttl_s;
}

} // namespace league_cache

} // namespace ppc
