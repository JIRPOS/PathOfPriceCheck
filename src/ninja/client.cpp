#include "ninja/client.hpp"

#include <utility>

#include "net/http.hpp"
#include "ninja/cache.hpp"
#include "util/debug_log.hpp"

namespace ppc::ninja {
namespace {

/// A gem overview is four megabytes of JSON, which is the one place this can be slow.
constexpr int kTimeoutMs = 20'000;

std::shared_ptr<const Overview> parse(const Key& k, const cache::Entry& e) {
    auto ov = std::make_shared<Overview>();
    ov->key = k;
    ov->fetched_at = e.fetched_at;
    const Category* c = category(k.type);
    if (!c || !parse_overview(e.body, c->feed, *ov)) return nullptr;
    return ov;
}

} // namespace

FetchOutcome load_overview(const Key& k, int64_t now_s) {
    FetchOutcome out;
    const std::string request_url = url(k);
    if (request_url.empty()) {
        out.error = "no poe.ninja overview called " + k.type;
        return out;
    }

    const std::filesystem::path path = cache::file(k);
    std::optional<cache::Entry> have = cache::load(path);
    if (have && cache::fresh(have->fetched_at, now_s)) {
        out.overview = parse(k, *have);
        if (out.overview) return out;
        have.reset(); // unparseable on disk: fall through and fetch a fresh copy
    }

    net::Request req;
    req.url = request_url;
    req.timeout_ms = kTimeoutMs;
    if (have) req.if_none_match = have->etag;
    const net::Response resp = net::get(req);

    cache::Entry entry;
    if (resp.not_modified() && have) {
        // Unchanged, so only the clock moves: the body already on disk is the current one and
        // re-downloading it is exactly what the conditional request was for.
        entry = std::move(*have);
        entry.fetched_at = now_s;
        debug::log("[ninja]  %s: 304, cache still current", k.type.c_str());
    } else if (resp.ok() && !resp.body.empty()) {
        entry.body = resp.body;
        if (const std::string* tag = resp.header("ETag")) entry.etag = *tag;
        entry.fetched_at = now_s;
        debug::log("[ninja]  %s: %zu bytes", k.type.c_str(), entry.body.size());
    } else {
        out.error = resp.error.empty() ? "HTTP " + std::to_string(resp.status) : resp.error;
        debug::log("[ninja]  %s: %s", k.type.c_str(), out.error.c_str());
        if (!have) return out;
        // Stale beats nothing, and the row prints how old it is.
        out.overview = parse(k, *have);
        return out;
    }

    cache::store(path, entry);
    out.overview = parse(k, entry);
    if (!out.overview) out.error = "the poe.ninja response could not be read";
    return out;
}

} // namespace ppc::ninja
