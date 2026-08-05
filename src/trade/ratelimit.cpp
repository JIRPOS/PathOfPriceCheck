#include "trade/ratelimit.hpp"

#include <algorithm>
#include <array>
#include <charconv>

namespace ppc::trade {
namespace {

/// Ceiling on a restriction read back from disk. The longest GGG publishes is an hour; a file
/// claiming more has been corrupted or hand-edited, and a client that can be locked shut by a
/// bad cache file is worse than one that occasionally under-waits.
constexpr int64_t kMaxRestoredBlockMs = 6 * 60 * 60 * 1000;

/// A comma-separated list of colon-separated triplets. Anything that does not parse as
/// exactly three integers is dropped rather than guessed at: a rule we misread is worse
/// than a rule we do not have, because it would space requests against a number GGG never
/// sent.
std::vector<std::array<int, 3>> parse_triplets(std::string_view h) {
    std::vector<std::array<int, 3>> out;
    size_t pos = 0;
    while (pos <= h.size()) {
        const size_t end = std::min(h.find(',', pos), h.size());
        std::string_view item = h.substr(pos, end - pos);
        pos = end + 1;
        std::array<int, 3> t{};
        size_t p = 0;
        bool ok = true;
        for (int i = 0; i < 3 && ok; ++i) {
            const size_t stop = i == 2 ? item.size() : std::min(item.find(':', p), item.size());
            const auto* first = item.data() + p;
            const auto* last = item.data() + stop;
            if (first >= last || std::from_chars(first, last, t[i]).ptr != last) ok = false;
            p = stop + 1;
        }
        if (ok) out.push_back(t);
    }
    return out;
}

} // namespace

std::vector<RateRule> parse_rate_rules(std::string_view header) {
    std::vector<RateRule> out;
    for (const auto& t : parse_triplets(header)) out.push_back(RateRule{t[0], t[1], t[2]});
    return out;
}

std::vector<RateState> parse_rate_state(std::string_view header) {
    std::vector<RateState> out;
    for (const auto& t : parse_triplets(header)) out.push_back(RateState{t[0], t[1], t[2]});
    return out;
}

RateLimiter::Policy& RateLimiter::policy_for(std::string_view name) {
    if (const auto it = policies_.find(name); it != policies_.end()) return it->second;
    return policies_.emplace(std::string(name), Policy{}).first->second;
}

void RateLimiter::seed(std::string_view policy, std::vector<RateRule> rules) {
    Policy& p = policy_for(policy);
    if (!p.windows.empty()) return; // something real has been observed; do not overwrite it
    for (const RateRule& r : rules) p.windows.push_back(Window{r, 0, 0});
}

LimiterState RateLimiter::snapshot(int64_t now_ms) const {
    LimiterState out;
    out.reserve(policies_.size());
    for (const auto& [name, p] : policies_) {
        PolicyState s;
        s.policy = name;
        for (const Window& w : p.windows) {
            s.rules.push_back(w.rule);
            s.hits.push_back(w.hits);
            // A window with no hits has no start — `note_request` opens it — and its
            // `started_ms` is still the zero `seed` left. Reporting the age of that is the
            // steady clock's whole reading, which lands in the file as a timestamp from
            // whenever the machine booted.
            s.window_age_ms.push_back(w.hits == 0 ? 0
                                                  : std::max<int64_t>(0, now_ms - w.started_ms));
        }
        s.blocked_for_ms = std::max<int64_t>(0, p.blocked_until_ms - now_ms);
        out.push_back(std::move(s));
    }
    return out;
}

void RateLimiter::restore(const LimiterState& s, int64_t now_ms) {
    for (const PolicyState& ps : s) {
        Policy& p = policy_for(ps.policy);
        p.windows.clear();
        for (size_t i = 0; i < ps.rules.size(); ++i) {
            const int hits = i < ps.hits.size() ? ps.hits[i] : 0;
            // A negative age would put the window's start in the future, where it never
            // expires; that is a clock that moved, not a window we should honour forever.
            const int64_t age =
                i < ps.window_age_ms.size() ? std::max<int64_t>(0, ps.window_age_ms[i]) : 0;
            p.windows.push_back(Window{ps.rules[i], std::max(0, hits), now_ms - age});
        }
        p.blocked_until_ms =
            ps.blocked_for_ms > 0 ? now_ms + std::min(ps.blocked_for_ms, kMaxRestoredBlockMs) : 0;
    }
}

int64_t RateLimiter::delay_ms(std::string_view policy, int64_t now_ms) const {
    const auto it = policies_.find(policy);
    if (it == policies_.end()) return 0;
    const Policy& p = it->second;
    int64_t wait = std::max<int64_t>(0, p.blocked_until_ms - now_ms);

    for (const Window& w : p.windows) {
        if (w.rule.hits <= 0 || w.rule.period_s <= 0) continue;
        const int64_t ends = w.started_ms + int64_t(w.rule.period_s) * 1000;
        if (now_ms >= ends) continue;    // the window has rolled over; its count is spent
        if (w.hits < w.rule.hits) continue; // still room in this one
        // A full window: the next request has to land after it closes. The margin covers the
        // clock skew between us and GGG, which is the difference between waiting a moment and
        // eating the restriction the rule threatens.
        wait = std::max(wait, ends - now_ms + 250);
    }
    return wait;
}

void RateLimiter::note_request(std::string_view policy, int64_t now_ms) {
    Policy& p = policy_for(policy);
    for (Window& w : p.windows) {
        if (w.rule.period_s <= 0) continue;
        if (w.hits == 0 || now_ms >= w.started_ms + int64_t(w.rule.period_s) * 1000) {
            w.started_ms = now_ms;
            w.hits = 0;
        }
        ++w.hits;
    }
}

void RateLimiter::observe(std::string_view policy, std::string_view rules, std::string_view state,
                          int retry_after_s, int64_t now_ms) {
    Policy& p = policy_for(policy);
    if (const std::vector<RateRule> parsed = parse_rate_rules(rules); !parsed.empty()) {
        // Rules change between patches and between endpoints. Match the new list against the
        // old one by period, so a window that still exists keeps its start time and its count.
        std::vector<Window> next;
        next.reserve(parsed.size());
        for (const RateRule& r : parsed) {
            const auto old = std::find_if(p.windows.begin(), p.windows.end(), [&](const Window& w) {
                return w.rule.period_s == r.period_s;
            });
            next.push_back(old != p.windows.end() ? Window{r, old->hits, old->started_ms}
                                                  : Window{r, 0, now_ms});
        }
        p.windows = std::move(next);
    }

    // The server's count outranks ours: it sees every client on this IP, including a second
    // copy of this program and the browser tab the user has trade open in.
    for (const RateState& s : parse_rate_state(state)) {
        for (Window& w : p.windows) {
            if (w.rule.period_s != s.period_s) continue;
            if (w.hits == 0) w.started_ms = now_ms;
            w.hits = std::max(w.hits, s.hits);
            if (s.restricted_s > 0)
                p.blocked_until_ms =
                    std::max(p.blocked_until_ms, now_ms + int64_t(s.restricted_s) * 1000);
        }
    }
    if (retry_after_s > 0)
        p.blocked_until_ms = std::max(p.blocked_until_ms, now_ms + int64_t(retry_after_s) * 1000);
}

} // namespace ppc::trade
