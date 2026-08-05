#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "net/http.hpp"
#include "trade/trade.hpp"

namespace ppc::trade {

/// The policy a request is spaced against. GGG counts each endpoint family separately and
/// publishes a different rule set for each, so these are the buckets the limiter keeps.
namespace policy {
inline constexpr std::string_view kSearch = "trade-search";
inline constexpr std::string_view kFetch = "trade-fetch";
inline constexpr std::string_view kData = "trade-data"; ///< leagues, static — cheap and rare
} // namespace policy

/// Every outbound GGG request goes through here. Waits out whatever the shared rate limiter
/// says is owed under `policy`, issues the request, then feeds the response's rate-limit
/// headers back in. Blocking; worker threads only.
///
/// Returns a transport error rather than waiting when the debt is longer than the caller
/// could plausibly want — a price check that arrives four minutes late is about an item the
/// user has long since sold.
net::Response request(const net::Request& r, std::string_view policy);

/// Unblocks a request that is sitting out a rate-limit wait, so shutdown does not stall for
/// the length of a restriction. Waits already elapsed are not undone.
void cancel_waits();

struct SearchOutcome {
    bool ok = false;
    SearchResults res;
    std::string error;
};

/// The two-step flow: POST the query, then GET the first `want` results in batches of ten.
SearchOutcome run_search(std::string_view league, const std::string& query_json, size_t want);

struct PageOutcome {
    bool ok = false;
    std::vector<Listing> listings;
    size_t consumed = 0; ///< hashes asked for, which is what the caller advances by
    std::string error;
};

/// One more page of a search already run: `count` hashes from `first`, in batches of ten.
/// Costs no search request — the hashes are already in hand from the original POST, and only
/// /fetch is spent. The search id does expire server-side eventually, which reads back as an
/// ordinary API error.
PageOutcome fetch_page(std::string_view query_id, const std::vector<std::string>& hashes,
                       size_t first, size_t count);

/// /api/trade/data/static, for the currency icons. Empty on failure — the panel falls back
/// to printing the currency's id.
std::vector<CurrencyEntry> fetch_static_currencies();

} // namespace ppc::trade
