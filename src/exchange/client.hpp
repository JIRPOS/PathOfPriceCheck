#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "exchange/exchange.hpp"

namespace ppc::exchange {

struct FetchOutcome {
    std::shared_ptr<const Digest> digest; ///< null only when nothing could be read at all
    std::string error;
};

/// The newest digest there is, for one league. Disk cache first, then the network. Blocking;
/// worker threads only.
///
/// Not routed through `trade::request`: that limiter serves GGG's published per-policy budgets
/// on the API host, and this is the CDN — no policy headers, no per-application budget, and a
/// body that never changes once published. What stands in for it is that an hour is downloaded
/// **once**, from `<cache>/exchange/`, however many items are priced against it.
///
/// `latest_hour` is only the newest hour that *could* exist; the feed publishes a few minutes
/// late often enough that asking for it is a normal miss, so this steps back up to
/// `kStepBackHours` before giving up.
FetchOutcome load_digest(const std::string& league, int64_t now_s);

inline constexpr int kStepBackHours = 3;

} // namespace ppc::exchange
