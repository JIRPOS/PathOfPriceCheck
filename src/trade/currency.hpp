#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "trade/trade.hpp"

namespace ppc::trade {

inline constexpr int64_t kStaticTtlSeconds = 7 * 24 * 60 * 60;
inline constexpr const char* kStaticUrl = "https://www.pathofexile.com/api/trade/data/static";

/// Entries of every group of /api/trade/data/static that carries an image. A price names
/// fragments and shards as well as orbs, so the whole set is kept rather than "Currency" —
/// it is a couple of hundred kilobytes fetched once a week.
std::vector<CurrencyEntry> parse_static_currencies(std::string_view body);

struct CurrencyList {
    std::vector<CurrencyEntry> entries;
    int64_t fetched_at = 0; ///< unix seconds; 0 means never
};

namespace currency_cache {

std::filesystem::path file(); ///< cache_dir()/trade-static.json

/// nullopt if absent, unreadable, malformed, or written by an older schema.
std::optional<CurrencyList> load(const std::filesystem::path& p);
inline std::optional<CurrencyList> load() { return load(file()); }

bool store(const std::filesystem::path& p, const CurrencyList& l);
inline bool store(const CurrencyList& l) { return store(file(), l); }

/// A future timestamp counts as stale — a clock rollback must not pin a cache forever.
bool fresh(const CurrencyList& l, int64_t now_s, int64_t ttl_s = kStaticTtlSeconds);

} // namespace currency_cache

} // namespace ppc::trade
