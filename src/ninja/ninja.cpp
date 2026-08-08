#include "ninja/ninja.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include <nlohmann/json.hpp>

#include "data/stat_normalize.hpp"
#include "util/sha256.hpp"

using json = nlohmann::json;

namespace ppc::ninja {
namespace {

/// Everything poe.ninja publishes that a price check can land on. The slug is the site's own
/// kebab-case of the category name, verified against the live pages rather than derived — the
/// two do not always agree, and a wrong slug is a link to a 404.
///
/// Deliberately not the whole list: what is missing is what nothing here can reach. A map is
/// priced by a trade search on its tier and never gets this far (`Strategy::Map`), and the rest —
/// incubators, beasts, memories, temples — has no strategy behind it either.
constexpr Category kCategories[] = {
    {Feed::Exchange, "Currency", "currency"},
    {Feed::Exchange, "Fragment", "fragments"},
    {Feed::Exchange, "DivinationCard", "divination-cards"},
    {Feed::Exchange, "Essence", "essences"},
    {Feed::Exchange, "Fossil", "fossils"},
    {Feed::Exchange, "Resonator", "resonators"},
    {Feed::Exchange, "Oil", "oils"},
    {Feed::Exchange, "Scarab", "scarabs"},
    {Feed::Exchange, "DeliriumOrb", "delirium-orbs"},
    {Feed::Exchange, "Artifact", "artifacts"},
    {Feed::Exchange, "Tattoo", "tattoos"},
    {Feed::Exchange, "Omen", "omens"},
    {Feed::Exchange, "AllflameEmber", "allflame-embers"},
    {Feed::Exchange, "Runegraft", "runegrafts"},
    {Feed::Exchange, "DjinnCoin", "djinn-coins"},
    {Feed::Exchange, "Ducat", "ducats"},
    {Feed::Exchange, "EnshroudingCrystal", "enshrouding-crystals"},
    {Feed::Exchange, "Astrolabe", "astrolabes"},
    // The only map item on the stash feed rather than the exchange: an invitation carries an
    // item level, so it is listed like an item instead of traded in bulk.
    {Feed::StashItem, "Invitation", "invitations"},
    {Feed::StashItem, "UniqueWeapon", "unique-weapons"},
    {Feed::StashItem, "UniqueArmour", "unique-armours"},
    {Feed::StashItem, "UniqueAccessory", "unique-accessories"},
    {Feed::StashItem, "UniqueFlask", "unique-flasks"},
    {Feed::StashItem, "UniqueJewel", "unique-jewels"},
    {Feed::StashItem, "UniqueTincture", "unique-tinctures"},
    {Feed::StashItem, "UniqueRelic", "unique-relics"},
    {Feed::StashItem, "SkillGem", "skill-gems"},
    {Feed::StashItem, "BaseType", "base-types"},
};

/// The exchange overview a currency-like item is in, when the currency market itself does not
/// have it. Keyed on the item's own name because that is the only thing the clipboard says
/// that separates a Scarab from an Essence: both are the "Stackable Currency" item class.
struct Keyword {
    std::string_view needle;
    std::string_view type;
};
constexpr Keyword kKeywords[] = {
    {"Scarab", "Scarab"},
    {"Essence of", "Essence"},
    {"Remnant of Corruption", "Essence"},
    {"Delirium Orb", "DeliriumOrb"},
    {"Oil", "Oil"},
    {"Artifact", "Artifact"},
    {"Tattoo", "Tattoo"},
    {"Omen", "Omen"},
    {"Allflame Ember", "AllflameEmber"},
    {"Runegraft", "Runegraft"},
    {"Djinn", "DjinnCoin"},
    {"Ducat", "Ducat"},
    {"Enshrouding Crystal", "EnshroudingCrystal"},
    {"Astrolabe", "Astrolabe"},
};

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

/// The overview the item's own name names, or empty.
std::string keyword_type(const Query& q) {
    if (q.names.empty()) return {};
    for (const Keyword& k : kKeywords)
        if (q.names.front().find(k.needle) != std::string::npos) return std::string(k.type);
    return {};
}

/// The overview a **map item** belongs in, or empty when it is not one.
///
/// Chosen off the item class and the name rather than off the strategy, because the strategy
/// no longer says: a map item that prints an item level is an item and is planned like one
/// (see `item::default_strategy`), and it still has to be priced out of the map-item feeds
/// rather than out of the crafting-base overview a `BaseItem` plan would otherwise ask for.
///
/// `map.fragment` is the whole of both map-item classes, so it says no more than "Stackable
/// Currency" does: scarabs, embers, splinters, breachstones and invitations all arrive under
/// it and poe.ninja publishes them in four different overviews. The name is the only thing
/// separating them, which is why the keyword table runs first and the rest is a fallback.
std::string map_item_type(const Query& q) {
    if (!starts_with(q.category, "map.fragment")) return {};
    if (std::string kw = keyword_type(q); !kw.empty()) return kw;
    if (!q.names.empty() && q.names.front().find("Invitation") != std::string::npos)
        return "Invitation";
    return "Fragment";
}

std::string percent_encode(std::string_view s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
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

/// Absolute URL for an image path. The exchange overview roots them at the CDN; the stash
/// overview already sends them absolute.
std::string image_url(std::string_view path) {
    if (path.empty()) return {};
    if (starts_with(path, "http")) return std::string(path);
    return "https://web.poecdn.com" + std::string(path);
}

double number(const json& j, const char* field) {
    const auto it = j.find(field);
    if (it == j.end() || !it->is_number()) return 0;
    return it->get<double>();
}

int integer(const json& j, const char* field) {
    const auto it = j.find(field);
    if (it == j.end() || !it->is_number()) return 0;
    return it->get<int>();
}

std::string text(const json& j, const char* field) {
    const auto it = j.find(field);
    if (it == j.end() || !it->is_string()) return {};
    return it->get<std::string>();
}

/// `sparkline` on an exchange line, `sparkLine` on a stash one — the same object under two
/// spellings, which is exactly the sort of thing that makes one feed's trend silently missing.
Spark parse_spark(const json& j) {
    Spark s;
    if (!j.is_object()) return s;
    s.change = static_cast<float>(number(j, "totalChange"));
    s.known = j.contains("totalChange");
    if (const auto d = j.find("data"); d != j.end() && d->is_array())
        for (const json& v : *d)
            if (v.is_number()) s.data.push_back(v.get<float>());
    return s;
}

bool parse_exchange(const json& j, Overview& out) {
    if (const auto core = j.find("core"); core != j.end() && core->is_object()) {
        // `rates` states how many divine one chaos buys, so it is inverted here — the rest of
        // this file only ever divides by a number in the hundreds.
        if (const auto r = core->find("rates"); r != core->end() && r->is_object()) {
            const double per_chaos = number(*r, "divine");
            if (per_chaos > 0) out.chaos_per_divine = 1.0 / per_chaos;
        }
    }

    // Names, images and page slugs live in a sibling array keyed by the same id as the prices.
    // A line with no metadata cannot be matched by name or drawn, so it is dropped.
    std::vector<Line> meta;
    if (const auto items = j.find("items"); items != j.end() && items->is_array())
        for (const json& e : *items) {
            if (!e.is_object()) continue;
            Line l;
            l.id = text(e, "id");
            l.name = text(e, "name");
            l.details_id = text(e, "detailsId");
            l.icon = image_url(text(e, "image"));
            if (!l.id.empty()) meta.push_back(std::move(l));
        }

    const auto lines = j.find("lines");
    if (lines == j.end() || !lines->is_array()) return false;
    for (const json& e : *lines) {
        if (!e.is_object()) continue;
        const std::string id = text(e, "id");
        const auto m = std::find_if(meta.begin(), meta.end(),
                                    [&](const Line& l) { return l.id == id; });
        if (m == meta.end()) continue;
        Line l = *m;
        l.chaos = number(e, "primaryValue");
        if (const auto s = e.find("sparkline"); s != e.end()) l.spark = parse_spark(*s);
        out.lines.push_back(std::move(l));
    }
    // The Divine Orb's own line, where there is one, outranks `core.rates`: that field is the
    // reciprocal rounded to four figures, so converting the divine line's price with it puts a
    // Divine Orb at 0.9995 divine. Priced against itself it is exactly one, which is the only
    // answer a reader will accept.
    if (const Line* divine = out.find_id("divine"); divine && divine->chaos > 0)
        out.chaos_per_divine = divine->chaos;
    return true;
}

bool parse_stash_items(const json& j, Overview& out) {
    const auto lines = j.find("lines");
    if (lines == j.end() || !lines->is_array()) return false;
    for (const json& e : *lines) {
        if (!e.is_object()) continue;
        Line l;
        l.name = text(e, "name");
        l.base_type = text(e, "baseType");
        l.variant = text(e, "variant");
        l.details_id = text(e, "detailsId");
        l.icon = image_url(text(e, "icon"));
        l.chaos = number(e, "chaosValue");
        l.links = integer(e, "links");
        l.level = integer(e, "levelRequired");
        l.gem_level = integer(e, "gemLevel");
        l.gem_quality = integer(e, "gemQuality");
        // Absent and null both mean "not corrupted"; only `true` splits the market.
        if (const auto c = e.find("corrupted"); c != e.end() && c->is_boolean())
            l.corrupted = c->get<bool>();
        l.listings = integer(e, "listingCount");
        if (const auto m = e.find("explicitModifiers"); m != e.end() && m->is_array())
            for (const json& mod : *m)
                if (mod.is_object())
                    if (std::string t = text(mod, "text"); !t.empty()) l.mods.push_back(std::move(t));
        if (const auto s = e.find("sparkLine"); s != e.end()) l.spark = parse_spark(*s);
        if (!l.name.empty()) out.lines.push_back(std::move(l));
    }
    return true;
}

bool iequal(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

/// Keep only the candidates the predicate accepts — **unless that would leave none**.
///
/// Every one of these is a property poe.ninja may or may not price separately: most uniques
/// are not split by corruption, only a handful are split by links. Dropping the whole set
/// because nothing carried the field would turn "poe.ninja does not distinguish this" into
/// "poe.ninja has never seen one".
template <typename Pred>
void narrow(std::vector<const Line*>& candidates, Pred pred) {
    std::vector<const Line*> kept;
    for (const Line* l : candidates)
        if (pred(*l)) kept.push_back(l);
    if (!kept.empty()) candidates.swap(kept);
}

std::vector<const Line*> by_name(const Overview& ov, const std::vector<std::string>& names) {
    std::vector<const Line*> out;
    for (const std::string& name : names) {
        if (name.empty()) continue;
        for (const Line& l : ov.lines)
            if (iequal(l.name, name)) out.push_back(&l);
        // The names are in preference order, so a hit on the first settles it: a "Foulborn
        // Headhunter" is priced separately from a Headhunter and is not one.
        if (!out.empty()) return out;
    }
    return out;
}

/// The influence set a base-type line is priced for, out of poe.ninja's `/`-joined variant
/// ("Shaper/Elder"). Compared as a set rather than as a string: the order is the game's own and
/// agrees with ours today, but an item is not priced differently for being read backwards.
std::vector<std::string> variant_influences(std::string_view variant) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= variant.size()) {
        const size_t end = std::min(variant.find('/', pos), variant.size());
        if (end > pos) out.emplace_back(variant.substr(pos, end - pos));
        pos = end + 1;
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// Replace poe.ninja's roll ranges with their low end: `+(15-25)% to Cold Resistance` becomes
/// `+15% to Cold Resistance`, which `placeholder_form` then reduces to the same `#% to Cold
/// Resistance` the game's own printed roll does. Only a parenthesis holding nothing but two
/// numbers and a dash is one — a modifier's reminder text is in parentheses too.
std::string collapse_ranges(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '(') {
            out += text[i];
            continue;
        }
        const size_t close = text.find(')', i);
        if (close == std::string_view::npos) {
            out += text[i];
            continue;
        }
        const std::string_view inner = text.substr(i + 1, close - i - 1);
        const size_t dash = inner.find('-', 1);
        const auto numeric = [](std::string_view s) {
            return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
                return std::isdigit(static_cast<unsigned char>(c)) || c == '.';
            });
        };
        if (dash == std::string_view::npos || !numeric(inner.substr(0, dash)) ||
            !numeric(inner.substr(dash + 1))) {
            out += text[i];
            continue;
        }
        out += inner.substr(0, dash);
        i = close;
    }
    return out;
}

