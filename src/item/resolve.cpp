#include "item/resolve.hpp"

#include <array>
#include <utility>
#include <vector>

#include "data/stat_normalize.hpp"

namespace ppc::item {
namespace {

/// What an item must show for a local wording to be the local one.
enum : uint8_t { kWeapon = 1, kArmour = 2, kEvasion = 4, kEnergyShield = 8 };

/// Wordings the bundle keeps a second, *local* record of, marked by a " (Local)" suffix on
/// the matcher — "20% increased Attack Speed" is the weapon's own speed on a weapon and the
/// character's everywhere else, and the two have different trade ids. The clipboard never
/// says which, so the item's properties decide: a local mod is one that changed a property
/// the item displays.
///
/// This list mirrors the bundle's `(Local)` matchers. A data release that adds one simply
/// keeps resolving to the global stat until this learns about it, which is a wrong trade id
/// on one mod rather than a failure to price the item.
struct LocalWording {
    std::string_view form;
    uint8_t needs;
};
constexpr std::array<LocalWording, 20> kLocalWordings{{
    {"Adds # to # Physical Damage", kWeapon},
    {"Adds # to # Fire Damage", kWeapon},
    {"Adds # to # Cold Damage", kWeapon},
    {"Adds # to # Lightning Damage", kWeapon},
    {"Adds # to # Chaos Damage", kWeapon},
    {"#% increased Attack Speed", kWeapon},
    {"#% chance to Poison on Hit", kWeapon},
    {"#% of Physical Attack Damage Leeched as Life", kWeapon},
    {"#% of Physical Attack Damage Leeched as Mana", kWeapon},
    {"# to Accuracy Rating", kWeapon},
    {"#% increased Armour", kArmour},
    {"# to Armour", kArmour},
    {"#% increased Evasion Rating", kEvasion},
    {"# to Evasion Rating", kEvasion},
    {"#% increased Energy Shield", kEnergyShield},
    {"# to maximum Energy Shield", kEnergyShield},
    {"#% increased Armour and Energy Shield", kArmour | kEnergyShield},
    {"#% increased Armour and Evasion", kArmour | kEvasion},
    {"#% increased Evasion and Energy Shield", kEvasion | kEnergyShield},
    {"#% increased Armour, Evasion and Energy Shield", kArmour | kEvasion | kEnergyShield},
}};

/// True when `line` should be looked up as the local variant on this item.
bool wants_local(const Item& it, std::string_view line) {
    uint8_t has = 0;
    if (it.is_weapon()) has |= kWeapon;
    if (it.armour) has |= kArmour;
    if (it.evasion) has |= kEvasion;
    if (it.energy_shield) has |= kEnergyShield;
    if (!has) return false;
    const std::string form = data::placeholder_form(line);
    for (const LocalWording& w : kLocalWordings)
        if (w.form == form) return (w.needs & has) != 0;
    return false;
}

/// Match one modifier, preferring the local stat where the item says it is local. The local
/// records are single-line wordings, so only the one line is offered for them.
std::optional<data::StatMatch> match_mod(const data::GameData& gd, const Item& it,
                                         std::span<const std::string> lines, size_t start,
                                         const data::MatchContext& ctx) {
    if (start < lines.size() && wants_local(it, lines[start])) {
        const std::array<std::string, 1> local{lines[start] + " (Local)"};
        if (std::optional<data::StatMatch> m = data::match_stat(gd, local, 0, ctx)) return m;
    }
    return data::match_stat(gd, lines, start, ctx);
}

std::vector<std::string_view> split_words(std::string_view s) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        const size_t start = i;
        while (i < s.size() && s[i] != ' ') ++i;
        if (i > start) out.push_back(s.substr(start, i - start));
    }
    return out;
}

std::string find_known_name(const data::GameData& gd, std::string_view printed,
                            data::Namespace ns, std::string_view item_class);

/// The bundle's record for this gem, or null.
///
/// Keyed on `Item::gem_name()`, which is the name both markets state a gem by rather than the
/// one the clipboard prints. Two records can answer to it — a transfigured gem is stored under
/// the skill it alters on bundles published before the display name was emitted, and three
/// "Vaal Blight" records then sit under that one key — so the discriminator decides: a
/// transfigured gem is exactly the one that has one. Getting it wrong searches somebody else's
/// gem, which is why nothing here falls back to "whichever came first".
const data::BaseType* resolve_gem(const data::GameData& gd, const Item& it) {
    const std::string name = it.gem_name();
    if (name.empty()) return nullptr;
    for (const data::BaseType* g : gd.find_bases(data::Namespace::Gem, name))
        if (it.transfigured == !g->trade_disc.empty()) return g;
    return nullptr;
}

