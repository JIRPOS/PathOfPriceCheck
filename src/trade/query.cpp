#include "trade/query.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <utility>

#include <nlohmann/json.hpp>

#include "data/types.hpp"
#include "util/base64.hpp"

using json = nlohmann::json;

namespace ppc::trade {
namespace {

/// Which filter group a `NumericFilter::key` belongs to. The trade API nests every filter
/// under a group and rejects one filed in the wrong place, so this table is the contract
/// between `item/plan` and the wire format.
std::string_view group_for(std::string_view key) {
    if (key == "ilvl" || key == "quality" || key == "gem_level" || key == "memory_level" ||
        key == "intangibility" || key == "stored_experience")
        return "misc_filters";
    if (key == "ar" || key == "ev" || key == "es" || key == "ward" ||
        key == "base_defence_percentile")
        return "armour_filters";
    if (key == "dps" || key == "pdps" || key == "edps" || key == "aps" || key == "crit")
        return "weapon_filters";
    if (key == "map_tier" || key == "map_iiq" || key == "map_iir" || key == "map_packsize" ||
        key == "area_level" || key == "chart_sulphur")
        return "map_filters";
    // The site's group also takes per-colour counts (`r`, `g`, `b`, `w`) under the same two
    // names. Nothing plans those: a socket colour is one Chromatic Orb away and pricing on it
    // would be pricing the wrong thing.
    if (key == "sockets" || key == "links") return "socket_filters";
    // The reveal counts and the nine rogue-job levels. `area_level` above is deliberately not
    // among them: a heist item asks about one, and the site still files it under Map/Chart.
    if (key.starts_with("heist_")) return "heist_filters";
    // Resolve, Maximum Resolve, Inspiration and Aureus — the whole group, and `area_level` is
    // the same exception it is for a heist item: a sanctum asks about one and it stays in
    // Map/Chart. `sanctum_gold` is the site's own name for what the item calls Aureus.
    if (key.starts_with("sanctum_")) return "sanctum_filters";
    return {};
}

/// The same contract for an option filter. Three groups take any: the `misc_filters` booleans
/// about the item itself, the `map_filters` ones about where a map or a chart goes, and the
/// `ultimatum_filters` four, which are the whole of what an Inscribed Ultimatum is.
std::string_view option_group_for(std::string_view key) {
    if (key.starts_with("ultimatum_")) return "ultimatum_filters";
    if (key.starts_with("heist_")) return "heist_filters";
    if (key.starts_with("map_") || key == "chart_shape") return "map_filters";
    return "misc_filters";
}

/// The `misc_filters` boolean for an influence. Searing Exarch and Eater of Worlds have
/// none — the trade site indexes those through the eldritch implicits themselves, which
/// are already ordinary stat filters, so there is nothing to add here.
std::string_view influence_key(item::Influence i) {
    switch (i) {
        case item::Influence::Shaper: return "shaper_item";
        case item::Influence::Elder: return "elder_item";
        case item::Influence::Crusader: return "crusader_item";
        case item::Influence::Redeemer: return "redeemer_item";
        case item::Influence::Hunter: return "hunter_item";
        case item::Influence::Warlord: return "warlord_item";
        default: return {};
    }
}

/// Trade wants booleans as the strings "true"/"false" under an "option" key.
json option(bool v) { return json{{"option", v ? "true" : "false"}}; }

/// A `{"min":…,"max":…}` value, omitting whichever side is unbounded.
json bounds(const std::optional<double>& min, const std::optional<double>& max) {
    json v = json::object();
    if (min) v["min"] = *min;
    if (max) v["max"] = *max;
    return v;
}

/// The same as whole numbers, for `socket_filters`.
///
/// **Measured, not assumed**: the site answers a socket bound of `6.0` with "Socket min must be an
/// integer" and runs no search at all — and it is right to, since there is no such thing as five
/// and a half sockets. Every other group takes the same value as a float without complaint, which
/// is why this is one group's rule rather than the wire format's. Rounded rather than truncated:
/// a floor of `5.9` truncated to 5 asks for something wider than what was typed.
json int_bounds(const std::optional<double>& min, const std::optional<double>& max) {
    json v = json::object();
    if (min) v["min"] = std::llround(*min);
    if (max) v["max"] = std::llround(*max);
    return v;
}

/// A name/type term, which is a bare string unless the base needs a discriminator to be
/// unambiguous ("Maelström Staff" exists once per influence variant).
json term(const std::string& text, const std::string& disc) {
    if (disc.empty()) return json(text);
    return json{{"option", text}, {"discriminator", disc}};
}

} // namespace

bool searchable(const item::SearchPlan& p) {
    switch (p.strategy) {
        case item::Strategy::BaseItem:
        case item::Strategy::Modifiers:
        case item::Strategy::Unique:
        case item::Strategy::Map: return true;
        // A beast is its species, and a plan that could not name one has nothing left: the
        // search would be every beast in the league at this item level. Same shape as a gem.
        case item::Strategy::Beast: return !p.type.empty();
        // A heist item is the area it opens, and the category is the coarser search left when
        // the bundle cannot name one — unlike a gem or a beast, that is still a real question
        // ("some blueprint at area level 83 with these wings revealed") rather than a useless
        // one, because the filters that tell two apart do not live in the type term.
        case item::Strategy::Heist: return !p.type.empty() || !p.category.empty();
        // An ultimatum is one base type, so the type term alone is every ultimatum in the league
        // — but the whole of what tells two apart is the ultimatum filters, and a plan that could
        // fill none of them in is that useless search. Type *and* something asked about it.
        case item::Strategy::Ultimatum:
            return !p.type.empty() &&
                   std::any_of(p.options.begin(), p.options.end(), [](const item::OptionFilter& f) {
                       return f.enabled && f.key.starts_with("ultimatum_");
                   });
        // A gem is bought by name and has no modifiers to fall back on, so a plan that could
        // not name it has nothing left to ask: the search would be every gem in the game at
        // this level, and its cheapest listing would read as this gem's price.
        // A sanctum, on the heist reading: the floor is the type term, and the category left
        // when the bundle cannot name one is still a real question, because resolve, aureus and
        // the boons are what tell two runs apart and none of them lives in the type.
        case item::Strategy::Sanctum: return !p.type.empty() || !p.category.empty();
        case item::Strategy::Gem: return !p.type.empty();
        // Currency is bought in bulk and has nothing a stat query could ask about — except the
        // one kind that is not interchangeable, which the plan says so about by naming a type.
        // See `build_plan`'s Facetor's Lens branch.
        case item::Strategy::Currency: return !p.type.empty();
        default: return false;
    }
}

// Declared in trade.hpp so `Config` can hold one without pulling in the item layer; defined
// here because `status` is a field of the query and nothing else.
const std::vector<StatusOption>& status_options() {
    // Verbatim from status_filters in /api/trade/data/filters, in the site's own order.
    static const std::vector<StatusOption> kOptions{
        {"available", "Instant Buyout and In Person"},
        {"securable", "Instant Buyout"},
        {"onlineleague", "In Person (Online in League)"},
        {"online", "In Person (Online)"},
        {"any", "Any"}};
    return kOptions;
}

const std::vector<int>& result_counts() {
    static const std::vector<int> kCounts{10, 20, 50, 100};
    return kCounts;
}

bool valid_result_count(int n) {
    const std::vector<int>& c = result_counts();
    return std::find(c.begin(), c.end(), n) != c.end();
}

int fetch_requests(int count) {
    return static_cast<int>((count + kFetchBatch - 1) / kFetchBatch);
}

bool valid_status(std::string_view id) {
    const std::vector<StatusOption>& o = status_options();
    return std::any_of(o.begin(), o.end(), [id](const StatusOption& s) { return s.id == id; });
}

std::string_view status_label(std::string_view id) {
    for (const StatusOption& s : status_options())
        if (s.id == id) return s.label;
    return id;
}

std::string build_query(const item::SearchPlan& p, std::string_view status) {
    // Two groups, because a filter can ask for a modifier's **absence** as readily as for its
    // presence: the site spells that as a second stat group of type "not", and a Valdo map
    // that does not void the character who dies in it is bought for exactly that.
    json stats = json::array(), absent = json::array();
    for (const item::StatFilter& f : p.stats) {
        if (!f.enabled) continue;
        if (f.negated) {
            absent.push_back(json{{"id", f.id}, {"disabled", false}});
            continue;
        }
        std::optional<double> min = f.min, max = f.max;
        // The site indexes this stat with the opposite sign to the one the game prints, so
        // the interval flips end for end as well as in sign — a floor becomes a ceiling.
        if (f.inverted) {
            const std::optional<double> lo = min, hi = max;
            min = hi ? std::optional<double>(-*hi) : std::nullopt;
            max = lo ? std::optional<double>(-*lo) : std::nullopt;
        }
        json e{{"id", f.id}, {"disabled", false}};
        if (min || max) e["value"] = bounds(min, max);
        stats.push_back(std::move(e));
    }

    json type_filters = json::object();
    if (!p.category.empty()) type_filters["category"] = json{{"option", p.category}};
    // A unique and a rare are different markets even for the same base, and which of the two is
    // being priced is the plan's own statement — a unique map is planned as a map, so reading
    // it back off the strategy would search it among the rare ones.
    if (!p.rarity.empty()) type_filters["rarity"] = json{{"option", p.rarity}};

    json misc = json::object(), armour = json::object(), weapon = json::object(),
         map = json::object(), ultimatum = json::object(), heist = json::object(),
         sanctum = json::object(), socket = json::object();
    // Whether an option has a row in the panel is the plan's business; all that matters here is
    // that an unticked one is not asked for. `map_completion_reward` rides the same path and
    // takes a unique's own name rather than a boolean, which is why the value is a string all
    // the way through — the site errors the whole search on a reward it does not know, so the
    // plan only ever fills one in from a name the bundle confirmed.
    for (const item::OptionFilter& f : p.options) {
        if (!f.enabled) continue;
        const std::string_view g = option_group_for(f.key);
        (g == "map_filters"         ? map
         : g == "ultimatum_filters" ? ultimatum
         : g == "heist_filters"     ? heist
                                    : misc)[f.key] = json{{"option", f.option}};
    }
    for (const item::Influence i : p.influences)
        if (const std::string_view k = influence_key(i); !k.empty()) misc[std::string(k)] = option(true);

    for (const item::NumericFilter& f : p.numerics) {
        if (!f.enabled) continue;
        const std::string_view g = group_for(f.key);
        if (g.empty()) continue;
        json v = g == "socket_filters" ? int_bounds(f.min, f.max) : bounds(f.min, f.max);
        if (v.empty()) continue;
        (g == "misc_filters"     ? misc
         : g == "armour_filters" ? armour
         : g == "map_filters"     ? map
         : g == "heist_filters"   ? heist
         : g == "sanctum_filters" ? sanctum
         : g == "socket_filters"  ? socket
                                  : weapon)[f.key] = std::move(v);
    }
    json filters = json::object();
    filters["type_filters"] = json{{"filters", std::move(type_filters)}};
    if (!misc.empty()) filters["misc_filters"] = json{{"filters", std::move(misc)}};
    if (!armour.empty()) filters["armour_filters"] = json{{"filters", std::move(armour)}};
    if (!weapon.empty()) filters["weapon_filters"] = json{{"filters", std::move(weapon)}};
    if (!map.empty()) filters["map_filters"] = json{{"filters", std::move(map)}};
    if (!ultimatum.empty()) filters["ultimatum_filters"] = json{{"filters", std::move(ultimatum)}};
    if (!heist.empty()) filters["heist_filters"] = json{{"filters", std::move(heist)}};
    if (!sanctum.empty()) filters["sanctum_filters"] = json{{"filters", std::move(sanctum)}};
    if (!socket.empty()) filters["socket_filters"] = json{{"filters", std::move(socket)}};

    json q = json::object();
    q["status"] = json{{"option", valid_status(status) ? status : kDefaultStatus}};
    // Whether a base is part of the search is the plan's call, not this layer's: a rare names
    // none, because it is bought for its modifiers and the category already says where those
    // can live, while a rare *flask* names one, because the base is half of what the same
    // modifiers are worth. See item/plan.
    if (!p.name.empty()) q["name"] = term(p.name, p.discriminator);
    // A unique's discriminator belongs to its name, which is what the site disambiguates —
    // except on a map, where the type "Map" carries the same one and needs it too.
    if (!p.type.empty())
        q["type"] = term(p.type, p.strategy == item::Strategy::Unique ? std::string()
                                                                      : p.discriminator);
    q["stats"] = json::array({json{{"type", "and"}, {"filters", std::move(stats)}}});
    if (!absent.empty())
        q["stats"].push_back(json{{"type", "not"}, {"filters", std::move(absent)}});
    q["filters"] = std::move(filters);

    return json{{"query", std::move(q)}, {"sort", json{{"price", "asc"}}}}.dump();
}

std::string url_encode(std::string_view s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
        }
    }
    return out;
}