/// Cut the variants down to the ones whose modifiers the item actually has.
///
/// This is the difference between a right price and a ten-fold wrong one. Ralakesh's
/// Impatience is three lines on poe.ninja — Power, Endurance, Frenzy — 805, 133 and 75 chaos,
/// and the copy in hand *says* which it is: "Count as having maximum number of Power Charges".
/// The variant labels are poe.ninja's own shorthand and are never guessed at, but the
/// modifiers behind them are the game's wordings and match outright.
///
/// Two kinds of comparison, because poe.ninja states a unique's rolled modifiers as ranges and
/// its fixed ones as plain numbers. A range means the number is whatever this copy rolled, so
/// only the wording is compared. **A number with no range around it is fixed on that variant**
/// — Mageblood's "Leftmost 4 Magic Utility Flasks" — so the item has to print that same
/// number, and the line is compared verbatim.
void narrow_by_mods(std::vector<const Line*>& candidates, const std::vector<std::string>& mods) {
    if (candidates.size() < 2 || mods.empty()) return;
    // A line the source published with no modifiers at all cannot be scored, and would win on
    // a mismatch count of zero — which is the opposite of what its silence means.
    for (const Line* l : candidates)
        if (l->mods.empty()) return;

    std::vector<std::string> generic, literal;
    for (const std::string& m : mods) {
        generic.push_back(data::placeholder_form(m));
        literal.push_back(m);
    }
    const auto has = [](const std::vector<std::string>& hay, const std::string& needle) {
        return std::find(hay.begin(), hay.end(), needle) != hay.end();
    };

    size_t best = SIZE_MAX;
    std::vector<size_t> misses(candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i) {
        for (const std::string& m : candidates[i]->mods) {
            const std::string rolled = collapse_ranges(m);
            const bool ok = rolled != m ? has(generic, data::placeholder_form(rolled))
                                        : has(literal, m);
            if (!ok) ++misses[i];
        }
        best = std::min(best, misses[i]);
    }
    // The count is relative, never absolute: poe.ninja's wording and the game's drift apart
    // now and then, and a mismatch every candidate shares says nothing about which this is.
    std::vector<const Line*> kept;
    for (size_t i = 0; i < candidates.size(); ++i)
        if (misses[i] == best) kept.push_back(candidates[i]);
    candidates.swap(kept);
}

