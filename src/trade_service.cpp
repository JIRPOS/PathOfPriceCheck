#include "trade_service.hpp"

#include <algorithm>
#include <ctime>
#include <memory>
#include <utility>

#include <SDL3/SDL.h>

#include "trade/client.hpp"
#include "trade/currency.hpp"
#include "util/debug_log.hpp"

namespace ppc {
namespace {

int64_t now_s() { return static_cast<int64_t>(std::time(nullptr)); }

} // namespace

TradeService::~TradeService() { shutdown(); }

void TradeService::init(uint32_t done_event_type) { done_event_ = done_event_type; }

void TradeService::load_cache() {
    if (auto c = trade::currency_cache::load()) {
        currencies_at_ = c->fetched_at;
        adopt_currencies(std::move(c->entries), /*persist=*/false);
    }
}

void TradeService::adopt_currencies(std::vector<trade::CurrencyEntry> entries, bool persist) {
    if (entries.empty()) return;
    if (persist) {
        currencies_at_ = now_s();
        trade::currency_cache::store(trade::CurrencyList{entries, currencies_at_});
    }
    for (trade::CurrencyEntry& e : entries) {
        if (!e.text.empty()) by_name_.try_emplace(e.text, e.image);
        std::string id = e.id;
        currencies_.insert_or_assign(std::move(id), std::move(e));
    }
}

void TradeService::clear() {
    ++gen_;
    results_ = {};
    error_.clear();
    state_ = TradeState::Idle;
}

void TradeService::search(std::string league, std::string query_json, int want) {
    if (busy_.exchange(true)) return;       // one in flight at a time
    if (worker_.joinable()) worker_.join(); // reap the previous, already-finished worker

    league_ = league;
    results_ = {};
    error_.clear();
    state_ = TradeState::Searching;
    debug::log("[trade]  searching %s: %s", league.c_str(), query_json.c_str());

    // Refresh the currency symbols on the same worker when they are stale, rather than
    // giving them a thread and a request budget of their own: they are only ever wanted
    // alongside a search result, and a week-old copy is still correct.
    const bool want_static =
        !trade::currency_cache::fresh(trade::CurrencyList{{}, currencies_at_}, now_s());

    const uint32_t ev = done_event_;
    const uint64_t gen = gen_;
    worker_ = std::thread([ev, gen, want, league = std::move(league), query = std::move(query_json),
                           want_static] {
        auto* r = new Result{};
        r->gen = gen;
        if (want_static) r->currencies = trade::fetch_static_currencies();
        trade::SearchOutcome out =
            trade::run_search(league, query, static_cast<size_t>(std::max(want, 0)));
        r->ok = out.ok;
        r->res = std::move(out.res);
        r->error = std::move(out.error);
        SDL_Event e{};
        e.type = ev;
        e.user.data1 = r;
        if (!SDL_PushEvent(&e)) delete r; // queue full, or SDL is shutting down
    });
}

bool TradeService::can_load_more() const {
    return !busy_ && state_ == TradeState::Ok && results_.fetched < results_.hashes.size();
}

void TradeService::load_more() {
    if (!can_load_more()) return;
    if (busy_.exchange(true)) return;
    if (worker_.joinable()) worker_.join();

    error_.clear();
    state_ = TradeState::Searching;

    const uint32_t ev = done_event_;
    const uint64_t gen = gen_;
    const size_t first = results_.fetched;
    // Copies, not references: `results_` belongs to the main thread and a search started in
    // the meantime would move it out from under the worker.
    const std::string qid = results_.query_id;
    std::vector<std::string> hashes = results_.hashes;
    debug::log("[trade]  loading results %zu.. of %zu", first, hashes.size());
    worker_ = std::thread([ev, gen, first, qid, hashes = std::move(hashes)] {
        auto* r = new Result{};
        r->gen = gen;
        r->append = true;
        trade::PageOutcome p = trade::fetch_page(qid, hashes, first, trade::kFetchBatch);
        r->ok = p.ok;
        r->page = std::move(p.listings);
        r->consumed = p.consumed;
        r->error = std::move(p.error);
        SDL_Event e{};
        e.type = ev;
        e.user.data1 = r;
        if (!SDL_PushEvent(&e)) delete r;
    });
}

void TradeService::on_done(const SDL_Event& e) {
    std::unique_ptr<Result> r(static_cast<Result*>(e.user.data1));
    busy_ = false;
    if (!r) return;
    // The symbols are about currency, not about this item, so they are kept either way.
    adopt_currencies(std::move(r->currencies), /*persist=*/true);
    if (r->gen != gen_) {
        debug::log("[trade]  dropped a result for the previous item");
        return;
    }
    if (r->append) {
        results_.listings.insert(results_.listings.end(),
                                 std::make_move_iterator(r->page.begin()),
                                 std::make_move_iterator(r->page.end()));
        results_.fetched += r->consumed;
        error_ = std::move(r->error);
        // Ok whatever this page did: what is already on screen is still a price, and a page
        // that got nothing leaves the row in place to try again.
        state_ = TradeState::Ok;
    } else {
        results_ = std::move(r->res);
        error_ = std::move(r->error);
        // A batch that failed after an earlier one succeeded leaves both a list and an error.
        // The list is the answer; the error explains why it is short, so it is kept either way.
        state_ = r->ok ? TradeState::Ok : TradeState::Error;
    }
    debug::log("[trade]  %zu listings of %d matches (%zu/%zu hashes)%s%s",
               results_.listings.size(), results_.total, results_.fetched, results_.hashes.size(),
               error_.empty() ? "" : ", error: ", error_.c_str());
}

std::string TradeService::currency_image(const std::string& id) const {
    const auto it = currencies_.find(id);
    if (it == currencies_.end()) return {};
    return std::string(trade::kCdnBase) + it->second.image;
}

std::string TradeService::image_for_name(const std::string& name) const {
    const auto it = by_name_.find(name);
    if (it == by_name_.end()) return {};
    return std::string(trade::kCdnBase) + it->second;
}

std::string TradeService::currency_name(const std::string& id) const {
    const auto it = currencies_.find(id);
    if (it == currencies_.end() || it->second.text.empty()) return id;
    return it->second.text;
}

void TradeService::shutdown() {
    // Join rather than detach, as LeagueService does: a detached worker pushing into a
    // torn-down SDL is far worse than stalling exit. cancel_waits() is what keeps that stall
    // bounded — a worker sitting out a rate-limit window would otherwise hold exit for it.
    trade::cancel_waits();
    if (worker_.joinable()) worker_.join();
    if (!done_event_) return; // never initialised, or already shut down

    SDL_Event drop[16];
    int n;
    while ((n = SDL_PeepEvents(drop, 16, SDL_GETEVENT, done_event_, done_event_)) > 0)
        for (int i = 0; i < n; ++i) delete static_cast<Result*>(drop[i].user.data1);

    done_event_ = 0; // App calls this before SDL_Quit; the destructor's call must be a no-op
}

} // namespace ppc
