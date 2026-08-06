#include "exchange_service.hpp"

#include <ctime>
#include <utility>

#include <SDL3/SDL.h>

#include "exchange/client.hpp"
#include "util/debug_log.hpp"

namespace ppc {
namespace {

int64_t now_s() { return static_cast<int64_t>(std::time(nullptr)); }

} // namespace

ExchangeService::~ExchangeService() { shutdown(); }

void ExchangeService::init(uint32_t done_event_type) { done_event_ = done_event_type; }

void ExchangeService::lookup(std::string league, std::string metadata_id) {
    league_ = std::move(league);
    metadata_id_ = std::move(metadata_id);
    listing_ = nullptr;
    error_.clear();
    if (metadata_id_.empty() || league_.empty()) {
        state_ = ExchangeState::Idle;
        return;
    }
    start();
}

void ExchangeService::start() {
    // A digest is immutable, so the only reason to fetch is that a newer hour exists or the
    // league changed under us. Everything else is answered from the copy in hand.
    if (digest_ && digest_->league == league_ && digest_->hour >= exchange::latest_hour(now_s())) {
        resolve();
        state_ = ExchangeState::Ok;
        return;
    }

    state_ = ExchangeState::Loading;
    if (busy_.exchange(true)) {
        restart_ = true;
        return;
    }
    if (worker_.joinable()) worker_.join();

    const uint32_t ev = done_event_;
    worker_ = std::thread([ev, league = league_] {
        auto* r = new Result{};
        exchange::FetchOutcome out = exchange::load_digest(league, now_s());
        r->digest = std::move(out.digest);
        r->error = std::move(out.error);
        SDL_Event e{};
        e.type = ev;
        e.user.data1 = r;
        if (!SDL_PushEvent(&e)) delete r;
    });
}

void ExchangeService::on_done(const SDL_Event& e) {
    std::unique_ptr<Result> r(static_cast<Result*>(e.user.data1));
    busy_ = false;
    if (!r) return;

    if (r->digest) {
        digest_ = std::move(r->digest);
        listing_ = nullptr; // the old one pointed into the digest just replaced
    }
    if (restart_) { // a check arrived mid-download, possibly in another league
        restart_ = false;
        start();
        return;
    }
    error_ = std::move(r->error);
    resolve();
    state_ = (!error_.empty() && !listing_) ? ExchangeState::Error : ExchangeState::Ok;
}

void ExchangeService::resolve() {
    listing_ = nullptr;
    if (!digest_ || digest_->league != league_) return;
    listing_ = digest_->find(metadata_id_);
    debug::log("[exchange] %s: %s in hour %lld", metadata_id_.c_str(),
               listing_ ? "traded" : "no market", static_cast<long long>(digest_->hour));
}

void ExchangeService::shutdown() {
    if (worker_.joinable()) worker_.join();
    if (!done_event_) return;

    SDL_Event drop[16];
    int n;
    while ((n = SDL_PeepEvents(drop, 16, SDL_GETEVENT, done_event_, done_event_)) > 0)
        for (int i = 0; i < n; ++i) delete static_cast<Result*>(drop[i].user.data1);

    done_event_ = 0;
}

} // namespace ppc