const Overview* overview_of(const std::vector<const Overview*>& have, std::string_view type) {
    for (const Overview* ov : have)
        if (ov && ov->key.type == type) return ov;
    return nullptr;
}

/// Whichever of the fetched overviews is not the currency market — the one that was fetched
/// because of this particular item.
const Overview* item_overview(const std::vector<const Overview*>& have) {
    for (const Overview* ov : have)
        if (ov && ov->key.type != "Currency") return ov;
    return nullptr;
}

/// What a stack of `n` is worth, on a `Reference` already priced per item. A no-op at one, so
/// every path can call it without asking whether the item stacks.
void fill_stack(Reference& r, const Line& l, double per_divine, int n) {
    if (n <= 1) return;
    r.stack = n;
    r.stack_price = quote(l.chaos * n, per_divine);
}

void fill_price(Reference& r, const Line& l, const Key& key, double per_divine, int stack = 1) {
    r.state = Reference::State::Priced;
    r.price = quote(l.chaos, per_divine);
    r.label = l.variant;
    r.spark = l.spark;
    r.listings = l.listings;
    r.url = page_url(key, l.details_id);
    fill_stack(r, l, per_divine, stack);
}

/// The two orbs the economy is denominated in, whose own price says nothing: poe.ninja quotes
/// everything in chaos, so a Chaos Orb is worth one chaos, and `quote` converts anything past
/// a divine into divine, so a Divine Orb is worth one divine. Both are tautologies. What a
/// player checks either of them for is the **rate between them**, which is one number — the
/// Divine Orb's own chaos price — and it is the answer for both.
bool is_denominating_currency(std::string_view id) { return id == "chaos" || id == "divine"; }

