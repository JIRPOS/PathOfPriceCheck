#include "data/lexicon.hpp"

#include <algorithm>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ppc::data {
namespace {

/// The JSON key each `Term` is named by, in enum order. A lexicon names the entries it
/// translates and nothing else, so these are the whole of its vocabulary.
constexpr std::array<std::string_view, static_cast<size_t>(Term::Count)> kTermKeys{
    "item_class_label", "rarity_label",  "requirements_label", "sockets_label",
    "note_label",       "superior_prefix", "map_tier_prefix",  "blighted_map",
    "blight_ravaged_map", "foulborn_prefix", "foulborn_word",  "vaal_prefix",
    "influence_suffix", "tier_prefix",   "rank_prefix",        "modifier_word",
    "prefix_word",      "suffix_word",   "unique_word",        "increased_word",
    "reduced_word",     "req_level",     "req_str",            "req_dex",
    "req_int",          "cosmetic_prefix", "cosmetic_suffix",  "augmented",
    "adds_prefix",      "fire_damage",   "cold_damage",        "lightning_damage"};

constexpr std::array<std::string_view, static_cast<size_t>(TermList::Count)> kListKeys{
    "rarities", "influences", "flags", "mod_suffixes", "generations", "value_annotations",
    "usage_needles", "quest_rarity", "chart_shapes", "ultimatum_challenges",
    "ultimatum_rewards"};

struct PropertyName {
    std::string_view key;
    PropertyKey value;
};
constexpr PropertyName kPropertyNames[]{
    {"quality", PropertyKey::Quality},
    {"quality_catalyst", PropertyKey::QualityCatalyst},
    {"physical_damage", PropertyKey::PhysicalDamage},
    {"chaos_damage", PropertyKey::ChaosDamage},
    {"elemental_damage", PropertyKey::ElementalDamage},
    {"critical_strike_chance", PropertyKey::CriticalStrikeChance},
    {"attacks_per_second", PropertyKey::AttacksPerSecond},
    {"armour", PropertyKey::Armour},
    {"evasion_rating", PropertyKey::EvasionRating},
    {"energy_shield", PropertyKey::EnergyShield},
    {"ward", PropertyKey::Ward},
    {"chance_to_block", PropertyKey::ChanceToBlock},
    {"item_level", PropertyKey::ItemLevel},
    {"level", PropertyKey::Level},
    {"stack_size", PropertyKey::StackSize},
    {"item_quantity", PropertyKey::ItemQuantity},
    {"item_rarity", PropertyKey::ItemRarity},
    {"monster_pack_size", PropertyKey::MonsterPackSize},
    {"more_maps", PropertyKey::MoreMaps},
    {"more_scarabs", PropertyKey::MoreScarabs},
    {"more_currency", PropertyKey::MoreCurrency},
    {"more_divination_cards", PropertyKey::MoreDivinationCards},
    {"memory_strands", PropertyKey::MemoryStrands},
    {"intangibility", PropertyKey::Intangibility},
    {"stored_experience", PropertyKey::StoredExperience},
    {"reward", PropertyKey::Reward},
    {"area_level", PropertyKey::AreaLevel},
    {"chart_shape", PropertyKey::ChartShape},
    {"sulphur", PropertyKey::Sulphur},
    {"genus", PropertyKey::Genus},
    {"group", PropertyKey::Group},
    {"family", PropertyKey::Family},
    {"challenge", PropertyKey::Challenge},
    {"requires_sacrifice", PropertyKey::RequiresSacrifice},
};

struct ClassKindName {
    std::string_view key;
    ClassKind value;
};
constexpr ClassKindName kClassKindNames[]{
    {"flask", ClassKind::Flask},
    {"map", ClassKind::Map},
    {"map_fragment", ClassKind::MapFragment},
    {"chart", ClassKind::Chart},
};

/// The mod-type suffix the game prints in a parenthetical, indexed by `ModType`. In English
/// these are letter for letter the trade site's own namespace prefixes, which is why the
/// parser used to reach for `mod_type_from_prefix` and get the right answer by coincidence.
constexpr std::string_view kEnglishModSuffixes[]{
    "explicit", "implicit", "fractured", "enchant", "crafted", "veiled", "pseudo",
    "scourge", "crucible", "sanctum", "delve", "ultimatum", "imbued", "mercenary"};

/// The word an Advanced Mod Descriptions info line opens with, indexed by `ModType`. Empty
/// for the types the game never names there — an explicit roll's info line says only which
/// side of the pool it came from, which is what makes Explicit the fallback rather than a word.
constexpr std::string_view kEnglishGenerations[]{
    "",        "Implicit", "Fractured", "Enchant", "Crafted",   "Veiled", "",
    "Scourge", "Crucible", "Sanctum",   "Delve",   "Ultimatum", "",       ""};

void set(std::array<std::string, static_cast<size_t>(Term::Count)>& t, Term k,
         std::string_view v) {
    t[static_cast<size_t>(k)] = v;
}

} // namespace

