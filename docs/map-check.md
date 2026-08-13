# Map check

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

**Built**, all five phases of the order of work at the foot — [ROADMAP.md](../ROADMAP.md)'s 0.7.
This document is both the design and the record of what the building changed about it; where a
section describes something that was measured rather than reasoned, it says so.

**[roadmap.md](roadmap.md)'s constraints for this version come first**, and this document is
written under them rather than beside them. Where the two disagree, that one wins.

The feature: a hotkey reads a map's rolled modifiers and says which ones you decided you cannot
take. The verdicts live in a table meant to fill in by being used — rate what the popup shows, on
the spot. Nothing about the feature requires knowing what a map *could* have rolled.

## The shape of it, from the outside

**Ctrl+Shift+D** over a map. It shares the whole copy path with the price check and parts company
only once there is an item — one flag, `App::copy_target_`, consumed in `poll_pending_copy`. The
popup opens at the cursor, exactly as the paste list does and through the same placement code.

The **one gate** is `is_rateable_item`: nothing else opens the popup, and an item that fails it
is dropped in silence like any other check that finds nothing. It is not a data question but an
item one — a ring's modifiers resolve to stats as a map's do, and without the gate they would be
rated into a map profile with nothing to stop them. It asks one thing: does this item roll from a
domain in `kDomains`? So a heist contract passes and a Crucible remnant does not, both of them for
the same reason and neither of them by name.

**It passes an item whose pool the bundle does not carry**, deliberately: the gate is about the
item and the pool is a convenience, so an affix that resolves to a stat is rateable either way.
The cost is that `pool_refs_for` cannot expand — a verdict lands on the printed wording alone
where the pool would have keyed it on the affix's whole set — and those two keys are not each
other. Ratings made on a bundle predating a domain's pool are therefore not found again once that
pool arrives. It shows as an unrated row, never as a wrong verdict, and it is the price of not
having the hotkey do nothing.

The popup, top to bottom: the **outlook** (below), the name plate and the item's own numbers laid
out across the panel rather than one to a line, the profile in use, and then one row per modifier
with its verdict. A click walks a row through the four states; a right-click puts it straight back
to unrated, which is otherwise three clicks away from deadly and is the one a misclick needs.

The numbers block is a **wrapping run of label-value pairs**, and a heist item is what it was
worth compacting for: a fully revealed blueprint prints fifteen property lines, six of which are
`Requires <job> (Level N)` sentences. Those fold into one pair — `Requires: Brute Force 5,
Demolition 5, …`, keeping any `(unmet)` the client annotated, both wordings out of the lexicon —
and the whole block comes to five lines. The banner's sentence names nothing for the same reason:
"You can run this safely" is true of a map, a chart and a contract alike.

**Nothing is split and nothing is merged.** A hybrid modifier keeps both its lines in one row, and
two modifiers wording the same thing stay two rows. The item printed them that way and the popup is
a reading of the item.

### The outlook

One line at the top, which is what the popup is read at a glance for. `mapcheck::assess` is the
whole of it and is tested headless; the order it checks in is the order of what is strongest:

| | when | colour |
| --- | --- | --- |
| a deadly modifier | any at all | red |
| more than half rated safe | | green, and a second sentence when some are unrated |
| half or more unrated, nothing worse than safe under it | | neutral, question mark |
| more safe than dangerous | | yellow |
| as many dangerous as safe, or more | | orange |

A deadly modifier decides the map on its own, whatever it is outnumbered by: averaging that away
would be exactly the confident wrong answer this codebase is built to avoid. A row that resolved to
no stat counts as **unrated** rather than being left out — it is a modifier on the map the reader
has not decided about, and dropping it would make a map of unreadable lines look fully rated.

**Implicits are rated like anything else**, and this is a reversal. They were printed above the
list and left alone on the argument that an implicit is what the base came with rather than what it
rolled — true of a Nightmare map saying it is one, and false of the Vaal corruption implicits,
which roll, and which the pool carries as generation 5. The rule bought simplicity and paid for it
with the inconsistency it admitted to in the same breath: a wording rateable in Settings and inert
on the map in front of you. So an implicit is a row, it counts in the tally, and it carries a
verdict. It keeps the game's duller blue so the reader can still see which is which — pulled back
towards the modifier colour rather than left where it was, because a row that resolved to nothing
is grey and the old tint was two shades off it.