/// The rate, as a price of `per_divine` chaos *per* Divine Orb. Everything about it comes from
/// the divine line, which is where the rate is measured; only the page linked is the item the
/// user is actually holding.
Reference rate_reference(const Overview& market, const Line& held, double per_divine, int stack) {
    Reference r;
    r.state = Reference::State::Priced;
    r.price = quote_in(per_divine, per_divine, "chaos");
    r.per = "divine";
    r.label = "the chaos/divine rate";
    r.url = page_url(market.key, held.details_id);
    // The stack is still worth what it is worth, and that is the useful half for these two: a
    // stack of chaos is how many divine, a stack of divine is how many divine.
    fill_stack(r, held, per_divine, stack);
    if (const Line* divine = market.find_id("divine")) {
        r.spark = divine->spark;
        r.listings = divine->listings;
    }
    return r;
}

/// The gem tier poe.ninja actually prices, which is rarely the one in hand: it publishes a
/// handful of standard combinations (1, 20, 20/20, 21/20 corrupted) and nothing between them.
/// The best of those the gem has already reached is a floor on what it is worth, so that is
/// what is shown — labelled with poe.ninja's own name for it, since it is not this gem.
const Line* nearest_gem(const std::vector<const Line*>& candidates, int level, int quality) {
    const Line* best = nullptr;
    for (const Line* l : candidates) {
        if (l->gem_level > level || l->gem_quality > quality) continue;
        if (!best || l->gem_level > best->gem_level ||
            (l->gem_level == best->gem_level && l->gem_quality > best->gem_quality))
            best = l;
    }
    if (best) return best;
    // Nothing at or below it: a gem under level 1 does not exist, so this is a quality the
    // cheapest listed tier already exceeds. Show that tier and say it is above this gem.
    for (const Line* l : candidates)
        if (!best || l->gem_level < best->gem_level ||
            (l->gem_level == best->gem_level && l->gem_quality < best->gem_quality))
            best = l;
    return best;
}