void Lexicon::assign_english() {
    language_ = "en";

    set(terms_, Term::ItemClassLabel, "Item Class");
    set(terms_, Term::RarityLabel, "Rarity");
    set(terms_, Term::RequirementsLabel, "Requirements");
    set(terms_, Term::SocketsLabel, "Sockets");
    set(terms_, Term::NoteLabel, "Note");
    set(terms_, Term::SuperiorPrefix, "Superior ");
    set(terms_, Term::MapTierPrefix, " (Tier ");
    set(terms_, Term::BlightedMap, "Blighted Map");
    set(terms_, Term::BlightRavagedMap, "Blight-ravaged Map");
    set(terms_, Term::FoulbornPrefix, "Foulborn ");
    set(terms_, Term::FoulbornWord, "Foulborn");
    set(terms_, Term::VaalPrefix, "Vaal ");
    set(terms_, Term::InfluenceSuffix, " Item");
    set(terms_, Term::TierPrefix, "Tier: ");
    set(terms_, Term::RankPrefix, "Rank: ");
    set(terms_, Term::ModifierWord, "Modifier");
    set(terms_, Term::PrefixWord, "Prefix");
    set(terms_, Term::SuffixWord, "Suffix");
    set(terms_, Term::UniqueWord, "Unique");
    set(terms_, Term::IncreasedWord, "Increased");
    set(terms_, Term::ReducedWord, "Reduced");
    set(terms_, Term::ReqLevel, "Level");
    set(terms_, Term::ReqStr, "Str");
    set(terms_, Term::ReqDex, "Dex");
    set(terms_, Term::ReqInt, "Int");
    set(terms_, Term::CosmeticPrefix, "Has ");
    set(terms_, Term::CosmeticSuffix, " Effect");
    set(terms_, Term::Augmented, "augmented");
    set(terms_, Term::AddsPrefix, "Adds ");
    set(terms_, Term::FireDamage, "Fire Damage");
    set(terms_, Term::ColdDamage, "Cold Damage");
    set(terms_, Term::LightningDamage, "Lightning Damage");

    lists_[static_cast<size_t>(TermList::Rarities)] = {
        "Unknown", "Normal", "Magic", "Rare", "Unique", "Gem", "Currency", "Divination Card",
        "Quest"};
    lists_[static_cast<size_t>(TermList::Influences)] = {
        "Shaper", "Elder", "Crusader", "Redeemer", "Hunter", "Warlord", "Searing Exarch",
        "Eater of Worlds"};
    lists_[static_cast<size_t>(TermList::Flags)] = {
        "Corrupted", "Unidentified", "Mirrored", "Split", "Synthesised Item", "Fractured Item",
        "Veiled", "Unmodifiable", "Transfigured"};
    lists_[static_cast<size_t>(TermList::ModSuffixes)].assign(std::begin(kEnglishModSuffixes),
                                                             std::end(kEnglishModSuffixes));
    lists_[static_cast<size_t>(TermList::Generations)].assign(std::begin(kEnglishGenerations),
                                                             std::end(kEnglishGenerations));
    lists_[static_cast<size_t>(TermList::ValueAnnotations)] = {"augmented", "gem", "unmet",
                                                              "Max", "fractured"};
    lists_[static_cast<size_t>(TermList::UsageNeedles)] = {
        "Right click", "Shift click", "Place into an item socket", "Map Device",
        "Can be used in a personal Map Device", "Modifiable only with",
        // A chart's, which is where a map prints its Map Device line.
        "Take this item to Valerie",
        // The game writes the same instruction both ways and the hyphen is not a variant of
        // the phrase above — a needle is a substring, so "Right click" never matches it. An
        // itemised beast prints only this one, and without it "Right-click to add this to
        // your bestiary." came back as an unrecognised modifier.
        "Right-click"};
    // "Quest Item" and "Divination Card" are printed with a trailing noun on some items, so
    // the rarity line is matched on a prefix as well as whole.
    lists_[static_cast<size_t>(TermList::QuestRarity)] = {"Quest"};
    lists_[static_cast<size_t>(TermList::ChartShapes)] = {"End", "Corner", "Straight",
                                                         "Junction", "Crossing"};
    lists_[static_cast<size_t>(TermList::UltimatumChallenges)] = {
        "Defeat Waves of Enemies", "Survive", "Protect the Altar",
        "Stand in the Stone Circles"};
    lists_[static_cast<size_t>(TermList::UltimatumRewards)] = {
        "Doubles sacrificed Currency", "Doubles sacrificed Divination Cards",
        "Item and Mirrored Copy"};

    quality_prefix_ = "Quality (";
    properties_ = {
        {"Quality", PropertyKey::Quality},
        {"Physical Damage", PropertyKey::PhysicalDamage},
        {"Chaos Damage", PropertyKey::ChaosDamage},
        {"Elemental Damage", PropertyKey::ElementalDamage},
        {"Critical Strike Chance", PropertyKey::CriticalStrikeChance},
        {"Attacks per Second", PropertyKey::AttacksPerSecond},
        {"Armour", PropertyKey::Armour},
        {"Evasion Rating", PropertyKey::EvasionRating},
        {"Energy Shield", PropertyKey::EnergyShield},
        {"Ward", PropertyKey::Ward},
        {"Chance to Block", PropertyKey::ChanceToBlock},
        // The game has printed both wordings; neither has been retired, so both are kept.
        {"Block chance", PropertyKey::ChanceToBlock},
        {"Item Level", PropertyKey::ItemLevel},
        {"Level", PropertyKey::Level},
        {"Stack Size", PropertyKey::StackSize},
        {"Item Quantity", PropertyKey::ItemQuantity},
        {"Item Rarity", PropertyKey::ItemRarity},
        {"Monster Pack Size", PropertyKey::MonsterPackSize},
        {"More Maps", PropertyKey::MoreMaps},
        {"More Scarabs", PropertyKey::MoreScarabs},
        {"More Currency", PropertyKey::MoreCurrency},
        {"More Divination Cards", PropertyKey::MoreDivinationCards},
        {"Memory Strands", PropertyKey::MemoryStrands},
        {"Intangibility", PropertyKey::Intangibility},
        {"Stored Experience", PropertyKey::StoredExperience},
        {"Reward", PropertyKey::Reward},
        {"Area Level", PropertyKey::AreaLevel},
        {"Chart Shape", PropertyKey::ChartShape},
        {"Dead Man's Sulphur", PropertyKey::Sulphur},
        {"Genus", PropertyKey::Genus},
        {"Group", PropertyKey::Group},
        {"Family", PropertyKey::Family},
        {"Challenge", PropertyKey::Challenge},
        {"Requires Sacrifice", PropertyKey::RequiresSacrifice},
    };

    class_kinds_ = {
        {"Life Flasks", ClassKind::Flask},   {"Mana Flasks", ClassKind::Flask},
        {"Hybrid Flasks", ClassKind::Flask}, {"Utility Flasks", ClassKind::Flask},
        {"Critical Utility Flasks", ClassKind::Flask},
        {"Maps", ClassKind::Map},
        {"Map Fragments", ClassKind::MapFragment},
        {"Misc Map Items", ClassKind::MapFragment},
        {"Chart", ClassKind::Chart},
    };
}