/// Resolve `it.base` / `it.base_name`, and for a unique the record naming it.
void resolve_base(const data::GameData& gd, Item& it) {
    if (it.rarity == Rarity::Gem) {
        it.base = resolve_gem(gd, it);
        return;
    }

    // A card is a base type in a namespace of its own, and resolving it is what gives the
    // check its `metadata_id` — which is the only key the in-game currency exchange states an
    // item by, and cards are traded there in bulk exactly as currency is.
    if (it.rarity == Rarity::DivinationCard) {
        for (const data::BaseType* c : gd.find_bases(data::Namespace::DivinationCard, it.base_name))
            it.base = c;
        return;
    }

    // An unidentified unique prints one name line and it is the base's, so the base is all it
    // says about itself. The bundle knows which uniques drop on it, and one candidate is not a
    // guess — that base rolls into exactly this item. Several is a question the user answers
    // (`choose_unique`); until then the item has no name and the search has none to ask for.
    if (it.rarity == Rarity::Unique && it.name.empty() && !it.identified) {
        it.unique_candidates = gd.find_uniques_on_base(it.base_type);
        if (it.unique_candidates.size() == 1) it.unique_entry = it.unique_candidates.front();
    }

    if (it.rarity == Rarity::Unique && !it.name.empty()) {
        for (const data::BaseType* u : gd.find_bases(data::Namespace::Unique, it.name)) {
            it.unique_entry = u;
            if (u->unique_base == it.base_type) break; // the same name can name two uniques
        }
        // A unique can carry a name it was not born with — "Foulborn Romira's Banquet" — so
        // when the whole line is unknown, look for the unique inside it.
        if (!it.unique_entry) {
            const std::string inner = find_known_name(gd, it.name, data::Namespace::Unique, {});
            for (const data::BaseType* u : gd.find_bases(data::Namespace::Unique, inner)) {
                it.unique_entry = u;
                if (u->unique_base == it.base_type) break;
            }
        }
    }

    if (it.rarity == Rarity::Magic) {
        const std::string stripped = strip_magic_affixes(gd, it.base_type, it.item_class);
        if (!stripped.empty()) it.base_name = stripped;
    }

    // Trade files a blighted map under the *ordinary* map base and says which it is with a
    // filter (`map_blighted` / `map_uberblighted`), not with a type: "Blighted Map" is a term
    // the site accepts and matches nothing at all, and no bundle carries a base under that
    // name either. Measured — a tier-12 search sent as "Blighted Map" returned 0 listings
    // against 1398 for the Map base plus the filter.
    if (it.blighted || it.blight_ravaged) it.base_name = "Map";

    for (const data::BaseType* b : gd.find_bases(data::Namespace::Item, it.base_name)) {
        // Same-named bases (the three Two-Stone Rings) are told apart by their defences,
        // which a normal item's own properties can decide but a modded one cannot.
        it.base = b;
        if (b->category == it.item_class) break;
    }
}

/// Split one Advanced-Mod-Descriptions affix into the stats it is searched as.
///
/// The info line groups every line of one affix, but an affix is not one stat: "+34 to Armour"
/// and "+28 to maximum Life" come from a single prefix and are two trade filters. Genuine
/// multi-line stats do exist — a cluster jewel's enchantment — so the matcher decides how many
/// lines each stat takes, exactly as it does for an item without the info lines.
void split_affix(const data::GameData& gd, const Item& it, std::vector<Modifier>& out,
                 Modifier m) {
    const std::vector<std::string> lines = std::move(m.lines);
    const std::vector<std::string> reminder = std::move(m.reminder);
    m.lines.clear();
    m.reminder.clear();

    size_t pos = 0;
    while (pos < lines.size()) {
        const data::MatchContext ctx{m.type, m.roll_incr};
        std::optional<data::StatMatch> match = match_mod(gd, it, lines, pos, ctx);
        const size_t consumed = std::min(match ? match->lines_consumed : 1, lines.size() - pos);

        Modifier part = m; // every part carries the same affix name, tier and tags
        part.continuation = pos != 0;
        part.lines.assign(lines.begin() + static_cast<ptrdiff_t>(pos),
                          lines.begin() + static_cast<ptrdiff_t>(pos + consumed));
        part.match = std::move(match);
        pos += consumed;
        // Reminder text follows the affix's last line, so it belongs to the last part.
        if (pos >= lines.size()) part.reminder = reminder;
        out.push_back(std::move(part));
    }
}