/// Price a normal, magic or rare item as its **base type**, which is the only thing poe.ninja
/// can say about one. A rare is bought for its modifiers and the trade search below is what
/// prices those; this is the floor under it — what the item is worth stripped back to the base
/// somebody would craft on, and for a white or magic item it is the whole answer.
///
/// Three things decide a base's price and poe.ninja splits on all three: the base itself, the
/// **item level** and the **influences**. Quality is not one of them — it carries no quality
/// field for bases, so a 20% quality base is priced as any other.
Reference base_type_reference(const Query& q, const Overview& ov, double per_divine) {
    Reference r;
    r.fetched_at = ov.fetched_at;

    std::vector<const Line*> candidates = by_name(ov, {q.base_type});
    if (candidates.empty()) {
        r.note = "\"" + q.base_type + "\" is not priced as a base";
        return r;
    }

    // Exact, never a preference: an uninfluenced Twilight Regalia is 5 chaos and a Warlord one
    // is 1370. Falling back to the wrong influence would be wrong by two orders of magnitude.
    std::vector<std::string> want = q.influences;
    std::sort(want.begin(), want.end());
    std::vector<const Line*> matched;
    for (const Line* l : candidates)
        if (variant_influences(l->variant) == want) matched.push_back(l);
    if (matched.empty()) {
        std::string influences;
        for (const std::string& i : want) influences += (influences.empty() ? "" : "/") + i;
        r.note = influences.empty() ? "no price for this base uninfluenced"
                                    : "no price for a " + influences + " one";
        return r;
    }

    if (q.item_level <= 0) {
        r.note = "no item level in the item text";
        return r;
    }
    // poe.ninja brackets bases by item level and publishes only the top few — 82 up, today.
    // The best bracket the item has reached is what it is worth, and its highest is an open
    // top end: an item level 92 base is sold as, and priced as, the last bracket.
    const Line* best = nullptr;
    int lowest = 0;
    for (const Line* l : matched) {
        if (lowest == 0 || l->level < lowest) lowest = l->level;
        if (l->level > q.item_level) continue;
        if (!best || l->level > best->level) best = l;
    }
    if (!best) {
        r.note = "ilvl too low (<" + std::to_string(lowest) + ")";
        return r;
    }

    fill_price(r, *best, ov.key, per_divine);
    r.label = "item level " + std::to_string(best->level);
    if (!best->variant.empty()) r.label += ", " + best->variant;
    if (q.strategy == item::Strategy::Modifiers)
        r.note = "the bare base, not this item: what its modifiers are worth is the search below";
    return r;
}

} // namespace

