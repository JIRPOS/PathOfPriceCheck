# Localisation, and what it is not

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

**Two unrelated problems wear the one word, and neither buys anything toward the other.**
Reading a *translated client* is game text — GGG's words, shipped in the bundle, load-bearing:
get one wrong and an item does not parse. Translating *our own* text is our words, compiled in,
cosmetic: get one wrong and a button reads oddly. So they are two settings and two tables.

- **`Config::client_language`** picks the assets `GameData::open` maps and the vocabulary
  `data/lexicon` reads item text with. Read **once at startup** (`DataUpdater::set_language`),
  because the bundle is opened with it and every parsed item points into that bundle — so
  Settings offers it, states that it lands on the next run, and does not pretend otherwise.
  Its options are `GameData::languages()`, off the manifest: asking for a language the bundle
  does not carry simply fails to open it, which is a worse way to find out.
- **`Config::ui_language`** is `src/ui/strings.cpp` — `ui::Msg`, one enum entry per piece of
  our own text, `ui::text()`, and one compiled-in table per language with English at index 0
  as the fallback. A null or empty entry falls through to English, so a table can be added with
  only the rows somebody has actually translated. `static_assert` on the table length, because
  a short one would otherwise zero-fill its tail silently. Defaults to `"auto"`, which follows
  the client. **One binary for every language**: a table is a few kilobytes against an
  executable already embedding four typefaces, and per-language builds would contradict the
  rule that a new league needs a data build rather than a release.

Three things that had to move for any of it to work, and are better even in English:

- **`Property::key`** (`data::PropertyKey`). The printed label used to be the key: `parse`
  dispatched on `"Attacks per Second"` and `item/plan` matched the same string again, two
  copies of one vocabulary drifting apart. The label stays on the `Property` for the tooltip
  to draw; everything downstream decides on the key.
- **`Item::class_kind`** (`data::ClassKind`). `is_flask()` was `item_class.ends_with("Flasks")`
  and `is_map()` was `== "Maps"`. The lexicon says which class is which kind, and parse needs
  no bundle to ask.
- **`BaseType::ref_name` on the wire.** The trade API's `name`/`type` terms are English
  whatever language the client is, so `wire_name` in `item/plan` states the order once —
  `trade_name` (where the site files the item somewhere else entirely, i.e. a transfigured
  gem), then `ref_name`, then the printed `name`, which is the same string as `ref_name` on an
  English bundle. `GameData::find_bases_by_ref` reads `<lang>-items-ref.index.bin` — which has
  shipped in every bundle and was never opened until now — and is how the app names a record
  the clipboard did not print: the blighted-map redirect to the `Map` base is the case that
  proves it, since `"Map"` is a reference name and not what a translated client shows.