## The verdict store — **built**

**The store keys on the stat records, never on the printed lines' `placeholder_form`.** A wording is
language-dependent the moment a localised bundle exists, and `find_stat` already refuses to guess
between two records that share one. So resolution happens first and the verdict attaches to what it
resolved to — as a **set**, because the thing being rated is an affix and an affix can grant several
stats. See "A verdict is keyed on the affix" below for why a verdict per wording was tried and does
not work.

**A profile is a file**, `<config>/map-profiles/<name>.json`, and the name *is* the file name —
`sanitize_profile_name` substitutes what a filesystem refuses rather than dropping it, so `a/b` and
`ab` stay two profiles. `config.json` records the names and their order, but **the directory is the
authority**: `Store::open` reconciles the two, so a table dropped in by hand appears and a name
whose file has gone is dropped. That is also what makes creating a profile safe without saving
Settings first — the file is written the moment the dialog closes.

**Which profile is in use is remembered the moment it changes**, from the popup as readily as from
Settings, and this had to be built rather than assumed: the selection lived in `config_` and
`config_.save()` is the Settings **Save** button's alone, so a switch made in the popup — which has
no such button — was lost on the next launch. It cannot simply call `save()` either, because the
Settings screen edits that same live object and would push out a league or an account name still
being typed. So `persist_map_profile` re-reads the file, lays the two map-check fields over what is
already there, and writes that. Creating and deleting go through it too, which is what makes the
config's ordering keep up with the directory rather than waiting for a Save.

**Picking a profile is the only way one is picked**, and that is not a gap left for later — see
"The client log cannot say which character is playing" below. There is no auto-load rule, no
per-character mapping in `config.json` and no rule under which a hand-picked profile is temporary.

There is **always at least one profile**. An empty directory is given `Default` at startup, and
deleting the last one puts it back: a verdict is only ever put into a table, so a popup opening
with no table is one where every click silently does nothing. Deleting one at all is behind a
confirmation that says how many ratings go with it — it is a few hundred decisions and a
thirty-pixel button.

The file format **parses an optional roll bound from day one and shows no UI for it**, per
[roadmap.md](roadmap.md): reading accepts both the bare word and the object form, writing uses the
bare word since that is every row this version can produce. Accepting both shapes costs one branch
now and a format migration costs every user's file later.

Ratings are **buffered and written 1.5 seconds after the last one**, and flushed outright whenever
a screen that can rate closes, when a profile is switched, when a bulk import is accepted, and on
the way out. The throttle is a ceiling on batching, never on durability — walking one modifier
through all four states is one write, and nothing outlives the process unwritten.

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

**And it means a heist contract or blueprint too.** A contract goes to Adiyah rather than into the
map device, so it is outside that sentence on the letter of it and inside on every other count: it
is an area you decide to open, it rolls prefixes and suffixes from a pool of its own, and the
decision a player makes about `Players have 60% reduced effect of Non-Curse Auras from Skills` is
the same decision they make about it on a map. The pool is `HEIST_AREA`, domain 22, and it costs
one entry in `kDomains` — which is the whole reason that list exists. What it does *not* cost is a
second feature: the store, the profiles, the popup, the settings page and the import were written
per domain, and none of them learned anything about heists.

**Crucible maps — `Primeval Remnant` and `Primordial Remnant`, domain 33 `CRUCIBLE_MAP` — are
out**, and the reason is not the code. Their pool is real (49 prefixes, 51 suffixes) and the two
bases are `Misc Map Items`, the same class as the 26 that pass the gate on domain 5, so this is the
one place the gate splits an item class. But the league is over, the items cannot drop, and what is
left is a handful on Standard: not worth an entry. Note the shape if it is ever reopened — it is
one line in `kDomains` and one in the data repo's `POOL_GENERATIONS`.

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
dat-schema does not name. Heist areas are **22** `HEIST_AREA` — the section below. **12**
`LEAGUESTONE` holds legacy map mods that still collide with domain-5 wordings. **14** `MAP_DEVICE`
holds the fixed implicit each map fragment carries (`VaalFragmentImplicit1`,
`ShaperFragmentImplicit`; all 274 rows are generation 3 and not one of them names an invitation —
invitations are domain 5, as the table below says). **11** `ATLAS` holds atlas-side mods.

