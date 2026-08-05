#include "trade/client.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>

#include <nlohmann/json.hpp>

#include "trade/currency.hpp"
#include "trade/query.hpp"
#include "trade/ratelimit.hpp"
#include "trade/ratelimit_store.hpp"
#include "util/debug_log.hpp"

namespace ppc::trade {
namespace {

/// Longer than this and the answer is stale before it arrives, so say so instead of
/// blocking a worker (and, at shutdown, the join behind it) for minutes.
constexpr int64_t kMaxWaitMs = 30'000;

std::mutex g_mutex;
RateLimiter g_limiter;
std::atomic<bool> g_cancelled{false};

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

/// The wall clock, which is the only one two runs of this program agree on. Used solely to
/// write the limiter's state down and read it back; every decision is still made on the
/// steady clock above, which no NTP step can move.
int64_t now_unix_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

/// Write the limiter down. `g_mutex` must be held. Called on every request and every
/// response, which is a few hundred bytes at the rate GGG allows requests at — cheap against
/// what it buys: a restriction that survives the user closing the app and opening it again.
void persist_locked() { ratelimit_store::store(g_limiter.snapshot(now_ms()), now_unix_ms()); }

/// What GGG publishes for an unauthenticated client, as measured on 2026-08-05. Only a
/// starting point: the first response replaces these with whatever is actually in force.
/// The point of seeding at all is the very first price check of a session, which is two
/// fetches back to back and would otherwise be spaced against nothing.
void seed_defaults() {
    static std::once_flag once;
    std::call_once(once, [] {
        // Restored first, and `seed` then declines to overwrite what it finds. A restriction
        // the previous run earned is still being served by GGG whether or not this process
        // remembers it, and a limiter that starts every run with a clean budget would spend
        // it straight into the lockout — which is how a client stops being throttled and
        // starts being blocked.
        const LimiterState restored = ratelimit_store::load(now_unix_ms());
        g_limiter.restore(restored, now_ms());
        for (const PolicyState& p : restored)
            if (p.blocked_for_ms > 0)
                debug::log("[trade]  %s: %llds of a restriction carried over from the last run",
                           p.policy.c_str(), static_cast<long long>(p.blocked_for_ms / 1000));

        g_limiter.seed(policy::kSearch, {{5, 10, 60}, {15, 60, 300}, {30, 300, 1800}});
        g_limiter.seed(policy::kFetch, {{12, 4, 10}, {16, 12, 300}, {50, 300, 300}});
        g_limiter.seed(policy::kData, {{8, 60, 60}});
    });
}

/// The rules and state headers for whichever policy the response says it is under. GGG
/// names the header after the rule *group* ("Ip"), not after the policy, so the group has
/// to be read out of `X-Rate-Limit-Rules` first.
void adopt_headers(std::string_view policy, const net::Response& resp) {
    int retry_after = 0;
    if (const std::string* ra = resp.header("Retry-After")) retry_after = std::atoi(ra->c_str());

    std::string rules, state;
    if (const std::string* groups = resp.header("X-Rate-Limit-Rules")) {
        // Several groups can apply at once ("Ip,Account"); every one of them is a separate
        // header pair and all of them bind, so they are folded into one list.
        size_t pos = 0;
        while (pos <= groups->size()) {
            const size_t end = std::min(groups->find(',', pos), groups->size());
            const std::string g = groups->substr(pos, end - pos);
            pos = end + 1;
            if (g.empty()) continue;
            if (const std::string* r = resp.header("X-Rate-Limit-" + g)) {
                if (!rules.empty()) rules += ',';
                rules += *r;
            }
            if (const std::string* s = resp.header("X-Rate-Limit-" + g + "-State")) {
                if (!state.empty()) state += ',';
                state += *s;
            }
        }
    }
    if (rules.empty() && state.empty() && retry_after == 0) return;

    const std::lock_guard lock(g_mutex);
    g_limiter.observe(policy, rules, state, retry_after, now_ms());
    persist_locked();
}

} // namespace

void cancel_waits() { g_cancelled = true; }

net::Response request(const net::Request& r, std::string_view policy) {
    seed_defaults();
    int64_t wait;
    {
        const std::lock_guard lock(g_mutex);
        wait = g_limiter.delay_ms(policy, now_ms());
    }
    if (wait > kMaxWaitMs) {
        net::Response resp;
        resp.error = "rate limited \xe2\x80\x94 " + std::to_string((wait + 999) / 1000) +
                     "s until the next request is allowed";
        debug::log("[trade]  %s: %s", std::string(policy).c_str(), resp.error.c_str());
        return resp;
    }
    if (wait > 0) {
        debug::log("[trade]  %s: waiting %lldms for the rate-limit window",
                   std::string(policy).c_str(), static_cast<long long>(wait));
        // In slices, so shutdown does not have to sit out the whole window.
        const int64_t until = now_ms() + wait;
        while (!g_cancelled && now_ms() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (g_cancelled) {
            net::Response resp;
            resp.error = "cancelled";
            return resp;
        }
    }
    {
        const std::lock_guard lock(g_mutex);
        g_limiter.note_request(policy, now_ms());
        // Before the request, not after: a run killed while a request is in flight still
        // spent it, and the count has to survive that too.
        persist_locked();
    }

    const net::Response resp = net::get(r);
    adopt_headers(policy, resp);
    if (resp.status == 429)
        debug::log("[trade]  %s: 429 from %s", std::string(policy).c_str(), r.url.c_str());
    return resp;
}

namespace {

/// The trade API answers a bad query with `{"error":{"code":2,"message":"…"}}` and a 400,
/// which says far more than the status does.
std::string api_error(const net::Response& resp) {
    if (!resp.error.empty()) return resp.error;
    const nlohmann::json j = nlohmann::json::parse(resp.body, nullptr, false);
    if (j.is_object())
        if (const auto e = j.find("error"); e != j.end() && e->is_object())
            if (const auto m = e->find("message"); m != e->end() && m->is_string())
                return m->get<std::string>();
    return "HTTP " + std::to_string(resp.status);
}

} // namespace

PageOutcome fetch_page(std::string_view query_id, const std::vector<std::string>& hashes,
                       size_t first, size_t count) {
    PageOutcome out;
    out.ok = true;
    const size_t end = std::min(hashes.size(), first + count);
    for (size_t i = first; i < end; i += kFetchBatch) {
        const size_t n = std::min(kFetchBatch, end - i);
        const std::vector<std::string> batch(hashes.begin() + static_cast<ptrdiff_t>(i),
                                             hashes.begin() + static_cast<ptrdiff_t>(i + n));
        net::Request f;
        f.url = fetch_url(batch, query_id);
        f.timeout_ms = 15'000;
        const net::Response fr = request(f, policy::kFetch);
        if (!fr.ok()) {
            // Keep whatever earlier batches returned: ten listings are still a price, and
            // the error explains the short list rather than replacing it.
            out.error = api_error(fr);
            out.ok = false;
            break;
        }
        if (!parse_fetch(fr.body, out.listings)) {
            out.error = "a fetch response could not be read";
            out.ok = false;
            break;
        }
        out.consumed = i + n - first;
    }
    return out;
}

SearchOutcome run_search(std::string_view league, const std::string& query_json, size_t want) {
    SearchOutcome out;
    net::Request req;
    req.url = search_url(league);
    req.body = query_json;
    req.timeout_ms = 15'000;
    const net::Response resp = request(req, policy::kSearch);
    if (!resp.ok()) {
        out.error = api_error(resp);
        return out;
    }
    if (!parse_search(resp.body, out.res)) {
        out.error = "the search response could not be read";
        return out;
    }
    debug::log("[trade]  search %s: %d matches, %zu hashes", out.res.query_id.c_str(),
               out.res.total, out.res.hashes.size());
    // A search that matched nothing still succeeded; there is simply nothing to fetch, and
    // the panel says so rather than reporting an error.
    out.ok = true;

    PageOutcome page = fetch_page(out.res.query_id, out.res.hashes, 0, want);
    out.res.listings = std::move(page.listings);
    out.res.fetched = page.consumed;
    // The search itself succeeded, so `out.ok` stands; a failed batch only explains the short
    // list. Whether it is short is what `fetched` against `hashes` says.
    out.error = std::move(page.error);
    return out;
}

std::vector<CurrencyEntry> fetch_static_currencies() {
    net::Request req;
    req.url = kStaticUrl;
    req.timeout_ms = 15'000;
    const net::Response resp = request(req, policy::kData);
    if (!resp.ok()) {
        debug::log("[trade]  static data: %s", api_error(resp).c_str());
        return {};
    }
    return parse_static_currencies(resp.body);
}

} // namespace ppc::trade
