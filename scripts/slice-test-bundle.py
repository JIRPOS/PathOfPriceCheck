#!/usr/bin/env python3
"""Rebuild tests/data/bundle from a bundle built by PathOfPriceCheck-Data.

The fixture is a slice of a real release, kept small enough to read in a diff. Its
`.index.bin` files address the ndjson by byte offset, so the two must be produced together:
hand-editing one line of the ndjson shifts every record out from under every lookup, and it
fails as null lookups rather than as a diff. That is what this script exists to prevent.

    ./scripts/slice-test-bundle.py ../PathOfPriceCheck-Data/out

Any built bundle works, including the one the app installed (`<cache>/data/<version>/`) — prefer
that when the checkout's `out/` predates a builder fix, or the fixture is sliced from records the
release no longer emits.

Every record is copied verbatim, so the fixture keeps the shapes the builder actually emits.
Adding a case means adding a key below, not writing a record by hand.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

# Chosen so the six-record slice still exercises every code path: a stat with negate and
# fixed-value matchers, a stat indexed in several namespaces, same-named bases told apart by
# their defences, and a unique with both a fixed and a pooled modifier.
STATS = [
    "Adds # to # Physical Damage",
    "# to maximum Life",
    "#% to Fire Resistance",
    "#% increased Physical Damage",
    "#% increased Attack Speed",
    "#% reduced Action Speed",
    # Added elemental damage feeds `edps` and not `pdps`, which is the other half of the rule
    # that a modifier already inside a searched number is not searched again by name. Global
    # critical strike multiplier is the control: it reads like a weapon number and is in none
    # of them, because trade's `crit` is the weapon's own chance.
    "Adds # to # Fire Damage",
    "Adds # to # Lightning Damage",
    "#% to Global Critical Strike Multiplier",
    # Ralakesh's Impatience, whose pool of three charge modifiers is the case per-unique
    # modifier data exists for: each rolls 1..1, so no printed range can reveal it.
    "#% to Chaos Resistance",
    "#% to Cold Resistance",
    # An eldritch implicit: its magnitude comes from the currency tier, not from a range the
    # clipboard prints, and it is negative because more of it is better. The case for which
    # side of an unbounded filter the roll goes on.
    "Inflict Cold Exposure on Hit, applying #% to Cold Resistance",
    "Corrupted Blood cannot be inflicted on you",
    "#% increased Movement Speed",
    "Count as having maximum number of Endurance Charges",
    "Count as having maximum number of Frenzy Charges",
    "Count as having maximum number of Power Charges",
    # The Surgeon's prefix. The game renders this stat two ways and trade hashes each, so it was
    # emitted as two records claiming both wordings and the app could not resolve either.
    "#% chance to gain a Flask Charge when you deal a Critical Strike",
    "#% increased Effect of Curses on you during Effect",
    "Used when Charges reach full",
    # Rumi's Concoction's two mods: fixed for the unique but variable in their roll, which is
    # what the per-unique data has to say next to the enchant it says nothing about.
    "#% Chance to Block Attack Damage during Effect",
    "#% Chance to Block Spell Damage during Effect",
    # A map's implicit — the one thing about a map that a currency cannot re-roll, and the only
    # kind of modifier a `Strategy::Map` plan searches on.
    "Area is influenced by The Shaper",
    # A two-line implicit, which is one stat and therefore one filter — the map's affixes are
    # printed in exactly the same shape and must still come out as none.
    # The game hardcodes the 20 that trade placeholds, and the record's ref is the game's
    # wording — which is what the clipboard prints and what the fixture must key on.
    "Map contains Baran's Citadel\n"
    "Item Quantity increases amount of Rewards Baran drops by 20% of its value",
    # And a map affix, which that plan must leave out without calling it unrecognised. It is
    # also half of what covers the mod pool: the same wording is in the map's pool and in the
    # chart's, under one trade id, which is what a domain-qualified index key is for.
    "Monsters have #% chance to Hinder on Hit with Spells",
    # A pooled modifier that prints no number at all, so its entry carries no bounds and the
    # reader has to tell that apart from bounds it failed to read.
    "Area contains many Totems",
    # A blighted map's implicit, both halves of it — the plan searches every implicit a map
    # has, so leaving one out of the fixture would show up as an unrecognised modifier.
    "Area is infested with Fungal Growths\n"
    "Map's Item Quantity Modifiers also affect Blight Chest count at 50% value\n"
    "Can be Anointed up to # times",
    "Natural inhabitants of this area have been removed",
    # An Expedition Logbook's destinations. The faction and the area are `pseudo.*` stats and
    # nothing else in the game is searched this way — the item prints the two as bare names and
    # the join back to a trade id is by the stat's own wording, so the slice needs both halves
    # of every destination the captures name. Two factions and five areas covers all three.
    "Has Logbook Faction: Druids of the Broken Circle",
    "Has Logbook Faction: Order of the Chalice",
    "Has Logbook Area: Scrublands",
    "Has Logbook Area: Volcanic Island",
    "Has Logbook Area: Sarn Slums",
    "Has Logbook Area: Battleground Graves",
    "Has Logbook Area: Bluffs",
    # A destination's implicits. Explosives is the one that matters most: the rare capture
    # grants it from two different destinations, which is the case for both the merge refusing
    # to fold two alternatives together and the bound being a floor only.
    "#% increased number of Explosives",
    "#% increased quantity of Artifacts dropped by Monsters",
    "#% increased Explosive Radius",
    "#% increased Explosive Placement Range",
    "Area contains #% increased number of Monster Markers",
    "Area contains #% increased number of Runic Monster Markers",
    "Remnants have #% chance to have an additional Suffix Modifier",
    # And the affixes a rare logbook prints below its destinations, which the plan must leave
    # out as a map's are — without them the case would pass by resolving nothing at all.
    "Players have #% to all maximum Resistances",
    "Monsters' skills Chain # additional times",
    "+#% Monster Chaos Resistance",
    "+#% Monster Elemental Resistances",
    "Monsters gain #% of Maximum Life as Extra Maximum Energy Shield",
    # The one modifier a Valdo map is searched on, and the only one anything is searched on in
    # **both** directions: absent, it becomes a `not` group rather than being left open.
    "Players who Die in area are sent to the Void",
    # A chart's implicit. The voyage modifier is not revealed until the chart is sailed, so on
    # most of them this promise of one is the whole of what an implicit search has to go on —
    # and it is a real stat with a real trade id, not prose.
    "Voyage Modifier will be revealed once Charted",
    # The two modifiers an Inscribed Ultimatum is searched on, and the only two: they are the
    # size of the deal rather than the shape of the danger. "#% more Monster Life" is also the
    # case for a stat the builder emits with **two** trade ids for one mod type — the first is
    # the one an ultimatum is indexed under, measured rather than assumed.
    "#% increased Monster Damage",
    "#% more Monster Life",
    # A heist blueprint's enchant and one of its hazards, which is the whole of what the heist
    # strategy has to tell apart in a modifier list: the enchant is what the run is for and is
    # ticked, the hazard is the danger it will hold and is offered unticked. "#% more Monster
    # Life" above is a hazard too — the same stat an ultimatum stakes on.
    "Heist Targets are always Enchanted Armaments",
    "Players are Cursed with Temporal Chains",
    # The one wording a map's pool and a contract's both grant, which is what the heist half of
    # the mod pool below is sliced for. It prints no number on either.
    "Area has patches of Burning Ground",
    # And a contract affix nothing else words: the pool entry behind it grants two more stats
    # that no contract prints, so this is the wording an expansion has to grow from.
    "Patrol Packs have #% increased chance to be replaced by an Elite Patrol Pack",
    # A sanctum's two shapes of modifier, both of them only searchable in the `sanctum`
    # namespace — which is what the parser has to type them as, and the reason they are here at
    # all. The first pair is the ordinary affix; the boons and afflictions after it are stats
    # too, one per effect, looked up by the name the item prints under a "Has ".
    "The Merchant has # additional Choices",
    "#% increased Merchant Prices",
    "# additional Rooms are revealed on the Sanctum Map",
    "Has Rusted Chimes",
    "Has Sharpened Arrowhead",
    "Has Gold Coin",
    "Has Weakened Flesh",
    "Has Scrying Crystal",
    "Has Gold Mine",
    "Has Enchanted Urn",
    "Has Empty Trove",
    # "Has Red Smoke" — the second affliction of the third capture — is deliberately **not**
    # here, for the reason Porcupine Goliath and Dialla's Subjugation are not: an effect the
    # bundle cannot name has to be left out of the search and said out loud.
    # The Dark Monarch's four modifiers. The last of them is the only shape in the game whose
    # roll is a *name*: one stat per minion skill gem, of which the item printed the range as
    # "(Animated Weapons-Holy Armaments)" and the roll as the gem in the wording itself.
    "# to maximum Energy Shield",
    "# to maximum Energy Shield (Local)",
    "# to Level of all Minion Skill Gems",
    "#% increased Light Radius",
    "Maximum number of Sentinels of Purity is Doubled\n"
    "Cannot have Minions other than Sentinels of Purity",
    # Replica Dragonfang's Flight, the other shape of a named range: the pool is every skill
    # gem, so the wording carries the roll *and* a number, and the parenthesis follows a word
    # with no space in front of it.
    "#% increased Dexterity",
    "#% increased Intelligence",
    "# to Level of all Storm Burst Gems",
    "#% to all Elemental Resistances",
    "#% increased Reservation Efficiency of Skills",
    "Items and Gems have #% increased Attribute Requirements",
    "# to all Attributes",
    # Bound Fate. A modifier that enumerates its alternatives is as long as the list — seven
    # lines here — which is what the join has to be able to reach.
    "Every 5 seconds, gain one of the following for 5 seconds:\n"
    "Your Hits are always Critical Strikes\n"
    "Hits against you are always Critical Strikes\n"
    "Attacks cannot Hit you\n"
    "Attacks against you always Hit\n"
    "Your Damage with Hits is Lucky\n"
    "Damage of Hits against you is Lucky",
    "# to Dexterity",
    "# to Intelligence",
    "#% increased Stun and Block Recovery",
    # Nebulis on a synthesised base: three implicits with a per-unique fixed magnitude and two
    # unique-only explicits, one of them shared by no other stat in the slice at all. The case
    # for the base line's "Synthesised " prefix, which is not part of any base's own name and
    # has to come off before "Void Sceptre" resolves.
    "#% increased Critical Strike Chance",
    "#% increased Chaos Damage",
    "Bleeding you inflict deals Damage #% faster",
    "#% increased Implicit Modifier magnitudes",
    "#% increased Cast Speed",
    "#% increased Elemental Damage per 1% Fire, Cold, or Lightning Resistance above 75%",
    # A rare Heist Gear item's affixes. Grants Level # is what proves the suffix reads past its
    # own tier line; the other two also cover the two boilerplate lines Heist Gear prints around
    # its affixes ("Any Heist member can equip this item.", "Can only be equipped to Heist
    # members."), which carry no roll and must not be read as unmatched modifiers.
    "#% increased Projectile Attack Damage",
    "# to # added Lightning Damage\n"
    "Players and their Minions have # to # added Lightning Damage",
    "Grants Level # Purity of Ice Skill",
    # Four Heist Contract affixes with no roll at all, which is what the game marks with a
    # title-case, em-dash "Unscalable Value" rather than the lowercase, parenthesized suffix a
    # numeric roll gets — a wording no matcher above already covers.
    "Monsters are Hexproof",
    "Reward Rooms have #% increased Monsters",
    "Monsters Poison on Hit",
    "The Ring takes no Cut",
]

ITEMS = [
    "ITEM::Two-Stone Ring",
    "ITEM::Vaal Regalia",
    "ITEM::Two-Toned Boots",
    "UNIQUE::Abberath's Hooves",
    "DIVINATION_CARD::The Doctor",
    "ITEM::Chaos Orb",
    "ITEM::Riveted Boots",
    "UNIQUE::Ralakesh's Impatience",
    # A unique whose whole modifier list is stated in prose and never enumerated, so the app
    # has something to say rather than implying the item has nothing more.
    "ITEM::Crimson Jewel",
    "UNIQUE::That Which Was Taken",
    "ITEM::Silver Flask",
    "ITEM::Granite Flask",
    "UNIQUE::Rumi's Concoction",
    # The three shapes a map's identity comes in: the one base every tiered map shares (which
    # is why the tier is the whole of what tells them apart), a map that names its own area
    # instead, and a unique map, whose record carries the same "map" discriminator the base does.
    "ITEM::Map",
    "ITEM::Shaper Guardian Map",
    "UNIQUE::Olmec's Sanctum",
    # A Valdo map and the unique one of them pays out. The reward is searched as the unique's
    # own name, so the record is what turns the printed "Foil Hrimsorrow" into a term the trade
    # site will accept — and a blighted map has no record of its own at all, which is why it
    # resolves against "ITEM::Map" above.
    "ITEM::Valdo Map",
    "UNIQUE::Hrimsorrow",
    # The two shapes an **unidentified** unique comes in, which states nothing but its base:
    # Riveted Boots above roll into one unique and the app can take it, while Goathide Gloves
    # roll into two and only the player can say which. Both are looked up through
    # `en-items-base.index.bin`, so the pair is also what covers that index at all.
    "ITEM::Goathide Gloves",
    "UNIQUE::Hrimburn",
    # A second card, because the capture that proves a card resolves at all is a real one.
    "DIVINATION_CARD::The Blazing Fire",
    # An essence, for the other half of that: both are traded in bulk on the in-game exchange,
    # which states every item by the `metadataId` only a resolved base carries.
    "ITEM::Weeping Essence of Hatred",
    # The three shapes a gem's name comes in. An ordinary one is what the clipboard prints; a
    # Vaal gem is filed under a name the clipboard prints only halfway down the tooltip; and a
    # transfigured one is filed under the skill it alters, with a discriminator, so its record
    # is the one whose name is neither the type sent nor anything a bundle without the display
    # name could produce.
    "GEM::Empower Support",
    "GEM::Tornado Shot",
    "GEM::Vaal Blight",
    "GEM::Raise Zombie of Falling",
    # A chart and the area it covers, which are two different records: the base is what the
    # clipboard's base line says, and the area is what trade files the chart under — under its
    # internal id, with the "chart" discriminator and no display name anywhere on it.
    "ITEM::Coral Reef Chart",
    "ITEM::SeafloorRidges",
    # Itemised beasts, which live in a namespace of their own — the species is the base and the
    # rare title above it is not looked up at all. Porcupine Goliath is captured too and is
    # deliberately **not** here: the beast list grows every league, so a bundle that does not
    # know a species is the ordinary case and needs a fixture of its own.
    "CAPTURED_BEAST::Wild Hellion Alpha",
    "CAPTURED_BEAST::Chrome-touched Croaker",
    "CAPTURED_BEAST::Farric Goliath",
    # An Inscribed Ultimatum, and the items two of the captures stake or pay out. All three are
    # looked up to be *confirmed*, because the trade site fails a search outright on a required
    # item or a reward unique it does not know. "Dialla's Subjugation" — the divination card the
    # fourth capture stakes — is deliberately **not** here, for the reason Porcupine Goliath is
    # not: a bundle that cannot confirm a name has to leave the filter off and say so.
    "ITEM::Inscribed Ultimatum",
    "ITEM::Divine Orb",
    "UNIQUE::Martyr of Innocence",
    "UNIQUE::Mageblood",
    # A card that *is* here, alongside the one that is not: a stake is looked up across all three
    # namespaces the site's own filter names, and the card half of that had no positive case.
    "DIVINATION_CARD::Blind Venture",
    "ITEM::Ancient Orb",
    # Heist. Two wings apiece because a contract and a blueprint of the same area are separate
    # bases under separate trade categories, and a magic blueprint's base line arrives wrapped in
    # its affixes ("Deployed Blueprint: Records Office of Spine-Chilling"), so Records Office is
    # what covers stripping them off a heist item at all. The unique contract and the quest base
    # it rolls on are the pair that proves a unique heist item is planned as a unique.
    "ITEM::Contract: Tunnels",
    "ITEM::Blueprint: Tunnels",
    "ITEM::Blueprint: Records Office",
    "ITEM::Vigilante Contract",
    "UNIQUE::Contract: The Slaver King",
    # An itemised sanctum. One floor of the four is enough: they differ only by name, and the
    # base is the whole of what a sanctum's identity is looked up for.
    "ITEM::Sanctum Vaults Research",
    # An Expedition Logbook. One base for the whole category, which is why the type term says
    # nothing a logbook's category does not — and why a magic one arriving as "Buffered
    # Expedition Logbook" has to resolve back to this record before anything is sent.
    "ITEM::Expedition Logbook",
    # A unique whose per-unique record states one modifier as a *pool of names* — the minion
    # skill gems — rather than as a numeric range, which is the case for a printed range the
    # normalizer has to drop before the wording resolves at all.
    "UNIQUE::The Dark Monarch",
    "ITEM::Lich's Circlet",
    "UNIQUE::Replica Dragonfang's Flight",
    "ITEM::Onyx Amulet",
    "UNIQUE::Bound Fate",
    "ITEM::Cloth Belt",
    # A synthesised base and the unique that rolls on it. The base line the client prints is
    # "Synthesised Void Sceptre", never a name of its own — the record here is filed under
    # "Void Sceptre" alone, which is the case for stripping the prefix before lookup.
    "ITEM::Void Sceptre",
    "UNIQUE::Nebulis",
    # A Heist Gear base, rare rather than unique — the case for the class's own boilerplate
    # lines ("Any Heist member can equip this item.", "Can only be equipped to Heist members.")
    # reading as usage text rather than as unmatched modifiers.
    "ITEM::Precise Arrowhead",
]

UNIQUE_MODS = [
    "Ralakesh's Impatience",
    "That Which Was Taken",
    "Rumi's Concoction",
    "The Dark Monarch",
    "Replica Dragonfang's Flight",
    "Bound Fate",
    "Nebulis",
]

# Keyed on the first of each entry's mod ids, which is stable and is what the debug log names.
# Between them these cover every shape the reader has to get right: a wording with no number,
# one shared by two domains, a modifier printing two wordings of which only one carries a
# range, an entry whose wording trade indexes twice and so carries no id at all, and a
# corruption implicit, whose hash is in the implicit namespace rather than the explicit one.
#
# The last four are the heist pool. `MapBurningGround` and `HeistContractBurningGround` word one
# wording identically and grant nothing else, so they fold into one row the way a chart's twin
# does — the whole of what a third domain had to be shown not to break. `HeistContractBurningGround1`
# is the same wording plus the two alert-level stats no contract prints, which is why the fold
# leaves it alone. `HeistContractMonsterPatrolAdditionalElite1` is that shape with nothing to share:
# one printed wording, two unprinted, and the entry an expansion from the printed one has to reach.
MOD_POOLS = [
    "MapTotems",
    "MapMonstersHinderOnHitMapWorlds",
    "MapDeepwaterChartMonstersHinderOnHit",
    "MapDeepwaterChartMonsterCannotBeStunned",
    "MapCorruptionItemQuantity",
    "MapBurningGround",
    "HeistContractBurningGround",
    "HeistContractBurningGround1",
    "HeistContractMonsterPatrolAdditionalElite1",
    # The four "Unscalable Value" affixes above, so map check can rate a contract that rolls
    # them rather than drawing it unrateable.
    "HeistContractHexproof",
    "HeistContractSideAreaIncreasedMonsters1_",
    "HeistContractPoisoning",
    "HeistContractNoGangCut1",
]

ITEM_CLASSES = ["Rings", "Boots", "Gloves", "Body Armours", "Stackable Currency",
                "Divination Cards", "Jewels", "Utility Flasks", "Maps", "Skill Gems",
                "Support Gems", "Chart", "Misc Map Items", "Contracts", "Blueprints",
                "Sanctum Research", "Expedition Logbooks", "Helmets", "Amulets", "Belts",
                "Sceptres", "Heist Gear"]

LANG = "en"


def fnv1a32(s: str) -> int:
    h = 0x811C9DC5
    for b in s.encode("utf-8"):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def read_ndjson(path: Path) -> list[tuple[bytes, dict]]:
    out = []
    for line in path.read_bytes().split(b"\n"):
        if line:
            out.append((line, json.loads(line)))
    return out


def pick(records: list[tuple[bytes, dict]], wanted: list[str], key) -> list[tuple[bytes, dict]]:
    """The wanted records, in the order listed above — the fixture's line order is ours."""
    by_key = {key(r): (line, r) for line, r in records}
    missing = [w for w in wanted if w not in by_key]
    if missing:
        sys.exit(f"not in the source bundle: {', '.join(missing)}")
    return [by_key[w] for w in wanted]