const Category* category(std::string_view type) {
    for (const Category& c : kCategories)
        if (c.type == type) return &c;
    return nullptr;
}

Key currency_key(std::string_view league) { return Key{"Currency", std::string(league)}; }

std::string league_slug(std::string_view league) {
    std::string name(league);
    std::string suffix;
    // "Hardcore Allflame" is `allflamehc` on the site, not `hardcore-allflame`. Plain
    // "Hardcore" is the permanent league and keeps its own name.
    constexpr std::string_view kHc = "Hardcore ";
    if (starts_with(name, kHc)) {
        name = name.substr(kHc.size());
        suffix = "hc";
    }
    std::string out;
    for (const unsigned char c : name)
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    return out + suffix;
}

std::string url(const Key& k) {
    const Category* c = category(k.type);
    if (!c) return {};
    const std::string path = c->feed == Feed::Exchange ? "/exchange/current/overview"
                                                       : "/stash/current/item/overview";
    return std::string(kApiBase) + path + "?league=" + percent_encode(k.league) +
           "&type=" + percent_encode(k.type);
}

std::string page_url(const Key& k, std::string_view details_id) {
    const Category* c = category(k.type);
    if (!c || details_id.empty()) return {};
    return std::string(kWebBase) + "/" + league_slug(k.league) + "/" + std::string(c->slug) +
           "/" + std::string(details_id);
}

std::string cache_name(const Key& k) {
    // The league is user-supplied text and goes in the file name, so it is hashed rather than
    // sanitised: a league called "../config" must not be able to name a path. The type is
    // ours, out of the table above, and is safe as it stands.
    return k.type + "-" + sha256_hex(k.league).substr(0, 16) + ".json";
}

const Line* Overview::find_id(std::string_view id) const {
    for (const Line& l : lines)
        if (l.id == id) return &l;
    return nullptr;
}

bool parse_overview(std::string_view body, Feed feed, Overview& out) {
    const json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return false;
    out.feed = feed;
    out.lines.clear();
    return feed == Feed::Exchange ? parse_exchange(j, out) : parse_stash_items(j, out);
}

int max_links(std::string_view sockets) {
    int best = 1, run = 1;
    for (size_t i = 0; i < sockets.size(); ++i) {
        if (sockets[i] == '-') {
            ++run;
            best = std::max(best, run);
        } else if (sockets[i] == ' ') {
            run = 1;
        }
    }
    return best >= 5 ? best : 0;
}

Query query_for(const item::Item& it, const item::SearchPlan& plan, std::string_view league) {
    Query q;
    q.strategy = plan.strategy;
    q.league = league;
    q.category = plan.category;
    q.item_class = it.item_class;
    q.corrupted = it.corrupted;
    q.links = max_links(it.sockets);
    // poe.ninja publishes English names whatever language the client is, so a resolved
    // record's reference name is what is matched against; the printed one is the fallback for
    // an item nothing resolved, where English is the only language it can have been.
    q.base_type = it.base && !it.base->ref_name.empty()
                      ? it.base->ref_name
                      : (it.base_name.empty() ? it.base_type : it.base_name);

    // A gem is looked up under one name and no other. `gem_name()` is what poe.ninja lists it
    // as, which for a Vaal gem is not what the clipboard printed — a Vaal Blight prints
    // "Blight" — and poe.ninja prices both, so falling back to the printed name would not miss
    // the price, it would find a real line for a different gem at a tenth of the value.
    if (const std::string gem = it.gem_name(); !gem.empty()) {
        q.names.push_back(gem);
    } else {
        // The printed name first: poe.ninja prices "Foulborn Headhunter" as its own line, and
        // it is not a Headhunter. Currency prints its name on the base line instead.
        const std::string unique_ref =
            it.unique_entry && !it.unique_entry->ref_name.empty() ? it.unique_entry->ref_name
                                                                  : std::string();
        for (const std::string* n : std::initializer_list<const std::string*>{
                 &unique_ref, &it.name, &plan.name, &q.base_type, &it.base_type})
            if (!n->empty() && std::find(q.names.begin(), q.names.end(), *n) == q.names.end())
                q.names.push_back(*n);
    }

    for (const item::Modifier& m : it.mods)
        for (const std::string& line : m.lines) q.mods.push_back(line);

    q.item_level = it.item_level.value_or(0);
    for (const item::Influence i : it.influences)
        // The two eldritch influences come from an implicit rather than from the base, so
        // poe.ninja does not split base types by them — and an item carrying one would match
        // nothing if they were asked for.
        if (i != item::Influence::SearingExarch && i != item::Influence::EaterOfWorlds)
            q.influences.emplace_back(item::to_string(i));

    // "Stack Size: 6000/20" — the count, not the maximum, which is what fits in one inventory
    // slot and says nothing about a currency stash tab's five or ten thousand.
    for (const item::Property& p : it.properties)
        if (p.key == data::PropertyKey::StackSize && p.num && *p.num >= 1)
            q.stack = static_cast<int>(*p.num);

    if (plan.strategy == item::Strategy::Gem) {
        q.gem_level = it.gem_level.value_or(0);
        q.gem_quality = it.quality.value_or(0);
    }
    return q;
}

