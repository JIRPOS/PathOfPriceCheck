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
]

UNIQUE_MODS = [
    "Ralakesh's Impatience",
    "That Which Was Taken",
    "Rumi's Concoction",
]

ITEM_CLASSES = ["Rings", "Boots", "Body Armours", "Stackable Currency", "Divination Cards",
                "Jewels", "Utility Flasks"]

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

    classes = pick(read_ndjson(src / "item-classes.ndjson"), ITEM_CLASSES, lambda r: r["itemClass"])
    write_ndjson(out / "item-classes.ndjson", classes)

    # Not the source's manifest: the fixture is not a release, and nothing may mistake it for
    # one. The attribution is real, though — it is what the app is tested for crediting.
    manifest = {
        "schema_version": 1,
        "data_version": "fixture",
        "game_patch": json.loads((src / "manifest.json").read_bytes()).get("game_patch", ""),
        "languages": [LANG],
        "source": {"unique_mods_attribution": "poewiki.net, CC BY-NC 3.0"},
        "files": [],
    }
    (out / "manifest.json").write_bytes(json.dumps(manifest, indent=2).encode() + b"\n")
    print(f"wrote {len(stats)} stats, {len(items)} items, {len(uniques)} unique-mod records "
          f"and {len(classes)} item classes to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
