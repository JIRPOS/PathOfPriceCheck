# Still to build

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

- **A language other than English to actually select.** The application side above is built;
  what is missing is upstream. The data build fetches only the English `stat_descriptions.txt`
  files and emits one language, so `manifest.json` declares `["en"]` and the Settings row has
  one entry. A second language needs the build to emit `<lang>-stats.ndjson`,
  `<lang>-items.ndjson` with `refName` filled in, and a `<lang>-lexicon.json`. Nothing in the
  app's schema has to change. **Say so in the README rather than letting it be discovered.**
- **Two things still matched on English wordings**, both deliberately left alone rather than
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
- **`chart_area_key`'s convention is Latin-only** — it capitalises words and drops apostrophes
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
