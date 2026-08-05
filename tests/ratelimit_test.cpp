#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "trade/ratelimit.hpp"
#include "trade/ratelimit_store.hpp"

namespace fs = std::filesystem;

using namespace ppc::trade;

TEST_CASE("rate rule headers parse as GGG writes them") {
    const std::vector<RateRule> r = parse_rate_rules("5:10:60,15:60:300,30:300:1800");
    REQUIRE(r.size() == 3);
    CHECK(r[0].hits == 5);
    CHECK(r[0].period_s == 10);
    CHECK(r[0].restrict_s == 60);
    CHECK(r[2].restrict_s == 1800);

    const std::vector<RateState> s = parse_rate_state("1:10:0,2:60:0");
    REQUIRE(s.size() == 2);
    CHECK(s[1].hits == 2);
    CHECK(s[1].period_s == 60);

    // A malformed triplet is dropped, never guessed at: spacing requests against a number
    // GGG did not send is worse than having no rule at all.
    CHECK(parse_rate_rules("").empty());
    CHECK(parse_rate_rules("5:10").empty());
    CHECK(parse_rate_rules("a:b:c").empty());
    CHECK(parse_rate_rules("5:10:60,broken").size() == 1);
}

TEST_CASE("a full window is waited out, not overrun") {
    RateLimiter rl;
    rl.seed("search", {{2, 10, 60}});

    int64_t t = 1'000'000;
    CHECK(rl.delay_ms("search", t) == 0);
    rl.note_request("search", t);
    CHECK(rl.delay_ms("search", t) == 0);
    rl.note_request("search", t);

    // Two hits in a 10s window that allows two: the next one waits for the window to close.
    const int64_t wait = rl.delay_ms("search", t);
    CHECK(wait > 10'000);
    CHECK(wait < 11'000);

    // ...and is free again once it has.
    CHECK(rl.delay_ms("search", t + 11'000) == 0);
}

TEST_CASE("an unknown policy never delays") {
    RateLimiter rl;
    CHECK(rl.delay_ms("nothing-seen-yet", 1000) == 0);
}

TEST_CASE("the server's own count outranks ours") {
    RateLimiter rl;
    const int64_t t = 500'000;
    // Nothing sent from here, but the state header says this IP is four requests into a
    // five-request window — another copy of the tool, or the user's browser.
    rl.observe("search", "5:10:60", "4:10:0", 0, t);
    CHECK(rl.delay_ms("search", t) == 0);
    rl.note_request("search", t);
    CHECK(rl.delay_ms("search", t) > 0);
}

TEST_CASE("an active restriction blocks for its stated length") {
    RateLimiter rl;
    const int64_t t = 42'000;
    rl.observe("fetch", "12:4:10", "13:4:10", 0, t);
    const int64_t wait = rl.delay_ms("fetch", t);
    CHECK(wait >= 10'000);
    CHECK(rl.delay_ms("fetch", t + 10'001) == 0);
}

TEST_CASE("Retry-After on a 429 is honoured even without rule headers") {
    RateLimiter rl;
    rl.observe("fetch", "", "", 30, 0);
    CHECK(rl.delay_ms("fetch", 0) == 30'000);
    CHECK(rl.delay_ms("fetch", 29'000) == 1'000);
    CHECK(rl.delay_ms("fetch", 30'000) == 0);
}

TEST_CASE("a rule set that changes keeps the counts of the windows that survive") {
    RateLimiter rl;
    const int64_t t = 900'000;
    rl.observe("search", "5:10:60,15:60:300", "0:10:0,0:60:0", 0, t);
    for (int i = 0; i < 5; ++i) rl.note_request("search", t);
    CHECK(rl.delay_ms("search", t) > 0);
    // The 10s rule is unchanged, so its five hits must carry over rather than reset to zero
    // because the 60s rule beside it was renumbered.
    rl.observe("search", "5:10:60,20:60:300", "5:10:0,5:60:0", 0, t);
    CHECK(rl.delay_ms("search", t) > 0);
}

TEST_CASE("a restriction outlives the process that earned it") {
    // The scenario this exists for: told to back off for half an hour, the user closes the
    // app and opens it again. A limiter that starts clean would spend its whole seeded budget
    // into the lockout, and a client that keeps hammering through restrictions is the one that
    // gets blocked outright rather than merely throttled.
    const fs::path p = fs::temp_directory_path() / "ppc-ratelimit-test.json";
    fs::remove(p);

    RateLimiter first;
    first.seed("trade-search", {{5, 10, 60}, {30, 300, 1800}});
    first.note_request("trade-search", 1000);
    first.note_request("trade-search", 2000);
    first.observe("trade-search", "5:10:60,30:300:1800", "30:300:1800", 0, 3000);
    CHECK(first.delay_ms("trade-search", 3000) >= 1'800'000);

    // Saved at one wall clock, read back at another 60s later, on a steady clock that shares
    // nothing with the first run's.
    constexpr int64_t kSavedUnix = 1'785'954'110'000;
    REQUIRE(ratelimit_store::store(p, first.snapshot(3000), kSavedUnix));

    RateLimiter second;
    second.restore(ratelimit_store::load(p, kSavedUnix + 60'000), 500'000);
    // Seeding after a restore must not wipe it — that would be the bug, silently.
    second.seed("trade-search", {{5, 10, 60}, {30, 300, 1800}});
    const int64_t left = second.delay_ms("trade-search", 500'000);
    CHECK(left > 1'730'000); // the 30 minutes, less the minute that passed while it was closed
    CHECK(left <= 1'740'000);

    // And once it has genuinely elapsed, the file lets go rather than holding the client shut.
    RateLimiter third;
    third.restore(ratelimit_store::load(p, kSavedUnix + 3'600'000), 500'000);
    CHECK(third.delay_ms("trade-search", 500'000) == 0);

    fs::remove(p);
}

TEST_CASE("a corrupt or absent state file is not allowed to wedge the client") {
    const fs::path missing = fs::temp_directory_path() / "ppc-ratelimit-nope.json";
    fs::remove(missing);
    CHECK(ratelimit_store::load(missing, 1000).empty());

    const fs::path junk = fs::temp_directory_path() / "ppc-ratelimit-junk.json";
    { std::ofstream(junk) << "not json at all\n"; }
    CHECK(ratelimit_store::load(junk, 1000).empty());
    fs::remove(junk);

    // A block days long is a file that has been hand-edited or corrupted, and a client that
    // can be locked shut by one is worse than one that occasionally under-waits.
    LimiterState s;
    PolicyState ps;
    ps.policy = "trade-search";
    ps.blocked_for_ms = 90LL * 24 * 3600 * 1000;
    s.push_back(ps);
    RateLimiter rl;
    rl.restore(s, 0);
    CHECK(rl.delay_ms("trade-search", 0) <= 6 * 60 * 60 * 1000);

    // A window whose start is in the future — the clock moved — must still expire.
    LimiterState back;
    PolicyState bs;
    bs.policy = "trade-fetch";
    bs.rules = {{12, 4, 10}};
    bs.hits = {12};
    bs.window_age_ms = {-60'000};
    back.push_back(bs);
    RateLimiter rl2;
    rl2.restore(back, 0);
    CHECK(rl2.delay_ms("trade-fetch", 10'000) == 0);
}
