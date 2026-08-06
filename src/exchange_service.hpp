#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "exchange/exchange.hpp"

union SDL_Event;

namespace ppc {

enum class ExchangeState { Idle, Loading, Ok, Error };

/// What the in-game currency exchange did with this item in the last published hour.
///
/// The fourth of the `LeagueService` family and the smallest, because the feed is so much
/// simpler than the other three: one download covers **every** item in every league, and it
/// is immutable once published. So there is one digest in memory at a time, a lookup against
/// it is a string compare, and the second check of the hour costs nothing whatever the item.
///
/// Every member is touched on the main thread only; the worker owns its stack and the heap
/// payload it hands over through the SDL event queue.
class ExchangeService {
public:
    struct Result {
        std::shared_ptr<const exchange::Digest> digest;
        std::string error;
    };

    ~ExchangeService();

    void init(uint32_t done_event_type); ///< after SDL_RegisterEvents
    void shutdown();                     ///< join the worker, drain events; before SDL_Quit

    /// Look this item up, downloading the newest digest if the one in hand is not it. An empty
    /// `metadata_id` — an older bundle, or a base the data build could not match to game data
    /// — is not an error and not a question: nothing is fetched and nothing is drawn.
    void lookup(std::string league, std::string metadata_id);

    void on_done(const SDL_Event& e);

    ExchangeState state() const { return state_; }
    /// The item's markets, or null when it does not trade on the exchange at all — which is
    /// the answer for most of what a price check sees, and is why this draws nothing rather
    /// than saying so.
    const exchange::Listing* listing() const { return listing_; }
    /// The hour the answer covers, 0 when there is none. Drawn, because an exchange price is
    /// always at least an hour old and saying so is the difference between a stale number and
    /// a wrong one.
    int64_t hour() const { return digest_ ? digest_->hour : 0; }
    const std::string& error() const { return error_; }

private:
    void start();
    void resolve();

    uint32_t done_event_ = 0;
    std::thread worker_;
    std::atomic<bool> busy_{false};
    ExchangeState state_ = ExchangeState::Idle;
    std::string league_, metadata_id_;
    std::shared_ptr<const exchange::Digest> digest_;
    /// Points into `digest_`, so it is cleared whenever that is replaced.
    const exchange::Listing* listing_ = nullptr;
    std::string error_;
    bool restart_ = false;
};

} // namespace ppc
