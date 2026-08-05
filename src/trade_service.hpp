#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "trade/trade.hpp"

union SDL_Event;

namespace ppc {

enum class TradeState { Idle, Searching, Ok, Error };

/// Runs one price-check search at a time and holds its result.
///
/// Shaped exactly like `LeagueService`, and for the same reason: every member is touched on
/// the main thread only, and the worker owns nothing but its own stack and the heap payload
/// it hands over through the SDL event queue.
class TradeService {
public:
    struct Result {
        uint64_t gen = 0; ///< which item this was a search for; see `clear`
        bool ok = false;
        bool append = false; ///< a `load_more` page, to add to what is on screen
        trade::SearchResults res;
        std::vector<trade::Listing> page; ///< `append` only
        size_t consumed = 0;              ///< `append` only: hashes this page got through
        std::string error;
        /// Set only when this run refreshed the static data, which is at most once a week.
        std::vector<trade::CurrencyEntry> currencies;
    };

    ~TradeService();

    void init(uint32_t done_event_type); ///< after SDL_RegisterEvents
    void shutdown();                     ///< join the worker, drain unconsumed events; before SDL_Quit

    void load_cache(); ///< currency names and images from disk; no network

    /// Start a search. `query_json` is built on this thread from the plan the panel is
    /// showing — the plan itself must not travel to the worker, since it points into the
    /// data bundle the updater can swap at any moment.
    void search(std::string league, std::string query_json, int want);

    /// Fetch the next ten results of the search already run and append them. Costs one /fetch
    /// request and no search — the hashes came back with the original POST, all hundred of
    /// them however large the match count was.
    void load_more();
    /// False while one is in flight, before a search has succeeded, and once the hundred
    /// hashes the API hands out are exhausted. That hundred is the API's own ceiling: past it
    /// there is nothing more to page to and the search has to be narrowed instead.
    bool can_load_more() const;
    /// Results still unfetched, which is what the load-more row counts down.
    size_t remaining() const { return results_.hashes.size() - results_.fetched; }

    void on_done(const SDL_Event& e);

    /// Forget the results, for when the panel moves to a different item. A search already in
    /// flight cannot be recalled, so it is disowned instead: its result carries the generation
    /// it was started under and is dropped rather than shown against the item it is not about.
    void clear();

    TradeState state() const { return state_; }
    const trade::SearchResults& results() const { return results_; }
    const std::string& error() const { return error_; }
    /// The league the results on screen came from, which is not necessarily the one in
    /// Settings any more.
    const std::string& league() const { return league_; }

    /// Full CDN URL of a currency's symbol, or empty when the static data has no such id.
    std::string currency_image(const std::string& id) const;
    /// "Divine Orb", or the raw id when the static data does not know it.
    std::string currency_name(const std::string& id) const;

private:
    void adopt_currencies(std::vector<trade::CurrencyEntry> entries, bool persist);

    uint32_t done_event_ = 0;
    std::thread worker_;
    std::atomic<bool> busy_{false};
    TradeState state_ = TradeState::Idle;
    trade::SearchResults results_;
    std::string error_;
    std::string league_;
    uint64_t gen_ = 0;

    std::unordered_map<std::string, trade::CurrencyEntry> currencies_;
    int64_t currencies_at_ = 0; ///< when the static data was fetched; 0 means never
};

} // namespace ppc
