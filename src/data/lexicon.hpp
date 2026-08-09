#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// The words the *client* prints, for one language.
///
/// Everything in `item/parse` used to match these as literals, which is why the app reads an
/// English client and nothing else. They are not strings we author: they are game text, so the
/// authority is the bundle (`<lang>-lexicon.json`), the same rule the rest of `data/` follows.
/// The English table is compiled in as the default because every bundle published so far
/// carries no lexicon at all, and a lexicon overrides only the entries it names — so a partial
/// or absent one degrades to English rather than to nothing.
namespace ppc::data {

/// What a property line *is*, independent of the words it was printed with.
///
/// The printed label used to be the key: `parse` dispatched on "Attacks per Second" and then
/// `item/plan` looked the same string up again, two copies of one vocabulary. This is the key
/// instead, and the label stays on the `Property` for the tooltip to draw.
enum class PropertyKey : uint8_t {
    None,
    Quality,
    QualityCatalyst, ///< "Quality (Critical Modifiers)" — jewellery a catalyst was used on
    PhysicalDamage,
    ChaosDamage,
    ElementalDamage,
    CriticalStrikeChance,
    AttacksPerSecond,
    Armour,
    EvasionRating,
    EnergyShield,
    Ward,
    ChanceToBlock,
    ItemLevel,
    Level, ///< a gem's own level; the character level to socket it is under Requirements
    StackSize,
    ItemQuantity,
    ItemRarity,
    MonsterPackSize,
    MoreMaps,
    MoreScarabs,
    MoreCurrency,
    MoreDivinationCards,
    MemoryStrands,
    Intangibility,
    StoredExperience,
    /// What an item pays out: a Valdo map's unique, and an Inscribed Ultimatum's reward. The
    /// only map that prints one, and the two are told apart by the item rather than by the key.
    Reward,
    AreaLevel,
    ChartShape,
    Sulphur,
    /// An itemised beast's taxonomy. Only a beast prints them, which is what makes `Genus` the
    /// marker for one: the item class it shares with every orb in the game says nothing.
    Genus,
    Group,
    Family,
    /// An Inscribed Ultimatum's trial. Only an ultimatum prints one, which is what makes
    /// `Challenge` the marker for one: its item class is the "Misc Map Items" it shares with
    /// every invitation and its rarity line says "Currency" like an orb's.
    Challenge,
    RequiresSacrifice,
    /// What a heist contract sends the crew after. Its parenthetical is the objective's value
    /// ("Ancient Seal (Precious)"), which is the one thing trade indexes about it.
    HeistTarget,
    /// A blueprint's reveal state, printed as "revealed/total" — two numbers in one value, and
    /// trade takes each as a filter of its own.
    WingsRevealed,
    EscapeRoutesRevealed,
    RewardRoomsRevealed,
    /// One "Requires Brute Force (Level 4)" line. Deliberately kept label-less with the whole
    /// line as its value, because that is how the game prints it and how the panel draws it;
    /// which job it names is a lexicon lookup at the point of use.
    HeistJob,
    /// An itemised sanctum's state. `Resolve` is printed as "299/300" — what is left and what
    /// the run started with, two numbers in one value, and trade takes each as a filter of its
    /// own.
    Resolve,
    Inspiration,
    Aureus,
    /// The boons and afflictions the run has picked up, printed as a comma-separated list of
    /// names. **One key each for minor and major**: the label says which, and what a search
    /// needs is the name, because every one of them is its own trade stat.
    Boons,
    Afflictions,
    Count
};

/// The item classes the app branches on. Every other class is `Other` — the trade category
/// comes from the bundle and needs no enum here.
enum class ClassKind : uint8_t {
    Other, Flask, Map, MapFragment, Chart, HeistContract, HeistBlueprint, SanctumResearch
};

/// A flag the game prints on a line of its own. Influence lines are not among them: they are
/// a wording (`" Item"` on the end of an influence's name) rather than a fixed set.
enum class ItemFlag : uint8_t {
    Corrupted,
    Unidentified,
    Mirrored,
    Split,
    Synthesised,
    Fractured,
    Veiled,
    Unmodifiable,
    Transfigured,
    Count
};

/// A single word or label. Anything the parser compares one string against.
enum class Term : uint8_t {
    ItemClassLabel,   ///< "Item Class", without the colon
    RarityLabel,      ///< "Rarity"
    RequirementsLabel,///< "Requirements"
    SocketsLabel,     ///< "Sockets"
    NoteLabel,        ///< "Note"
    SuperiorPrefix,   ///< "Superior ", worn by a quality item nothing else has named — a white
                      ///< one or any unidentified one — and known to no lookup
    MapTierPrefix,    ///< " (Tier ", closed by ')' — "Map (Tier 16)"
    BlightedMap,      ///< the whole base line of a blighted map
    BlightRavagedMap,
    FoulbornPrefix,   ///< "Foulborn ", the mutation stated as part of the name
    FoulbornWord,     ///< how the info line of the modifier it added opens
    VaalPrefix,       ///< "Vaal ", which heads the second half of a Vaal gem's tooltip
    InfluenceSuffix,  ///< " Item", as in "Elder Item"
    TierPrefix,       ///< "Tier: " inside an info line's parenthetical
    RankPrefix,       ///< "Rank: "
    ModifierWord,     ///< "Modifier", the noun an info line's generation words end with
    PrefixWord,
    SuffixWord,
    UniqueWord,       ///< a bare "Unique" generation, which `added_unique` must not count
    IncreasedWord,    ///< a catalyst's "— 20% Increased" segment
    ReducedWord,
    ReqLevel,
    ReqStr,
    ReqDex,
    ReqInt,
    CosmeticPrefix,   ///< "Has " … " Effect"
    CosmeticSuffix,
    HeistJobPrefix,   ///< "Requires ", opening a heist item's job line
    HeistJobLevel,    ///< " (Level ", closed by ')' — "Requires Brute Force (Level 4)"
    /// "Has ", how a **stat** words a sanctum boon or affliction — `Has Rusted Chimes`, where
    /// the item prints the name alone under a `Minor Boons:` label. The one entry here that is
    /// a stat's wording rather than the client's, and it is here because a translated bundle
    /// translates it just the same and the lookup is by exact wording.
    SanctumEffectPrefix,
    Augmented,        ///< the "(augmented)" annotation, the one whose presence is recorded
    AddsPrefix,       ///< "Adds " — which added-damage mod coloured which elemental entry
    FireDamage,
    ColdDamage,
    LightningDamage,
    Count
};

/// A set of strings. The fixed-order ones are indexed by the enum named in the comment, so a
/// language's list has to keep that order; the free ones are searched.
enum class TermList : uint8_t {
    Rarities,        ///< indexed by `item::Rarity`
    Influences,      ///< indexed by `item::Influence`
    Flags,           ///< indexed by `ItemFlag`
    ModSuffixes,     ///< indexed by `ModType` — the " (implicit)" parenthetical, "" for none
    Generations,     ///< indexed by `ModType` — the info line's generation word, "" for none
    ValueAnnotations,///< free — "(augmented)", "(gem)", "(unmet)", "(Max)", "(fractured)"
    UsageNeedles,    ///< free — what tells the usage note apart from flavour text
    QuestRarity,     ///< free — rarity lines printed with a trailing noun ("Quest Item")
    /// A chart's shape, in the order of the `chart_shape` option ids trade publishes — so
    /// entry `i` is option id `i + 1`. The game prints the option's own text, which is what
    /// makes the join possible; sending that text answers "Invalid chart shape" and fails the
    /// whole search, so the id is never guessed from the words.
    ChartShapes,
    /// An ultimatum's trial, in the order of the `ultimatum_challenge` option ids trade
    /// publishes — entry `i` is `kUltimatumChallengeIds[i]`. Same join as the chart shapes: the
    /// game prints the option's own words, so the English entries are the site's own text and
    /// the comparison is case-insensitive, because the client prints "Defeat waves of enemies"
    /// where the site prints "Defeat Waves of Enemies".
    UltimatumChallenges,
    /// An ultimatum's reward, in the order of the first three `ultimatum_reward` option ids.
    /// **Three, not four**: the fourth reward is a unique, and the line the game prints for it
    /// is that unique's name rather than a wording of its own.
    UltimatumRewards,
    /// The nine rogue jobs, in the order of the `heist_*` filter keys they map to. Searched
    /// *inside* a job line rather than compared to it, because the line the game prints wraps
    /// the name ("Requires Counter-Thaumaturgy (Level 4)") and the panel draws that line whole.
    HeistJobs,
    /// A heist objective's value, in the order of the `heist_objective_value` option ids. What
    /// the game prints in the parenthetical after the target's name, so "Precious" and not
    /// "(Precious)".
    HeistObjectiveValues,
    Count
};

class Lexicon {
public:
    /// The compiled-in English table. Never null, and the base every other language is laid
    /// over — so an entry a bundle's lexicon omits keeps working rather than becoming empty.
    static const Lexicon& english();