**Only three domain names contain the word AREA**, which is the question worth asking once: **5**
`AREA`, **17** `DELVE_AREA` and **22** `HEIST_AREA`. Delve is closed rather than deferred — its
155 rows are all generation type 14 and **no base item in the game carries the domain**, because a
delve biome is an area nobody holds a copy of. That is the Vaal side area argument again, and it
is why heist is the only reachable area pool outside the map device's own.

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

One thing to fix while here — **fixed**: the bundle resolved an invitation to its **`Quest
Items`** row (`Metadata/Items/MapFragments/…/Quest…`, domain 43) while the clipboard prints
`Item Class: Misc Map Items`, which is the *other* row of the same name — domain 5, and the one
that carries the tags. Thirteen names are shaped that way (the nine Maven ones and the four
Eldritch), the emit picked the wrong one, and the map device now outranks the quest item in the
liveness rule that decides.

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

**Domain 22, the heist pool.** 146 prefixes + 153 suffixes = 299 rows, **90 wording-sets**, and
nothing else: no implicits, no legacy generation types, nothing the hygiene rules drop. It is
behind 18 bases — 9 contracts and 9 blueprints, which share the one pool. The quest contracts
(`Vigilante Contract` and its kin) are domain **43** with the stackable currency and roll nothing,
which is why the `Contracts` item class publishes no domain of its own: its bases disagree, so a
contract is placed by its base and by `is_heist()` if the base did not resolve.