std::string search_url(std::string_view league) {
    return std::string(kApiBase) + "/search/" + url_encode(league);
}

std::string fetch_url(const std::vector<std::string>& hashes, std::string_view query_id) {
    std::string ids;
    for (const std::string& h : hashes) {
        if (!ids.empty()) ids += ',';
        ids += h;
    }
    return std::string(kApiBase) + "/fetch/" + ids + "?query=" + url_encode(query_id);
}

std::string web_url(std::string_view league, std::string_view query_id) {
    return std::string(kWebBase) + "/" + url_encode(league) + "/" + url_encode(query_id);
}

std::string web_url_for_query(std::string_view league, std::string_view query_json) {
    return std::string(kWebBase) + "/" + url_encode(league) + "?q=" + url_encode(query_json);
}

bool parse_search(std::string_view body, SearchResults& out) {
    const json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return false;
    const auto id = j.find("id");
    if (id == j.end() || !id->is_string()) return false;
    out.query_id = id->get<std::string>();
    if (const auto t = j.find("total"); t != j.end() && t->is_number()) out.total = t->get<int>();
    if (const auto r = j.find("result"); r != j.end() && r->is_array())
        for (const json& h : *r)
            if (h.is_string()) out.hashes.push_back(h.get<std::string>());
    return true;
}

