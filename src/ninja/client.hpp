#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "ninja/ninja.hpp"

namespace ppc::ninja {

struct FetchOutcome {
    std::shared_ptr<const Overview> overview; ///< null only when there was nothing to parse
    std::string error;                        ///< set even when a stale copy was served
};

/// Disk cache first, then the network. Blocking; worker threads only.
///
/// Deliberately **not** routed through `trade::request`: that limiter exists for GGG's
/// published per-policy budgets and this is a different host with different rules. poe.ninja's
/// are honoured differently — a thirty-minute cache (the same figure it sets on its own
/// responses), a conditional request when that expires so an unchanged overview costs a 304,
/// and one request per category rather than per price check.
///
/// A network failure with a stale copy on disk serves the stale copy: a price that is an hour
/// old is still a price, and `Overview::fetched_at` is drawn so the user can see which it is.
FetchOutcome load_overview(const Key& k, int64_t now_s);

} // namespace ppc::ninja
