#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ppc::trade {

/// One rule out of an `X-Rate-Limit-<policy>` header: `hits:period:restriction`, meaning
/// "more than `hits` requests inside `period` seconds and you are locked out for
/// `restriction` seconds". A policy states several at once and every one of them binds.
struct RateRule {
    int hits = 0;
    int period_s = 0;
    int restrict_s = 0;
};

/// The matching `-State` triplet: how many hits the *server* has counted in this window,
/// and how long a restriction it is currently serving.
struct RateState {
    int hits = 0;
    int period_s = 0;
    int restricted_s = 0;
};

/// "5:10:60,15:60:300" -> two rules. Empty on anything malformed; never throws.
std::vector<RateRule> parse_rate_rules(std::string_view header);
std::vector<RateState> parse_rate_state(std::string_view header);

/// One policy's live state, stated as **ages and time remaining** rather than as clock
/// readings. That is what lets it be written to disk and read back under a clock that has no
/// relationship to the one that wrote it — the limiter runs on `steady_clock`, whose zero is
/// the boot or the process, while a restriction has to outlive the process that earned it.
struct PolicyState {
    std::string policy;
    std::vector<RateRule> rules;
    std::vector<int> hits;              ///< parallel to `rules`
    std::vector<int64_t> window_age_ms; ///< parallel to `rules`: how long ago each window opened
    int64_t blocked_for_ms = 0;         ///< restriction still to serve, 0 when there is none
};
using LimiterState = std::vector<PolicyState>;

/// Proactively spaces GGG requests so a 429 never happens.
///
/// GGG publishes the limits in the response headers of every request, so the first call
/// under a policy is a guess and every one after it is measured. The state header carries
/// the server's own count, which is what makes this correct across restarts and alongside
/// another client on the same IP: our local tally is replaced by theirs after each response
/// rather than added to it.
///
/// Time is passed in rather than read, so the whole thing is testable without sleeping.
/// Not thread-safe; `trade/client` owns the one instance and holds a mutex over it.
class RateLimiter {
public:
    /// How long to wait before issuing the next request under `policy`. 0 to go now.
    int64_t delay_ms(std::string_view policy, int64_t now_ms) const;

    /// Record a request as sent. Must follow the wait, not replace it.
    void note_request(std::string_view policy, int64_t now_ms);

    /// Adopt the rules and the server's own counters from a response.
    /// `rules` is the `X-Rate-Limit-<policy>` value, `state` the `-State` one; a 429's
    /// `Retry-After` (seconds) goes in `retry_after_s`, 0 when there was none.
    void observe(std::string_view policy, std::string_view rules, std::string_view state,
                 int retry_after_s, int64_t now_ms);

    /// Seed a policy's rules before anything has been observed, so the very first burst is
    /// spaced too. A later `observe` overwrites them, and a `restore` before it wins outright.
    void seed(std::string_view policy, std::vector<RateRule> rules);

    /// The state of every policy, for persisting across runs. See `trade/ratelimit_store`.
    LimiterState snapshot(int64_t now_ms) const;

    /// Adopt a persisted state. Replaces whatever each named policy had, so it belongs before
    /// `seed` — a window restored from the last run is real and a seeded one is a guess.
    /// Nonsense from a corrupt file (negative ages, a block days long) is clamped rather than
    /// trusted: this must not be able to wedge the client shut.
    void restore(const LimiterState& s, int64_t now_ms);

private:
    struct Window {
        RateRule rule;
        int hits = 0;
        int64_t started_ms = 0;
    };
    struct Policy {
        std::vector<Window> windows;
        int64_t blocked_until_ms = 0; ///< an active restriction, from a state header or a 429
    };

    Policy& policy_for(std::string_view name);

    std::map<std::string, Policy, std::less<>> policies_;
};

} // namespace ppc::trade