namespace {

/// Put back the mod-type markers `extended.text` leaves off.
///
/// That text is the clipboard format, which is the whole point of it — one parser, one view —
/// but the site's renderer only writes the " (enchant)" suffix. An implicit, a crafted mod and
/// a fractured one arrive as bare lines: a fractured mod is simply printed first in the explicit
/// block. Nothing downstream can tell them apart, so a listing's fractured mod rendered in the
/// explicit blue and its implicit sat in the explicit block.
///
/// The payload does say which is which — `domain` per mod, and the array it arrived in for the
/// older shape where each entry is a plain string — and the mod's own domain outranks the array,
/// since a fractured or crafted mod is listed among the explicits.
std::string restore_mod_markers(std::string text, const json& item) {
    struct ModArray {
        const char* key;
        data::ModType type;
    };
    static constexpr ModArray kArrays[]{
        {"implicitMods", data::ModType::Implicit}, {"explicitMods", data::ModType::Explicit},
        {"craftedMods", data::ModType::Crafted},   {"fracturedMods", data::ModType::Fractured},
        {"enchantMods", data::ModType::Enchant},   {"veiledMods", data::ModType::Veiled},
        {"scourgeMods", data::ModType::Scourge},   {"crucibleMods", data::ModType::Crucible},
    };

    // The markers still owed, as (line, type). One entry per printed line: the game suffixes
    // every line of a multi-line mod, and a description holds them all.
    std::vector<std::pair<std::string, data::ModType>> want;
    for (const ModArray& a : kArrays) {
        const auto arr = item.find(a.key);
        if (arr == item.end() || !arr->is_array()) continue;
        for (const json& m : *arr) {
            data::ModType type = a.type;
            std::string desc;
            if (m.is_string()) {
                desc = m.get<std::string>();
            } else if (m.is_object()) {
                if (const auto d = m.find("description"); d != m.end() && d->is_string())
                    desc = d->get<std::string>();
                if (const auto d = m.find("domain"); d != m.end() && d->is_string())
                    type = data::mod_type_from_prefix(d->get<std::string>()).value_or(type);
            }
            if (desc.empty() || type == data::ModType::Explicit) continue;
            for (size_t at = 0; at < desc.size();) {
                const size_t nl = desc.find('\n', at);
                std::string line = desc.substr(at, nl - at);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) want.emplace_back(std::move(line), type);
                if (nl == std::string::npos) break;
                at = nl + 1;
            }
        }
    }
    if (want.empty()) return text;

