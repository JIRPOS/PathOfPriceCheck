#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "data/stat_matcher.hpp"
#include "data/types.hpp"

/// Structured Path of Exile items, parsed from the clipboard text the game writes on Ctrl+C.
///
/// Everything here is pure: `parse_item` needs no bundle, so it is testable against captured
/// clipboard text alone. Resolving mods to stats and bases (`resolve.hpp`) is a second pass
/// that does need `data::GameData`.
namespace ppc::item {

enum class Rarity : uint8_t {
    Unknown, Normal, Magic, Rare, Unique, Gem, Currency, DivinationCard, Quest
};

std::string_view to_string(Rarity r);
/// `Rarity::Unknown` for anything unrecognised — never an error, the item still renders.
Rarity rarity_from_string(std::string_view s);

enum class Influence : uint8_t {
    Shaper, Elder, Crusader, Redeemer, Hunter, Warlord, SearingExarch, EaterOfWorlds, Count
};

std::string_view to_string(Influence i);
/// "Elder Item" -> Influence::Elder. The clipboard prints one such line per influence.
std::optional<Influence> influence_from_line(std::string_view line);

/// Which side of the mod pool a roll came from. Only Advanced Mod Descriptions say.
enum class Affix : uint8_t { Unknown, Prefix, Suffix };

enum class Element : uint8_t { Physical, Fire, Cold, Lightning, Chaos };

struct DamageRange {
    double min = 0, max = 0;
    Element element = Element::Physical;
    bool augmented = false;

    double avg() const { return (min + max) / 2.0; }
};

/// One "Label: value" line from a property block, kept verbatim so the tooltip can render
/// whatever the game prints — map tier, memory strands, gem level — without a field per
/// item type. A label-less entry is a property the game prints as prose ("Lasts 7.20 Seconds").
struct Property {
    std::string label, value;
    bool augmented = false;
    std::optional<double> num; ///< the first number in the value, when there is one
    /// Reminder text the game prints under the value but keeps out of the tooltip — what a
    /// utility flask's buff does. Shown on hover, like a modifier's.
    std::vector<std::string> reminder;
};

/// One modifier: the lines the game printed for it, whatever its Advanced Mod Descriptions
/// info line said, and the stat it resolved to.
///
/// A hybrid mod ("Adds # to # Physical Damage" + "#% increased Attack Speed" from one affix)
/// holds more than one line. Without Advanced Mod Descriptions the parser cannot know where
/// the boundaries are, so it emits one `Modifier` per line and `resolve_mods()` merges them
/// once the matcher says how many lines a stat consumed.
struct Modifier {
    data::ModType type = data::ModType::Explicit;
    std::vector<std::string> lines;    ///< as printed, mod-type suffix stripped
    std::vector<std::string> reminder; ///< parenthesised reminder text printed underneath

    // Advanced Mod Descriptions. All absent when the game option is off.
    bool advanced = false; ///< an info line described this mod, so `lines` is already grouped
    /// This is the second or later stat of one affix — "+34 to Armour" and "+28 to maximum
    /// Life" come from a single prefix but are two separate stats to search on. The affix
    /// fields below are the shared ones, and only the first part prints them.
    bool continuation = false;
    Affix affix = Affix::Unknown;
    std::string affix_name;      ///< "Urchin's"
    std::string generation;      ///< the info line's leading words, e.g. "Prefix", "Master Crafted Prefix"
    std::string qualifier;       ///< an eldritch implicit's rank: "Lesser", "Grand", …
    int tier = 0;                ///< 0 when unknown
    int rank = 0;                ///< crucible passive rank; 0 when unknown
    std::vector<std::string> tags;
    double roll_incr = 0;        ///< percentage the info line says the roll is scaled by

    /// Filled by `resolve_mods()`; absent when the wording resolved to no searchable stat.
    std::optional<data::StatMatch> match;

    /// A modifier *added* to a unique rather than one of its own: the info line names what
    /// added it ("Foulborn Unique Modifier") and the game prints it in magenta. Not every copy
    /// of the unique has it, so unlike the unique's fixed mods it is worth searching on.
    bool added_unique() const;

