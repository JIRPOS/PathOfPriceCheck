#include "league_service.hpp"

#include <ctime>
#include <memory>

#include <SDL3/SDL.h>

#include "net/http.hpp"

namespace ppc {
namespace {

int64_t now_s() { return static_cast<int64_t>(std::time(nullptr)); }

} // namespace

LeagueService::~LeagueService() { shutdown(); }

void LeagueService::init(uint32_t done_event_type) { done_event_ = done_event_type; }

void LeagueService::load_cache() {
    if (auto c = league_cache::load()) {
        list_ = std::move(c->ids);
        cached_at_ = c->fetched_at;
        state_ = LeagueState::Ok;
    }
}

int LeagueService::cooldown_s() const {
    if (last_fetch_at_ == 0) return 0;
    const int64_t left = kRefreshCooldownSeconds - (now_s() - last_fetch_at_);
    return left > 0 ? static_cast<int>(left) : 0;
}

void LeagueService::refresh(bool force) {
    const int64_t now = now_s();
    if (!force && cached_at_ && now - cached_at_ < kLeagueTtlSeconds) return; // cache still good
    if (force && cooldown_s() > 0) return;
    if (busy_.exchange(true)) return;       // one in flight at a time
    if (worker_.joinable()) worker_.join(); // reap the previous, already-finished worker

    last_fetch_at_ = now;
    state_ = LeagueState::Loading;
    error_.clear();

    const uint32_t ev = done_event_;
    worker_ = std::thread([ev] {
        // Touches nothing owned by LeagueService. Ownership of `r` transfers to the main
        // thread through the event queue.
        auto* r = new Result{};
        // TODO(rate-limiter): route through the shared GGG limiter once it exists.
        net::Request req;
        req.url = kLeaguesUrl;
        const net::Response resp = net::get(req);
        if (!resp.ok()) {
            r->error = !resp.error.empty() ? resp.error : ("HTTP " + std::to_string(resp.status));
        } else if (r->ids = parse_leagues(resp.body, kRealm); r->ids.empty()) {
            r->error = "no leagues in response";
        } else {
            r->ok = true;
        }
        SDL_Event e{};
        e.type = ev;
        e.user.data1 = r;
        if (!SDL_PushEvent(&e)) delete r; // queue full, or SDL is shutting down
    });
}

void LeagueService::on_done(const SDL_Event& e) {
    std::unique_ptr<Result> r(static_cast<Result*>(e.user.data1));
    busy_ = false;
    if (!r) return;
    if (r->ok) {
        list_ = std::move(r->ids);
        cached_at_ = now_s();
        state_ = LeagueState::Ok;
        league_cache::store(LeagueList{list_, cached_at_});
    } else {
        // Demote the status line, not the dropdown: list_ keeps its cache or the fallback.
        error_ = std::move(r->error);
        state_ = LeagueState::Error;
    }
}

void LeagueService::shutdown() {
    // Join rather than detach: a detached worker pushing into a torn-down SDL is far worse
    // than stalling exit for the request timeout, which is why that budget is short.
    if (worker_.joinable()) worker_.join();
    if (!done_event_) return; // never initialised, or already shut down

    // Drain anything the worker pushed but the loop never consumed, or its payload leaks.
    // Must run before SDL_Quit — after it, the queue is gone.
    SDL_Event drop[16];
    int n;
    while ((n = SDL_PeepEvents(drop, 16, SDL_GETEVENT, done_event_, done_event_)) > 0)
        for (int i = 0; i < n; ++i) delete static_cast<Result*>(drop[i].user.data1);

    // App calls this explicitly before SDL_Quit, then the destructor calls it again once
    // App itself dies — which is after SDL_Quit. Clearing the type makes that second call
    // a no-op instead of a PeepEvents against a torn-down subsystem.
    done_event_ = 0;
}

} // namespace ppc
