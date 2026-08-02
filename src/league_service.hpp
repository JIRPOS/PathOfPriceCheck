#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "leagues.hpp"

union SDL_Event;

namespace ppc {

enum class LeagueState { Idle, Loading, Ok, Error };

/// Hammering the trade API from a Refresh button is the one way a user can generate
/// unbounded traffic, so force-refresh is rate-limited independently of the cache TTL.
inline constexpr int64_t kRefreshCooldownSeconds = 60;

/// Owns the league list, its disk cache, and the background fetch.
///
/// Every member is touched on the main thread only. The worker owns nothing but its own
/// stack and the heap payload it hands over through the SDL event queue.
class LeagueService {
public:
    struct Result {
        bool ok = false;
        std::vector<std::string> ids;
        std::string error;
    };

    ~LeagueService();

    void init(uint32_t done_event_type); ///< after SDL_RegisterEvents
    void shutdown();                     ///< join the worker, drain unconsumed events; before SDL_Quit

    void load_cache();                ///< cheap file read; no network
    void refresh(bool force);         ///< fetch if forced (and off cooldown) or the cache is stale
    void on_done(const SDL_Event& e); ///< main thread: adopt and persist the worker's result

    LeagueState state() const { return state_; }
    const std::vector<std::string>& list() const { return list_; } ///< never empty
    const std::string& error() const { return error_; }
    int cooldown_s() const; ///< >0 while Refresh should stay disabled

private:
    uint32_t done_event_ = 0;
    std::thread worker_;
    std::atomic<bool> busy_{false};
    LeagueState state_ = LeagueState::Idle;
    std::vector<std::string> list_ = fallback_leagues();
    std::string error_;
    int64_t cached_at_ = 0;
    int64_t last_fetch_at_ = 0;
};

} // namespace ppc
