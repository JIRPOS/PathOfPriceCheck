# Per-unique modifier data (`en-unique-mods.ndjson`)

Reference for the dataset behind `item/plan`'s `apply_unique_mods`. Points 1, 2, 4 and 6 below
are **built**: pool membership decides what a unique's search asks for, and the app degrades to
what a printed range can prove when a record is absent. Points 3 (offering the pool modifiers the
item does *not* have) and 5 (unidentified uniques) are **not**, and stay listed in
[docs/roadmap.md](docs/roadmap.md). Produced by
[PathOfPriceCheck-Data](https://github.com/JIRPOS/PathOfPriceCheck-Data) -
`builder/ppcdata/emit/unique_mods.py` and `builder/ppcdata/sources/wiki.py`.

## The problem it solves

A unique's modifier can be **variable without printing a range**. Ralakesh's Impatience rolls
one of three charge modifiers, each `1..1`. A Watcher's Eye picks two or three mods out of 93. A
synthesised Circle of Anguish picks Herald of Ash mods out of a pool of five. The clipboard
prints such a mod exactly like a fixed one, so `item/plan`'s `Strategy::Unique` currently leaves
every fixed-looking mod out of the search and says so in a note - and that is the difference
between a 1-chaos listing and a 100-divine one.

This dataset says, per unique, which mods it always has and which it picks from a pool.

## What is in the bundle

| asset | key | what |
|---|---|---|
| `en-unique-mods.ndjson` | - | one record per unique, ~1.5 MB, 1,413 records |
| `en-unique-mods-name.index.bin` | `UNIQUE::{name}` | fnv1a32, same reader as every other index |
| `en-items-base.index.bin` | `UNIQUE::{unique.base}` | uniques that drop on a base, over `en-items.ndjson` |

Both indices are the existing `data/index` format - a hit is a *run*, walked and re-verified,
exactly like `find_bases`. The name key is the same string `find_bases(Namespace::Unique, name)`
is built from, so the lookup is by the name the clipboard already gave us.

## Record shape

```json
{"base": "Prismatic Jewel", "name": "Watcher's Eye",
 "fixed": [{"mod": "IncreasedEnergyShieldPercentUnique__2_",
            "filters": [{"range": [[4, 6]], "ref": "#% increased maximum Energy Shield",
                         "tradeId": "explicit.stat_2482852589"}]}],
 "pools": [{"count": [2, 3], "hint": "Two or Three random aura modifiers",
            "mods": [{"mod": "AngerIncreasedFireDamage",
                      "filters": [{"range": [[40, 60]],
                                   "ref": "#% increased Fire Damage while affected by Anger",
                                   "tradeId": "explicit.stat_3337107517"}]}]}],
 "unlisted": ["One to three random Synthesis implicit modifiers"]}
```

- **`name`**, **`base`** - trade's own strings. `name` matches the unique's name line; `base` is
  the same value as `unique.base` on the item record.
- **`fixed[]`** - mods every copy of the item has.
- **`pools[]`** - one group per pool. `mods[]` is the full pool; `count` is `[min, max]` of how
  many actually roll, **absent when the source does not state it** (27 of 51 groups) - treat that
  as "at least one, unknown", never as "all of them". `hint` is the source's prose, fit to show
  in the UI. `implicit: true` marks a pool of implicits (a synthesised unique).
- **`unlisted[]`** - a pool stated in prose but not enumerated, e.g. the general Synthesis
  implicit pool. Nothing to search; it exists so the app can say what it is leaving out instead
  of implying the item has nothing more.
- **A pool of one modifier** is not a mistake. Some modifiers roll a **name** rather than a
  number - The Dark Monarch doubles the limit of one of sixteen minion types, Replica
  Dragonfang's Flight raises one of 287 skill gems, Forbidden Shako supports one of 164 support
  gems in one of four slots. The source calls these fixed, and it is right that every copy has
  the modifier; what varies is *which wording* it rendered as, which is the whole of what the
  copy in hand is worth searching for. They arrive as a pool with `count: [1, 1]` whose `mods`
  all share one `mod` id and differ by wording and `tradeId`. This is why a pool can be 656
  entries long, and why a single filter would have been worse than none: it would have claimed
  this copy rolled whichever option the game's own data happens to list first.
- **entry `mod`** - GGG's own mod id. Stable across patches, useful for debugging and for
  deduplicating; not needed to build a query.
- **entry `implicit: true`** - the mod is an implicit, so its trade id is in the `implicit.`
  namespace. Absent means explicit.
- **filter `ref`** - the canonical `#`-placeholder wording. When `tradeId` is present this is a
  `ref` in `en-stats.ndjson` and `find_stat_by_ref` resolves it. When `tradeId` is absent it may
  be the client's wording only, so **do not assume it resolves** - it is there to render.
- **filter `tradeId`** - the ready-to-use stat hash. **Absent means not searchable** (see
  Limitations); the mod is still real and should still be displayed. Use it **verbatim** and do
  not re-derive its namespace from `implicit`: 41 filters are legitimately `sanctum.*` (Sanctum
  relic mods) and one is `enchant.*` (a talisman enchantment), because trade indexes those items
  outside `explicit.*` entirely.
- **filter `range`** - one `[min, max]` per stat the wording covers. Two entries for
  `Adds # to # Fire Damage`; one otherwise. **Already in displayed units** - `Mods.dat` stores
  hundredths and milliseconds raw and the stat's `dp` has been applied, so these compare directly
  against what `stat_matcher` reads off the clipboard. One mod can have several filters when it
  grants several unrelated stats.

## How the app should use it

**1. Pool membership replaces the range test.** *(built)* `Strategy::Unique` used to enable a roll
only when a printed range proved it variable; it now also enables one whose stat id is in a
`pools[].mods` filter for this unique, and labels the row with the pool's `hint`.
`Modifier::added_unique()` keeps working as it does - an added mod is not in the unique's own lists.
Nothing in the join ever *disables* a filter: the item's own printed range outranks a record about
the unique in general.

**2. Join on the trade id, not on text.** *(built)* The clipboard line already resolves through
`stat_matcher` to a `Stat` whose `trade.ids` holds the id for the mod's type; compare that against
filter `tradeId`. Matching on `ref` looks equivalent and is not: 21 wordings are shared by two
stat records, which is exactly the case the ids disambiguate.

**3. Offer the pool mods the item does *not* have.** *(not built)* This is the point of the dataset and the one
thing text matching can never do: a Watcher's Eye search is worth little without being able to
add "and also has Discipline energy-shield-on-hit". `ref` gives the wording to show, `tradeId` the
filter to send, `range` the bounds to seed the min/max with.

**4. Ranges are a roll-quality source that does not depend on the user's settings.** *(built, with a
guard)* The clipboard only prints `(20-30)` when Advanced Mod Descriptions is on; `range` gives the
same bounds regardless, and `StatFilter::unique_min/max` carry them to the UI. The guard: a range
is only trusted when it **contains the roll**. `dp` is missing from some stat records, which emits a
range 100× the roll it bounds (`0.4% of Physical Attack Damage Leeched as Mana` against `40..40`),
and a legacy roll genuinely sits outside its own range. Neither describes the item in hand.

