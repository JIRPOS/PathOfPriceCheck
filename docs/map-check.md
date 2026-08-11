# Map check (not built)

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

**Nothing in this document is built.** It is the design for [ROADMAP.md](../ROADMAP.md)'s 0.7 and
for the two layers underneath it that have to move first — the data bundle and the app's data
layer. Sections become the layer's own documentation as they ship; until then read every sentence
as intent.

**[roadmap.md](roadmap.md)'s constraints for this version come first**, and this document is
written under them rather than beside them. Where the two disagree, that one wins.

The feature: a hotkey reads a map's rolled modifiers and says which ones you decided you cannot
take. The verdicts live in a table meant to fill in by being used — rate what the popup shows, on
the spot. Nothing about the feature requires knowing what a map *could* have rolled.

**The store keys on the stat record, never on the printed line's `placeholder_form`.** A wording is
language-dependent the moment a localised bundle exists, and `find_stat` already refuses to guess
between two records that share one. So resolution happens first and the verdict attaches to what it
resolved to — which also means a modifier printing several stats carries a verdict per stat, since
that is what the store can key. The pool below is grouped by modifier for reading; the store
underneath it is not.

What a **pool** buys is the one thing that cannot fill in by use: pre-filling. A searchable list of
every modifier that exists, so a user can rate ones they have not met yet, or paste a regex and
have it propose verdicts across the lot. That is the whole reason the bundle grows a new asset, and
it is worth keeping in view — the pool is a convenience for one settings page, not a correctness
requirement anywhere.

**"A map" means anything that opens in the map device**: ordinary maps, nightmare and Originator
maps, unique maps, charts, expedition logbooks and invitations. That is a scope decision, and the
game's own data groups them the same way — one domain, which is the section below. Maven's
invitations are handed over by Kirac now rather than slotted, and are kept in scope anyway, because
they are still rollable and still carry modifiers a player wants a verdict on. **Vaal side areas
are out**: there is no copy of a side area to read, and reading one off the screen is not something
this project does.

Every layer built so far starts from the item in hand, so nothing in the bundle describes a pool
except the per-unique dataset, which is built from poewiki rather than from the client. Map check
is the first feature where the client's own data expresses one directly.

## What the game's data actually says

Measured against patch **3.29.2.1** / data bundle **20260811.45**. Re-derivable from the data
repo's cached `builder/.work/tables/English/*.json`; the numbers are stated so a later patch can be
compared against them rather than re-argued.