std::vector<Key> keys_for(const Query& q) {
    std::vector<Key> out;
    std::string type = map_item_type(q);
    if (!type.empty()) {
        out.push_back(currency_key(q.league));
        if (type != "Currency") out.push_back(Key{type, q.league});
        return out;
    }
    switch (q.strategy) {
    case item::Strategy::Unique:
        if (starts_with(q.category, "weapon.")) type = "UniqueWeapon";
        else if (starts_with(q.category, "armour.")) type = "UniqueArmour";
        else if (starts_with(q.category, "accessory.")) type = "UniqueAccessory";
        else if (q.category == "flask") type = "UniqueFlask";
        else if (starts_with(q.category, "jewel")) type = "UniqueJewel";
        else if (q.category == "tincture") type = "UniqueTincture";
        else if (starts_with(q.category, "sanctum.relic")) type = "UniqueRelic";
        break;
    case item::Strategy::Gem:
        type = "SkillGem";
        break;
    // A white, magic or rare item is priced as its **base**, which is all poe.ninja knows how
    // to say about one. For a rare that is the floor under what the trade search finds; for the
    // other two it is the whole answer.
    case item::Strategy::BaseItem:
    case item::Strategy::Modifiers:
        type = "BaseType";
        break;
    case item::Strategy::Currency:
        if (q.category == "card") {
            type = "DivinationCard";
        } else if (q.category == "currency.fossil") {
            type = "Fossil";
        } else if (q.category == "currency.resonator") {
            type = "Resonator";
        } else {
            type = keyword_type(q);
        }
        break;
    default:
        return out; // a map, or a class nothing prices: there is no overview to ask
    }
    if (type.empty() && q.strategy != item::Strategy::Currency) return out;

    // The currency market leads whatever the item is: it carries the rate every price is
    // converted with, and for a currency item it is usually also the answer.
    out.push_back(currency_key(q.league));
    if (!type.empty() && type != "Currency") out.push_back(Key{type, q.league});
    return out;
}

Quote quote(double chaos, double chaos_per_divine) {
    const bool divine = chaos_per_divine > 0 && chaos >= chaos_per_divine;
    return quote_in(chaos, chaos_per_divine, divine ? "divine" : "chaos");
}

Quote quote_in(double chaos, double chaos_per_divine, std::string_view currency) {
    Quote out;
    out.currency = currency;
    double amount = chaos;
    if (currency == "divine" && chaos_per_divine > 0) amount = chaos / chaos_per_divine;
    // Nobody quotes 5.34285 divine. Two decimals below ten, one below a hundred, none above —
    // which is how the site prints it and how a trade whisper reads.
    const double step = amount < 10 ? 100.0 : (amount < 100 ? 10.0 : 1.0);
    out.amount = std::round(amount * step) / step;
    return out;
}

