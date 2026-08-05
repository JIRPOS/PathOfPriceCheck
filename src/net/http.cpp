#include "net/http.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include <curl/curl.h>

#include "paths.hpp"

namespace ppc::net {
namespace {

struct Sink {
    const Request* req = nullptr;
    Response* resp = nullptr;
    bool aborted = false;
};

size_t on_write(char* p, size_t sz, size_t nm, void* ud) {
    auto* s = static_cast<Sink*>(ud);
    const size_t n = sz * nm;
    if (s->req->on_body) {
        if (!s->req->on_body(p, n)) {
            s->aborted = true;
            return 0; // short write aborts the transfer
        }
    } else {
        s->resp->body.append(p, n);
    }
    return n;
}

size_t on_header(char* p, size_t sz, size_t nm, void* ud) {
    auto* s = static_cast<Sink*>(ud);
    const size_t n = sz * nm;
    std::string_view line(p, n);
    const size_t colon = line.find(':');
    if (colon != std::string_view::npos) {
        std::string name(line.substr(0, colon));
        std::string value(line.substr(colon + 1));
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto trim = [](std::string& v) {
            const auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
            while (!v.empty() && ws(static_cast<unsigned char>(v.front()))) v.erase(v.begin());
            while (!v.empty() && ws(static_cast<unsigned char>(v.back()))) v.pop_back();
        };
        trim(name);
        trim(value);
        // Redirects mean several header blocks arrive; the last one wins, so drop any
        // earlier value for the same name rather than accumulating both.
        auto& h = s->resp->headers;
        h.erase(std::remove_if(h.begin(), h.end(), [&](const auto& kv) { return kv.first == name; }),
                h.end());
        h.emplace_back(std::move(name), std::move(value));
    }
    return n;
}

void restrict_permissions(const std::string& path) {
    std::error_code ec;
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, ec);
}

int on_progress(void* ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* s = static_cast<Sink*>(ud);
    if (!s->req->on_progress) return 0;
    if (!s->req->on_progress(static_cast<uint64_t>(dlnow), static_cast<uint64_t>(dltotal))) {
        s->aborted = true;
        return 1; // non-zero aborts
    }
    return 0;
}

} // namespace

const std::string* Response::header(std::string_view name) const {
    std::string want(name);
    std::transform(want.begin(), want.end(), want.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& [k, v] : headers)
        if (k == want) return &v;
    return nullptr;
}

const char* user_agent() {
    // Points at the public data repo: the app repo is private, so its URL would be a dead
    // contact route for anyone at GGG trying to reach us.
    static const std::string ua = "PathOfPriceCheck/" APP_VERSION
                                  " (+https://github.com/JIRPOS/PathOfPriceCheck-Data)";
    return ua.c_str();
}

// curl self-initialises on first use, but that path is not thread-safe; do it up front
// while the process is still single-threaded.
void init() { curl_global_init(CURL_GLOBAL_DEFAULT); }
void shutdown() { curl_global_cleanup(); }

Response get(const Request& r) {
    Response resp;
    CURL* h = curl_easy_init();
    if (!h) {
        resp.error = "curl_easy_init failed";
        return resp;
    }
    Sink sink{&r, &resp, false};

    curl_slist* hdrs = nullptr;
    for (const std::string& line : r.headers) hdrs = curl_slist_append(hdrs, line.c_str());
    if (!r.if_none_match.empty())
        hdrs = curl_slist_append(hdrs, ("If-None-Match: " + r.if_none_match).c_str());
    if (!r.body.empty()) {
        curl_easy_setopt(h, CURLOPT_POST, 1L);
        curl_easy_setopt(h, CURLOPT_POSTFIELDS, r.body.c_str());
        curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, static_cast<long>(r.body.size()));
        // curl's own default here is form-encoded, which GGG's trade search answers with a
        // 400 that says nothing about why.
        static constexpr std::string_view kCt = "content-type:";
        const bool typed = std::any_of(r.headers.begin(), r.headers.end(), [](const std::string& l) {
            return l.size() >= kCt.size() &&
                   std::equal(kCt.begin(), kCt.end(), l.begin(), [](char a, char b) {
                       return a == std::tolower(static_cast<unsigned char>(b));
                   });
        });
        if (!typed) hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
        // A 100-continue handshake on a sub-kilobyte body costs a round trip and, on a proxy
        // that never answers it, a full second of curl waiting for the go-ahead.
        hdrs = curl_slist_append(hdrs, "Expect:");
    }

    curl_easy_setopt(h, CURLOPT_URL, r.url.c_str());
    curl_easy_setopt(h, CURLOPT_USERAGENT, user_agent());
    curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, ""); // GGG serves gzip; curl decodes it
    // Mandatory off the main thread: without it curl uses signals for DNS timeouts.
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, static_cast<long>(r.timeout_ms));
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    // GitHub's releases/latest/download/... 302s twice, across hosts, to blob storage.
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, on_write);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(h, CURLOPT_HEADERDATA, &sink);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, &sink);
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    if (hdrs) curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdrs);

    // Persist cookies so a Cloudflare cf_clearance survives a restart rather than being
    // re-earned on every launch.
    static const std::string jar = (config_dir() / "cookies.txt").string();
    ensure_dir(config_dir());
    curl_easy_setopt(h, CURLOPT_COOKIEFILE, jar.c_str());
    curl_easy_setopt(h, CURLOPT_COOKIEJAR, jar.c_str());

    const CURLcode rc = curl_easy_perform(h);
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &resp.status);
    // The jar holds POESESSID. curl creates it with the process umask, i.e. world-readable
    // on a default Linux setup; a session cookie has no business being that.
    restrict_permissions(jar);

    if (rc != CURLE_OK) {
        resp.error = sink.aborted ? "cancelled" : curl_easy_strerror(rc);
    } else if (resp.status == 403) {
        // Distinguish GGG's bot protection from a bug in the tool — it is by far the most
        // likely cause of a 403 here and the message is what the user will report back.
        resp.error = "blocked by Cloudflare (403) \xe2\x80\x94 GGG bot protection";
    }

    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(h);
    return resp;
}

} // namespace ppc::net
