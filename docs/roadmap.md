# Implementation notes for the plan, and what is still to build

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

**The plan itself is [../ROADMAP.md](../ROADMAP.md)** — public, published on the site, and the
single statement of which version brings what and why it is in that order. Do not restate it
here. What follows is what that page has no business carrying: the code-level constraints each
version has to respect, and then the **backlog** — gaps that are known, argued and unscheduled.

## Why that order

The public page states the order and not the argument for it. Two of the choices are
load-bearing and should not be reshuffled casually:

- **The updater went first because its value compounds.** Shipped at 0.8 it would have reached
  only the people who already update by hand; at 0.3 it carries the rest of the list. It is also
  the riskiest item here, which was an argument for going first rather than against: a bad updater
  is fixed in 0.3.x builds before anything downstream depends on it. Built — it and the Windows
  installer are [updater.md](updater.md) now, not a plan.
- **The paste list precedes the map check** because both need an overlay placed at the cursor,
  which does not exist yet, and the paste list is much the simpler first consumer of it. 0.7 also
  reads its regexes from what 0.6 stores.

## Implementation notes, per planned version

### 0.4, editable filter ranges — **built**

Every constraint below held; the layer is [trade-layer.md](trade-layer.md) now, not a plan.
`StatFilter::min`/`max` are edited in a popover on the row, the slider's track beside them is
`roll_min`..`roll_max` where the game published one and derived from the number in hand where it
did not, `seed_min`/`seed_max` are what reset restores, the typed number goes through
`std::from_chars`, only the Search button sends, and nothing survives the item.

Two more changes to the same panel rode along, both of them the filter list rather than the ranges:
what a strategy leaves out is a **collapsed section at its foot** rather than nothing
(`StatFilter::hidden`, `NumericFilter::hidden`), and a linked item is searched on its **sockets and
its links**, which is a price the tool missed entirely — see [trade-layer.md](trade-layer.md) and
[item-layer.md](item-layer.md).

### 0.6, QuickPaste — **built**

Every constraint below held; the layer is [quickpaste.md](quickpaste.md) now, not a plan. The
clipboard owner is a thread with its own `Display` (`clipboard_set_text`), nothing injects
Ctrl+V, the popup is placed against a cursor sampled at hotkey time and clamped into the game
window, and the number keys are read as **scancodes** — which was the one thing the note below
did not anticipate, and the thing that decides whether the feature works outside a US layout.

One rule the plan did not have and the code now does: nine slots is a limit on the *keyboard*,
so storage is unbounded, `enabled` is what competes for a number, and the ceiling is enforced on
load as well as in the UI because `config.json` is hand-editable.

### 0.7, map check — **built**

Every constraint below was honoured; what building it changed about the design, and the two
semantics it had left open, are in [map-check.md](map-check.md).

- Reuse the price-check copy path whole, and the map strategy's existing parse and resolve.
  → [strategy-map.md](strategy-map.md)
- The verdict store keys on the stat record's id, never `placeholder_form` — 0.8 makes the
  wording language-dependent and `find_stat` already refuses to guess between shared wordings.
  → [data-layer.md](data-layer.md)
- **Verdict per stat, not per roll** — the practice this copies (map regexes) has no notion of a
  threshold and neither should the UI. The store still *parses* an optional roll bound from day
  one, because a reader that accepts both shapes costs one branch and a format migration costs
  everyone's file. Do not build UI for it.