**Two independent axes.** `Mods.Domain` is the *pool namespace* a modifier is generated from;
`Mods.GenerationType` is *how it arrives* (prefix, suffix, unique, corrupted, enchant, essence…).
Both are needed: a map's affixes are domain 5 + generation 1/2, its Vaal corruption implicits are
domain 5 + generation 5. Four live domains and two live generation types have no name in
[dat-schema](https://github.com/poe-tool-dev/dat-schema), so **the numbers are the identity** and
any display name is an app-side table with a fallback.

**Domain 5 is `AREA` and it is the map pool.** 407 prefixes + 393 suffixes = 800 rows, which
collapse to **207 distinct wording-sets** once tier variants fold together — and they do fold,
because a verdict is attached to a wording, not to a roll. Of those:

| | |
| --- | --- |
| `MapCorruptedSideArea*` / `*Map2Tier*` only — a Vaal side area's own mods and legacy map-series rows, neither of which prints on a map item | ~52 |
| `MapUber*`-exclusive — nightmare and Originator only | 59 |
| shared between `MapUber*` and ordinary map mods, so the distinction collapses at wording level | 55 |

Leaving roughly **155 rateable wordings**, which is small enough that the settings page is a list
with a search box rather than a data-management problem.

**The other map-adjacent domains.** Charts are **39** (49 prefixes, 32 suffixes), a domain
dat-schema does not name. **12** `LEAGUESTONE` holds legacy map mods that still collide with
domain-5 wordings. **14** `MAP_DEVICE` holds invitation implicits. **11** `ATLAS` holds
atlas-side mods.

**Domain 5 is the map device.** Every kind of thing that opens in it shares the domain and carries
a tag on its base saying which kind it is. The tags are recorded here because they are what proves
the scope above is the game's own grouping rather than a guess — **they are not used**, and the
pool is deliberately not split by them:

| item | domain | tags on the base |
| --- | --- | --- |
| ordinary map | 5 | `top_tier_map`, `mid_tier_map`, … |
| Nightmare Map | 5 | `uber_tier_map`, `top_tier_map`, `map_force_6_mods` |
| Maven's Invitation: The Feared | 5 | `maven_map`, `maven_void_map_feared` |
| Writhing Invitation | 5 | `primordial_map` |
| Expedition Logbook | 5 | `expedition_logbook` |
| Coral Reef Chart | 39 | `chart` |

Domain 5's generation types say the same thing from the other side: 1 and 2 are the affixes, 5 the
Vaal corruption implicits, 23 `EXPEDITION_LOGBOOK`, 36 `MEMORY_ALTAR`, 8 `TEMPEST` (legacy), 3 the
fixed ones including `IsUberMap` and `MapZanaInfluenced`.

One thing to fix while here: the bundle resolves an invitation to its **`Quest Items`** row
(`Metadata/Items/MapFragments/…/Quest…`, domain 43) while the clipboard prints
`Item Class: Misc Map Items`, which is the *other* row of the same name — domain 5, and the one
that carries the tags. Two bases share each invitation's name and the emit picks the wrong one.
Harmless today; not harmless once the base decides which pool is shown.

**The three map variants, precisely.**

- A **Nightmare Map** is a base — `Metadata/Items/Maps/MapKeyNightmareBoss`, tags `uber_tier_map`
  and `map_force_6_mods`, implicit `IsUberMap`.
- An **Originator map** is *not* a base. It is an ordinary `Map` carrying the implicit
  `MapZanaInfluenced` (`map_zana_influence` → `implicit.stat_2696470877`), and the half-and-half
  pool follows from that implicit rather than from the base.
- An **invitation** is domain 5 like everything else, tagged `maven_map` (plus a per-boss tag such
  as `maven_void_map_feared`) or `primordial_map` for the Eldritch four, and carries one fixed
  implicit. Whether it *also* rolls affixes is not answerable from what the builder fetches, and
  with an unpartitioned pool it does not need to be: whatever an invitation prints gets rated like
  anything else. An earlier reading of a Normal-rarity capture as "invitations have no pool" was
  unsound — that capture shows one invitation that had no affixes, not a base that cannot have
  them.

**A base has exactly one domain.** A flask is `FLASK`, not `ITEM` plus a refinement; the domains
are mutually exclusive namespaces. Only 11 of 82 item classes span two, and always because the
class holds genuinely different things (Jewels covers `BASE_JEWEL` and `AFFLICTION_JEWEL`).

**Which is why domain buys nothing for gear, and everything for maps.** Granularity inside domain
1 comes from spawn weights against tags, and the class-level tag (`ring`, `flask`) is not even on
the base row — `Iron Ring` has an empty `TagsKeys` and reaches its tag through `InheritsFrom`,
whose tag set lives in the game's metadata files rather than in any dat table. Maps need none of
that because domain 5 is effectively one slot with one pool.

**And nothing for uniques.** A unique's modifiers are domain 1, the same as a rare's; the
discriminator is generation type 3, except that 276 of the mod rows the unique dataset references
are ordinary generation 1/2 anyway. The unique→mod link is a relation, not a property of the mod,
which is what [UNIQUE-MODS.md](../UNIQUE-MODS.md) exists for.

## The rule this whole design hangs on

**The pool describes; it never gates.** It says what can spawn *naturally* on this kind of thing,
which is strictly less than what an item can print. An essence (generation type 11) puts a modifier
on a base whose weights would never have produced it, and so do crafted (domain 9), veiled
(26 / 28) and Harvest. On top of that the pool here is filtered by mod-id convention and is
knowingly imperfect. So:

- **A modifier the item prints and the pool does not contain is normal, not an error.** It renders
  as it does today, it is rateable on the spot like any other, and the verdict sticks — the store
  keys on the resolved stat and has no idea whether the pool mentions it.
- The pool may be used to *offer* and to *pre-fill*. It may never be used to reject, to hide a
  printed modifier, or to decide that a line failed to parse.

## What the bundle gains

One change in [the data repo](https://github.com/JIRPOS/PathOfPriceCheck-Data) — a separate
repository and therefore a separate change set — and one decision that keeps it to an emit.

**1. One flat pool, deliberately not partitioned per base.** Domain 5 covers every map-device item
at once, and the pool is emitted exactly that way — a single list, not one list per kind of map.
The reason it can be is the shape of the feature: the popup shows only what the item in hand
actually rolled, so a modifier that could never appear on this base never comes up. The settings
list is a rating dictionary, and an entry that goes unrated for a year costs one row.

**So `Mods.SpawnWeight_TagsKeys` / `SpawnWeight_Values` stay unfetched.** They are the only thing
in the game's data that says which base a modifier can spawn on — a membership list rather than the
probability model the name suggests — but nothing here consumes that. Splitting the list was the
only use, and the list is not split. **Decided, not deferred**: revisit only if something needs to
answer "can this base roll that", which nothing in 0.7 does.

The one thing borrowed from that idea is **list hygiene, by mod id**: entries whose every mod row
matches `CorruptedSideArea` (a Vaal side area's own modifiers, which never print on anything the
user can copy) or `Map2Tier` (legacy map series) are left out of the emit. Roughly 52 of 207
wording-sets. This is a naming convention rather than data, it is allowed to be imperfect, and the
cost of a mistake either way is one row in a searchable list.

**2. Emit what is already fetched.** `Mods.Domain`, `Mods.GenerationType` and
`BaseItemTypes.ModDomain` are all downloaded today and dropped at emit time.

- **`domain` on base-type records**, straight from `BaseItemTypes.ModDomain`. One integer, exact,
  no ambiguity — this is how the app knows a Map is `AREA` and a chart is 39 without a compiled-in
  name list.
- **A new optional asset, `en-mod-pools.ndjson`**, plus an fnv1a32 index over each entry's
  normalized wordings, built with the same machinery as `en-stats-matcher.index.bin`. Named for
  the general case and carrying its `domain` per record, because the settings page below is
  pool-agnostic and flasks, abyss jewels and idols are the same shape. Seed it with domain 5 and
  domain 39; nothing else, until something asks.
- **`source.mod_pools`** in the manifest, written only when non-zero, so "no data" is
  distinguishable from "no pool for this domain" — the same reasoning as `source.exchange_items`
  in [data-layer.md](data-layer.md).

One record is one **wording-set**, not one mod row, because tiers collapse:

```json
{"domain": 5, "gen": 1, "name": "Hungering", "tiers": 1,
 "mods": ["MapUberModDrowningOrbs"],
 "stats": [{"ref": "Area contains Drowning Orbs",
            "trade": "explicit.stat_25225034", "min": null, "max": null}]}
```

`name` is `Mods.Name`, the affix name the client prints with Advanced Mod Descriptions on, and it
is free. `mods` is provenance, for a debug log that has to explain itself. `min`/`max` span the
lowest and highest tier **in displayed units with `dp` applied**, exactly as `unique_mods` already
emits ranges — `Mods.dat` stores hundredths and milliseconds raw and leaving that to the client is
a silent factor of 100. The ranges are not decoration: they are what lets a pasted regex be tested
against a rendered line rather than against a placeholder.

**Deliberately not done: a domain set on stat records.** It was measured. 643 wordings are
rendered by more than one description block; domain sets separate only 47 of them, 335 overlap and
261 involve a block no mod uses. In the emitted file that is 4 wordings currently reported as
"ambiguous wording, not searched", none of them map mods. It buys too little to be worth a field on
every one of 12,288 records, and it cannot touch the larger class described next.

## What it cannot fix, and must not be expected to

791 records carry **more than one trade id inside a single namespace** (249 explicit, 506 crucible,
38 enchant, 27 implicit), and `to_filter` in [plan.cpp](../src/item/plan.cpp) sends `ids.front()`.
No amount of client-side data resolves those: both hashes carry identical text, and the trade
hash is **not** a derivable function of the client stat id — fnv1a32, crc32, djb2, sdbm, Java
hashCode and adler32 were each tested against `map_zana_influence`, over the raw, uppercased and
`stat_`-prefixed forms, and none produce `2696470877`. Only observing a live listing can pin a hash
to a printed line. This is a separate known issue, out of scope here, and named so it is not
rediscovered as a symptom of this work.

## What the app gains

**`src/data`.** `en-mod-pools.ndjson` and its index load exactly as the optional datasets already
do, with a `has_mod_pools()` gate mirroring `has_unique_mods()` / `has_unique_bases()` — a bundle
published before this asset existed must keep working, which is most of why the gate exists rather
than a null check. `BaseType` gains `mod_domain`. Neither addition may pull SDL, ImGui, X11 or
libcurl into `ppc_core`; the pool is data and is testable headless.

**The verdict store.** A table per character profile of stat → verdict, keyed as above and
persisted beside the existing settings. The verdicts are the roadmap's and the roadmap's wording
sticks: **safe**, **dangerous**, **deadly**, and **unrated** as the zero state — not a fourth choice
a user picks but the absence of one, which is why a row can be drawn as unrated and why the table
grows by being used. The file format **parses an optional roll bound from day one and shows no UI
for it**, per [roadmap.md](roadmap.md): accepting both shapes costs one branch now and a format
migration costs every user's file later. It is a **new file on disk**, so it is a change to
[PRIVACY.md](../PRIVACY.md) as much as to the code — that document enumerates every file written,
and it is the one that goes stale silently. Write both in the same change.

**A pool browser in Settings, built pool-agnostic from the start.** One flat searchable list of
every entry in the pool, rated and unrated together, with the rating control on the row. Two things
make a long list usable and both are cheap: a **search box**, and **rated entries sorted to the
top** so the part the user has an opinion about is the part they see first.

The page is expected to be **rarely opened**. The table is meant to fill in by playing — rate what
the popup shows you, on the spot — and the settings list exists for the one session where someone
sits down to pre-fill it, usually by pasting a regex. Design accordingly: it is a bulk-editing
tool, not the primary way anything gets rated.

Parameterise it by domain now rather than hard-coding the map list. The same page serves 246 flask
mods, 511 abyss jewel mods or 552 idol mods later at no extra cost, and retrofitting a hard-coded
list into that is the expensive order. This is the largest independent piece of UI in 0.7 and
depends on no map logic at all.

**Seeding from a pasted map regex.** [roadmap.md](roadmap.md) settles the shape of this and it is
worth restating only because it is the part most easily got wrong: the pasted string is **PoE's
search syntax, not one regex** — quoted terms, a leading `!` for negation, space-separated terms
ANDed, bare trailing terms (`"!a|b|c" pte`). Tokenize first, hand each *term* to the engine.
Feeding the whole string to one is how the `!` ends up matched literally. And **match against a
rendered wording, never `placeholder_form`**: a term like `\d+ e` was written against printed item
text and can never match a `#`.

This is where the pool's `min`/`max` earn their place. With a map in hand the printed lines are
right there, but seeding from the whole list needs each entry rendered with something in the
placeholder — and a term that can only match a number is one this cannot honestly resolve, so it
says so rather than guessing. **The import proposes and the user confirms; nothing writes a verdict
the user has not seen.**

Two smaller semantics are left, and they are the doc's to settle rather than the roadmap's: whether
an entry counts as hit when **any** of its wordings matches (a modifier can print two to four
lines), and whether the affix-name line is in scope, since the in-game search sees it when Advanced
Mod Descriptions is on.

## Order of work

Each phase is shippable and reversible on its own, and the first two are the "pre-fire" the feature
sits on.

1. **Data repo.** Emit `domain` on base records, fix the invitation base-row pick, emit
   `en-mod-pools.ndjson` + index + manifest count. No new columns are fetched, so this is an emit
   change and a data release. The app ignores assets it does not know, so it ships safely ahead of
   any app change — and that property should be confirmed against the current release rather than
   assumed.
2. **App plumbing, no visible feature.** Load and gate the pool, add `BaseType::mod_domain`, extend
   the test bundle slice per [testing.md](testing.md). Nothing on screen changes.
3. **Settings: the pool browser and the verdict store**, with `PRIVACY.md`. Visible, usable, and
   inert without the hotkey — a user can rate modifiers before anything reads the ratings.
4. **The feature.** Hotkey, popup, worst-verdict-leads, rate-on-the-spot, profiles.
5. **Regex import**, last, because it is the part the roadmap marks *might*.

## Open, and needing an answer rather than a guess

- **Do atlas-side modifiers print on the map item?** If they do, domain 11's 145 rows belong in the
  pool so they can be rated like anything else. This wants a **capture**, not reasoning — the same
  rule every number in this project is held to.
- **The two import semantics above** — any-wording-hits, and whether the affix name is matchable.
- **What a profile is keyed on**, if `LatestClient.log` watching is ever built. It is outside the
  1.0 promise and goes in `PRIVACY.md` either way.