**Every heist affix grants two stats no contract prints**, and this is the thing to know about the
pool. Alongside its own wording each one carries `#% more raising of Alert Level` and `#% increased
time before Lockdown`, which the client does not print as lines — it sums them into the properties
`Alert Level Reduction` and `Time Before Lockdown`. Measured on a real capture: six affixes
carrying -7, -6, -6, -5, -6 and -4, and the item printing `Alert Level Reduction: +34%`. So the
`pool_refs_for` expansion is not an optimisation here but the ordinary case — 88 of the 90 entries
need it — and it is the same mechanism the Nightmare map's unprinted `#% more Currency found in
Area` already needed. The two entries that are *only* the alert pair are affixes that print no line
at all: rateable in Settings, never on a popup, and harmless.

**The wordings mostly do not collide with a map's, and that is data rather than design.** 58 of
the 90 entries word their printed stat exactly as a domain-5 entry does — heist `Chaining` and map
`Chaining` are the same sentence — but the alert pair makes the *sets* different, so `pool_groups`
folds only **one** group across the two domains (`Area has patches of Burning Ground`, where a
legacy heist row grants nothing else). Rating a map's `Chaining` therefore does not rate a
contract's. That is arguable either way and was left as the data has it: the two affixes genuinely
differ, and the alternative is dropping stats from a set to make keys collide, which is a lie about
what the affix is. If it is ever revisited, revisit it in `pool_groups`, not in the bundle.

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

## What the bundle gained — **built**

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
user can copy) or `Map2Tier` (legacy map series) are left out of the emit. 55 wording-sets, and
five more go with them under a second rule the build found rather than designed: an entry whose
every wording carries GGG's own **`[DNT]`** marker (a Sirus modifier and the expedition chest
counters) is developer content the client does not show — the one case where the data says
outright that nothing prints. This is a naming convention rather than data, it is allowed to be
imperfect, and the cost of a mistake either way is one row in a searchable list.

**2. Emit what is already fetched.** `Mods.Domain`, `Mods.GenerationType` and
`BaseItemTypes.ModDomain` are all downloaded today and dropped at emit time.

- **`domain` on base-type records**, straight from `BaseItemTypes.ModDomain`. One integer, exact
  for a base that is one — and **the "Map" record is not one**, which is the correction this
  section needed most. Trade lists all 491 maps under a single entry whose game row is
  `Metadata/Items/TradeProxy/MapKey`, a stand-in sitting with the stackable currency in domain 43.
  Emitting that would tell the app every map rolls from the currency pool. So a trade proxy (21
  rows) states **no** domain, and **`domain` on item-class records** answers instead, emitted only
  where every base of that class agrees on one: 75 of 86 classes, including all 511 rows of
  `Maps`. Ask the base first, its class second, and nothing is ever guessed — a class holding
  genuinely different things (Jewels covers two domains) publishes none, and only a base can
  answer for those.
- **A new optional asset, `en-mod-pools.ndjson`**, plus an fnv1a32 index over each entry's
  normalized wordings, built with the same machinery as `en-stats-matcher.index.bin`. Named for
  the general case and carrying its `domain` per record, because the settings page below is
  pool-agnostic and flasks, abyss jewels and idols are the same shape. Seeded with domain 5 and
  domain 39; nothing else, until something asks. **The index key is `{domain}::{wording}`**, not
  the wording alone: a map and a chart word 42 modifiers identically and are separate pools, so an
  answer mixing them would offer a chart's affix for a map.
- **`source.mod_pools`** in the manifest, written only when non-zero. Unlike
  `source.exchange_items`, which it was modelled on, it is **not** what gates anything: this is a
  whole file, so its absence already says "no data" the way `en-unique-mods.ndjson`'s does, and
  `has_mod_pools()` reads the index. It is written through so the installed bundle records what
  the build produced.

**Which generation types.** The 207 wording-sets counted above are domain 5's prefixes and
suffixes, which is the pool this design is sized around — but the scope decision at the top puts
logbooks, invitations and charts in it too, and those roll from generation types of their own. The
emit takes every type a player **rolls**: prefixes and suffixes, the Vaal corruption implicits,
the legacy Tempest set, and what an expedition logbook, a memory altar and a chart's voyage grant.
It leaves out domain 5's generation 3 — the fixed implicit a base simply has, 545 wordings nobody
rolls and nobody would rate. Domain 39's single generation-3 row is kept, because "Voyage Modifier
will be revealed once Charted" is what an unsailed chart prints *instead of* the modifier, so it
is the only rateable thing on one. **270 entries** over 897 mod rows:

| domain 5 | | domain 39 | |
| --- | --- | --- | --- |
| 1 prefix | 83 | 1 prefix | 17 |
| 2 suffix | 73 | 2 suffix | 17 |
| 5 Vaal corruption implicit | 13 | 3 the promise of a voyage modifier | 1 |
| 8 Tempest / Eclipse (legacy) | 8 | 37 voyage | 10 |
| 23 expedition logbook | 15 | | |
| 36 memory altar | 33 | | |

One record is one **wording-set**, not one mod row, because tiers collapse:

```json
{"domain": 5, "gen": 2, "name": "of Impedance", "tiers": 3,
 "mods": ["MapMonstersHinderOnHitMapWorlds", "MapMonstersHinderOnHitMapWorldsMaven",
          "MapMonstersHinderOnHitMapWorldsExpedition"],
 "stats": [{"ref": "Monsters have #% chance to Hinder on Hit with Spells",
            "trade": "explicit.stat_962720646", "min": 100, "max": 100}]}
```

`name` is `Mods.Name`, the affix name the client prints with Advanced Mod Descriptions on, and it
is free — absent where the rows disagree is not a case that arises, but where they *do* disagree
(7 sets, "Twinned" and "of Twinning" wording the same thing as a prefix and a suffix) the most
common one wins and the build reports the count, because the name is decoration and the wording
under it is not. `mods` is provenance, for a debug log that has to explain itself, and it lists
every row behind the wording including a side-area twin that shares it. `min`/`max` span the
lowest and highest tier **in displayed units with `dp` applied**, exactly as `unique_mods` already
emits ranges — `Mods.dat` stores hundredths and milliseconds raw and leaving that to the client is
a silent factor of 100. They are absent together for a wording that prints no number, which a
reader must not confuse with bounds it failed to parse. The ranges are not decoration: they are
what lets a pasted regex be tested against a rendered line rather than against a placeholder.

**A stat entry with no `trade` id is the ordinary case here**, not a gap — 85 of 371 wordings.
Trade indexes many map affixes under no hash at all, and where it indexes one under two the build
refuses to pick, exactly as everywhere else. The pool is rated, not searched, so it costs the
entry nothing.

One thing fixed on the way through, because it was in the path: a description's `[id|Label]`
markup is now rendered to the label **before** the wording is looked up. Trade indexes the printed
form, so the markup was what stood between such a wording and its stat record — `Rare Monsters
have [PhysicalThorns|Physical Thorns] reflecting # Physical Damage` reaches a real trade id now,
as do 12 pool wordings and 10 that had been carrying the markup into `en-unique-mods.ndjson`.

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

