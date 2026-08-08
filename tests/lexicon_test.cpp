#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include "data/lexicon.hpp"
#include "item/item.hpp"
#include "parse_en.hpp"
#include "ui/strings.hpp"

using namespace ppc;
using ppc::data::ClassKind;
using ppc::data::Lexicon;
using ppc::data::PropertyKey;
using ppc::data::Term;
using ppc::data::TermList;

TEST_CASE("the English table is complete") {
    const Lexicon& en = Lexicon::english();
    CHECK(en.language() == "en");

    // Every term has a word. An empty one is not a translation gap, it is a parse rule that
    // silently stops firing — `starts_with("")` is true of every line.
    for (size_t i = 0; i < static_cast<size_t>(Term::Count); ++i)
        CHECK_MESSAGE(!en.term(static_cast<Term>(i)).empty(), "empty Term at index ", i);

    // The fixed-order lists are indexed by an enum, so a short one silently answers -1 for
    // everything past its end.
    CHECK(en.list(TermList::Rarities).size() == 9);
    CHECK(en.list(TermList::Influences).size() ==
          static_cast<size_t>(item::Influence::Count));
    CHECK(en.list(TermList::Flags).size() == static_cast<size_t>(data::ItemFlag::Count));
    CHECK(en.list(TermList::ModSuffixes).size() == static_cast<size_t>(data::ModType::Count));
    CHECK(en.list(TermList::Generations).size() == static_cast<size_t>(data::ModType::Count));
    CHECK(en.list(TermList::ChartShapes).size() == 5);

    // An explicit mod has no generation word of its own, and an empty entry must never match
    // — otherwise every line is an explicit info line.
    CHECK(en.at(TermList::Generations, static_cast<size_t>(data::ModType::Explicit)).empty());
    CHECK(en.index_of(TermList::Generations, "") == -1);
    CHECK(en.index_of(TermList::Generations, "Crafted") ==
          static_cast<int>(data::ModType::Crafted));
}

TEST_CASE("English keys the property and class tables") {
    const Lexicon& en = Lexicon::english();
    CHECK(en.property_key("Attacks per Second") == PropertyKey::AttacksPerSecond);
    CHECK(en.property_key("Stack Size") == PropertyKey::StackSize);
    // Two wordings, one key: the game has printed both and neither has been retired.
    CHECK(en.property_key("Chance to Block") == PropertyKey::ChanceToBlock);
    CHECK(en.property_key("Block chance") == PropertyKey::ChanceToBlock);
    // The catalyst's own name varies, so this one is matched on the label's opening.
    CHECK(en.property_key("Quality (Critical Modifiers)") == PropertyKey::QualityCatalyst);
    CHECK(en.property_key("Quality") == PropertyKey::Quality);
    CHECK(en.property_key("Lasts") == PropertyKey::None);

    CHECK(en.class_kind("Critical Utility Flasks") == ClassKind::Flask);
    CHECK(en.class_kind("Maps") == ClassKind::Map);
    CHECK(en.class_kind("Misc Map Items") == ClassKind::MapFragment);
    CHECK(en.class_kind("Chart") == ClassKind::Chart);
    CHECK(en.class_kind("Rings") == ClassKind::Other);
    // Only a translated lexicon fills this in; English finds its row by the printed name.
    CHECK(en.class_id("Rings").empty());
}

TEST_CASE("a lexicon overlays English rather than replacing it") {
    std::string err;
    const Lexicon lex = Lexicon::parse(R"({
      "language": "xx",
      "terms": { "rarity_label": "Rareté" }
    })",
                                       &err);
    CHECK(err.empty());
    CHECK(lex.language() == "xx");
    CHECK(lex.term(Term::RarityLabel) == "Rareté");
    // Everything it did not name is still English, which is what keeps a partial translation
    // usable instead of leaving blank rules that match every line.
    CHECK(lex.term(Term::ItemClassLabel) == "Item Class");
    CHECK(lex.list(TermList::Flags).size() == 9);
    CHECK(lex.property_key("Armour") == PropertyKey::Armour);
}

TEST_CASE("an unreadable lexicon is English and says so") {
    std::string err;
    const Lexicon lex = Lexicon::parse("not json at all", &err);
    CHECK(!err.empty());
    CHECK(lex.language() == "en");
    CHECK(lex.term(Term::RarityLabel) == "Rarity");
}