**5. Unidentified uniques.** *(not built)* `en-items-base.index.bin` turns the base the clipboard states into the
candidate uniques; this dataset then gives each candidate's mods to show while the user picks. That
is the whole "Unidentified uniques" gap, minus the art.

**6. Absent record is normal.** *(built)* 43 of trade's 1,456 unique names have no record, and the
source lags a league launch by days, so a brand-new unique will not be here. The app degrades to
what a printed range can prove and says so - and `GameData::has_unique_mods()` distinguishes "this
unique has no record" from "this bundle predates the dataset", because the two read differently to
a user. Never a wrong filter.

## Limitations, stated plainly

- **No `tradeId` on some filters.** Of 11,327 filters, 793 carry no id: 434 wordings resolve to
  two different trade ids (the known ambiguous-wording problem) and 359 to none at all. Both are
  emitted with `ref` and `range` and no id, so a pool list still matches the count its `hint`
  states. Display them; do not search them.
- **394 mods are dropped entirely** because every stat they grant is unsearchable - cosmetic
  footprints, hidden behaviour. A pool list can therefore be shorter than the pool the game rolls.
- **Foulborn is not here.** It is Chayula currency, absent from both the client tables and the
  wiki's pools. Identification already works off the clipboard's `{ Foulborn Unique Modifier }`
  info line; only the roll's range stays unknown.
- **Forbidden Flame / Forbidden Flesh** get a filter with a wording but no `tradeId`: trade
  searches them through an option stat over ascendancy notables, which is not one of the two
  name tables the expansion above reads. The same shape, a third table.
- **Counts can be missing or coarse.** Precursor's Emblem describes several sub-pools in one hint
  and the source gives no way to tell which mod belongs to which, so it gets pool groups without
  counts.

## Where it comes from, and the one obligation attached

The grouping is **not in the game client** - verified by enumerating all 1,205,200 paths of patch
3.29.1.2.2: the client ships names, art, stash layout and jewel limits per unique and nothing that
groups mods under one, because mod assignment is server-side. `Mods.dat` does carry all 15,886
unique-generation mods with stats and ranges, so only the grouping was missing.

It comes from **poewiki's `item_mods` cargo table** as an id → id edge list; every stat, range and
trade hash above is client- and trade-API-derived. Wiki content is **CC BY-NC 3.0**, so
**attribution is required**: the manifest carries it in `source.unique_mods_attribution`, `install`
writes it through into the installed bundle, and Settings renders it under the bundle version as
*"Unique modifier data from poewiki.net, CC BY-NC 3.0"*. It is read from the bundle rather than
hardcoded, because it describes the data that is installed. Non-commercial is not a constraint this
project has trouble with.
