#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "item/item.hpp"
#include "item/plan.hpp"

/// poe.ninja's economy overviews — the reference price for what a stat query cannot price.
/// A rare is worth whatever its own modifiers are worth and only the trade site can say; a
/// Divine Orb, a level-21 gem and a unique whose every copy is the same are worth what the
/// market is paying for them, which is the one thing poe.ninja measures and trade does not.
///
/// Everything in this header is pure — parsing, matching, URL building — so it is testable
/// against a captured payload with no network. `ninja/client` fetches, `ninja/cache` stores,
/// and `NinjaService` owns the threading.
///
/// **Only the economy endpoints are public API** (https://poe.ninja/docs/api); the builds and
/// profile endpoints are explicitly closed to third parties and are not touched here. The docs
/// also ask for a descriptive User-Agent (`net::user_agent`), conditional requests, and no
/// polling faster than the data moves — PoE 1 overviews refresh about every fifteen minutes
/// and the CDN sets `max-age=1800`, which is where `kTtlSeconds` comes from.
namespace ppc::ninja {

/// How long a downloaded overview is served from disk before it is asked for again. Matches
/// the cache-control poe.ninja itself sets, so a refresh at this age is the first one that
/// could return anything new.
inline constexpr int64_t kTtlSeconds = 30 * 60;

inline constexpr const char* kApiBase = "https://poe.ninja/poe1/api/economy";
inline constexpr const char* kWebBase = "https://poe.ninja/poe1/economy";
/// The site's own favicon, drawn as the row's source marker. Downloaded like a currency
/// symbol; the row still reads without it.
inline constexpr const char* kLogoUrl = "https://poe.ninja/favicons/favicon-32x32.png";

/// Which of the two overviews serves a category. They are different measurements rather than
/// two halves of one: the exchange is the currency market's own rates, the stash overview is
/// what individual items are actually listed at.
enum class Feed : uint8_t { Exchange, StashItem };

/// One category poe.ninja publishes: the feed that serves it, the `type` query parameter, and
/// the path segment its items' pages live under on the website.
struct Category {
    Feed feed = Feed::Exchange;
    std::string_view type;
    std::string_view slug;
};

/// nullptr for a type this build does not know.
const Category* category(std::string_view type);

/// One overview: a category and a league. This pair is the cache key.
struct Key {
    std::string type;
    std::string league;

    bool operator==(const Key& o) const = default;
};

/// The currency market, which every check needs whatever the item is: it carries the divine
/// rate every price is converted with and the symbols they are drawn with.
Key currency_key(std::string_view league);

/// poe.ninja's own path segment for a league: lower-cased, spaces dropped, and Hardcore moved
/// to the end as "hc" — "Hardcore Allflame" is `allflamehc`, not `hardcore-allflame`.
std::string league_slug(std::string_view league);

/// The API request for this overview, or empty when the type is not one we know.
std::string url(const Key& k);
/// The page a priced line has on the website. Empty without a `details_id`.
std::string page_url(const Key& k, std::string_view details_id);
/// File name for this overview under `<cache>/ninja/`. Stable across runs.
std::string cache_name(const Key& k);

/// The seven daily samples poe.ninja plots, as cumulative percent change against the start of
/// the window. `change` is the figure printed beside them (the payload's `totalChange`), which
/// is the last sample rather than a separate measurement. The API sends nulls for days with no
/// data; those are dropped, so `data` can be shorter than the window.
struct Spark {
    std::vector<float> data;
    float change = 0;
    bool known = false; ///< false when the line carried no trend at all
};

/// One priced line, reduced to what a reference price needs. The two feeds fill different
/// halves of it: the exchange has ids and no item detail, the stash overview has the item
/// detail and no id.
struct Line {
    std::string id;         ///< exchange only: the trade currency id ("divine"), else a slug
    std::string name;
    std::string base_type;  ///< stash overview only
    std::string variant;    ///< poe.ninja's shorthand for *which* one this price is for
    std::string details_id; ///< last path segment of its page
    std::string icon;       ///< absolute image URL; empty when the payload carried none
    double chaos = 0;
    int links = 0; ///< 0 unless the price is specifically for a linked item
    /// The payload's `levelRequired`, which means **two different things**: on a base-type line
    /// it is the item-level bracket the price is for (82 to 86), on every other line it is the
    /// character level needed to equip the item. Only the base-type path reads it.
    int level = 0;
    int gem_level = 0; ///< 0 when not a gem
    int gem_quality = 0;
    bool corrupted = false;
    int listings = 0; ///< listings behind the price; 0 when the payload did not say
    Spark spark;
    /// The variant's own explicit modifiers, as poe.ninja words them — with the unique's roll
    /// ranges in place of a roll, "+(15-25)% to Cold Resistance". This is what tells two
    /// variants of one unique apart; see `reference_for`.
    std::vector<std::string> mods;
};

struct Overview {
    Key key;
    Feed feed = Feed::Exchange;
    int64_t fetched_at = 0; ///< unix seconds, from the cache entry it was read out of
    /// Exchange only: `core.rates.divine` inverted. 0 when the payload did not carry it,
    /// which is what a league poe.ninja has no economy for answers with.
    double chaos_per_divine = 0;
    std::vector<Line> lines;