- **A map search string is PoE's search syntax *around* regexes, not one regex**: quoted terms, a
  leading `!` for negation, space-separated terms ANDed, and bare trailing terms (`"!a|b|c" pte`).
  Each term genuinely is a regular expression — `\d+ e` and `ll damage$` mean what they look like,
  anchor included — so tokenize the string first and hand every term to the engine as it stands.
  Feeding the whole string to one engine is how the `!`, the quotes and the term boundaries end up
  matched as literal text. The wiki's
  [Guide:Regex](https://www.poewiki.net/wiki/Guide:Regex) is the written reference and confirms
  all of it; it also documents keywords (`ilvl:`, `"rarity: rare"`, `ts:`) that ask about the item
  and therefore cannot be asked of a modifier.
- **Match against a rendered wording, never `placeholder_form`.** A term like `\d+ e` was written
  against printed item text and can never match a `#`. With a map in hand the printed lines are
  right there; seeding from the full known-mod list needs the wording rendered with something in
  the placeholder, and a term that can only match a number is one this cannot honestly resolve.
- The import proposes and the user confirms. Nothing writes a verdict the user has not seen.
- The store and the profiles are `PRIVACY.md` entries. The client log is not read at all — see
  **Decided against** below.

### 0.8, client languages

- Blocked upstream: `<lang>-stats.ndjson`, `<lang>-items.ndjson` with `refName`, and
  `<lang>-lexicon.json`. Nothing in the app's schema changes. → [localisation.md](localisation.md)
- Re-keying `item/derive`'s local lists, `item/resolve`'s `kLocalWordings` and
  `ninja::narrow_by_mods` onto `Stat::ref` needs the two upstream answers in the backlog below
  answered first, **pinned to a real localised bundle**. → [item-layer.md](item-layer.md)
- `chart_area_key` moves into the bundle.
- `fonts.unicode`'s system fallback already covers the scripts Fontin does not.

### 0.9, UI language

- `ui::Msg` plus one table per language, `static_assert` on the length. Droppable.

## Backlog

Known, argued, and unscheduled — except where a planned version claims one, which is marked.

- **A language other than English to actually select.** *(Claimed by 0.8.)* The application side is built;
  what is missing is upstream. The data build fetches only the English `stat_descriptions.txt`
  files and emits one language, so `manifest.json` declares `["en"]` and the Settings row has
  one entry. A second language needs the build to emit `<lang>-stats.ndjson`,
  `<lang>-items.ndjson` with `refName` filled in, and a `<lang>-lexicon.json`. Nothing in the
  app's schema has to change. **Say so in the README rather than letting it be discovered.**
- **Two things still matched on English wordings** *(claimed by 0.8)*, both deliberately left alone rather than
  guessed at, because getting either wrong is a confident wrong price rather than a failure:
  `item/derive`'s local-modifier lists (`kLocalDefences` and the weapon wordings) and
  `item/resolve`'s `kLocalWordings` compare `placeholder_form` of the printed line, and
  `ninja`'s `narrow_by_mods` compares a unique's printed modifiers against poe.ninja's English
  ones. The obvious fix — key them on `Stat::ref`, which is already treated as language-neutral
  by `Item::sum_of` and `plan`'s `kVoid` — needs two upstream answers first: whether a
  localised bundle's `ref` stays English, and whether the `" (Local)"` suffix rides on a
  localised matcher. **Pin it to a real localised bundle, not to a guess.**
  `ninja::kKeywords` is the same shape and already reads `ref_name`, which is why it is not on
  this list.
- **`chart_area_key`'s convention is Latin-only** *(claimed by 0.8)* — it capitalises words and drops apostrophes
  to turn a printed area name into the internal id trade files a chart under. That cannot work
  for Russian, Korean or Thai, and the answer is for the bundle to carry the mapping rather
  than for the app to keep deriving it. A wrong key already costs breadth and never correctness
  (the search falls back to the chart's own base type plus a note), so it degrades honestly.
- **Telling apart the variants a modifier wording cannot.** `narrow_by_mods` resolves a unique
  whose variants differ in *wording*; the ones that differ only in a **number poe.ninja publishes
  for some variants and not others** stay a span. Mageblood is the case: the item prints "Leftmost
  5 Magic Utility Flasks" and the dearest line carries no modifiers at all, so there is nothing to
  compare it against. Matching the number where every candidate does publish one would close most
  of the gap.
- **Offering the pool modifiers the item does *not* have.** Reading the per-unique data is built
  (see [item-layer.md](item-layer.md)); the other half of what it is for is not. A Watcher's Eye search is worth little without
  being able to add "and also has Discipline energy-shield-on-hit" — `ref` gives the wording to show,
  `tradeId` the filter to send and `range` the bounds to seed. That needs a `StatFilter` not tied to a
  `mod_index` and a way to pick one in the UI. Filters the record carries **without** a `tradeId` (428
  ambiguous wordings, 695 with no id at all) belong in that list too: display them, never search them.
- **An unidentified unique's own modifiers.** Which unique it is, is built (see [item-layer.md](item-layer.md)); what a copy
  of it *could* roll is the same gap as offering the pool modifiers, one item further along —
  the candidates' mods are in `en-unique-mods.ndjson` and nothing reads them for an
  unidentified item, so the search is the name, the base and the item level.
- **Ambiguous wordings** — two stat records can share a wording and both be searchable.
  `GameData::find_stat` refuses to guess, and the plan says "ambiguous wording, not searched" rather
  than picking whichever came first in the file. As of the `20260805.11` bundle this is **4 wordings**
  (the three "Grants Summon Visiting Harbinger of …" and "Attacks fire # additional Projectiles when
  in Off Hand"), and telling those apart needs context the bundle does not carry.
  **It used to be 59, and 55 of those were a data-repo bug rather than real ambiguity** — worth
  knowing, because the shape recurs: `emit/stats.py` keys a record on a *trade* wording, so one game
  stat rendered several ways ("#% chance to gain a Flask Charge when you deal a Critical Strike" and
  its 100% form, "Recover #% of Life on Kill" and "Lose #%", one entry per option of an option stat)
  became several records — and each was handed the whole description's matcher list, so they all
  claimed each other's wordings. The Surgeon's prefix on every crit-charge flask went unsearched.
  Fixed upstream: a wording trade hashes separately belongs only to the record carrying that hash,
  and the build now reports `wordings_ambiguous_in_a_namespace` so a regression is visible.
  **Do not "fix" the app side by picking a record.** Two ids behind one wording is a filter on the
  wrong stat half the time, and a confident wrong price is the failure mode this whole layer avoids.
- **No per-tier range for an affix, so no track can state what one rolls beyond the tier in hand.**
  Asked for: a track spanning *every* tier — the lowest tier's floor to the highest tier's ceiling
  — so a buyer can drag toward a roll better than the one they are holding. Nothing we have can
  say what those are. `roll_min`/`roll_max` come from what the clipboard printed, which is the
  rolled tier and only with Advanced Mod Descriptions on; `Stat` carries no tiers, and
  `en-unique-mods.ndjson` is per-unique rather than per-affix. **Upstream**: the data build would
  have to emit the mod table (GGG's `Mods.dat` joined to its tiers, keyed by item class and
  domain, in displayed units the way `UniqueModFilter::ranges` already are). **The tables are
  already extracted** — `Mods` comes out of `game_bundle.py` with `Level`, `Domain`,
  `GenerationType` and all eight `StatMin`/`StatMax` pairs, and grouping the 4,369 item-domain
  prefix/suffix rows by their stat-key set gives the ladders straight off (`local_physical_damage_+%`
  comes back as eight tiers, `Heavy` 40–49 through `Merciless` 170–179). What is *not* extracted is
  `SpawnWeight_TagsKeys`/`SpawnWeight_Values`, which is what decides whether a ladder can appear on
  the base in hand at all; without it the union would sweep in essence-, influence- and
  class-specific variants and overstate the range. So the cost is: two more columns, an emitter, a
  bundle asset keyed by stat id and item class, and the app-side join — plus a rule for the case
  where the wording in hand maps to more than one ladder, which is **refuse to guess**, the same as
  everywhere else here.
  Until then `ui::range_slider` answers the need without the claim — the track reaches past what is
  known (`ui::widen_track`, half again either side, one constant for every row), the knobs carry on
  past even that, a knob released against an end grows the track, and the numbers can be typed.
  **The line that must hold is the one between a track and a statement**: what the game published is
  `roll_min`..`roll_max` and is marked with ticks, wherever the track's own ends have got to, and a
  row with no published range gets no ticks and says on hover that it has none. **The track's width
  may be a convenience; the ticks may not.** Do not tick a derived track, and do not move a tick off
  `roll_min`/`roll_max` to make the reach look authoritative. A tick is read as a statement about
  what the affix rolls, and a wrong one is the confident wrong number this layer exists to avoid.
- **Pseudo mods on gear** — trade's `pseudo.*` totals (total resistances, total life) are not built;
  mods are matched verbatim. The bundle does carry the ids (`pseudo.pseudo_total_cold_resistance`
  and the rest), so this is a plan-layer job, not a data one. A map's pseudo stats *are* built (see
  `item/plan`'s map strategy) and are the shape to copy: the ids are literals in the plan layer,
  because trade publishes them and no bundle record is involved.
- **A data-repo bug, not an app one: `dp` is missing on stats whose trade wording matched no game
  description.** `emit/stats.py` takes `dp` from the description's variant modifiers, so a stat that
  fell back to trade's own wording gets none — `#% of Physical Attack Damage Leeched as Mana` is one,
  and every unique-mod range for it is then emitted 100× too large (`40..40` for a roll of `0.4`).
  The app refuses such a range rather than believing it, so the damage is contained; the fix is
  upstream, and `examples/item_3` is the case to check it against.

## Decided against

Not backlog. Each of these was proposed, investigated against the real thing, and closed — the
reason is here so it is not proposed a second time.

- **Switching the map-check profile by watching the client log** — 0.7's second "might", and it
  cannot be built rather than merely being unbuilt. The game never writes the selected character's
  name to `logs/LatestClient.txt`, and the three line shapes that do carry a name name your party
  as readily as you. The full evidence, the shape of a login as it is actually logged, and why the
  account API is not an alternative are in [map-check.md](map-check.md#the-client-log-cannot-say-which-character-is-playing).
  What replaced it is the profile last picked being remembered, which is a `persist_map_profile`
  call and no new file, host or log line. **If this is reopened, reopen it with a capture** — a
  client log in which a character selection is actually named — and not with reasoning.

- **Rating Crucible maps in map check** — the two `Misc Map Items` bases `Primeval Remnant` and
  `Primordial Remnant`, domain 33 `CRUCIBLE_MAP`. Nothing technical is in the way: the pool is
  real (49 prefixes, 51 suffixes), and the work is one entry in `mapcheck::kDomains` and one in
  the data repo's `POOL_GENERATIONS`, exactly as heist was. It is closed because the league is
  over and the items cannot drop — what is left is a handful on Standard, and the gate splitting
  one item class in two is a smaller oddity than a pool nobody can obtain. **Reopen it only if
  the content returns**, and note that it is the one place the domain gate disagrees with an item
  class it otherwise accepts. → [map-check.md](map-check.md)

- **Rating delve areas, and any other domain whose name says AREA.** There are exactly three —
  5 `AREA`, 17 `DELVE_AREA`, 22 `HEIST_AREA` — and 17 has no base item in the game behind it. A
  delve biome's modifiers exist and are generated, but nobody holds a copy of one to copy, which
  is the Vaal side area argument and closes it the same way. Nothing is deferred here: with heist
  built, every area pool a player can put on the clipboard is read.