## The client log cannot say which character is playing

0.7's second "might" — switch profile by watching the client log — is **not deferred. It cannot be
built**, and the evidence is here so it is not proposed again next league.

The log is `logs/LatestClient.txt` beside the game's executable, one file per launch, and
`logs/Client.txt` is the same lines appended forever. **Selecting a character writes nothing named
to either.** A launch that goes through character select into the game reads, whole:

```text
[SCENE] Set Source [(null)]
Got Instance Details from login server
Connecting to instance server at <ip>:6112
Client-Safe Instance ID = <n>
Generating level <n> area "<id>" with seed <n>
: You have entered <Area>.
```

Checked against a full 1h13m session on 3.29.2, from `***** LOG FILE OPENING *****` through login,
character select and two zones: the character's name appears in it **zero times**.

Three line shapes in the whole 48 MB history do carry a character name, and each names other
players as readily as you:

- `<Name> (<Class>) is now level <N>` — also fires for party members
- `<Name> has been slain.` — same
- `#<>GUILD<> <Name>: text` — a channel line, identical whoever typed it

Nothing distinguishes the local character from anyone else in the party or the channel, so there
is no signal to key a profile on. State outside the log is no better: the game's
`production_Config.ini` leaves `account_name` empty, and its minimap cache names files by hash.
The one deterministic hook is whispering yourself, which prints `@To <YourName>:` — a name that
can be trusted, at the cost of the user typing a whisper after every character switch, which is
worse than the combo box they would otherwise use.

So the profile in use is the one the user picked, and remembering it is the whole of the feature.
Asking GGG's account API instead is not an alternative worth having: it needs a session cookie,
and this program deliberately holds no account credentials — see [../PRIVACY.md](../PRIVACY.md).

## What the app gains

**`src/data` — built.** `en-mod-pools.ndjson` and its index load exactly as the optional datasets
already do, with a `has_mod_pools()` gate mirroring `has_unique_mods()` / `has_unique_bases()` — a
bundle published before this asset existed keeps working, which is most of why the gate exists
rather than a null check. `BaseType` gained `mod_domain` and `ItemClass` gained one too. Nothing
in it pulls SDL, ImGui, X11 or libcurl into `ppc_core`; the pool is data and is tested headless.

Three entry points, and [data-layer.md](data-layer.md) owns them now:

- `mod_pool(domain)` — the whole pool, for the settings list. One pass over the file the first
  time it is asked, memoised; a few hundred records, and a pool is only ever wanted entire.
- `find_pool_mods(domain, wording)` — the other direction, from a wording resolved off an item,
  through the `{domain}::{wording}` index. Empty is normal and never a gate.
- `mod_domain_for(base, item_class)` — which pool the item in hand rolls from, base first and its
  class second. **Use this, not `BaseType::mod_domain`**, for the trade-proxy reason above: a
  map's own record states no domain at all.

**The pool browser in Settings — built, and pool-agnostic.** `map_check_tab` reads
`mapcheck::kDomains` and nothing else knows the number 5, so the same page serves 246 flask mods,
511 abyss jewel mods or 552 idol mods the day one is published. Heist was the first test of that
claim and it held: adding domain 22 was one entry in `kDomains`, one `is_heist()` line in the
fallback, and no change to this page at all.

The page is **rarely opened** by design. The table fills in by playing; this exists for the one
session where somebody sits down to pre-fill it. So the row carries **four buttons rather than a
click that cycles** — the popup does the cycling, because there the target is the modifier and the
rows are few; here the job is putting a particular entry into a particular state and one click to
any of them is worth the chrome.

