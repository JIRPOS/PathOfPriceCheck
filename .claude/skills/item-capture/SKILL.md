---
name: item-capture
description: Diagnose a PathOfPriceCheck item that parses, resolves, prices or searches wrongly, usually from clipboard text the user pasted or a screenshot of the panel. Use when a modifier is unrecognised or ambiguous, a base or unique fails to match, a price or DPS number looks wrong, a search returns nothing or the wrong market, or a new item class needs supporting.
---

# Working from a captured item

The single most common request in this project: the user pastes the clipboard text of an item (or
screenshots the panel) and reports that something about it is wrong. The capture **is** the
specification — pin every number to it, never to another tool's output or a screenshot of one.

## 1. Save the capture first

Before analysing anything, write the pasted text to a file — it is what the fix is verified against
and what the regression test will load.

- `tests/data/items/<kind>-<name>.txt` for a capture with no screenshot beside it. Existing naming:
  `map-*.txt`, `gem-*.txt`, `currency-*.txt`, `unique-unidentified-*.txt`, `chart-*.txt`.
- `tests/data/examples/item_N.txt` when the user also supplied `item_N.jpeg`, the screenshot of the
  same tooltip that the rendering is checked against.
- A `listing-` prefix means it came from a **fetch response**, not from the game: the site's
  renderer writes keyword-link markup (`[Intangibility|Intangibility]: 8%`) and leaves the mod-type
  markers off, so those files hold what `restore_mod_markers` puts back.

Keep the text byte-exact, including the `--------` separators and trailing flag lines. Prefer a
real capture over a written one, and ask the maintainer to reproduce one in game if the case is not
covered.

## 2. Find the layer that owns the symptom

Read the doc for the layer before editing it — each argues out rules whose violation is a confident
wrong price rather than a crash.

| Symptom | Layer | Doc |
| --- | --- | --- |
| A line read as the wrong kind — a modifier that is really prose, flavour text, a property, a usage note; a missing section; the wrong mod type | `src/item/parse.cpp` | [docs/item-layer.md](../../../docs/item-layer.md) |
| "unrecognised modifier", a wording that matched nothing | `data/stat_matcher`, `data/stat_normalize` | [docs/data-layer.md](../../../docs/data-layer.md) |
| "ambiguous wording" | usually a data-repo bug — **never** resolve it by picking a record | [docs/roadmap.md](../../../docs/roadmap.md) |
| A base, unique, gem or card that did not resolve; local vs global stat; an unidentified unique | `src/item/resolve.cpp` | [docs/item-layer.md](../../../docs/item-layer.md) |
| A wrong DPS, quality-20 or base-percentile number | `src/item/derive.cpp` | [docs/item-layer.md](../../../docs/item-layer.md) |
| The wrong filters ticked, a wrong bound, a missing note, the wrong strategy | `src/item/plan.cpp` | [docs/item-layer.md](../../../docs/item-layer.md) for the shared rules, then the strategy's own doc: [unique](../../../docs/strategy-unique.md), [map](../../../docs/strategy-map.md), [gem](../../../docs/strategy-gem.md) |
| A search that returns nothing, or the wrong JSON on the wire | `src/trade/query.cpp` | [docs/trade-layer.md](../../../docs/trade-layer.md) |
| No reference price, or the wrong variant | `src/ninja/` | [docs/ninja.md](../../../docs/ninja.md) |
| A currency, scarab, fragment or essence with no market | `src/exchange/` | [docs/exchange.md](../../../docs/exchange.md) |
| Colours, spacing, tooltips, the filter table, the item card | `src/screens/` | [docs/trade-layer.md](../../../docs/trade-layer.md) |

An item that reaches the panel wrongly has usually gone wrong one layer earlier than it looks:
check parse output before blaming the plan.

## 3. Reproduce without the game

```sh
cmake --build build -j
PPC_DEV_OVERLAY=1 PPC_DEV_ITEM=tests/data/items/<file>.txt ./build/PathOfPriceCheck
```

That opens the price-check panel on the capture — the only way to iterate on the panel without PoE
running. `PPC_DEV_IDLE=1` keeps the idle marker up instead. See the **run-overlay** skill for
driving and screenshotting the app.

For anything below the UI, a unit test is faster than the overlay: `item_parse_test`,
`item_pricing_test`, `stat_matcher_test`, `trade_query_test`, `ninja_test`, `exchange_test`.

```sh
ctest --test-dir build -R item_parse -V
```

## 4. Verify, then pin it

- The offline bundle slice in `tests/data/bundle/` is what tests resolve against. If the case needs
  a record the slice lacks, add its key to the lists in `./scripts/slice-test-bundle.py` and
  regenerate — **never hand-write a record**, the indices address the ndjson by byte offset. See
  [docs/testing.md](../../../docs/testing.md).
- Add the capture to the relevant test as a case. `parse_item` takes a lexicon and has no default;
  tests state the English one once via `tests/parse_en.hpp`.
- Run the whole suite, not just the new case: `ctest --test-dir build`.
- Where a fix changes what a user sees, it is a Release-notes line — see the **commit-work** skill.
