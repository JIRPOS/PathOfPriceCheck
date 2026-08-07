#include "exchange/exchange.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ppc::exchange {
namespace {

constexpr int64_t kHour = 3600;

/// The ratio maps are integer counts of the two sides — `{A: 1, B: 50}` reads as one A for
/// fifty B — so the price of one A in B is B/A. Both ends have to be present and non-zero:
/// a market that saw no trade in the hour is published with zeros, and dividing by that
/// would put a nonsense price on screen rather than none.
bool ratio(const json& r, const std::string& item, const std::string& against, double& out) {
    const auto a = r.find(item);
    const auto b = r.find(against);
    if (a == r.end() || b == r.end() || !a->is_number() || !b->is_number()) return false;
    const double na = a->get<double>();
    const double nb = b->get<double>();
    if (na <= 0 || nb <= 0) return false;
    out = nb / na;
    return true;
}

double number(const json& j, const std::string& key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : 0.0;
}

/// Fill the item's side of one market. `low`/`high` are ordered here rather than taken as
/// named: the payload's "lowest ratio" is the lowest value of *item over against*, which is
/// the item's **dearest** price, and reading the two names as a price band gets it backwards.
///
/// Both volumes are kept, not just the item's: the two of them divided are the hour's
/// volume-weighted average price, which is the one number in the payload that says where in
/// the band the market actually sat.
void fill(Rate& out, const json& market, const std::string& item, const std::string& against) {
    const auto lo = market.find("lowest_ratio");
    const auto hi = market.find("highest_ratio");
    if (lo == market.end() || hi == market.end() || !lo->is_object() || !hi->is_object()) return;
    double a = 0, b = 0;
    if (!ratio(*lo, item, against, a) || !ratio(*hi, item, against, b)) return;
    out.low = std::min(a, b);
    out.high = std::max(a, b);
    const auto vol = market.find("volume_traded");
    if (vol != market.end() && vol->is_object()) {
        out.volume = number(*vol, item);
        out.volume_against = number(*vol, against);
    }
}

} // namespace

int64_t latest_hour(int64_t now_s) {
    if (now_s < kHour) return 0;
    return now_s / kHour * kHour - kHour;
}

std::string url(int64_t hour) { return std::string(kApiBase) + "/" + std::to_string(hour); }

std::string cache_name(int64_t hour) { return std::to_string(hour) + ".json"; }

double Rate::average() const {
    if (volume <= 0 || volume_against <= 0) return 0;
    return volume_against / volume;
}

Reading read(const Rate& r) {
    Reading out;
    if (!r.known()) return out;
    const double avg = r.average();
    // Which of the two is worth more decides the direction, and the average is what says so —
    // it is the number the line leads with, and a band that straddles one (0.5 – 2 chaos an
    // ember) has no direction of its own. Without volume to average, the top of the band is
    // the only thing left to ask.
    out.inverted = avg > 0 ? avg < 1 : r.high < 1;
    out.low = out.inverted ? 1 / r.high : r.low;
    out.high = out.inverted ? 1 / r.low : r.high;
    if (avg > 0) out.avg = out.inverted ? 1 / avg : avg;
    for (double* v : {&out.low, &out.high, &out.avg}) {
        // Nobody quotes 2.04082 to a chaos. Same rule as `ninja::quote_in`, so the two prices
        // on screen are said to the same precision.
        const double step = *v < 10 ? 100.0 : (*v < 100 ? 10.0 : 1.0);
        *v = std::round(*v * step) / step;
    }
    return out;
}

const Listing* Digest::find(std::string_view metadata_id) const {
    if (metadata_id.empty()) return nullptr;
    for (const Listing& l : listings)
        if (l.metadata_id == metadata_id) return &l;
    return nullptr;
}

bool parse_digest(std::string_view body, std::string_view league, int64_t hour, Digest& out) {
    const json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return false;
    const auto markets = j.find("markets");
    if (markets == j.end() || !markets->is_array()) return false;

    out.hour = hour;
    out.league = league;
    out.any_league = !markets->empty();
    out.listings.clear();

    // One item trades against both denominators as two separate markets, so the two halves of
    // a listing arrive apart and are joined here.
    std::unordered_map<std::string, size_t> by_id;
    for (const json& m : *markets) {
        if (!m.is_object() || m.value("league", std::string()) != league) continue;
        const auto pair = m.find("market_pair");
        if (pair == m.end() || !pair->is_array() || pair->size() != 2) continue;
        const auto& a = (*pair)[0];
        const auto& b = (*pair)[1];
        if (!a.is_string() || !b.is_string()) continue;

        const std::string ids[2] = {a.get<std::string>(), b.get<std::string>()};
        for (int i = 0; i < 2; ++i) {
            const std::string& item = ids[i];
            const std::string& against = ids[1 - i];
            if (against != kChaosId && against != kDivineId) continue;
            // The rate between the two denominators is a market like any other and is worth
            // keeping from both sides: it is the one number a player checking either orb is
            // actually after.
            Rate rate;
            fill(rate, m, item, against);
            if (!rate.known()) continue;

            const auto [pos, fresh] = by_id.try_emplace(item, out.listings.size());
            if (fresh) out.listings.push_back(Listing{item, {}, {}});
            Listing& l = out.listings[pos->second];
            (against == kChaosId ? l.chaos : l.divine) = rate;
        }
    }
    return true;
}

} // namespace ppc::exchange