**A verdict is keyed on the affix — its whole set of wordings — not on any one wording.** 21 of
the pool's wordings sit on more than one affix (`Monsters cannot be Stunned` is granted by
`Unwavering` and by `of the Juggernaut`) and 50 affixes grant more than one, so a verdict per
wording cannot tell two decisions apart: rating `Impaling` also rated the `#% more Currency found
in Area` it shares with fourteen other affixes, and they all changed on screen untouched. The key
is the sorted set of stat `ref`s, and a shorter key speaks for the affixes that contain it only
until they are rated in their own right, when the longer key wins for being the more particular
statement. A one-wording affix keys as that wording alone, so a table written before this reads
correctly rather than needing a migration; the file now stores each row as a `mods` array, because
a set is not something a JSON object can be keyed by and joining them would make a hand-editable
file unreadable.

**One affix is one row, however many pools grant it.** A map and a chart word 42 modifiers
identically and roll them from pools of their own, so `Resistant` arrives as two entries differing
only in range — `10-25` chaos on a map, `0-40` on a chart. The verdict key is the sorted ref set
with no domain in it, so those two can never hold different verdicts: one click lit both, and the
list was showing 39 decisions as 82 rows. `pool_groups` collapses them, **360 entries to 315 rows**,
and the row draws the entry from the first domain in `kDomains` — a map's, which is what the reader
is nearly always deciding about. A search is still asked about every entry in the group, so a term
naming a number hits if either pool's range would print it. Of the 40 groups holding more than one
entry, **35 are a map and a chart, 4 are two entries of one pool** (domain 5 words `Area has
patches of Burning Ground` as both `of Flames` and `of Fire`) **and 1 is a map and a contract** —
the heist pool barely folds in, for the data reason given above, and the mechanism did not have to
change to say so.

Putting the domain into the key instead was the alternative and was rejected: it is a format change
to every profile file, and it would make rating a map's `Resistant` stop speaking for a chart's,
which is not a distinction anybody asked for.

**And the propagation is drawn, not just obeyed.** A row whose verdict is lent by a shorter key
lights that verdict faintly rather than showing nothing — three strengths of lit on the row: solid
for a verdict set here, brighter for one a pending proposal would write, faint for one inherited,
with a tooltip on the faint one saying where it came from. The rule was invisible before, and a
rule the user cannot see is one they discover by being surprised on a map. Pressing a button still
writes this affix's own verdict and nothing else, so no control ever moves except by being pressed
— which was the whole argument for showing `exact()` alone, and it survives intact.

Grouping a map's printed lines back into affixes is what **Advanced Mod Descriptions** supplies —
the parser marks the second and later stats of one affix `continuation`. Every map capture we hold
has it on. Without it each line stands alone, which still rates every single-wording affix and is
why this degrades rather than fails.

**Known gap.** An affix can grant stats the item never prints — `#% more Currency / Maps / Scarabs
found in Area` is on every Nightmare-map modifier and appears in none of 62 captures. `rate` asks
`pool_refs_for` to turn what a map printed into the pool entry's full set, and it takes the
smallest entry covering those lines; where an ordinary and a Nightmare affix share a name and the
Nightmare one prints no more than the ordinary one does, that resolves to the ordinary entry. So a
verdict set on the Nightmare `Oppressive` is not read back off a Nightmare map. Ordinary affixes
are unaffected. The fix is a discriminator the item does carry — a Nightmare modifier prints no
`(Tier: N)` — and it is not built.

Rated entries are **not** sorted to the top, which the design asked for. The search box turned out
to be the whole of what makes the list usable and a list that reorders itself under a click being
used to rate things is worse than one that does not.

## Seeding from a pasted map search string — **built**