const Lexicon& Lexicon::english() {
    static const Lexicon lex = [] {
        Lexicon l;
        l.assign_english();
        return l;
    }();
    return lex;
}

std::string_view Lexicon::at(TermList l, size_t i) const {
    const std::vector<std::string>& v = list(l);
    return i < v.size() ? std::string_view(v[i]) : std::string_view();
}

int Lexicon::index_of(TermList l, std::string_view s) const {
    const std::vector<std::string>& v = list(l);
    for (size_t i = 0; i < v.size(); ++i)
        if (!v[i].empty() && v[i] == s) return static_cast<int>(i);
    return -1;
}

bool Lexicon::any_in(TermList l, std::string_view line) const {
    const std::vector<std::string>& v = list(l);
    return std::any_of(v.begin(), v.end(), [line](const std::string& n) {
        return !n.empty() && line.find(n) != std::string_view::npos;
    });
}

PropertyKey Lexicon::property_key(std::string_view label) const {
    if (const auto it = properties_.find(std::string(label)); it != properties_.end())
        return it->second;
    // Catalyst quality on jewellery: "Quality (Critical Modifiers): +20%". The parenthetical
    // is the catalyst's name and varies, so this one is matched on the label's opening.
    if (!quality_prefix_.empty() && label.starts_with(quality_prefix_) && label.ends_with(")"))
        return PropertyKey::QualityCatalyst;
    return PropertyKey::None;
}

