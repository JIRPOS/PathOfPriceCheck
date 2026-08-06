#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/// GGG's own record of the **in-game currency exchange** — the market a stack of currency, a
/// scarab or a fragment actually changes hands on. Nothing in that market is a trade listing,
/// so the trade site has nothing to say about any of it: the search below the panel is not
/// merely empty for these items, it is the wrong question. This is the right one.
///
/// The feed is public and unauthenticated, on the CDN rather than on the API host, and needs
/// no registered application — see https://www.pathofexile.com/developer/docs/reference. It
/// is published as **hourly digests** of every market in every league, addressed by the unix
/// timestamp of the hour they cover. Two consequences shape everything here:
///
/// - **It is purely historical.** The current hour does not exist yet (it answers 404 with a
///   well-formed empty payload), so the freshest possible answer is the hour that just ended.
/// - **A published hour never changes**, so a digest can be cached for as long as there is
///   disk to keep it on and the only reason to fetch again is that a newer hour exists.
///
/// The payload states every item by its `Metadata/Items/...` path and carries no names at all,
/// which is why `data::BaseType::metadata_id` exists. A bundle without that field simply has
/// no exchange prices — the honest failure, and the reason nothing here may assume one.
///
/// Everything in this header is pure, so it is testable against a captured digest with no
/// network. `exchange/client` fetches, `exchange/cache` stores, `ExchangeService` threads it.
namespace ppc::exchange {

inline constexpr const char* kApiBase = "https://web.poecdn.com/api/currency-exchange";

/// The two the market is denominated in. Every other item is quoted against one or both, and
/// they are the only pairs kept: a scarab-for-essence market is a real market and is no use
/// to somebody asking what a scarab is worth.
inline constexpr const char* kChaosId = "Metadata/Items/Currency/CurrencyRerollRare";
inline constexpr const char* kDivineId = "Metadata/Items/Currency/CurrencyModValues";

/// The newest hour the feed can hold: the last one that has fully elapsed. The hour in
/// progress is not published, and asking for it is a 404.
int64_t latest_hour(int64_t now_s);

std::string url(int64_t hour);
/// File name for this digest under `<cache>/exchange/`. The hour is the whole key — a
/// published digest never changes, so there is nothing else to version it by.
std::string cache_name(int64_t hour);

/// What one item was worth in one denominating currency over the hour.
///
/// The payload gives the cheapest and dearest *ratio* traded rather than an average, so the
/// band is the honest shape of the hour: the exchange is an order book, not a price list, and
/// an hour of one has two ends. What it does publish is how much of **each side** changed
/// hands, and that is an average — see `average()`.
struct Rate {
    double low = 0;            ///< price of one, at the cheapest ratio the hour saw
    double high = 0;           ///< and at the dearest
    double volume = 0;         ///< units of the *item* that changed hands in this market
    double volume_against = 0; ///< and units of the currency it traded against

    bool known() const { return low > 0 && high > 0; }

    /// What one item cleared at on average over the hour, weighted by volume — every trade in
    /// the market moved some of each side, so the two totals divided are the mean ratio of all
    /// of them, not the midpoint of the band. 0 when either side published no volume, which
    /// is not a price of zero and must not be drawn as one.
    double average() const;
};

/// A rate turned into something that reads like a price.
///
/// Quoted against whichever of the two is worth more, so the counts stay above one — that is
/// the direction players say it in: three embers to a chaos, never a third of a chaos each.
/// The **average** decides which way round that is, because it is the number the summary line
/// leads with; the top of the band stands in when the hour published no volume to average.
/// Amounts are rounded to what a price is actually quoted in, the same rule the poe.ninja row
/// uses.
struct Reading {
    double low = 0, high = 0;
    double avg = 0; ///< 0 when the hour published no volume on one of the sides
    /// True when the pair was turned round, so every number counts **items** per one of the
    /// currency rather than the other way about.
    bool inverted = false;
};
Reading read(const Rate& r);

/// One item's markets against the two currencies everything is denominated in.
struct Listing {
    std::string metadata_id;
    Rate chaos, divine;

    bool known() const { return chaos.known() || divine.known(); }
};

/// One hourly digest, reduced to one league and to the pairs a price check can read.
struct Digest {
    int64_t hour = 0; ///< the hour it covers, which is also its cache key
    std::string league;
    /// True when the payload held markets for *some* league. It is what tells "this hour is
    /// not published yet" (nothing at all) from "this league does not trade" (plenty, none
    /// ours) — the first is worth stepping back an hour for and the second is an answer.
    bool any_league = false;
    std::vector<Listing> listings;

    const Listing* find(std::string_view metadata_id) const;
};

/// Parse a digest body, keeping only `league` and only markets against Chaos or Divine.
/// False when the body is not a digest at all; an hour with no markets parses fine and is
/// reported through `any_league`.
bool parse_digest(std::string_view body, std::string_view league, int64_t hour, Digest& out);

} // namespace ppc::exchange
