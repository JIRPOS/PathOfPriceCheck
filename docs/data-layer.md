# Static game data (built)

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

`src/data/` turns clipboard text into things the trade API understands. It is fed by a bundle
downloaded at runtime from **[JIRPOS/PathOfPriceCheck-Data](https://github.com/JIRPOS/PathOfPriceCheck-Data)**
— nothing is baked into the binary, so a league only needs a data build.

- **`data/updater`** fetches `releases/latest/download/manifest.json` at startup on a worker thread,
  downloads and sha256-verifies each asset, installs, and maps it. Deliberately *not* the GitHub
  API: unauthenticated `api.github.com` allows 60 requests an hour per IP.
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
  whole; three of them are ordered by a **trade option id** instead, `ChartShapes` and
  `UltimatumChallenges` and `UltimatumRewards`, which is what lets the client's words be sent as
  the id the site wants), and the property and item-class tables are keyed the other way round,
  printed label to key, so a translated one replaces the English outright.
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
- **`data/stat_matcher`** joins clipboard lines into one modifier and resolves it to a stat and a
  roll. Mod type is the primary disambiguator: explicit/implicit/fractured/crafted/enchant variants
  share a wording and differ only by trade namespace. Two separate negation concepts —
  `matcher.negate` (the *wording* is inverse; store the roll canonically) and `trade.inverted` (the
  *trade site* indexes the opposite sign; applied at query-build time, not here).
  An inverse wording's Advanced Mod Descriptions range is **printed high to low** —
  `64(65-60)% reduced Effect of Curses on you during Effect` — so `NumberToken::bound_min/max` are
  ordered at parse time, not taken as printed. Negating an already-descending pair leaves it
  descending, and a filter wanting at least -60 and at most -65 matches nothing.