ClassKind Lexicon::class_kind(std::string_view item_class) const {
    const auto it = class_kinds_.find(std::string(item_class));
    return it == class_kinds_.end() ? ClassKind::Other : it->second;
}

std::string_view Lexicon::class_id(std::string_view item_class) const {
    const auto it = class_ids_.find(std::string(item_class));
    return it == class_ids_.end() ? std::string_view() : std::string_view(it->second);
}

Lexicon Lexicon::parse(std::string_view json_text, std::string* err) {
    Lexicon lex = english();
    const json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        if (err) *err = "lexicon is not a JSON object";
        return lex;
    }
    lex.language_ = j.value("language", lex.language_);

    if (const auto t = j.find("terms"); t != j.end() && t->is_object()) {
        for (size_t i = 0; i < kTermKeys.size(); ++i) {
            const auto e = t->find(std::string(kTermKeys[i]));
            if (e != t->end() && e->is_string()) lex.terms_[i] = e->get<std::string>();
        }
    }

    // A list is replaced whole or not at all. Merging entry by entry would leave a
    // fixed-order list half in one language and half in another, and every one of them is
    // indexed by position — an influence at the wrong index is a wrong trade filter.
    if (const auto l = j.find("lists"); l != j.end() && l->is_object()) {
        for (size_t i = 0; i < kListKeys.size(); ++i) {
            const auto e = l->find(std::string(kListKeys[i]));
            if (e == l->end() || !e->is_array()) continue;
            std::vector<std::string> out;
            for (const json& s : *e) out.push_back(s.is_string() ? s.get<std::string>() : "");
            lex.lists_[i] = std::move(out);
        }
    }

    // The property and item-class tables are keyed the other way round — printed label to
    // key — so a translated one replaces the English table outright rather than adding to it.
    if (const auto p = j.find("properties"); p != j.end() && p->is_object()) {
        lex.properties_.clear();
        for (const auto& [label, name] : p->items()) {
            if (!name.is_string()) continue;
            const std::string v = name.get<std::string>();
            for (const PropertyName& pn : kPropertyNames)
                if (pn.key == v) lex.properties_[label] = pn.value;
        }
    }
    lex.quality_prefix_ = j.value("quality_prefix", lex.quality_prefix_);

    if (const auto c = j.find("item_classes"); c != j.end() && c->is_object()) {
        lex.class_kinds_.clear();
        lex.class_ids_.clear();
        for (const auto& [printed, spec] : c->items()) {
            if (!spec.is_object()) continue;
            if (const std::string id = spec.value("id", std::string()); !id.empty())
                lex.class_ids_[printed] = id;
            const std::string kind = spec.value("kind", std::string());
            for (const ClassKindName& ck : kClassKindNames)
                if (ck.key == kind) lex.class_kinds_[printed] = ck.value;
        }
    }
    return lex;
}

} // namespace ppc::data
