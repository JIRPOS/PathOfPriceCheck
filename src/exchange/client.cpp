#include "exchange/client.hpp"

#include "exchange/cache.hpp"
#include "net/http.hpp"
#include "util/debug_log.hpp"

namespace ppc::exchange {
namespace {

/// An hourly digest is a couple of megabytes covering every league.
constexpr int kTimeoutMs = 20'000;

std::shared_ptr<const Digest> parse(const std::string& body, const std::string& league,
                                    int64_t hour) {
    auto d = std::make_shared<Digest>();
    if (!parse_digest(body, league, hour, *d)) return nullptr;
    return d;
}

} // namespace

FetchOutcome load_digest(const std::string& league, int64_t now_s) {
    FetchOutcome out;
    int64_t hour = latest_hour(now_s);
    for (int step = 0; step < kStepBackHours && hour > 0; ++step, hour -= 3600) {
        if (const std::string have = cache::load(hour); !have.empty()) {
            if (auto d = parse(have, league, hour)) {
                // An hour on disk is the hour it says it is — nothing rewrites one — so a hit
                // is final even when the league has nothing in it.
                out.digest = std::move(d);
                return out;
            }
        }

        net::Request req;
        req.url = url(hour);
        req.timeout_ms = kTimeoutMs;
        const net::Response resp = net::get(req);
        if (!resp.ok() || resp.body.empty()) {
            // The hour in progress, and one that has ended but is not published yet, both
            // answer 404 with a well-formed empty payload. Stepping back is the normal path,
            // not the failure path.
            out.error = resp.error.empty() ? "HTTP " + std::to_string(resp.status) : resp.error;
            debug::log("[exchange] %lld: %s", static_cast<long long>(hour), out.error.c_str());
            continue;
        }

        auto d = parse(resp.body, league, hour);
        if (!d) {
            out.error = "the currency exchange response could not be read";
            debug::log("[exchange] %lld: unparseable, %zu bytes", static_cast<long long>(hour),
                       resp.body.size());
            continue;
        }
        if (!d->any_league) {
            debug::log("[exchange] %lld: published empty, stepping back",
                       static_cast<long long>(hour));
            continue; // not published yet; do not cache an empty hour as if it were the answer
        }

        cache::store(hour, resp.body);
        cache::prune();
        debug::log("[exchange] %lld: %zu bytes, %zu markets in %s",
                   static_cast<long long>(hour), resp.body.size(), d->listings.size(),
                   league.c_str());
        out.digest = std::move(d);
        out.error.clear();
        return out;
    }
    if (out.error.empty()) out.error = "no currency exchange data published yet";
    return out;
}

} // namespace ppc::exchange
