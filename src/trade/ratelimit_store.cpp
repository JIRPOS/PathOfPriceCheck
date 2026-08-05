#include "trade/ratelimit_store.hpp"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

#include "paths.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ppc::trade::ratelimit_store {
namespace {

/// Bump to invalidate every file written by an older layout.
constexpr int kVersion = 1;

} // namespace

fs::path file() { return cache_dir() / "trade-ratelimit.json"; }

bool store(const LimiterState& s, int64_t now_unix_ms) { return store(file(), s, now_unix_ms); }

bool store(const fs::path& p, const LimiterState& s, int64_t now_unix_ms) {
    json policies = json::array();
    for (const PolicyState& ps : s) {
        json windows = json::array();
        for (size_t i = 0; i < ps.rules.size(); ++i) {
            windows.push_back(json{
                {"hits", ps.rules[i].hits},
                {"period", ps.rules[i].period_s},
                {"restrict", ps.rules[i].restrict_s},
                {"count", i < ps.hits.size() ? ps.hits[i] : 0},
                {"opened_at_ms",
                 now_unix_ms - (i < ps.window_age_ms.size() ? ps.window_age_ms[i] : 0)}});
        }
        policies.push_back(json{{"policy", ps.policy},
                                {"blocked_until_ms", ps.blocked_for_ms > 0
                                                         ? now_unix_ms + ps.blocked_for_ms
                                                         : int64_t{0}},
                                {"windows", std::move(windows)}});
    }

    json j;
    j["version"] = kVersion;
    j["saved_at_ms"] = now_unix_ms;
    j["policies"] = std::move(policies);

    ensure_dir(p.parent_path());
    std::ofstream out(p);
    if (!out) return false;
    out << j.dump() << "\n";
    return out.good();
}

LimiterState load(int64_t now_unix_ms) { return load(file(), now_unix_ms); }

LimiterState load(const fs::path& p, int64_t now_unix_ms) {
    LimiterState out;
    std::ifstream in(p);
    if (!in) return out;
    const json j = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return out;
    if (j.value("version", -1) != kVersion) return out;

    const auto policies = j.find("policies");
    if (policies == j.end() || !policies->is_array()) return out;

    for (const json& e : *policies) {
        if (!e.is_object()) continue;
        PolicyState ps;
        ps.policy = e.value("policy", std::string());
        if (ps.policy.empty()) continue;
        const int64_t until = e.value("blocked_until_ms", int64_t{0});
        ps.blocked_for_ms = std::max<int64_t>(0, until - now_unix_ms);
        if (const auto w = e.find("windows"); w != e.end() && w->is_array()) {
            for (const json& wj : *w) {
                if (!wj.is_object()) continue;
                ps.rules.push_back(RateRule{wj.value("hits", 0), wj.value("period", 0),
                                            wj.value("restrict", 0)});
                ps.hits.push_back(wj.value("count", 0));
                // Clamped in `RateLimiter::restore` too; done here as well so a file written
                // before a clock change cannot make a window look like it opened in the future.
                ps.window_age_ms.push_back(
                    std::max<int64_t>(0, now_unix_ms - wj.value("opened_at_ms", now_unix_ms)));
            }
        }
        out.push_back(std::move(ps));
    }
    return out;
}

} // namespace ppc::trade::ratelimit_store