    std::string out;
    out.reserve(text.size() + want.size() * 12);
    for (size_t at = 0; at < text.size();) {
        const size_t nl = text.find('\n', at);
        const size_t end = nl == std::string::npos ? text.size() : nl + 1;
        size_t stop = nl == std::string::npos ? text.size() : nl;
        if (stop > at && text[stop - 1] == '\r') --stop;
        const std::string_view line(text.data() + at, stop - at);
        // A line the site already marked does not match any description, so it is left alone.
        const auto w = std::find_if(want.begin(), want.end(),
                                    [line](const auto& p) { return p.first == line; });
        out.append(text, at, stop - at);
        if (w != want.end()) {
            out += " (";
            out += data::trade_prefix(w->second);
            out += ')';
            want.erase(w); // one marker per printed line, so a repeated mod marks each copy
        }
        out.append(text, stop, end - stop);
        at = end;
    }
    return out;
}

} // namespace

bool parse_fetch(std::string_view body, std::vector<Listing>& out) {
    const json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return false;
    const auto r = j.find("result");
    if (r == j.end() || !r->is_array()) return false;

    for (const json& e : *r) {
        // A hash whose listing has gone since the search is returned as a null element.
        if (!e.is_object()) continue;
        const auto l = e.find("listing");
        if (l == e.end() || !l->is_object()) continue;
        Listing li;
        if (const auto a = l->find("account"); a != l->end() && a->is_object())
            if (const auto n = a->find("name"); n != a->end() && n->is_string())
                li.account = n->get<std::string>();
        if (const auto w = l->find("whisper"); w != l->end() && w->is_string())
            li.whisper = w->get<std::string>();
        if (const auto i = l->find("indexed"); i != l->end() && i->is_string())
            li.indexed_at = parse_iso8601_utc(i->get<std::string>());
        // Sibling of "price", not a field of it.
        if (const auto f = l->find("fee"); f != l->end() && f->is_number())
            li.fee = f->get<int64_t>();
        if (const auto p = l->find("price"); p != l->end() && p->is_object()) {
            const auto amt = p->find("amount");
            const auto cur = p->find("currency");
            if (amt != p->end() && amt->is_number() && cur != p->end() && cur->is_string()) {
                li.amount = amt->get<double>();
                li.currency = cur->get<std::string>();
                li.priced = true;
            }
            if (const auto t = p->find("type"); t != p->end() && t->is_string())
                li.price_type = t->get<std::string>();
        }
        if (const auto it = e.find("item"); it != e.end() && it->is_object())
            if (const auto x = it->find("extended"); x != it->end() && x->is_object())
                if (const auto t = x->find("text"); t != x->end() && t->is_string())
                    if (std::optional<std::string> s = base64_decode(t->get<std::string>()))
                        li.item_text = restore_mod_markers(std::move(*s), *it);
        out.push_back(std::move(li));
    }
    return true;
}

