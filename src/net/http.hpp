#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ppc::net {

struct Response {
    long status = 0; ///< HTTP status; 0 if the request never completed
    std::string body; ///< empty when the caller supplied Request::on_body
    std::vector<std::pair<std::string, std::string>> headers; ///< names lowercased, in order
    std::string error; ///< transport-level failure; empty on success

    bool ok() const { return error.empty() && status >= 200 && status < 300; }
    bool not_modified() const { return error.empty() && status == 304; }

    /// nullptr if absent. Case-insensitive.
    const std::string* header(std::string_view name) const;
};

struct Request {
    std::string url;
    std::vector<std::string> headers; ///< extra "Name: value" lines; User-Agent is added for you
    std::string if_none_match;        ///< ETag for a conditional GET; empty to skip
    int timeout_ms = 8000;            ///< total budget, connect included

    /// Called as bytes arrive. Return false to abort the transfer.
    std::function<bool(uint64_t done, uint64_t total)> on_progress;

    /// Sink for the body. Return false to abort. When null, the body is buffered into
    /// Response::body instead — only do that for small payloads.
    std::function<bool(const char* data, size_t n)> on_body;
};

/// Blocking GET. Thread-safe. MUST NOT be called on the UI thread.
Response get(const Request& r);

/// GGG requires unregistered clients to identify themselves and offer a contact route.
/// An empty User-Agent is a hard 403 from their edge.
const char* user_agent();

void init();     ///< process-wide, before any worker thread exists
void shutdown();

} // namespace ppc::net
