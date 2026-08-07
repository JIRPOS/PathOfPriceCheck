#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "item/plan.hpp"
#include "trade/trade.hpp"

namespace ppc::trade {

/// False for a plan nothing can be searched from — an unsupported class, a strategy priced in
/// bulk rather than through listings (currency), or a gem the plan could not name.
bool searchable(const item::SearchPlan& p);

/// The search query as the trade API's JSON. `StatFilter::inverted` is applied here and
/// nowhere earlier: the plan states the roll as the game prints it, and only the wire
/// format wants the opposite sign.
///
/// `status` is a user preference rather than anything about the item, which is why it is a
/// parameter and not a `SearchPlan` field.
std::string build_query(const item::SearchPlan& p, std::string_view status = kDefaultStatus);

std::string search_url(std::string_view league);
std::string fetch_url(const std::vector<std::string>& hashes, std::string_view query_id);
/// The page for a search the API has already run — the id is the whole state.
std::string web_url(std::string_view league, std::string_view query_id);
/// The same search in the browser without spending an API call on it: the site accepts the
/// query JSON in `?q=`, so the button works whether or not we searched ourselves.
std::string web_url_for_query(std::string_view league, std::string_view query_json);

/// Both return false on malformed input; neither throws.
bool parse_search(std::string_view body, SearchResults& out);
bool parse_fetch(std::string_view body, std::vector<Listing>& out);

/// "2026-08-05T18:21:50Z" -> unix seconds, 0 if unparsable. Deliberately not timegm(),
/// which Windows spells differently and which consults the process time zone on some libcs.
int64_t parse_iso8601_utc(std::string_view s);

/// How long ago a listing was indexed, as the panel prints it: "3m", "5h", "12d".
std::string age_text(int64_t indexed_at, int64_t now_s);

/// The amount as the trade site writes it: no trailing zeros, halves kept ("1.5").
std::string price_text(double amount);

/// A listing fee, grouped as the site prints it ("3,520"). Grouped by hand because the C
/// locale is off limits here — see CLAUDE.md — and would not group under "C" anyway.
std::string gold_text(int64_t gold);

std::string url_encode(std::string_view s);

} // namespace ppc::trade
