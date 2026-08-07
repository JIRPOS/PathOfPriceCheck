#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <clocale>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "item/item.hpp"

namespace fs = std::filesystem;
using namespace ppc::item;
using ppc::data::ModType;

namespace {

/// Clipboard captures live as files so a new one can be dropped in without touching code.
/// `examples/` holds captures taken from the live game alongside a screenshot of the same
/// tooltip; `items/` holds the ones with no screenshot beside them.
std::string capture(const char* dir, const char* name) {
    const fs::path p = fs::path(PPC_TEST_DATA_DIR) / dir / name;
    std::ifstream in(p, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing capture " << p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

Item parse(const char* dir, const char* name) {
    std::optional<Item> it = parse_item(capture(dir, name));
    REQUIRE_MESSAGE(it.has_value(), "failed to parse " << name);
    return *it;
}

Item example(const char* name) { return parse("examples", name); }

const Modifier* find_mod(const Item& it, std::string_view first_line) {
    for (const Modifier& m : it.mods)
        if (!m.lines.empty() && m.lines.front() == first_line) return &m;
    return nullptr;
}

} // namespace

TEST_CASE("text that is not an item is rejected") {
    CHECK_FALSE(parse_item("").has_value());
    CHECK_FALSE(parse_item("git commit -m nope").has_value());
    CHECK(looks_like_item("Item Class: Rings\nRarity: Normal\nIron Ring"));
}

TEST_CASE("decimals survive a locale whose separator is not a dot") {
    // The game always writes '.'. Parsing it with the C locale's strtod under cs_CZ read
    // "Attacks per Second: 1.79" as 1, and every DPS number downstream was wrong.
    const char* had = std::setlocale(LC_NUMERIC, nullptr);
    const std::string saved = had ? had : "C";
    if (!std::setlocale(LC_NUMERIC, "cs_CZ.UTF-8") && !std::setlocale(LC_NUMERIC, "de_DE.UTF-8"))
        return; // no comma locale on this machine; nothing to prove
    const Item it = parse("items", "rare-rapier.txt");
    CHECK(it.attacks_per_second == doctest::Approx(1.79));
    CHECK(it.crit_chance == doctest::Approx(5.0));
    std::setlocale(LC_NUMERIC, saved.c_str());
}

TEST_CASE("a rare weapon's header, properties and mods") {
    const Item it = parse("items", "rare-rapier.txt");

    CHECK(it.item_class == "Thrusting One Hand Swords");
    CHECK(it.rarity == Rarity::Rare);
    CHECK(it.name == "Sorrow Saw");
    CHECK(it.base_type == "Wyrmbone Rapier");
    CHECK(it.type_line == "One Handed Sword");
    CHECK(it.identified);
    CHECK_FALSE(it.corrupted);

    REQUIRE(it.physical.has_value());
    CHECK(it.physical->min == 25);
    CHECK(it.physical->max == 98);
    CHECK(it.physical->augmented);
    CHECK(it.crit_chance == doctest::Approx(5.0));
    CHECK(it.attacks_per_second == doctest::Approx(1.79));
    CHECK(it.item_level == 67);
    CHECK(it.sockets == "R-G-B");
    CHECK(it.socket_count == 3);

    // "(gem)" marks a requirement a socketed gem raised; the number is still the requirement.
    CHECK(it.req.level == 43);
    CHECK(it.req.str == 98);
    CHECK(it.req.dex == 122);
    CHECK_FALSE(it.req.intelligence.has_value());

    // Two elemental entries, named by the mods that added them and in the game's order.
    REQUIRE(it.elemental.size() == 2);
    CHECK(it.elemental[0].element == Element::Fire);
    CHECK(it.elemental[0].min == 23);
    CHECK(it.elemental[1].element == Element::Lightning);
    CHECK(it.elemental[1].max == 50);

    REQUIRE(it.mods_of(ModType::Implicit).size() == 1);
    CHECK(it.mods_of(ModType::Implicit).front()->lines.front() ==
          "+20% to Global Critical Strike Multiplier");
    CHECK(it.mods_of(ModType::Explicit).size() == 6);

    // A cosmetic effect is not a modifier and must not become a search filter.
    CHECK(it.cosmetic_lines == std::vector<std::string>{"Has Vampiric Weapon Effect"});
    CHECK(it.unparsed.empty());
}

TEST_CASE("influence, quality and crafted mods") {
    const Item it = parse("items", "rare-bow-elder.txt");

    CHECK(it.quality == 41);
    CHECK(it.influences == std::vector<Influence>{Influence::Elder});
    CHECK(it.mods_of(ModType::Crafted).size() == 1);
    CHECK(it.mods_of(ModType::Crafted).front()->lines.front() == "+11% to Quality");
    CHECK(it.mods_of(ModType::Explicit).size() == 6);
    CHECK(it.elemental.empty());
}

TEST_CASE("Advanced Mod Descriptions group hybrid mods and name their affix") {
    const Item it = example("item_6.txt");

    CHECK(it.energy_shield == 1003);
    CHECK(it.quality == 20);
    // Eight affixes, not the nine lines they printed.
    REQUIRE(it.mods.size() == 8);

    const Modifier& hybrid = it.mods[4];
    CHECK(hybrid.advanced);
    CHECK(hybrid.affix == Affix::Prefix);
    CHECK(hybrid.affix_name == "Seraphim's");
    CHECK(hybrid.tier == 1);
    CHECK(hybrid.tags == std::vector<std::string>{"Defences", "Energy Shield"});
    REQUIRE(hybrid.lines.size() == 2);
    CHECK(hybrid.lines[0] == "42(39-42)% increased Energy Shield");
    CHECK(hybrid.lines[1] == "16(16-17)% increased Stun and Block Recovery");
    CHECK(hybrid.type == ModType::Explicit);

    // Two eldritch implicits, each naming the influence that put it there and its rank.
    REQUIRE(it.mods_of(ModType::Implicit).size() == 2);
    const Modifier& eldritch = *it.mods_of(ModType::Implicit).front();
    CHECK(eldritch.generation == "Searing Exarch Implicit");
    CHECK(eldritch.qualifier == "Lesser");
    // The influence lines came glued to the end of the last mod block, not in one of their own.
    CHECK(it.influences ==
          std::vector<Influence>{Influence::SearingExarch, Influence::EaterOfWorlds});
    CHECK(it.mods.back().lines.size() == 1);
    CHECK(it.unparsed.empty());
}

TEST_CASE("an info line separated by a plain hyphen parses the same") {
    // What a Latin-1 clipboard read hands us: Wine serves CF_TEXT as the X11 STRING target and
    // the em dash comes through as '-'. The same copy alternates between the two forms, so an
    // item must not price differently depending on which one the poll caught.
    const std::optional<Item> parsed = parse_item(R"(Item Class: Body Armours
Rarity: Rare
Rift Carapace
Twilight Regalia
--------
Energy Shield: 1003
--------
{ Prefix Modifier "Incandescent" (Tier: 2) - Defences, Energy Shield }
+86(77-90) to maximum Energy Shield
{ Suffix Modifier "of Ephij" (Tier: 1) - Elemental, Lightning, Resistance }
+46(46-48)% to Lightning Resistance
)");
    REQUIRE(parsed.has_value());
    const Item& it = *parsed;

    REQUIRE(it.mods.size() == 2);
    CHECK(it.mods[0].advanced);
    CHECK(it.mods[0].affix == Affix::Prefix);
    CHECK(it.mods[0].affix_name == "Incandescent");
    CHECK(it.mods[0].tier == 2);
    CHECK(it.mods[0].tags == std::vector<std::string>{"Defences", "Energy Shield"});
    CHECK(it.mods[1].affix == Affix::Suffix);
    CHECK(it.mods[1].tier == 1);
    CHECK(it.mods[1].tags ==
          std::vector<std::string>{"Elemental", "Lightning", "Resistance"});
}

TEST_CASE("reminder text belongs to the mod above it") {
    const Item it = example("item_11.txt");

    CHECK(it.rarity == Rarity::Magic);
    CHECK(it.base_type == "Piledriver of the Brute");
    REQUIRE(it.mods_of(ModType::Implicit).size() == 1);
    const Modifier& implicit = *it.mods_of(ModType::Implicit).front();
    CHECK(implicit.lines == std::vector<std::string>{"20% reduced Enemy Stun Threshold"});
    REQUIRE(implicit.reminder.size() == 1);
    CHECK(implicit.reminder.front().starts_with("(The Stun Threshold"));

    REQUIRE(it.mods_of(ModType::Explicit).size() == 1);
    CHECK(it.mods_of(ModType::Explicit).front()->tier == 9);
    CHECK(it.mods_of(ModType::Explicit).front()->affix_name == "of the Brute");
}

TEST_CASE("a unique keeps its flavour text out of its mods") {
    const Item it = example("item_4.txt");

    CHECK(it.rarity == Rarity::Unique);
    CHECK(it.name == "Tulfall");
    CHECK(it.base_type == "Opal Wand");
    CHECK(it.mods_of(ModType::Implicit).size() == 1);
    CHECK(it.mods_of(ModType::Explicit).size() == 5);
    REQUIRE(it.flavour_text.size() == 3);
    CHECK(it.flavour_text.front() == "We fracture and splinter.");
    CHECK(it.help_text.empty());
}

TEST_CASE("a flask's flavour text and usage note are neither mods nor each other") {
    const Item it = example("item_1.txt");

    CHECK(it.rarity == Rarity::Unique);
    CHECK(it.name == "Rumi's Concoction");
    CHECK(it.base_type == "Granite Flask");
    CHECK(it.quality == 20);
    // The base's own effect sits in the property block; it is displayed, never searched.
    CHECK(it.inherent_lines == std::vector<std::string>{"+1500 to Armour"});
    CHECK(it.mods_of(ModType::Enchant).size() == 1);
    CHECK(it.mods_of(ModType::Explicit).size() == 2);
    // Quoted flavour, then the usage note under it — the game prints them in that order and in
    // different colours, and the attribution line starting with '-' is not a negative roll.
    REQUIRE(it.flavour_text.size() == 3);
    CHECK(it.flavour_text.back() == "-Rumi of the Vaal");
    REQUIRE(it.help_text.size() == 1);
    CHECK(it.help_text.front().starts_with("Right click to drink"));
    CHECK(find_mod(it, "-Rumi of the Vaal") == nullptr);
    CHECK(it.unparsed.empty());
}

TEST_CASE("a magic flask's enchant, own effect and affixes") {
    const Item it = example("item_2.txt");

    CHECK(it.rarity == Rarity::Magic);
    CHECK(it.base_type == "Surgeon's Quicksilver Flask of the Cheetah");
    CHECK(it.quality == 20);
    CHECK(it.inherent_lines == std::vector<std::string>{"40% increased Movement Speed"});
    REQUIRE(it.mods_of(ModType::Enchant).size() == 1);
    CHECK(it.mods_of(ModType::Enchant).front()->lines.front() == "Used when Charges reach full");
    CHECK(it.mods_of(ModType::Explicit).size() == 2);
    CHECK(it.flavour_text.empty());
    CHECK(it.help_text.size() == 1);
    // "Lasts 7.20 Seconds" is prose, but it is still a property and not a mod.
    CHECK(find_mod(it, "Lasts 7.20 Seconds") == nullptr);
}

TEST_CASE("an unquality flask's own effect is properties, not modifiers") {
    // No quality means the block after the header has no `Label: value` line at all, which is
    // the only thing that used to mark it as the property block; every line of it became a mod.
    const Item it = example("item_12.txt");

    CHECK(it.rarity == Rarity::Magic);
    CHECK(it.base_type == "Surgeon's Silver Flask of the Owl");
    CHECK(!it.quality);
    CHECK(it.type_line.empty());
    CHECK(find_mod(it, "Lasts 6 Seconds") == nullptr);
    CHECK(find_mod(it, "Onslaught") == nullptr);
    REQUIRE(it.properties.size() >= 4);
    CHECK(it.properties[0].value == "Lasts 6 Seconds");
    CHECK(it.properties[3].value == "Onslaught");
    // The buff's reminder text belongs to it, the way a modifier's does — never a mod of its own.
    REQUIRE(it.properties[3].reminder.size() == 1);
    CHECK(it.properties[3].reminder.front().starts_with("(Onslaught grants"));

    REQUIRE(it.mods_of(ModType::Enchant).size() == 1);
    CHECK(it.mods_of(ModType::Enchant).front()->lines.front() == "Used when Charges reach full");
    REQUIRE(it.mods_of(ModType::Explicit).size() == 2);
    CHECK(it.mods_of(ModType::Explicit).front()->affix_name == "Surgeon's");
    CHECK(it.mods_of(ModType::Explicit).back()->affix_name == "of the Owl");
}

TEST_CASE("corruption and fracturing are flags as well as mods") {
    const Item it = example("item_7.txt");

    CHECK(it.corrupted);
    CHECK(it.fractured_item);
    CHECK(it.evasion == 219);
    CHECK(it.energy_shield == 44);

    REQUIRE(it.mods_of(ModType::Implicit).size() == 1);
    const Modifier& corrupted = *it.mods_of(ModType::Implicit).front();
    CHECK(corrupted.generation == "Corruption Implicit");
    CHECK(corrupted.lines.front() == "+2 to Level of Socketed AoE Gems");

    // A fractured mod is printed among the explicits and only its info line says so.
    REQUIRE(it.mods_of(ModType::Fractured).size() == 1);
    const Modifier& fractured = *it.mods_of(ModType::Fractured).front();
    CHECK(fractured.affix == Affix::Suffix);
    CHECK(fractured.affix_name == "of the Underground");
    CHECK(fractured.tier == 0); // a fractured mod's info line carries no tier
    CHECK(fractured.reminder.size() == 1);
    CHECK(it.mods_of(ModType::Explicit).size() == 3);
}

TEST_CASE("a catalyst names the mods it scaled and the item's own quality kind") {
    const Item it = example("item_3.txt");

    CHECK(it.quality == 20);
    CHECK(it.quality_kind == "Critical Modifiers");
    CHECK(it.name == "Foulborn Romira's Banquet");

    // The catalyst scaled the Critical-tagged mods, and the roll printed for them is the
    // unscaled one — the tooltip shows 36% where the clipboard says 30.
    REQUIRE(it.mods_of(ModType::Implicit).size() == 1);
    const Modifier& implicit = *it.mods_of(ModType::Implicit).front();
    CHECK(implicit.roll_incr == doctest::Approx(20));
    CHECK(implicit.tags == std::vector<std::string>{"Critical"});
    CHECK(find_mod(it, "+54(40-60) to maximum Mana")->roll_incr == 0);

    // A modifier added to the unique, which not every copy of it has.
    const Modifier& added = it.mods.back();
    CHECK(added.generation == "Foulborn Unique");
    CHECK(added.added_unique());
    CHECK(added.lines.front() == "+1 to Maximum Power Charges");
    CHECK_FALSE(find_mod(it, "+54(40-60) to maximum Mana")->added_unique());
    CHECK(it.flavour_text.size() == 4);
}

TEST_CASE("a currency item describes itself and has no mods") {
    const Item it = example("item_8.txt");

    CHECK(it.rarity == Rarity::Currency);
    CHECK(it.base_type == "Divine Orb");
    CHECK(it.mods.empty());
    REQUIRE(it.properties.size() == 1);
    CHECK(it.properties.front().label == "Stack Size");
    CHECK(it.description ==
          std::vector<std::string>{"Randomises the values of the random modifiers on an item"});
    CHECK(it.help_text.size() == 2);
    CHECK(it.flavour_text.empty());
}

TEST_CASE("a gem's properties, description and granted stats") {
    const Item it = example("item_10.txt");

    CHECK(it.rarity == Rarity::Gem);
    CHECK(it.base_type == "Zealotry");
    CHECK(it.type_line == "Aura, Critical, Spell, AoE");
    CHECK(it.quality == 8);
    CHECK(it.req.level == 24);
    CHECK(it.req.intelligence == 58);
    // A gem has no rolled modifiers: what it prints is what the skill does.
    CHECK(it.mods.empty());
    CHECK(it.description.size() == 1);
    CHECK(it.inherent_lines.size() == 6);
    CHECK(it.help_text.size() == 1);
}

TEST_CASE("a gem's level is the property, never the requirement under it") {
    // The clipboard prints "Level:" twice and the two are different numbers on every gem past
    // the first: the gem's own level in the property block, the character level to socket it
    // under `Requirements:`. Pricing on the wrong one prices a different gem.
    const Item it = parse("items", "gem-support-hypothermia.txt");

    CHECK(it.gem_level == 16);
    CHECK(it.req.level == 62);
    CHECK(it.req.dex == 99);
    CHECK(it.gem_name() == "Hypothermia Support");
    CHECK_FALSE(it.transfigured);
    CHECK(it.mods.empty());
}

TEST_CASE("a Vaal gem is named by its Vaal skill, which the name line does not print") {
    // Two skills in one gem: the header says "Blight" and the second half says "Vaal Blight",
    // and it is the second that both markets file it under.
    const Item it = parse("items", "gem-vaal-blight.txt");

    CHECK(it.base_type == "Blight");
    CHECK(it.vaal_name == "Vaal Blight");
    CHECK(it.gem_name() == "Vaal Blight");
    CHECK(it.gem_level == 1);
    CHECK(it.corrupted);
    CHECK(it.mods.empty());
    // Everything the Vaal half prints is the skill describing itself, exactly as the first
    // half's is — never a modifier.
    CHECK(it.description.size() == 1);
    CHECK_FALSE(it.inherent_lines.empty());
}

TEST_CASE("a transfigured gem is flagged, not left as an unrecognised line") {
    const Item it = parse("items", "gem-transfigured-raise-zombie.txt");

    CHECK(it.transfigured);
    CHECK(it.gem_name() == "Raise Zombie of Falling");
    CHECK(it.gem_level == 1);
    CHECK(it.vaal_name.empty());
    CHECK(it.mods.empty());
}

TEST_CASE("a gem's quality is a property like any other") {
    const Item it = parse("items", "gem-tornado-shot.txt");

    CHECK(it.gem_level == 1);
    CHECK(it.quality == 7);
    CHECK(it.gem_name() == "Tornado Shot");
    CHECK(it.type_line == "Attack, Projectile, Bow");
}

TEST_CASE("a map fragment describes itself in prose and has no modifiers at all") {
    // It prints "Rarity: Normal", which is the only rarity the game has for one — and every
    // rule that tells a rare's mods from its prose then fires on lines that are neither. A
    // scarab's effect became a modifier, its verse became two more, and each came back an
    // "unrecognised modifier" note over a price check that had nothing to warn about.
    SUBCASE("its effect, then its flavour") {
        const Item it = parse("items", "fragment-scarab.txt");
        CHECK(it.rarity == Rarity::Normal);
        CHECK(it.base_type == "Cartography Scarab of Corruption");
        CHECK(it.mods.empty());
        CHECK(it.description ==
              std::vector<std::string>{
                  "Non-Unique Maps found in Area are Corrupted with 8 Modifiers"});
        CHECK(it.flavour_text == std::vector<std::string>{"Corruption bleeds between realities."});
        CHECK(it.help_text.size() == 1);
    }
    SUBCASE("a two-line effect and no verse") {
        const Item it = parse("items", "fragment-allflame-ember.txt");
        CHECK(it.mods.empty());
        CHECK(it.description.size() == 2);
        CHECK(it.flavour_text.empty());
    }
    SUBCASE("nothing but a verse, which cannot be told from an effect") {
        // The Maven's Writ prints no effect line, so its verse is the only prose block and is
        // taken for the description. Nothing distinguishes the two, and reading a verse as the
        // item's own text is the harmless way to be wrong about it.
        const Item it = parse("items", "fragment-mavens-writ.txt");
        CHECK(it.mods.empty());
        CHECK(it.description.size() == 2);
        CHECK(it.flavour_text.empty());
    }
    SUBCASE("an invitation, which is Misc Map Items and carries an item level") {
        const Item it = parse("items", "invitation-writhing.txt");
        CHECK(it.item_class == "Misc Map Items");
        CHECK(it.item_level == 83);
        CHECK(it.mods.empty());
        CHECK(it.description.size() == 1);
        CHECK(it.flavour_text.size() == 2);
    }
}

TEST_CASE("a map's tier comes off the base line, which is otherwise the same on every one") {
    // Every ordinary map shares one base type now, so "Map (Tier 16)" is the whole of what
    // tells one from another — and the parenthetical is not part of a name any lookup knows.
    SUBCASE("a rare, whose base line is the bare type") {
        const Item it = parse("items", "map-rare-t16-corrupted.txt");
        CHECK(it.item_class == "Maps");
        CHECK(it.is_map());
        CHECK(it.name == "Graven Secrets");
        CHECK(it.base_type == "Map");
        CHECK(it.map_tier == 16);
        CHECK(it.corrupted);
        CHECK(it.item_level == 83);
        // Four prefixes and four suffixes, which is what corruption buys and what the search
        // asks for as a total.
        CHECK(it.mods.size() == 9);
        CHECK(it.mods_of(ModType::Implicit).size() == 1);
    }
    SUBCASE("a magic, whose affixes sit between the base and the tier") {
        const Item it = parse("items", "map-magic-t16.txt");
        CHECK(it.base_type == "Map of Impedance");
        CHECK(it.map_tier == 16);
    }
    SUBCASE("a unique") {
        const Item it = parse("items", "map-unique-olmecs.txt");
        CHECK(it.name == "Olmec's Sanctum");
        CHECK(it.base_type == "Map");
        CHECK(it.map_tier == 16);
        CHECK(it.flavour_text.size() == 4);
    }
    SUBCASE("a map that names its own area instead has no tier at all") {
        const Item it = parse("items", "map-rare-guardian.txt");
        CHECK(it.base_type == "Shaper Guardian Map");
        CHECK_FALSE(it.map_tier.has_value());
    }
    SUBCASE("one info line over two implicit lines is one modifier here") {
        // The Elder's influence and which of his generals holds the map are printed under a
        // single `{ Implicit Modifier }`. They are two stats to search on, but only the matcher
        // can say so — the parser groups by the info line and `resolve` splits what it grouped.
        const Item it = parse("items", "map-rare-t16-elder.txt");
        REQUIRE(it.mods_of(ModType::Implicit).size() == 1);
        CHECK(it.mods.front().lines ==
              std::vector<std::string>{"Area is influenced by The Elder",
                                       "Map is occupied by The Enslaver"});
    }
    SUBCASE("the drop bonuses a chisel adds are properties, not modifiers") {
        const Item it = parse("items", "map-rare-more-drops.txt");
        const auto value = [&it](std::string_view label) {
            for (const Property& p : it.properties)
                if (p.label == label) return p.num;
            return std::optional<double>();
        };
        CHECK(value("More Maps") == 70);
        CHECK(value("More Scarabs") == 53);
        CHECK(value("Item Quantity") == 110);
        CHECK(value("Monster Pack Size") == 42);
    }
}

TEST_CASE("\"Modifiable only with…\" is a note about the map, not a modifier of it") {
    // It is prose in a section of its own under the usage note, exactly where a mod block can
    // also sit — and read as one it came back as an unmatchable modifier on every Nightmare map.
    const Item it = parse("items", "map-rare-nightmare.txt");
    CHECK(it.help_text.size() == 2);
    for (const Modifier& m : it.mods)
        CHECK(m.lines.front().find("Modifiable only with") == std::string::npos);
}