int64_t parse_iso8601_utc(std::string_view s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (std::sscanf(std::string(s).c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6)
        return 0;
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    // Howard Hinnant's days_from_civil: exact, branch-free, and needs no time zone database —
    // which is the point, since timegm() is not portable and mktime() is local time.
    const int64_t yy = y - (mo <= 2 ? 1 : 0);
    const int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    const int64_t yoe = yy - era * 400;
    const int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = era * 146097 + doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + se;
}

std::string age_text(int64_t indexed_at, int64_t now_s) {
    if (indexed_at <= 0) return "?";
    const int64_t age = now_s - indexed_at;
    if (age < 60) return "now";
    if (age < 3600) return std::to_string(age / 60) + "m";
    if (age < 86400) return std::to_string(age / 3600) + "h";
    return std::to_string(age / 86400) + "d";
}

std::string price_text(double amount) {
    char buf[32];
    // Prices are whole orbs far more often than not, and "10.00 chaos" reads as a precision
    // the seller did not state.
    if (amount == std::floor(amount) && std::fabs(amount) < 1e15)
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(amount));
    else
        std::snprintf(buf, sizeof buf, "%g", amount);
    return buf;
}

std::string gold_text(int64_t gold) {
    const std::string digits = std::to_string(gold < 0 ? -gold : gold);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return gold < 0 ? "-" + out : out;
}

} // namespace ppc::trade
