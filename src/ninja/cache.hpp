#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "ninja/ninja.hpp"

namespace ppc::ninja::cache {

/// Where an overview's body is kept between runs. One file per league and category.
std::filesystem::path file(const Key& k);

/// A downloaded overview exactly as it arrived, plus what a conditional request needs.
struct Entry {
    std::string body;
    std::string etag;       ///< empty when the response carried none
    int64_t fetched_at = 0; ///< unix seconds
};

/// The body is stored verbatim after a one-line JSON header rather than *inside* a JSON
/// document: a gem overview is four megabytes, and escaping it into a string field would
/// cost more than the download did.
std::optional<Entry> load(const std::filesystem::path& p);
bool store(const std::filesystem::path& p, const Entry& e);

/// A future timestamp counts as stale — a clock rollback must not pin a cache forever.
bool fresh(int64_t fetched_at, int64_t now_s, int64_t ttl_s = kTtlSeconds);

/// How long an untouched overview is kept before it is deleted rather than refreshed. Nothing
/// re-reads a file this old, and the league it belongs to is usually over.
inline constexpr int64_t kKeepSeconds = 7 * 24 * 60 * 60;

/// Delete overviews nothing has read in `keep_s`. A league's categories are a few megabytes
/// each and a new league never touches the old one's files again, so without this the cache
/// grows by every league the user ever plays. Best effort: a file that will not delete is left.
void prune(int64_t keep_s = kKeepSeconds);

} // namespace ppc::ninja::cache
