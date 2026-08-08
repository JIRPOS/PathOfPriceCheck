#include "item/item.hpp"

#include <array>
#include <cmath>
#include <cstdio>

namespace ppc::item {
namespace {

constexpr std::array<std::string_view, 9> kRarities{
    "Unknown", "Normal", "Magic", "Rare", "Unique", "Gem", "Currency", "Divination Card",
    "Quest"};

constexpr std::array<std::string_view, static_cast<size_t>(Influence::Count)> kInfluences{
    "Shaper", "Elder", "Crusader", "Redeemer", "Hunter", "Warlord", "Searing Exarch",
    "Eater of Worlds"};

} // namespace

std::string_view to_string(Rarity r) { return kRarities[static_cast<size_t>(r)]; }

Rarity rarity_from_string(std::string_view s, const data::Lexicon& lex) {
    // Index 0 is Unknown and has no word of its own, so it is never searched for.
    if (const int i = lex.index_of(data::TermList::Rarities, s); i > 0)
        return static_cast<Rarity>(i);
    // "Quest Item" and "Divination Card" are printed with a trailing noun on some items.
    for (const std::string& q : lex.list(data::TermList::QuestRarity))
        if (!q.empty() && s.starts_with(q)) return Rarity::Quest;
    return Rarity::Unknown;
}

std::string_view to_string(Influence i) { return kInfluences[static_cast<size_t>(i)]; }

std::optional<Influence> influence_from_line(std::string_view line, const data::Lexicon& lex) {
    const std::string_view suffix = lex.term(data::Term::InfluenceSuffix);
    if (suffix.empty() || !line.ends_with(suffix)) return std::nullopt;
    line.remove_suffix(suffix.size());
    if (const int i = lex.index_of(data::TermList::Influences, line); i >= 0)
        return static_cast<Influence>(i);
    return std::nullopt;
}

std::string Modifier::text() const {
    std::string out;
    for (const std::string& l : lines) {
        if (!out.empty()) out.push_back('\n');
        out += l;
    }
    return out;
}

/// "77-90", or "4-6 / 10-14" for a mod with a range per number. Empty when nothing is known or
/// every range is a point, which says nothing a reader cannot already see.
std::string Modifier::range_text() const {
    if (!match) return {};
    std::string out;
    bool any = false;
    for (const auto& [lo, hi] : match->roll_bounds) {
        const int dp = lo == std::floor(lo) && hi == std::floor(hi) ? 0 : 2;
        char buf[64];
        if (lo == hi) std::snprintf(buf, sizeof buf, "%.*f", dp, lo);
        else {
            std::snprintf(buf, sizeof buf, "%.*f-%.*f", dp, lo, dp, hi);
            any = true;
        }
        if (!out.empty()) out += " / ";
        out += buf;
    }
    return any ? out : std::string();
}

std::string Modifier::info_text(const data::Lexicon& lex) const {
    if (!advanced) return {};
    std::string out = generation;
    if (!out.empty()) out += " " + std::string(lex.term(data::Term::ModifierWord));
    if (!affix_name.empty()) out += " \"" + affix_name + "\"";
    if (!qualifier.empty()) out += " (" + qualifier + ")";
    // The range rides with the tier: it is the tier's own bounds, and the mod line no longer
    // prints it inline.
    const std::string range = range_text();
    if (tier) {
        out += " (" + std::string(lex.term(data::Term::TierPrefix)) + std::to_string(tier);
        out += range.empty() ? ")" : " [" + range + "])";
    } else if (!range.empty()) {
        out += " [" + range + "]";
    }
    if (rank)
        out += " (" + std::string(lex.term(data::Term::RankPrefix)) + std::to_string(rank) + ")";
    for (size_t i = 0; i < tags.size(); ++i) out += (i ? ", " : " \xe2\x80\x94 ") + tags[i];
    // The roll on the line is the unscaled one; this is what says so.
    if (roll_incr != 0) {
        char buf[32];
        std::snprintf(buf, sizeof buf, " \xe2\x80\x94 %g%% ", std::abs(roll_incr));
        out += buf;
        out += roll_incr > 0 ? lex.term(data::Term::IncreasedWord)
                             : lex.term(data::Term::ReducedWord);
    }
    return out;
}

bool Item::is_weapon() const { return attacks_per_second.has_value(); }

bool Item::is_flask() const { return class_kind == data::ClassKind::Flask; }

bool Item::is_map_fragment() const { return class_kind == data::ClassKind::MapFragment; }

bool Item::is_map() const { return class_kind == data::ClassKind::Map; }

bool Item::is_chart() const { return class_kind == data::ClassKind::Chart; }

bool Item::has_defences() const {
    return armour || evasion || energy_shield || ward;
}

bool Item::needs_unique_choice() const {
    return rarity == Rarity::Unique && !identified && !unique_entry &&
           unique_candidates.size() > 1;
}

bool Item::is_gear() const {
    // A fragment prints "Rarity: Normal" only because the game has nothing else to print on
    // that line. It has no modifiers at all — what looks like one is what the fragment does —
    // so none of the rules that exist to tell a rare's mods from its prose apply to it.
    if (is_map_fragment()) return false;
    return rarity == Rarity::Normal || rarity == Rarity::Magic || rarity == Rarity::Rare ||
           rarity == Rarity::Unique;
}

std::string Item::gem_name() const {
    if (rarity != Rarity::Gem) return {};
    // The printed name, which for a Vaal gem is the base skill and for a transfigured one is
    // the alternate skill's own name. Both markets state a Vaal gem by its Vaal skill, and a
    // transfigured Vaal gem as the pair — "Vaal Blight (Blight of Atrophy)" is verbatim what
    // trade's `data/items` and poe.ninja's gem overview both call it.
    const std::string& printed = name.empty() ? base_type : name;
    if (vaal_name.empty()) return printed;
    return transfigured ? vaal_name + " (" + printed + ")" : vaal_name;
}

std::vector<const Modifier*> Item::mods_of(data::ModType t) const {
    std::vector<const Modifier*> out;
    for (const Modifier& m : mods)
        if (m.type == t) out.push_back(&m);
    return out;
}

double Item::sum_of(std::string_view stat_ref) const {
    double sum = 0;
    for (const Modifier& m : mods)
        if (m.match && m.match->stat && m.match->stat->ref == stat_ref) sum += m.match->value;
    return sum;
}

} // namespace ppc::item