TEST_CASE("a translated lexicon reads a translated item") {
    // Not real French — the point is that nothing here is English, so anything the parser
    // still gets right it got from the lexicon rather than from a literal.
    std::string err;
    const Lexicon fr = Lexicon::parse(R"({
      "language": "fr",
      "terms": {
        "item_class_label": "Classe d'objet",
        "rarity_label": "Rareté",
        "requirements_label": "Prérequis",
        "req_level": "Niveau",
        "modifier_word": "Modificateur",
        "prefix_word": "Préfixe",
        "tier_prefix": "Rang: "
      },
      "lists": {
        "rarities": ["Inconnu", "Normal", "Magique", "Rare", "Unique", "Gemme", "Monnaie",
                     "Carte de divination", "Quête"],
        "flags": ["Corrompu", "Non identifié", "Reflété", "Divisé", "Objet synthétisé",
                  "Objet fracturé", "Voilé", "Non modifiable", "Transfiguré"],
        "mod_suffixes": ["explicite", "implicite", "fracturé", "enchantement", "artisanal",
                         "voilé", "pseudo", "fléau", "creuset", "sanctum", "delve",
                         "ultimatum", "imprégné", "mercenaire"]
      },
      "properties": {
        "Qualité": "quality",
        "Armure": "armour",
        "Niveau d'objet": "item_level"
      },
      "item_classes": {
        "Armures": { "id": "Body Armour" },
        "Cartes": { "id": "Map", "kind": "map" }
      }
    })",
                                      &err);
    REQUIRE(err.empty());

    const std::string text =
        "Classe d'objet: Armures\n"
        "Rareté: Rare\n"
        "Chagrin de Corbeau\n"
        "Cuirasse Astrale\n"
        "--------\n"
        "Qualité: +20% (augmented)\n"
        "Armure: 512 (augmented)\n"
        "--------\n"
        "Prérequis:\n"
        "Niveau: 62\n"
        "--------\n"
        "+18 to maximum Life (implicite)\n"
        "--------\n"
        "{ Préfixe Modificateur \"Urchin's\" (Rang: 2) }\n"
        "+89 to maximum Life\n"
        "--------\n"
        "Corrompu\n";

    const std::optional<item::Item> it = item::parse_item(text, fr);
    REQUIRE(it.has_value());
    CHECK(it->item_class == "Armures");
    CHECK(it->rarity == item::Rarity::Rare);
    CHECK(it->name == "Chagrin de Corbeau");
    CHECK(it->base_type == "Cuirasse Astrale");
    CHECK(it->quality == 20);
    CHECK(it->armour == 512);
    CHECK(it->req.level == 62);
    CHECK(it->corrupted);
    // The property block resolved to keys, so everything downstream reads it without knowing
    // a word of the language.
    REQUIRE(it->properties.size() == 2);
    CHECK(it->properties[0].key == PropertyKey::Quality);
    CHECK(it->properties[1].key == PropertyKey::Armour);
    CHECK(it->properties[1].augmented);
    // The mod-type suffix and the info line are the two halves of typing a modifier, and both
    // came out of the lexicon.
    REQUIRE(it->mods.size() == 2);
    CHECK(it->mods[0].type == data::ModType::Implicit);
    CHECK(it->mods[1].advanced);
    CHECK(it->mods[1].affix == item::Affix::Prefix);
    CHECK(it->mods[1].tier == 2);
    CHECK(it->mods[1].affix_name == "Urchin's");
    CHECK_FALSE(it->mods[1].added_unique);

    // And the same bytes read as English are not an item at all, which is the failure the
    // whole layer exists to remove.
    CHECK_FALSE(item::parse_item_en(text).has_value());
}

TEST_CASE("the item class kind comes from the lexicon, not from the printed words") {
    std::string err;
    const Lexicon fr = Lexicon::parse(R"({
      "terms": { "item_class_label": "Classe", "rarity_label": "Rareté" },
      "lists": { "rarities": ["Inconnu", "Normal"] },
      "item_classes": { "Fioles de vie": { "id": "LifeFlask", "kind": "flask" } }
    })",
                                      &err);
    REQUIRE(err.empty());
    const std::optional<item::Item> it =
        item::parse_item("Classe: Fioles de vie\nRareté: Normal\nFiole de Granit\n", fr);
    REQUIRE(it.has_value());
    CHECK(it->is_flask());
    CHECK(fr.class_id("Fioles de vie") == "LifeFlask");
}

TEST_CASE("our own text falls back to English and follows the client") {
    ui::set_language("en", "en");
    CHECK(std::string(ui::text(ui::Msg::Save)) == "Save");

    // "auto" follows the client, and a language nothing is compiled in for is English rather
    // than a panel of blank controls.
    ui::set_language("auto", "ru");
    CHECK(ui::language() == "en");
    ui::set_language("zz", "zz");
    CHECK(ui::language() == "en");
    CHECK(std::string(ui::text(ui::Msg::League)) == "League");

    // Every entry is non-empty, or a control silently loses its label.
    for (size_t i = 0; i < static_cast<size_t>(ui::Msg::Count); ++i) {
        const char* s = ui::text(static_cast<ui::Msg>(i));
        REQUIRE(s != nullptr);
        CHECK_MESSAGE(*s != '\0', "empty ui::Msg at index ", i);
    }
    CHECK(ui::languages().size() >= 1);
    CHECK(ui::languages().front() == "en");
}
