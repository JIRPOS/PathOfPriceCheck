#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ninja/ninja.hpp"

union SDL_Event;

namespace ppc {

enum class NinjaState { Idle, Loading, Ok, Error };

/// The poe.ninja reference price for the item in hand.
///
/// Shaped like `TradeService` and for the same reason: every member is touched on the main
/// thread only, and the worker owns nothing but its own stack and the heap payload it hands
/// over through the SDL event queue.
///
/// The split of work is the other way round from a trade search, though. The worker only
/// *downloads* — matching an item to a priced line is pure and cheap, so it happens on the
/// main thread, which means a second check of the same kind of item costs nothing at all.
class NinjaService {
public:
    struct Result {
        std::vector<std::shared_ptr<const ninja::Overview>> overviews;
        std::string error;
    };

    ~NinjaService();

    void init(uint32_t done_event_type); ///< after SDL_RegisterEvents
    void shutdown();                     ///< join the worker, drain unconsumed events; before SDL_Quit

    /// Price this item: resolve against the overviews already in memory, and download the
    /// ones that are missing or older than `ninja::kTtlSeconds`.
    void price(ninja::Query q);

    void on_done(const SDL_Event& e);

    NinjaState state() const { return state_; }
    const ninja::Reference& reference() const { return ref_; }
    const std::string& error() const { return error_; }

    /// The symbol and name poe.ninja publishes for a currency id. Used only as a fallback:
    /// the trade static data is the same picture under the same id, and is usually already
    /// cached — but a user who has never run a search does not have it yet, and a reference
    /// price should not be the one thing that needs a trade request to render.
    std::string currency_icon(const std::string& id) const;
    std::string currency_name(const std::string& id) const;
    /// A symbol found by **display name**, across every overview held rather than only the
    /// currency market. The other half of the same fallback: an item the currency-exchange
    /// feed prices has no trade id to be looked up by, and the trade static data is not
    /// fetched at all until the user runs their first search — but the overview this item's
    /// own reference price came out of was, and it carries the picture.
    std::string icon_for_name(const std::string& name) const;

private:
    void start(); ///< resolve `query_` against memory, download what it is missing
    void resolve();
    /// The overview for this key, or null. Held for the life of the process: they are a few
    /// hundred kilobytes parsed and the whole point is that the second check is free.
    std::shared_ptr<const ninja::Overview> held(const ninja::Key& k) const;

    uint32_t done_event_ = 0;
    std::thread worker_;
    std::atomic<bool> busy_{false};
    NinjaState state_ = NinjaState::Idle;
    ninja::Query query_;
    ninja::Reference ref_;
    std::string error_;
    bool restart_ = false; ///< a check arrived mid-download and still needs its own categories
    std::vector<std::shared_ptr<const ninja::Overview>> overviews_;
};

} // namespace ppc
