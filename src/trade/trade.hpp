#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ppc::trade {

/// Which listings a search asks for. Ids and labels are GGG's own, from `status_filters` in
/// /api/trade/data/filters — a closed five-entry vocabulary, unlike the league list, so it is
/// a table here rather than something fetched. Order matches the site's dropdown.
struct StatusOption {
    std::string_view id;    ///< what `status.option` is set to
    std::string_view label; ///< what the site calls it, for the Settings dropdown
};
const std::vector<StatusOption>& status_options();

/// **Instant Buyout.** A listing whose price can be taken without the seller being at their
/// keyboard, which is what most people mean by "for sale" now — searching In Person instead
/// hides an order of magnitude more offers than it shows.
inline constexpr std::string_view kDefaultStatus = "securable";

bool valid_status(std::string_view id);
/// The site's own name for an id, or the id itself if it is not one we know.
std::string_view status_label(std::string_view id);

inline constexpr const char* kApiBase = "https://www.pathofexile.com/api/trade";
inline constexpr const char* kWebBase = "https://www.pathofexile.com/trade/search";
/// Where the static data's image paths are rooted.
inline constexpr const char* kCdnBase = "https://web.poecdn.com";

/// Hard API limit, not a courtesy: a fetch with eleven ids is a 400.
inline constexpr size_t kFetchBatch = 10;

/// How many listings a price check pulls, offered in Settings. The choice is really about
/// **rate limit**, not latency: every ten listings is one more fetch request, and the binding
/// policy allows 50 fetches per five minutes before a five-minute lockout. So Top 20 is 25
/// price checks in that window, Top 50 is 10 and Top 100 is 5 — and Top 100 also trips the
/// 16-per-12-seconds rule on two checks in a row, which the limiter then has to stall for.
///
/// The cost lands where the rows help least. Only `min(want, total)` is ever fetched, so a
/// rare with four matches costs one request whatever this is set to; the price is paid on
/// liquid items, where the cheapest twenty already set the price and the rest are outliers.
const std::vector<int>& result_counts();
bool valid_result_count(int n);
inline constexpr int kDefaultResultCount = 20;

/// Fetch requests one price check spends, which is what Settings shows beside the choice.
int fetch_requests(int count);

/// One listing, reduced to what the panel shows.
struct Listing {
    std::string account;    ///< "Name#1234"
    std::string whisper;    ///< the in-game whisper the site would put on the clipboard
    std::string price_type; ///< "~price", "~b/o", or empty
    double amount = 0;
    std::string currency;   ///< trade's own id ("chaos"), which keys the static currency data
    int64_t indexed_at = 0; ///< unix seconds; 0 when the listing carried no timestamp
    bool priced = false;    ///< a listing in a tab with no price note is offer-only
    /// Gold the trade charges on top of the price, which the site prints under it as "Fee".
    /// A sibling of `price` rather than part of it, and 0 when the listing carries none.
    int64_t fee = 0;

    /// The seller's item **as clipboard text** — `item.extended.text`, which the API sends
    /// base64'd and which is byte-for-byte the format PoE writes on Ctrl+C. So a listing goes
    /// through `parse_item` → `resolve` → `derive` → the same tooltip renderer as the item in
    /// hand, with no second parser and no second view. Empty when the listing carried none.
    std::string item_text;
};

struct SearchResults {
    std::string query_id; ///< identifies the search, both to /fetch and to the web UI
    int total = 0;        ///< matches found, which is usually far more than were fetched
    /// Capped at 100 by the API however large `total` is, so that is the ceiling on how deep
    /// "load more" can go — past it the search has to be narrowed, not paged.
    std::vector<std::string> hashes;
    std::vector<Listing> listings;
    /// Hashes asked for so far, which is where the next page starts. Not `listings.size()`:
    /// a listing sold since the search comes back as a null element and is dropped, so the
    /// two diverge and paging off the listing count would re-fetch what was already seen.
    size_t fetched = 0;
};

/// One entry of /api/trade/data/static — a currency's display name and its CDN image path.
struct CurrencyEntry {
    std::string id;    ///< what a listing's price names, "divine"
    std::string text;  ///< "Divine Orb"
    std::string image; ///< path under kCdnBase
};

} // namespace ppc::trade