Reference reference_for(const Query& q, const std::vector<const Overview*>& have) {
    Reference r;
    const Overview* currency = overview_of(have, "Currency");
    const double per_divine = currency ? currency->chaos_per_divine : 0;
    if (currency) r.fetched_at = currency->fetched_at;
    if (currency && currency->lines.empty()) {
        r.note = "no economy tracked for " + q.league;
        return r;
    }

    // A currency item is looked for in the market itself first — it is already in hand, and it
    // holds the orbs and catalysts most currency checks are about.
    const Overview* ov = nullptr;
    std::vector<const Line*> candidates;
    if (q.strategy == item::Strategy::Currency && currency) {
        candidates = by_name(*currency, q.names);
        if (!candidates.empty()) ov = currency;
        // Neither orb can be priced in itself, and both are checked for the same thing.
        // Without a rate there is nothing to state, so those fall through and read one chaos
        // and one divine — which is at least true.
        if (!candidates.empty() && per_divine > 0 && is_denominating_currency(candidates.front()->id)) {
            Reference rate = rate_reference(*currency, *candidates.front(), per_divine, q.stack);
            rate.fetched_at = currency->fetched_at;
            return rate;
        }
    }
    if (candidates.empty()) {
        ov = item_overview(have);
        if (!ov) {
            r.note = "nothing of this kind is priced here";
            return r;
        }
        r.fetched_at = ov->fetched_at;
        // A base is looked up by its base name alone. A rare's own name is randomly generated
        // and matching on it could only ever be a coincidence, and a wrong price.
        if (ov->key.type == "BaseType") {
            Reference base = base_type_reference(q, *ov, per_divine);
            base.fetched_at = ov->fetched_at;
            return base;
        }
        candidates = by_name(*ov, q.names);
    }
    if (candidates.empty()) {
        r.note = "no price for this item";
        return r;
    }

    if (q.strategy == item::Strategy::Gem) {
        // Corruption is a hard split on a gem — a corrupted 21/20 and an uncorrupted 20/20 are
        // different markets — so it filters before the tier is chosen rather than after.
        narrow(candidates, [&](const Line& l) { return l.corrupted == q.corrupted; });
        const Line* best = nearest_gem(candidates, q.gem_level, q.gem_quality);
        if (!best) {
            r.note = "no price for this gem";
            return r;
        }
        fill_price(r, *best, ov->key, per_divine);
        if (best->gem_level != q.gem_level || best->gem_quality != q.gem_quality)
            r.note = "the nearest tier poe.ninja prices, not this gem";
        return r;
    }

    // A base type of its own is a hard filter rather than a preference: two uniques sharing a
    // name across bases are two items, not two variants of one.
    if (!q.base_type.empty())
        narrow(candidates, [&](const Line& l) {
            return l.base_type.empty() || iequal(l.base_type, q.base_type);
        });
    narrow(candidates, [&](const Line& l) { return l.corrupted == q.corrupted; });
    narrow(candidates, [&](const Line& l) { return l.links == q.links; });
    narrow_by_mods(candidates, q.mods);

    std::sort(candidates.begin(), candidates.end(),
              [](const Line* a, const Line* b) { return a->chaos < b->chaos; });

    if (candidates.size() > 1) {
        // Variants the item's own modifiers could not tell apart: poe.ninja's labels are its
        // shorthand for what a copy rolled, and where the modifiers behind them are identical
        // (Mageblood's flask count, Voices' passive count) picking one would be a confident
        // wrong price. State the span instead and let the click-through settle it.
        const bool same = candidates.front()->chaos == candidates.back()->chaos;
        if (!same) {
            r.state = Reference::State::Ambiguous;
            // Both ends in one currency, decided by the dearest: a span quoted half in chaos
            // and half in divine is two numbers that cannot be compared.
            r.high = quote(candidates.back()->chaos, per_divine);
            r.price = quote_in(candidates.front()->chaos, per_divine, r.high.currency);
            r.spark = candidates.front()->spark;
            r.url = page_url(ov->key, candidates.front()->details_id);
            for (const Line* l : candidates)
                r.variants.push_back(Variant{l->variant, quote(l->chaos, per_divine)});
            r.note = std::to_string(candidates.size()) + " variants, and the item text does not"
                                                         " say which this is";
            return r;
        }
    }
    fill_price(r, *candidates.front(), ov->key, per_divine, q.stack);
    return r;
}

} // namespace ppc::ninja
