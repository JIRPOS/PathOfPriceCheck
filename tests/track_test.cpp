#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "ui/track.hpp"

using namespace ppc::ui;

// The arithmetic behind every range slider's track. It is here rather than beside the widget
// because the widget links ImGui and this does not — and because the numbers are the part worth
// pinning: a track that fails to reach past the roll is the slider people call broken, and one
// that reaches by a different rule on each row is worse.

TEST_CASE("each end moves by half its own magnitude") {
    const TrackSpan s = widen_track(110, 134, 0); // a physical damage tier
    CHECK(s.lo == doctest::Approx(55));
    CHECK(s.hi == doctest::Approx(201));
}

TEST_CASE("the roll always has room either side of it") {
    // The property the whole thing exists for, over the shapes the game actually prints.
    struct Case {
        double lo, hi;
        int dp;
    };
    for (const Case c : {Case{110, 134, 0}, Case{1.30, 1.45, 2}, Case{-13, -9, 0}, Case{1, 1, 0},
                         Case{0, 0, 0}, Case{0, 0, 2}, Case{6, 6, 0}, Case{20, 24, 1}}) {
        const TrackSpan s = widen_track(c.lo, c.hi, c.dp);
        CHECK(s.lo < c.lo);
        CHECK(s.hi > c.hi);
    }
}

TEST_CASE("a range printed negative grows away from zero, not toward it") {
    const TrackSpan s = widen_track(-13, -9, 0); // an eldritch implicit's cold resistance
    CHECK(s.lo == doctest::Approx(-20));         // -13 - 6.5, rounded outwards
    CHECK(s.hi == doctest::Approx(-4));          // -9 + 4.5, rounded outwards
}

TEST_CASE("a tier that rolls one number still gets a track") {
    const TrackSpan s = widen_track(6, 6, 0);
    CHECK(s.lo == doctest::Approx(3));
    CHECK(s.hi == doctest::Approx(9));
}

TEST_CASE("the reach is at least one step at the row's own precision") {
    // Half of nothing is nothing, which would hand back a track with no width at all.
    const TrackSpan zero = widen_track(0, 0, 0);
    CHECK(zero.lo == doctest::Approx(-1));
    CHECK(zero.hi == doctest::Approx(1));

    const TrackSpan tenths = widen_track(0, 0, 1);
    CHECK(tenths.lo == doctest::Approx(-0.1));
    CHECK(tenths.hi == doctest::Approx(0.1));

    // And a value small enough that half of it rounds away at this precision: 0.02 either side
    // of 0.04 would land inside the same step, so the step wins.
    const TrackSpan small = widen_track(0.04, 0.04, 2);
    CHECK(small.lo == doctest::Approx(0.02));
    CHECK(small.hi == doctest::Approx(0.06));
}

TEST_CASE("ends are rounded outwards, never inwards") {
    // 1.30 - 0.65 = 0.65 and 1.45 + 0.725 = 2.175 at one decimal: 0.6 and 2.2, never 0.7 or 2.1.
    const TrackSpan s = widen_track(1.30, 1.45, 1); // an attack speed
    CHECK(s.lo == doctest::Approx(0.6));
    CHECK(s.hi == doctest::Approx(2.2));
}

TEST_CASE("a crossed range is ordered, not refused") {
    // The bounds behind a track can be mid-edit, and half of a number being typed is a real one.
    const TrackSpan s = widen_track(134, 110, 0);
    CHECK(s.lo == doctest::Approx(55));
    CHECK(s.hi == doctest::Approx(201));
}

TEST_CASE("nothing escapes the range limit") {
    const TrackSpan s = widen_track(-kRangeLimit, kRangeLimit, 0);
    CHECK(s.lo == doctest::Approx(-kRangeLimit));
    CHECK(s.hi == doctest::Approx(kRangeLimit));
}
