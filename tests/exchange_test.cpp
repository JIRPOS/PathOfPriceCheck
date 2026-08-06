#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "exchange/exchange.hpp"

using namespace ppc;

namespace {

/// A slice of a real hourly digest, kept verbatim: a payload change has to read back as a
/// parse failure rather than as a test that quietly stopped covering anything.
std::string fixture() {
    std::ifstream in(std::string(PPC_TEST_DATA_DIR) + "/exchange/digest.json", std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing fixture: exchange/digest.json");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

constexpr const char* kToad = "Metadata/Items/MapFragments/ToadAllflamePack";
constexpr const char* kAwakener = "Metadata/Items/Currency/CurrencyBreachUpgradeUniqueGeneral";
constexpr const char* kCard = "Metadata/Items/DivinationCards/DivinationCardTheCataclysm";
constexpr const char* kVaal = "Metadata/Items/Currency/CurrencyCorrupt";

exchange::Digest parse(const char* league) {
    exchange::Digest d;
    REQUIRE(exchange::parse_digest(fixture(), league, 1785999600, d));
    return d;
}

} // namespace

TEST_CASE("the newest hour is the last one that has fully elapsed") {
    // The hour in progress is not published — asking for it is a 404 with an empty payload —
    // so the freshest answer there can be is the hour before it.
    CHECK(exchange::latest_hour(1785999600 + 61) == 1785996000);
    CHECK(exchange::latest_hour(1785999600) == 1785996000); // exactly on the hour: still behind
    CHECK(exchange::latest_hour(1785999600 + 3599) == 1785996000);
    CHECK(exchange::latest_hour(1785999600 + 3600) == 1785999600);
    CHECK(exchange::latest_hour(0) == 0); // no clock: no hour to ask for
    CHECK(exchange::url(1785996000) ==
          "https://web.poecdn.com/api/currency-exchange/1785996000");
}

TEST_CASE("a digest is cut down to one league and to the pairs a price check can read") {
    const exchange::Digest d = parse("Allflame");
    CHECK(d.hour == 1785999600);
    CHECK(d.any_league);

    // Four items trade against a denominating currency in this slice. Everything else in it is
    // there to be dropped, and each for its own reason.
    CHECK(d.listings.size() == 4);
    // A market against neither Chaos nor Divine is a real market and no use to somebody asking
    // what a thing is worth.
    CHECK(d.find(kVaal) == nullptr);
    // A market that saw no trade in the hour is published with zero ratios. Dividing by that
    // would put a nonsense price on screen rather than none.
    CHECK(d.find(kCard) == nullptr);
    CHECK(d.find("Metadata/Items/Currency/NoSuchThing") == nullptr);

    SUBCASE("the band is ordered, not taken as named") {
        // "lowest ratio" is the lowest value of item-over-against, which is the item's
        // *dearest* price: 1 Awakener's for 50 chaos is the low ratio and the high price.
        const exchange::Listing* aw = d.find(kAwakener);
        REQUIRE(aw != nullptr);
        CHECK(aw->chaos.low == doctest::Approx(35));
        CHECK(aw->chaos.high == doctest::Approx(50));
        CHECK(aw->chaos.volume == doctest::Approx(151)); // the item's units…
        CHECK(aw->chaos.volume_against == doctest::Approx(6464)); // …and the chaos they cost
        CHECK_FALSE(aw->divine.known());                 // it has no divine market this hour

        // And it is not a fixed sense of the two names: on this market the counts move on both
        // sides, so the "lowest ratio" is the *cheaper* end. Ordering is what covers both.
        const exchange::Listing* toad = d.find(kToad);
        REQUIRE(toad != nullptr);
        CHECK(toad->chaos.low == doctest::Approx(0.5));
        CHECK(toad->chaos.high == doctest::Approx(2));
        CHECK_FALSE(toad->divine.known()); // its divine market is published all zeros
    }

    SUBCASE("both denominators are priced against the other, which is the rate itself") {
        const exchange::Listing* div = d.find(exchange::kDivineId);
        REQUIRE(div != nullptr);
        CHECK(div->chaos.low == doctest::Approx(199));
        CHECK(div->chaos.high == doctest::Approx(204));

        // The same market read from the other side, which is what a Chaos Orb in hand asks.
        const exchange::Listing* chaos = d.find(exchange::kChaosId);
        REQUIRE(chaos != nullptr);
        CHECK(chaos->divine.low == doctest::Approx(1.0 / 204));
        CHECK(chaos->divine.high == doctest::Approx(1.0 / 199));
    }
}

TEST_CASE("the average is the hour weighted by what actually traded") {
    const exchange::Digest d = parse("Allflame");

    // Not the midpoint of the band — a single trade can set either end of that. The two sides'
    // volumes divided are the mean ratio every trade in the hour cleared at, and it lands
    // inside the band, which is the evidence that the two are measuring the same thing.
    const exchange::Listing* aw = d.find(kAwakener);
    CHECK(aw->chaos.average() == doctest::Approx(6464.0 / 151)); // 42.8, band 35 – 50

    // Both denominators, from both sides: 17.3M chaos against 86k divine.
    CHECK(d.find(exchange::kDivineId)->chaos.average() == doctest::Approx(201.4).epsilon(0.001));
    CHECK(d.find(exchange::kChaosId)->divine.average() == doctest::Approx(1 / 201.4).epsilon(0.001));

    // A market published with zeros has no average, and 0 is not a price. Same for a side that
    // traded nothing at all.
    CHECK(exchange::Rate{}.average() == 0);
    CHECK(exchange::Rate{35, 50, 151, 0}.average() == 0);
}

TEST_CASE("a market is quoted against whichever of the two is worth more") {
    const exchange::Digest d = parse("Allflame");

    // A Chaos Orb is worth 1/204th of a divine, which is not how anyone says it.
    const exchange::Reading rate = exchange::read(d.find(exchange::kChaosId)->divine);
    CHECK(rate.inverted);
    CHECK(rate.low == doctest::Approx(199));
    CHECK(rate.high == doctest::Approx(204));
    // Rounded to what a price is said in: past a hundred, nobody quotes the decimal.
    CHECK(rate.avg == doctest::Approx(201));

    // The same market from the other side is already the right way round, and reads the same.
    const exchange::Reading div = exchange::read(d.find(exchange::kDivineId)->chaos);
    CHECK_FALSE(div.inverted);
    CHECK(div.low == doctest::Approx(199));
    CHECK(div.avg == doctest::Approx(201));

    // 0.5 – 2 chaos an ember: the band straddles one and has no direction of its own, so the
    // average is what decides. It cleared at 1.33, so the ember is the dearer of the two and
    // the price stays in chaos rather than turning into a count of embers.
    const exchange::Reading toad = exchange::read(d.find(kToad)->chaos);
    CHECK_FALSE(toad.inverted);
    CHECK(toad.avg == doctest::Approx(76.0 / 57).epsilon(0.01));
    CHECK(toad.low == doctest::Approx(0.5));
    CHECK(toad.high == doctest::Approx(2));

    // Without volume to average there is nothing but the band, and the top of it is the only
    // thing left to take the direction from: four to a chaos, not a quarter of one each.
    const exchange::Reading no_vol = exchange::read(exchange::Rate{0.2, 0.25, 0, 0});
    CHECK(no_vol.inverted);
    CHECK(no_vol.avg == 0);
    CHECK(no_vol.low == doctest::Approx(4));
    CHECK(no_vol.high == doctest::Approx(5));

    CHECK(exchange::read(exchange::Rate{}).low == 0); // nothing known, nothing claimed
}

TEST_CASE("the league is the whole of the filter, and having none of it is an answer") {
    // The same hour holds every league. Hardcore's rate is its own economy's, not a copy.
    const exchange::Digest hc = parse("Hardcore Allflame");
    CHECK(hc.listings.size() == 2); // only the two denominators trade in this slice
    REQUIRE(hc.find(exchange::kDivineId) != nullptr);
    CHECK(hc.find(exchange::kDivineId)->chaos.low == doctest::Approx(178));
    CHECK(hc.find(kToad) == nullptr);

    // A league with nothing in the digest still parsed one: `any_league` is what tells that
    // apart from an hour that is not published yet, which is worth stepping back for.
    const exchange::Digest none = parse("Standard");
    CHECK(none.listings.empty());
    CHECK(none.any_league);
}

TEST_CASE("an unpublished hour and a broken body are different failures") {
    exchange::Digest d;
    // What the feed answers for the hour in progress: a 404 carrying a well-formed payload.
    REQUIRE(exchange::parse_digest(R"({"next_change_id":1786003200,"markets":[]})", "Allflame",
                                   1786003200, d));
    CHECK_FALSE(d.any_league);
    CHECK(d.listings.empty());

    CHECK_FALSE(exchange::parse_digest("<html>502</html>", "Allflame", 0, d));
    CHECK_FALSE(exchange::parse_digest(R"({"next_change_id":1})", "Allflame", 0, d));
}