**GGG publishes no grammar, but the community has written one down.** The reference is the wiki's
[Guide:Regex](https://www.poewiki.net/wiki/Guide:Regex), and it agrees with the convention
[roadmap.md](roadmap.md) fixed before any of this was written: an unquoted space is a logical AND,
double quotes group a term containing spaces, and a leading `!` negates — inside the quotes or
outside them, since generators write `"!a|b"` and players type `!"a b"`. Two things the wiki settles
that the roadmap had only assumed: **quotes group, they do not escape**, so a `|` inside them is
still an alternation and not a literal; and **every search field in the game takes the same syntax**,
which is why a string kept for the stash tab works here.

**The terms really are regular expressions** — `\d+ e` is a digit run before a space and an `e`, and
`ll damage$` anchors to the end of a printed line. What is not a regex is the *string*: the quotes,
the spaces and the `!` are the syntax holding the patterns apart, and handing the whole of it to one
engine is how those end up matched as literal text. So the string is tokenized first and each term
goes to `std::regex` as it stands, in its ECMAScript dialect. The game's is a custom engine, so the
two can disagree on a corner — lookbehind is the known one, ECMAScript having none — and where they
do it costs a *proposed* verdict the user is about to accept or reject anyway.

**A term that will not compile hits nothing**, and this was the other way round first. Falling back
to the literal text a broken pattern is made of let one box serve a pasted regex and somebody typing
two plain words — at the price of the box being two search languages at once, with nothing on screen
saying which one a given term had got: `Damage (` found a substring and `Damage (Fire|Cold)` found a
pattern. One language. An unfinished pattern showing an empty list is what the game's own box does,
and `\(` is what a player who meant the bracket writes.

The `?` beside the search box says all of this in six examples, because the syntax is the game's own
and somebody who does not already know that will not go looking for a paragraph to tell them.

**Two semantics the design had left open, and the wiki decides one of them:**

- **Any wording hits, and each is tested on its own.** Not the lines joined: the wiki documents `^`
  and `$` as anchoring to a *printed line*, so a join would put that anchor somewhere no term's
  author has ever seen. This was built on that reasoning before the reference was read; it is no
  longer a judgement call.
- **The affix name is in scope.** With Advanced Mod Descriptions on it is a line of the tooltip the
  game's own search reads, so a term naming one was written knowing it is matchable.

**A term that asks about the item is set aside.** The syntax has keywords — `ilvl:84`,
`"rarity: rare"`, `ts:`, `"item level: 78"` — and they are questions no modifier wording can answer.
Left in the AND, one of them empties the list and never says why: `ilvl:84 monster` matched **0 of
270** entries before this and matches 136 after it. They are recognised **by shape, not by a list of
the keywords**: a term is item-scope when it opens with `<word>:`, and no wording in the published
pool contains a colon at all. The bare keywords are deliberately left as literal text, because the
wiki's own list is partial and the words are real modifier wordings — `currency` alone appears in 17
of them and `corrupted` in two, so honouring them as keywords would silently swallow the searches
most worth typing. The row under the search box says how many terms were set aside.

**And one the design did not see coming: filtering and proposing cannot read the terms the same
way.** The game ANDs them because it is deciding about a whole *map*, where each term can be
answered by a different modifier on it. The subject here is one modifier, and a modifier cannot
satisfy two unrelated wanted terms at once — so `classify` asks each term separately (which is also
what ROADMAP.md promises: "every modifier an excluding term hits is proposed dangerous, every one a
wanted term hits proposed safe") while `matches`, which narrows the list, keeps the game's rule so
that typing two plain words means both of them. Two methods, two jobs, and the header says which
is which. Both read the same lines: every wording the affix prints, at both ends of its range, and
its name.

The wiki's own worked example is the clearest case of why. It advises writing a search in CNF —
`"oj|r at|m el|r damage$|poss|ra c|haz" "fier$"` is *any of these seven affixes* **and** *an
additional modifier* — because that is the only form the game's AND can express. Run over the pool
that string filters to **0 of 270**, correctly: no single modifier is both. Asked term by term it
proposes **21 safe**, which is what its author meant by it.

This is where the pool's `min`/`max` earn their place: the wording is rendered into printed text
before a term meets it, because a term like `\d+ e` was written against a tooltip and can never
match a placeholder. A wording with no bounds prints no number at all and is left alone.

**Both ends of the range, and the wording the game would actually print.** The rule started as "the
top of the range", on the grounds that these strings are written to catch the roll that ends a map.
It was wrong twice over.

A stat record carries alternative wordings, and one may be flagged `negate`: the same stat said the
other way round, for a roll below zero. `Players have #% more Defences` rolls `[-30, -25]`, so the
game *always* prints `Players have 30% less Defences` — while the pool rendered `-25% more
Defences`, a line no player has ever seen and no search term was ever written against. **16 of the
pool's wordings are in that position**, and they are disproportionately the ones a hardcore string
is about: `of Miring`, `of Smothering`, `of Rust`, `of Impotence`, `of Fatigue`, `of Imprecision`,
`of Congealment`, `Hexwarded`, `of Revolt`. It was found by a search string whose `s def` term
silently caught nothing; with `printed_wording` consulting the record's matchers, the same string
now names 24 modifiers instead of 23, and the settings list shows the wording the reader is rating.

And once a wording can be negated, "the top of the range" stops meaning anything — the top of
`[-30, -25]` prints as the *smallest* number that wording can say. So both ends are rendered, and a
term naming a number hits if any roll would say it.

**This never touched a verdict.** The popup keys on the stat record's `ref`, and the matcher
resolves the printed `less Defences` line back to that same `ref`, so a rating made in Settings has
always been read correctly off a real map. What was broken was finding the modifier and being shown
what it says — which for a screen whose whole job is *rate this wording* is bad enough.

**A proposal is per affix, and getting there took two wrong answers.** A pool entry is an *affix*,
and an affix can grant several stats at once: `Protected` is elemental resistance, physical damage
reduction, chaos resistance **and** `#% more Maps found in Area`.

The first answer rated the whole entry on one term's hit and wrote the verdict **per wording**, so
`ter e` matching the resistance line marked more-maps-found deadly on its own — deadly thereafter on
every map that rolls it, from any affix at all. Over the roadmap's example string that is 38
wordings rated where 23 were named.

The second answer kept the per-wording key and asked `classify` once per stat, which fixed that
example and left the real fault standing: a verdict keyed on one wording is a **one-element key**,
and the propagation rule below says a key that short speaks for every affix whose wordings contain
it. Accepting a proposal therefore reached across the pool exactly as far as before — the same bug,
now arriving through the rule that was supposed to be the feature.

The key is the affix, so the proposal is too. `classify` is asked once, about every line the entry
prints and its name, and what it decides is written under the entry's whole sorted set. `Protected`
becomes one four-wording row; `#% more Maps found in Area` on its own is untouched, and stays
untouched on the fourteen other affixes granting it. This is also what makes the preview honest:
the rows the list lights are the keys Accept writes, one for one.

**The import proposes and the user confirms.** Pressing the button writes nothing: the list becomes
the proposal's own rows, each lit with the verdict it would get, under a bar saying how many of each
and an Accept that is the only thing which writes. It lights the individual wordings it would rate,
so the preview is literally what Accept writes. Measured against
the roadmap's own example string over the published pool: **23 deadly** out of 270 entries, 23
wordings rated.

**And the limit worth stating.** A term can be aimed at item text that is not a modifier at all —
the rarity line, `Corrupted`, the map's name — and against a pool of modifier wordings it will
either miss or hit something by accident. In that same example string the trailing `pte` proposes
two modifiers safe, both because `Corrupted` contains those letters; the term was plainly written to
find corrupted *maps*. Nothing can tell that apart from a term that meant it, which is the whole
argument for the import proposing rather than writing.

## Order of work

Each phase is shippable and reversible on its own, and the first two are the "pre-fire" the feature
sits on.

1. **Data repo — done.** Emit `domain` on base records, fix the invitation base-row pick, emit
   `en-mod-pools.ndjson` + index + manifest count. No new columns are fetched, so this was an emit
   change and a data release. The app ignores assets it does not know, so it ships safely ahead of
   any app change. Two things the plan did not have: a trade proxy states no domain and its item
   class answers instead, and the `[DNT]`/markup handling above.
2. **App plumbing, no visible feature — done.** Load and gate the pool, add
   `BaseType::mod_domain`, extend the test bundle slice per [testing.md](testing.md). Nothing on
   screen changes.
3. **Settings: the pool browser and the verdict store — done**, with `PRIVACY.md`.
4. **The feature — done.** Hotkey, popup, worst-verdict-leads, rate-on-the-spot, profiles.
5. **Search-string import — done**, rather than deferred: the same matching the list's own filter
   box needs, so the button on top of it was the cheap half.

## Open, and needing an answer rather than a guess

- **Do atlas-side modifiers print on the map item?** If they do, domain 11's 145 rows belong in the
  pool so they can be rated like anything else. This wants a **capture**, not reasoning — the same
  rule every number in this project is held to.
- **The two import semantics above** — any-wording-hits, and whether the affix name is matchable.