    std::string text() const; ///< `lines` joined with '\n'
    /// The info line as the game prints it, plus the tier's own roll range, or "" when this
    /// mod had no info line. The range is here rather than inline in `lines` because the view
    /// strips "+86(77-90)" down to "+86" and this is where the reader gets it back.
    std::string info_text() const;
    /// The range each of this mod's numbers rolled within, "77-90"; "" when unknown or fixed.
    std::string range_text() const;
};

struct Requirements {
    std::optional<int> level, str, dex, intelligence;
};

struct Item {
    std::string item_class; ///< as printed: "Thrusting One Hand Swords"
    Rarity rarity = Rarity::Unknown;
    std::string name;       ///< rare/unique name; empty for normal and magic items
    std::string base_type;  ///< the base line as printed — a magic item's still has its affixes
    std::string base_name;  ///< base with magic affixes stripped; == base_type until resolved
    std::string type_line;  ///< the property block's leading prose line: "Bow", "Spell, AoE, Fire"

    bool identified = true;
    bool corrupted = false;
    bool mirrored = false;
    bool split = false;
    bool synthesised = false;
    bool fractured_item = false;
    bool veiled = false;
    bool unmodifiable = false;
    std::vector<Influence> influences;

    std::vector<Property> properties; ///< every property line, in printed order
    std::optional<int> item_level, quality;
    /// The tier the base line printed, "Map (Tier 16)". Absent on a map that names its own
    /// area instead ("Shaper Guardian Map"), and on everything that is not a map.
    std::optional<int> map_tier;
    std::string quality_kind; ///< catalyst quality's parenthetical, e.g. "Critical Modifiers"
    Requirements req;
    std::string sockets;      ///< as printed: "R-G-B"
    int socket_count = 0;

    // Property values the pricing layer computes with, pulled out of `properties`.
    std::optional<DamageRange> physical, chaos;
    std::vector<DamageRange> elemental; ///< in the game's fire, cold, lightning order
    std::optional<double> crit_chance, attacks_per_second;
    std::optional<int> armour, evasion, energy_shield, ward, block;

    std::vector<Modifier> mods;
    std::vector<std::string> inherent_lines; ///< a flask's own effect; rendered, never searched
    std::vector<std::string> description;    ///< what a gem or a currency item does
    std::vector<std::string> flavour_text;
    /// The usage note under the flavour text — "Right click to drink…". Neither a modifier nor
    /// flavour, and it sits exactly where flavour text does, so it is told apart by its wording.
    std::vector<std::string> help_text;
    std::vector<std::string> cosmetic_lines; ///< "Has Vampiric Weapon Effect"
    std::vector<std::string> unparsed;       ///< lines we did not understand; shown, never searched
    std::string note;                        ///< the seller's "Note: ~price 1 divine"

    /// What this resolved to in the bundle, or null. Both point into the `GameData` passed to
    /// `resolve_item()` — that snapshot must outlive the item.
    const data::BaseType* base = nullptr;         ///< the base type's record
    const data::BaseType* unique_entry = nullptr; ///< the unique's own record, uniques only

    bool is_weapon() const;   ///< has weapon properties (attacks per second)
    /// Any of the five flask classes ("Life Flasks" … "Critical Utility Flasks").
    bool is_flask() const;
    /// Scarabs, allflame embers, splinters, invitations — everything the game files under
    /// "Map Fragments" or "Misc Map Items". Printed as Normal rarity but currency-like: no
    /// modifiers, no base to compare, bought and sold as a stack.
    bool is_map_fragment() const;
    /// A map proper — the "Maps" item class. Not a fragment, which is a different class and a
    /// different market entirely.
    bool is_map() const;
    bool has_defences() const;
    /// True for the rarities whose mods are rolled from a pool, i.e. tier-matchable. False for
    /// a map fragment, whose Normal rarity says nothing about it.
    bool is_gear() const;
    std::vector<const Modifier*> mods_of(data::ModType t) const;
    /// Sum of the rolls of every mod resolving to `stat_ref`, whatever its mod type.
    double sum_of(std::string_view stat_ref) const;
};

/// Parse clipboard text. Returns nothing when the text is not an item at all.
std::optional<Item> parse_item(std::string_view clipboard);

/// True when `text` has the shape of PoE item text. Cheap; used to accept a clipboard read.
bool looks_like_item(std::string_view text);

} // namespace ppc::item
