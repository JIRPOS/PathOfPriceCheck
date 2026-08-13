# Static game data (built)

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

`src/data/` turns clipboard text into things the trade API understands. It is fed by a bundle
downloaded at runtime from **[JIRPOS/PathOfPriceCheck-Data](https://github.com/JIRPOS/PathOfPriceCheck-Data)**
— nothing is baked into the binary, so a league only needs a data build.

- **`data/updater`** fetches `releases/latest/download/manifest.json` at startup on a worker thread,
  downloads and sha256-verifies each asset, installs, and maps it. Deliberately *not* the GitHub
  API: unauthenticated `api.github.com` allows 60 requests an hour per IP. It is re-checked while
  the application runs; **when** is `App::refresh_checks`, in
  [architecture.md](architecture.md).
- **`data/install`** writes a fresh `<cache>/data/<version>/` directory and flips a one-line
  `current` file by rename. **Never write over a live bundle** — Windows will not replace or delete
  a memory-mapped file. Superseded directories are reclaimed by `prune()` at the next startup,
  before anything is mapped. `manifest.json` is untrusted input: asset names are restricted to a
  flat `[A-Za-z0-9._-]`, https is required, and sizes are capped.
- **`data/game_data`** memory-maps the ndjson and parses records on first hit, so a loaded bundle
  costs about a megabyte resident and no startup time. Lookups go through published fnv1a32 indices
  (`data/index`); a hit is a *run*, because colliding keys are kept and re-verified.
  `en-unique-mods.ndjson` and its name index are **optional**: bundles published before that dataset
  existed simply do not have them, and `has_unique_mods()` is what tells "this unique has no record"
  apart from "nothing here has one". The wiki attribution it is licensed on rides in the manifest's
  `source.unique_mods_attribution` and is written through by `install`, because an attribution that
  stays behind in the publisher's repo is not an attribution — Settings renders it.
  `en-items-base.index.bin` — base → the uniques that drop on it, which is all an
  **unidentified** unique states — is optional in the same way, and `has_unique_bases()` is what
  tells a base nothing drops on from a bundle that cannot be asked.
  **`source.exchange_items` rides the same path** and is read back as `has_exchange_flags()` — the
  bundle-level signal saying whether `BaseType::exchange` means anything, because unlike a whole
  missing file an absent boolean cannot tell "no data" from "no". `install` writes it only when
  non-zero, since a 0 would claim the opposite of what it means. See the currency-exchange section.
  `en-mod-pools.ndjson` and its index are optional in the same way `en-unique-mods.ndjson` is —
  see the mod-pool section. `source.mod_pools` is written through beside the other two so the
  installed bundle records what the build produced, but it is **not** what gates anything: a
  whole missing file says that already, and `has_mod_pools()` reads the index.
- **The mod pools** are the one thing here that does not start from an item in hand.
  `en-mod-pools.ndjson` is, per **mod domain**, every modifier that domain can spawn — the whole
  set, whether or not anything is holding one. It exists so a modifier can be rated in Settings
  before it has ever been rolled; see [map-check.md](map-check.md), which owns the feature.
  **The pool describes and never gates.** What it lists is what spawns *naturally*, which is
  strictly less than what an item can print: an essence, a craft, a veiled mod or Harvest all put
  modifiers on an item whose weights would never have produced them, and the published list is
  trimmed by naming conventions besides. A printed modifier no entry covers is normal, renders as
  it always did, and is rateable on the spot. Nothing may use a pool to reject a line, hide one,
  or decide it failed to parse.
  One record — `PoolMod` — is one **wording-set**, not one roll: the tiers of an affix print the
  same wordings and a verdict attaches to a wording, so they collapse, and `min`/`max` span the
  lowest tier's floor to the highest tier's ceiling in displayed units. `tiers` and `mods` are
  provenance for the debug log. A `PoolStat` with no `trade_id` is the ordinary case and not a
  gap — the pool is rated, not searched, and a wording trade indexes under two hashes is one the
  build refuses to pick between, here as everywhere else.
  Two lookups, because the feature needs both directions: `mod_pool(domain)` is the whole pool,
  for the settings list, filled by one pass over the file the first time it is asked for (a few
  hundred records, and a pool is only ever wanted entire); `find_pool_mods(domain, wording)` goes
  the other way, from a wording resolved off an item, through an index keyed on
  `"{domain}::{wording}"`. **The domain is part of the key**, because a map, a chart and a heist
  area are separate pools — a map and a chart word 42 modifiers identically, and 58 of the heist
  pool's 90 entries word their printed stat as a map's does — and an answer mixing them would offer
  a chart's affix for a map.
  **Which domain an item rolls from is `mod_domain_for(base, item_class)`, not
  `BaseType::mod_domain`.** The base is asked first and its class answers where it cannot, and
  that fallback is not a nicety: trade lists all 491 maps under one entry whose game row is a
  *stand-in* sitting with the stackable currency in domain 43, so a map's own record deliberately
  states no domain at all and the `Maps` class is what knows the answer is 5. The other way round
  would be wrong — a class holding genuinely different things (Jewels covers two domains)
  publishes none, and only a base can answer for those.
- **`data/lexicon`** is every word the *client* prints, for one language: the section labels
  (`Item Class`, `Rarity`, `Requirements`, `Sockets`, `Note`), the flag lines, the rarity and
  influence names, the mod-type suffixes and Advanced Mod Descriptions generation words, the
  property labels, the usage-note needles, the base-line decorations (`Superior `,
  `Blighted Map`, ` (Tier `, `Foulborn `, `Vaal `) and the chart shapes. `item/parse` used to
  compare each of those against a literal, which is the whole of why the app reads an English
  client and nothing else.
  **It is game data, so the bundle is the authority** — `<lang>-lexicon.json`, read by
  `GameData::open` and reachable as `GameData::lexicon()`. The English table is *compiled in*
  as the default because every bundle published so far carries no lexicon at all, and a
  lexicon **overlays** rather than replaces: an entry it does not name stays English, so a
  partial translation degrades to English instead of to a blank rule that matches every line.
  `has_lexicon()` is what says which of the two happened. Three shapes, and the difference is
  load-bearing: `Term` is a single string, a `TermList` is a set (the fixed-order ones are
  **indexed by an enum** — `Rarities` by `item::Rarity`, `ModSuffixes` and `Generations` by
  `ModType` — so a language's list has to keep the order and a replaced list is replaced
  whole; five of them are ordered by a **trade filter key or option id** instead — `ChartShapes`,
  `UltimatumChallenges`, `UltimatumRewards`, `HeistJobs` and `HeistObjectiveValues` — which is what
  lets the client's own words be sent as the id the site wants), and the property and item-class
  tables are keyed the other way round, printed label to key, so a translated one replaces the
  English outright.
  **Three entries here are not the client's wording at all**: `Term::SanctumEffectPrefix`, the
  `Has ` a sanctum boon or affliction's *stat* is worded with, where the item prints the name
  alone under a `Minor Boons:` label; and `LogbookFactionPrefix` / `LogbookAreaPrefix`, the
  `Has Logbook Faction: ` and `Has Logbook Area: ` an Expedition Logbook's destination is
  searched under, where the item prints the two as bare names in a block of their own. They live
  here because a translated bundle translates the stat along with everything else and the lookup
  is by exact wording, so they are per-language in the same way the rest of this table is — and
  because keeping the join in the lexicon is what keeps a compiled-in list of the four expedition
  factions out of the parser. See [strategy-logbook.md](strategy-logbook.md).
  **An empty entry never matches**, deliberately: `starts_with("")` is true of every line, and
  `ModType::Explicit` has no generation word of its own.
  `parse_item` and `looks_like_item` **take a lexicon and have no default**. The language is
  the one input this layer cannot infer, and a default is precisely the bug — the app would go
  on reading English after the user said their client is not. Tests state it once, in
  `tests/parse_en.hpp`.
- **`data/stat_normalize`** turns `+42 to maximum Life` into `# to maximum Life` and its fallbacks.
  **`NORMALIZATION.md` in the data repo is normative** and this must reproduce it exactly — a
  divergence does not crash, it silently mismatches a mod and returns a confident wrong price.
  `normalize_test` replays the conformance vectors shipped with every data release.
  A modifier can roll over a **list instead of an interval**, and the game prints that range the
  same way — "Maximum number of Sentinels of Purity (Animated Weapons-Holy Armaments) is
  Doubled" rolls over the minion skill gems. `scan_numbers` cannot see it: there is no number in
  front of the parenthesis to carry the bounds. `strip_named_ranges` drops it, and the roll is
  the name left in the wording, which is what trade indexes. It only ever **adds** candidates —
  a wording whose parenthesis is genuinely part of it, `Unique Monsters (Blood-Filled Vessel)`,
  is enumerated as printed first and never reaches the stripped form.
- **`data/stat_matcher`** joins clipboard lines into one modifier and resolves it to a stat and a
  roll. Mod type is the primary disambiguator: explicit/implicit/fractured/crafted/enchant variants
  share a wording and differ only by trade namespace.
  `kMaxModLines` is **8**, not the two a hybrid needs: a modifier that enumerates its alternatives
  is as long as its list — Bound Fate's "Every # seconds, gain one of the following" is seven
  lines and the class-connection jewel is eight, which is the longest wording in the data. Below
  the true maximum the join can never be built and *every line* of such a modifier is reported
  unrecognised on its own. Reaching further is safe because a shorter join always wins: the loop
  returns at the first join that resolves, so the cap only bounds a failed match. Two separate negation concepts —
  `matcher.negate` (the *wording* is inverse; store the roll canonically) and `trade.inverted` (the
  *trade site* indexes the opposite sign; applied at query-build time, not here).
  An inverse wording's Advanced Mod Descriptions range is **printed high to low** —
  `64(65-60)% reduced Effect of Curses on you during Effect` — so `NumberToken::bound_min/max` are
  ordered at parse time, not taken as printed. Negating an already-descending pair leaves it
  descending, and a filter wanting at least -60 and at most -65 matches nothing.
