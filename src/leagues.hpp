#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ppc {

/// The trade API repeats every league id once per realm. We only ever want "pc": this binary
/// reads a PC clipboard, matches a PC window title and synthesises PC keystrokes, so a console
/// player cannot be running it. Promote to a Config field the day that stops being true.
inline constexpr std::string_view kRealm = "pc";

inline constexpr int64_t kLeagueTtlSeconds = 24 * 60 * 60;
inline constexpr const char* kLeaguesUrl = "https://www.pathofexile.com/api/trade/data/leagues";

struct LeagueList {
    std::vector<std::string> ids; ///< API order preserved — the challenge league sorts first
    int64_t fetched_at = 0;       ///< unix seconds; 0 means never
};

/// Ids for `realm` from /api/trade/data/leagues, API order preserved, duplicates dropped.
/// Returns empty on malformed input — never throws.
std::vector<std::string> parse_leagues(std::string_view body, std::string_view realm = kRealm);

/// Used when there is neither a cache nor a successful fetch, so the dropdown is never empty.
const std::vector<std::string>& fallback_leagues();

namespace league_cache {

std::filesystem::path file(); ///< cache_dir()/leagues.json

/// nullopt if absent, unreadable, malformed, or written by a different schema/realm.
std::optional<LeagueList> load(const std::filesystem::path& p);
inline std::optional<LeagueList> load() { return load(file()); }

bool store(const std::filesystem::path& p, const LeagueList& l);
inline bool store(const LeagueList& l) { return store(file(), l); }

/// A future timestamp counts as stale: a clock rollback or a hand-edited file must not pin
/// a cache forever.
bool fresh(const LeagueList& l, int64_t now_s, int64_t ttl_s = kLeagueTtlSeconds);

} // namespace league_cache

} // namespace ppc