    /// English overlaid with `json`, a `<lang>-lexicon.json` payload. Returns the English
    /// table unchanged and fills `err` when the payload cannot be read at all; an entry it
    /// simply does not name is not an error.
    static Lexicon parse(std::string_view json, std::string* err);

    std::string_view language() const { return language_; }

    std::string_view term(Term t) const { return terms_[static_cast<size_t>(t)]; }
    const std::vector<std::string>& list(TermList l) const {
        return lists_[static_cast<size_t>(l)];
    }
    /// The `l`th entry of a fixed-order list, or "" when this language stops short of it.
    std::string_view at(TermList l, size_t i) const;
    /// The index of `s` in a fixed-order list, or -1. Empty entries never match, so a type
    /// with no word of its own (an explicit mod) cannot be found by an empty line.
    int index_of(TermList l, std::string_view s) const;
    bool contains(TermList l, std::string_view s) const { return index_of(l, s) >= 0; }
    /// True when any entry of `l` appears anywhere in `line`.
    bool any_in(TermList l, std::string_view line) const;

    /// What `label` (a property line's label, markup already stripped) is asking about.
    PropertyKey property_key(std::string_view label) const;
    /// Which of the classes the app branches on `item_class` is.
    ClassKind class_kind(std::string_view item_class) const;
    /// The game's internal class id for `item_class`, or "" when this language does not say.
    ///
    /// Only a translated lexicon fills this in. English needs none: `item-classes.ndjson` is
    /// keyed on the printed English name already, so `GameData::trade_category_for` finds the
    /// row directly and only falls back to this when it cannot.
    std::string_view class_id(std::string_view item_class) const;

private:
    /// Fill this table with the English vocabulary. Every other language is this, overlaid.
    void assign_english();

    std::string language_ = "en";
    std::array<std::string, static_cast<size_t>(Term::Count)> terms_;
    std::array<std::vector<std::string>, static_cast<size_t>(TermList::Count)> lists_;
    std::unordered_map<std::string, PropertyKey> properties_;
    /// The label prefix a catalyst's "Quality (Critical Modifiers)" opens with, kept apart
    /// because it is the one property matched on a prefix rather than on the whole label.
    std::string quality_prefix_;
    std::unordered_map<std::string, ClassKind> class_kinds_;
    std::unordered_map<std::string, std::string> class_ids_;
};

} // namespace ppc::data