/// Match one run of mods that share a type and came without an info line.
///
/// The parser emits one Modifier per printed line because only Advanced Mod Descriptions say
/// where an affix ends. The matcher does know — `lines_consumed` — so the merge happens here.
void resolve_run(const data::GameData& gd, const Item& it, std::vector<Modifier>& out,
                 std::vector<Modifier>& in, size_t begin, size_t end) {
    std::vector<std::string> lines;
    lines.reserve(end - begin);
    for (size_t i = begin; i < end; ++i) lines.push_back(in[i].lines.front());

    size_t pos = 0;
    while (pos < lines.size()) {
        Modifier m = std::move(in[begin + pos]);
        const data::MatchContext ctx{m.type, m.roll_incr};
        std::optional<data::StatMatch> match = match_mod(gd, it, lines, pos, ctx);
        const size_t consumed = match ? match->lines_consumed : 1;
        for (size_t k = 1; k < consumed && pos + k < lines.size(); ++k) {
            Modifier& tail = in[begin + pos + k];
            m.lines.push_back(std::move(tail.lines.front()));
            for (std::string& r : tail.reminder) m.reminder.push_back(std::move(r));
        }
        m.match = std::move(match);
        out.push_back(std::move(m));
        pos += consumed;
    }
}

/// The longest run of words in `printed` that names something in `ns`.
///
/// This is how a name with things added to it is read: a magic item's base under its two
/// affixes, or the unique inside "Foulborn Romira's Banquet". Longest span first, so
/// "Two-Stone Ring" wins over "Ring"; `item_class` gets a first pass of its own so a base in
/// the right class beats a same-named one elsewhere, without missing an unmapped class.
std::string find_known_name(const data::GameData& gd, std::string_view printed,
                            data::Namespace ns, std::string_view item_class) {
    const std::vector<std::string_view> words = split_words(printed);
    for (int require_class = item_class.empty() ? 0 : 1; require_class >= 0; --require_class) {
        for (size_t len = words.size(); len > 0; --len) {
            for (size_t start = 0; start + len <= words.size(); ++start) {
                std::string candidate(words[start]);
                for (size_t k = 1; k < len; ++k) {
                    candidate.push_back(' ');
                    candidate.append(words[start + k]);
                }
                for (const data::BaseType* b : gd.find_bases(ns, candidate)) {
                    if (require_class && b->category != item_class) continue;
                    return candidate;
                }
            }
        }
    }
    return {};
}

} // namespace

std::string strip_magic_affixes(const data::GameData& gd, std::string_view printed,
                                std::string_view item_class) {
    return find_known_name(gd, printed, data::Namespace::Item, item_class);
}

void choose_unique(Item& it, const data::BaseType* chosen) {
    if (!chosen) {
        it.unique_entry = nullptr;
        return;
    }
    for (const data::BaseType* u : it.unique_candidates)
        if (u == chosen) it.unique_entry = u;
}

std::string find_unique_in(const data::GameData& gd, std::string_view printed) {
    return find_known_name(gd, printed, data::Namespace::Unique, {});
}

void resolve_item(const data::GameData& gd, Item& it) {
    resolve_base(gd, it);

    std::vector<Modifier> out;
    out.reserve(it.mods.size());
    size_t i = 0;
    while (i < it.mods.size()) {
        if (it.mods[i].advanced) {
            split_affix(gd, it, out, std::move(it.mods[i]));
            ++i;
            continue;
        }
        size_t end = i;
        while (end < it.mods.size() && !it.mods[end].advanced &&
               it.mods[end].type == it.mods[i].type && it.mods[end].lines.size() == 1)
            ++end;
        resolve_run(gd, it, out, it.mods, i, end);
        i = end;
    }
    it.mods = std::move(out);
}

} // namespace ppc::item