def write_ndjson(path: Path, chosen: list[tuple[bytes, dict]]) -> list[int]:
    """Write LF-terminated lines; return each line's byte offset, which is what an index stores."""
    offsets, pos, blob = [], 0, bytearray()
    for line, _ in chosen:
        offsets.append(pos)
        blob += line + b"\n"
        pos += len(line) + 1
    path.write_bytes(bytes(blob))
    return offsets


def write_index(path: Path, pairs: list[tuple[str, int]]) -> None:
    rows = sorted(((fnv1a32(k), off) for k, off in pairs), key=lambda r: (r[0], r[1]))
    path.write_bytes(b"".join(struct.pack("<II", h, off) for h, off in rows))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("source", type=Path, help="a built bundle directory (the data repo's out/)")
    ap.add_argument("--out", type=Path,
                    default=Path(__file__).resolve().parent.parent / "tests" / "data" / "bundle")
    args = ap.parse_args()
    src, out = args.source, args.out
    out.mkdir(parents=True, exist_ok=True)

    stats = pick(read_ndjson(src / f"{LANG}-stats.ndjson"), STATS, lambda r: r["ref"])
    offsets = write_ndjson(out / f"{LANG}-stats.ndjson", stats)
    write_index(out / f"{LANG}-stats-ref.index.bin",
                [(r["ref"], off) for (_, r), off in zip(stats, offsets)])
    write_index(out / f"{LANG}-stats-matcher.index.bin",
                [(m["string"], off) for (_, r), off in zip(stats, offsets) for m in r["matchers"]])

    items = pick(read_ndjson(src / f"{LANG}-items.ndjson"), ITEMS,
                 lambda r: f"{r['namespace']}::{r['name']}")
    offsets = write_ndjson(out / f"{LANG}-items.ndjson", items)
    write_index(out / f"{LANG}-items-name.index.bin",
                [(f"{r['namespace']}::{r['name']}", off) for (_, r), off in zip(items, offsets)])
    write_index(out / f"{LANG}-items-ref.index.bin",
                [(f"{r['namespace']}::{r['refName']}", off) for (_, r), off in zip(items, offsets)])
    write_index(out / f"{LANG}-items-base.index.bin",
                [(f"UNIQUE::{r['unique']['base']}", off) for (_, r), off in zip(items, offsets)
                 if r["namespace"] == "UNIQUE" and r.get("unique", {}).get("base")])

    uniques = pick(read_ndjson(src / f"{LANG}-unique-mods.ndjson"), UNIQUE_MODS, lambda r: r["name"])
    offsets = write_ndjson(out / f"{LANG}-unique-mods.ndjson", uniques)
    write_index(out / f"{LANG}-unique-mods-name.index.bin",
                [(f"UNIQUE::{r['name']}", off) for (_, r), off in zip(uniques, offsets)])

    pools = pick(read_ndjson(src / f"{LANG}-mod-pools.ndjson"), MOD_POOLS,
                 lambda r: r["mods"][0])
    offsets = write_ndjson(out / f"{LANG}-mod-pools.ndjson", pools)
    # One key per wording, qualified by domain: a map and a chart share wordings and are
    # separate pools, so the domain is part of what is being asked for.
    write_index(out / f"{LANG}-mod-pools-ref.index.bin",
                [(f"{r['domain']}::{s['ref']}", off) for (_, r), off in zip(pools, offsets)
                 for s in r["stats"]])

    classes = pick(read_ndjson(src / "item-classes.ndjson"), ITEM_CLASSES, lambda r: r["itemClass"])
    write_ndjson(out / "item-classes.ndjson", classes)

    # Not the source's manifest: the fixture is not a release, and nothing may mistake it for
    # one. The attribution is real, though — it is what the app is tested for crediting.
    src_manifest = json.loads((src / "manifest.json").read_bytes())
    source = {"unique_mods_attribution": "poewiki.net, CC BY-NC 3.0"}
    # Carried through rather than invented: it is what `GameData::has_exchange_flags()` reads,
    # and it is the *only* thing that tells a bundle predating the currency-exchange flags from
    # one where the flag is genuinely absent because the item does not trade there. A fixture
    # sliced from a bundle that has the dataset must say so, or the flags copied verbatim onto
    # its item records would read as "unknown" and never be tested at all.
    if items_exchange := src_manifest.get("source", {}).get("exchange_items", 0):
        source["exchange_items"] = items_exchange
    # Carried through the same way, though what gates the mod pools in the app is the file
    # itself: this is the bundle's own record of how many the build emitted.
    if pool_count := src_manifest.get("source", {}).get("mod_pools", 0):
        source["mod_pools"] = pool_count
    manifest = {
        "schema_version": 1,
        "data_version": "fixture",
        "game_patch": src_manifest.get("game_patch", ""),
        "languages": [LANG],
        "source": source,
        "files": [],
    }
    (out / "manifest.json").write_bytes(json.dumps(manifest, indent=2).encode() + b"\n")
    print(f"wrote {len(stats)} stats, {len(items)} items, {len(uniques)} unique-mod records, "
          f"{len(pools)} pool modifiers and {len(classes)} item classes to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