    const Line* find_id(std::string_view id) const;
};

/// Parse an overview body. False when it is not one at all — note that a league poe.ninja has
/// no economy for answers 200 with an empty but well-formed payload, which parses fine and
/// matches nothing. That is the difference between "no prices for this league" and "broken".
bool parse_overview(std::string_view body, Feed feed, Overview& out);

/// Everything about an item poe.ninja can be asked about, as plain values.
///
/// Deliberately not the `Item` itself: the service has to re-resolve when a download lands,
/// and an `Item` points into the data bundle the updater may have swapped by then.
struct Query {
    item::Strategy strategy = item::Strategy::Unsupported;
    std::string league;
    /// What to match a line's name against, best first. A unique renamed by what was done to
    /// it ("Foulborn Headhunter") is a separate line on poe.ninja, so the printed name is
    /// tried before the canonical one.
    std::vector<std::string> names;
    std::string base_type;
    std::string category;   ///< the trade category, which is what picks the overview
    std::string item_class; ///< as printed, for the categories a trade category cannot tell apart
    /// Every modifier line the game printed, as printed. Matched against a variant's own
    /// wordings when a unique has more than one line on poe.ninja.
    std::vector<std::string> mods;
    bool corrupted = false;
    int links = 0;      ///< the item's largest linked group, 0 below five
    /// How many are in hand, off the "Stack Size: 18/20" line — the count, never the maximum
    /// beside it. That maximum is what one *inventory* slot holds and a currency stash tab
    /// holds 5000 or 10000 of them in one stack, so "6000/20" is a normal thing to copy.
    int stack = 1;
    int gem_level = 0;  ///< gems only
    int gem_quality = 0;
    int item_level = 0; ///< base-type pricing only; 0 when the item text did not print one
    /// The influences a base is priced by, as poe.ninja names them. **Not** every influence the
    /// item has: Searing Exarch and Eater of Worlds come from an implicit rather than from the
    /// base, so poe.ninja does not split base types by them and neither does this.
    std::vector<std::string> influences;
};

Query query_for(const item::Item& it, const item::SearchPlan& plan, std::string_view league);

/// The overviews a check needs, the currency market always first. Empty when poe.ninja prices
/// nothing of this kind — a rare, a base item, a map.
///
/// A currency-like item is looked for in the currency market before anything else is
/// downloaded for it: that overview is fetched for the rate regardless, and it already holds
/// the orbs and catalysts most currency checks are about. Only the names it does not have
/// send us to one of the seventeen other exchange overviews.
std::vector<Key> keys_for(const Query& q);

/// A price, in the currency it reads best in.
struct Quote {
    double amount = 0;
    /// Trade's own currency id, "chaos" or "divine" — the same key the trade static data's
    /// symbols and names are under, so the row draws with the icons already cached.
    std::string currency;
};

/// poe.ninja quotes everything in chaos. Anything worth a divine or more is quoted in divine
/// by the site and by every player, so it is converted once it crosses that line. Amounts are
/// rounded to what a price is actually said in — nobody quotes 5.34285 divine.
Quote quote(double chaos, double chaos_per_divine);

/// The same, in a currency chosen for you. Both ends of a span have to be in one currency or
/// the two numbers are not comparable — "79.4 – 4" is what the pair reads as otherwise.
Quote quote_in(double chaos, double chaos_per_divine, std::string_view currency);

struct Variant {
    std::string label; ///< poe.ninja's own shorthand: "5 Flasks", "Gem Level", "21/20c"
    Quote price;
};

/// The reference price the panel draws, or the reason there is not one.
struct Reference {
    enum class State : uint8_t {
        None,      ///< poe.ninja has no price for this item; `note` says why
        Priced,    ///< one line matched
        Ambiguous, ///< several variants matched and nothing in the clipboard tells them apart
    };

    State state = State::None;
    Quote price;    ///< the price of **one**; Ambiguous: the cheapest variant
    Quote high;     ///< Ambiguous only: the dearest
    /// What the whole stack in hand is worth, and how many that is. Set only above one, and
    /// quoted on its own — six thousand chaos is a number said in divine even though one is
    /// not. Never on an `Ambiguous` price: a span times a count is four numbers.
    int stack = 1;
    Quote stack_price;
    /// A trade currency id when `price` is a **rate** rather than what one of these costs:
    /// this many `price.currency` per one of *these*. Set only for the Chaos and Divine Orb,
    /// whose own price is a tautology — the market is denominated in them, so each is worth
    /// exactly one of itself and the number a player checks either for is the rate between
    /// the two. Empty for every other item.
    std::string per;
    std::string label; ///< which variant or gem tier this price is for, when it needed saying
    std::vector<Variant> variants; ///< Ambiguous only, cheapest first
    Spark spark;
    std::string url; ///< the item's page, for the click-through
    /// Why there is no price, or the caveat on the one there is. **Never names poe.ninja when
    /// the state is `None`** — that note is drawn after the row's own "poe.ninja — " prefix and
    /// would read twice. The other two states put it in the tooltip, where naming it is right.
    std::string note;
    int listings = 0;
    int64_t fetched_at = 0; ///< when the overview behind this was downloaded
};

/// Match the item against the overviews already in hand. Pure; `have` may hold fewer than
/// `keys_for` asked for, which reads back as no price rather than as an error.
Reference reference_for(const Query& q, const std::vector<const Overview*>& have);

/// The largest linked socket group in a `sockets` line ("R-G-B B-B"), or 0 below five —
/// poe.ninja only prices links at five and six, and a lower count is not a variant.
int max_links(std::string_view sockets);

} // namespace ppc::ninja
