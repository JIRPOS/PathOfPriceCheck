#pragma once

#include <cstdint>
#include <filesystem>

#include "trade/ratelimit.hpp"

namespace ppc::trade::ratelimit_store {

/// Where the limiter's state is kept between runs.
///
/// **Under `cache_dir()`, and deliberately not in `config.json`.** It is not a setting and
/// nobody should be editing it; putting it beside the user's preferences invites exactly that.
std::filesystem::path file();

/// Persist / read back the limiter's state, converting between the limiter's own relative
/// terms and absolute wall-clock milliseconds.
///
/// Wall clock rather than `steady_clock` because that is the only one with meaning across two
/// processes. **This is what stops a restart from clearing a restriction.** Without it, an app
/// that has just been told to back off for thirty minutes forgets the moment it is relaunched
/// and spends its whole seeded budget straight into the lockout — and a client that keeps
/// hammering through restrictions is the one that ends up blocked outright rather than merely
/// throttled. It is not a security boundary: deleting the file resets it, and it cannot be
/// made otherwise from inside the process. It stops the *accidental* circumvention, which is
/// the one that actually happens.
bool store(const LimiterState& s, int64_t now_unix_ms);
bool store(const std::filesystem::path& p, const LimiterState& s, int64_t now_unix_ms);

/// Empty when there is no file, it is unreadable, or it was written by an older layout —
/// which then simply means the run starts from the seeded defaults.
LimiterState load(int64_t now_unix_ms);
LimiterState load(const std::filesystem::path& p, int64_t now_unix_ms);

} // namespace ppc::trade::ratelimit_store
