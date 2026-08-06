#include "ninja_service.hpp"

#include <algorithm>
#include <ctime>
#include <utility>

#include <SDL3/SDL.h>

#include "ninja/cache.hpp"
#include "ninja/client.hpp"
#include "util/debug_log.hpp"

namespace ppc {
namespace {

int64_t now_s() { return static_cast<int64_t>(std::time(nullptr)); }

} // namespace

NinjaService::~NinjaService() { shutdown(); }

void NinjaService::init(uint32_t done_event_type) {
    done_event_ = done_event_type;
    ninja::cache::prune(); // last league's overviews, which nothing will ask for again
}

std::shared_ptr<const ninja::Overview> NinjaService::held(const ninja::Key& k) const {
    for (const std::shared_ptr<const ninja::Overview>& ov : overviews_)
        if (ov->key == k) return ov;
    return nullptr;
}

void NinjaService::price(ninja::Query q) {
    query_ = std::move(q);
    ref_ = {};
    error_.clear();
    start();
}

void NinjaService::start() {
    const std::vector<ninja::Key> keys = ninja::keys_for(query_);
    if (keys.empty()) {
        // Not an error and not a gap: a rare is priced by its modifiers, and poe.ninja has
        // never claimed to price one. The panel draws nothing at all for this.
        state_ = NinjaState::Idle;
        return;
    }

    const int64_t now = now_s();
    std::vector<ninja::Key> want;
    for (const ninja::Key& k : keys) {
        const std::shared_ptr<const ninja::Overview> have = held(k);
        if (!have || !ninja::cache::fresh(have->fetched_at, now)) want.push_back(k);
    }
    if (want.empty()) {
        resolve();
        state_ = NinjaState::Ok;
        return;
    }

    state_ = NinjaState::Loading;
    // One download at a time. A check that lands on one already running cannot simply wait for
    // it — that one was told to fetch a *different* item's categories, and would leave this
    // one waiting on an overview nobody asked for. So it is repeated when that one is done.
    if (busy_.exchange(true)) {
        restart_ = true;
        return;
    }
    if (worker_.joinable()) worker_.join(); // reap the previous, already-finished worker

    const uint32_t ev = done_event_;
    worker_ = std::thread([ev, now, want = std::move(want)] {
        auto* r = new Result{};
        for (const ninja::Key& k : want) {
            ninja::FetchOutcome out = ninja::load_overview(k, now);
            if (out.overview) r->overviews.push_back(std::move(out.overview));
            if (r->error.empty()) r->error = std::move(out.error);
        }
        SDL_Event e{};
        e.type = ev;
        e.user.data1 = r;
        if (!SDL_PushEvent(&e)) delete r; // queue full, or SDL is shutting down
    });
}

void NinjaService::on_done(const SDL_Event& e) {
    std::unique_ptr<Result> r(static_cast<Result*>(e.user.data1));
    busy_ = false;
    if (!r) return;

    // An overview is about the economy, not about the item it happened to be fetched for, so
    // it is kept and re-used whatever the panel has moved on to.
    for (std::shared_ptr<const ninja::Overview>& ov : r->overviews) {
        const auto slot = std::find_if(
            overviews_.begin(), overviews_.end(),
            [&](const std::shared_ptr<const ninja::Overview>& h) { return h->key == ov->key; });
        if (slot == overviews_.end()) overviews_.push_back(std::move(ov));
        else *slot = std::move(ov);
    }
    if (restart_) { // a check arrived while this was in flight, and wants its own categories
        restart_ = false;
        start();
        return;
    }
    error_ = std::move(r->error);
    resolve();
    // An error with a price behind it is a stale copy served from disk, which is still a
    // price; without one there is nothing to show but the error.
    state_ = (!error_.empty() && ref_.state == ninja::Reference::State::None) ? NinjaState::Error
                                                                             : NinjaState::Ok;
}

void NinjaService::resolve() {
    std::vector<const ninja::Overview*> have;
    for (const ninja::Key& k : ninja::keys_for(query_))
        if (const std::shared_ptr<const ninja::Overview> ov = held(k)) have.push_back(ov.get());
    ref_ = ninja::reference_for(query_, have);
    debug::log("[ninja]  %s: state=%d %g %s%s", query_.names.empty() ? "?" : query_.names[0].c_str(),
               static_cast<int>(ref_.state), ref_.price.amount, ref_.price.currency.c_str(),
               ref_.note.empty() ? "" : (" \xe2\x80\x94 " + ref_.note).c_str());
}

std::string NinjaService::currency_icon(const std::string& id) const {
    const std::shared_ptr<const ninja::Overview> ov = held(ninja::currency_key(query_.league));
    if (!ov) return {};
    const ninja::Line* l = ov->find_id(id);
    return l ? l->icon : std::string();
}

std::string NinjaService::icon_for_name(const std::string& name) const {
    if (name.empty()) return {};
    for (const std::shared_ptr<const ninja::Overview>& ov : overviews_)
        for (const ninja::Line& l : ov->lines)
            if (l.name == name && !l.icon.empty()) return l.icon;
    return {};
}

std::string NinjaService::currency_name(const std::string& id) const {
    const std::shared_ptr<const ninja::Overview> ov = held(ninja::currency_key(query_.league));
    if (!ov) return id;
    const ninja::Line* l = ov->find_id(id);
    return l && !l->name.empty() ? l->name : id;
}

void NinjaService::shutdown() {
    // Join rather than detach, as the other two services do: a detached worker pushing into a
    // torn-down SDL is far worse than stalling exit.
    if (worker_.joinable()) worker_.join();
    if (!done_event_) return; // never initialised, or already shut down

    SDL_Event drop[16];
    int n;
    while ((n = SDL_PeepEvents(drop, 16, SDL_GETEVENT, done_event_, done_event_)) > 0)
        for (int i = 0; i < n; ++i) delete static_cast<Result*>(drop[i].user.data1);

    done_event_ = 0; // App calls this before SDL_Quit; the destructor's call must be a no-op
}

} // namespace ppc
