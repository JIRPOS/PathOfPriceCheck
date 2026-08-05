#include "trade/currency.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

#include "paths.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ppc::trade {
namespace {

/// Bump to invalidate every cache written by an older layout.
constexpr int kCacheVersion = 1;

} // namespace

std::vector<CurrencyEntry> parse_static_currencies(std::string_view body) {
    std::vector<CurrencyEntry> out;
    const json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return out;
    const auto r = j.find("result");
    if (r == j.end() || !r->is_array()) return out;
    for (const json& group : *r) {
        if (!group.is_object()) continue;
        const auto entries = group.find("entries");
        if (entries == group.end() || !entries->is_array()) continue;
        for (const json& e : *entries) {
            if (!e.is_object()) continue;
            const auto id = e.find("id");
            const auto img = e.find("image");
            // No image, nothing to draw: what the panel wants is the symbol, and the name is
            // only the tooltip behind it. Half the Heist and Sanctum entries have none.
            if (id == e.end() || !id->is_string() || img == e.end() || !img->is_string()) continue;
            CurrencyEntry c;
            c.id = id->get<std::string>();
            c.image = img->get<std::string>();
            if (const auto t = e.find("text"); t != e.end() && t->is_string())
                c.text = t->get<std::string>();
            out.push_back(std::move(c));
        }
    }
    return out;
}

namespace currency_cache {

fs::path file() { return cache_dir() / "trade-static.json"; }

std::optional<CurrencyList> load(const fs::path& p) {
    std::ifstream in(p);
    if (!in) return std::nullopt;
    const json j = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    if (j.value("version", -1) != kCacheVersion) return std::nullopt;

    const auto entries = j.find("entries");
    if (entries == j.end() || !entries->is_array()) return std::nullopt;

    CurrencyList l;
    l.fetched_at = j.value("fetched_at", int64_t{0});
    for (const json& e : *entries) {
        if (!e.is_object()) continue;
        CurrencyEntry c;
        c.id = e.value("id", std::string());
        c.text = e.value("text", std::string());
        c.image = e.value("image", std::string());
        if (!c.id.empty() && !c.image.empty()) l.entries.push_back(std::move(c));
    }
    if (l.entries.empty()) return std::nullopt;
    return l;
}

bool store(const fs::path& p, const CurrencyList& l) {
    ensure_dir(p.parent_path());
    json entries = json::array();
    for (const CurrencyEntry& c : l.entries)
        entries.push_back(json{{"id", c.id}, {"text", c.text}, {"image", c.image}});
    json j;
    j["version"] = kCacheVersion;
    j["fetched_at"] = l.fetched_at;
    j["entries"] = std::move(entries);
    std::ofstream out(p);
    if (!out) return false;
    out << j.dump() << "\n";
    return out.good();
}

bool fresh(const CurrencyList& l, int64_t now_s, int64_t ttl_s) {
    if (l.fetched_at <= 0 || l.fetched_at > now_s) return false;
    return now_s - l.fetched_at < ttl_s;
}

} // namespace currency_cache

} // namespace ppc::trade
