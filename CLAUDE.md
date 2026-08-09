# CLAUDE.md

Guidance for Claude Code (claude.ai/code) working in this repository.

**This file is a map, not the manual.** It holds what is true of the whole project and the rules
that must hold whether or not anything else was read. Everything else lives in `docs/`, is loaded
on demand, and is where new detail belongs — **do not grow this file back**. When something
changes, update the doc that owns it; this file only changes when the map does.

## What this is

A native, lightweight **Path of Exile price-check overlay**, in the spirit of
[Awakened PoE Trade](https://github.com/SnosMe/awakened-poe-trade) but with **no Electron / no
wrapper runtime**. The user hovers an item in-game and presses the copy hotkey; PoE writes the item
text to the clipboard; the tool parses it, queries prices, and draws an overlay with the result.

Pipeline: **hotkey → auto-copy → clipboard → parse → identify → price → render**.

## Project status

The overlay, Settings, the league list, the static game-data layer, the item layer (parse →
resolve → price-relevant numbers → search plan, plus the game-styled tooltip), the trade search,
poe.ninja reference pricing and the in-game currency exchange feed are all **built and tested**.
What is not built is [docs/roadmap.md](docs/roadmap.md) — including the fact that a language other
than English cannot yet be selected, because the data build emits only English.

Sections of any doc describing an unbuilt layer say so explicitly. Keep them honest.

## The docs

Read the one that owns what you are about to touch, before touching it. They are written to be
read whole; each is one layer.

| Doc | Read it when |
| --- | --- |
| [docs/platform.md](docs/platform.md) | Hotkeys, foreground detection, input injection, clipboard, single-instance — the per-OS seams in `src/platform/`, and the clipboard failures they exist for. |
| [docs/architecture.md](docs/architecture.md) | `App`, the SDL loop, the copy path, focus rules, overlay placement, Settings, fonts, the icon, `ppc_core`, the debug log. |
| [docs/data-layer.md](docs/data-layer.md) | `src/data/` — the runtime bundle, the updater, the lexicon, stat normalization and matching. |
| [docs/item-layer.md](docs/item-layer.md) | `src/item/` — parse, resolve, derive, range matching, and the plan rules every strategy shares. Where most pricing judgement lives. |
| [docs/strategy-unique.md](docs/strategy-unique.md), [strategy-map.md](docs/strategy-map.md), [strategy-gem.md](docs/strategy-gem.md) | One per search strategy that has more to say than the shared rules: uniques (including unidentified), maps (with charts and Valdo maps), gems. |
| [docs/trade-layer.md](docs/trade-layer.md) | `src/trade/` — query building, the two-step client, the rate limiter, and how results and the filter list are drawn. |
| [docs/ninja.md](docs/ninja.md) | `src/ninja/` — the poe.ninja reference price. |
| [docs/exchange.md](docs/exchange.md) | `src/exchange/` — GGG's hourly in-game currency exchange digests. |
| [docs/localisation.md](docs/localisation.md) | Reading a translated client vs. translating our own text — two unrelated problems, two settings. |
| [docs/external-apis.md](docs/external-apis.md) | The endpoints themselves: trade, poe.ninja, currency exchange, and GGG's rate-limit policy. |
| [docs/conventions.md](docs/conventions.md) | Comment style, commit and PR shape, the maintainer alias, which docs are public. |
| [docs/testing.md](docs/testing.md) | Build prerequisites, sanitizers, and regenerating the test fixtures and bundle slice. |
| [docs/roadmap.md](docs/roadmap.md) | What is deliberately not built, and what was decided against. Check before proposing work. |
| [UNIQUE-MODS.md](UNIQUE-MODS.md) | The per-unique modifier dataset's contract, including what it does not cover. |

Skills carry the recurring workflows: **commit-work** (commit and PR messages), **item-capture** (a
clipboard capture that parses, prices or searches wrong), **clipboard-debug** (a copy that hung or
came back stale), **run-overlay** (build and drive the app without the game). Invoke the skill
rather than reconstructing the procedure.

## Locked technical decisions (do not relitigate without asking)

- **Language:** plain C++20. (Rust and Fil-C were considered and rejected: Fil-C is Linux-only so it
  can't build the Windows target — it may return later purely as an optional *hardened Linux build
  variant*, never as the primary/only toolchain.)
- **Build:** CMake. Clang/GCC on Linux, MSVC or Clang on Windows. `-fsanitize=address,undefined` in
  debug builds.
- **Overlay UI:** [Dear ImGui](https://github.com/ocornut/imgui) on a borderless, always-on-top
  **SDL3 + OpenGL** window — transparent, with click-through still missing, which is why the window
  is never larger than it needs to be.
- **Dependencies:** CMake **FetchContent** builds SDL3 + ImGui + nlohmann/json + doctest from source
  (pinned tags in `CMakeLists.txt`).
- **HTTP:** libcurl behind `src/net/http.hpp` (static Schannel build on Windows, so the release is a
  single `.exe`; gzip required, not `AUTO`). Do not re-add a `CURL::libcurl` alias — curl declares
  that name itself. **JSON:** nlohmann/json. **Tests:** doctest. **Clipboard:** our own platform
  seam, never SDL's.
- **Game data:** never baked into the binary. Built and published by the separate public repo
  **[JIRPOS/PathOfPriceCheck-Data](https://github.com/JIRPOS/PathOfPriceCheck-Data)** and downloaded
  at runtime, so a new league needs a data build rather than a new release.
- **Cross-platform target:** Windows + Linux **X11**. A native Wayland backend is **won't
  implement** — decided, not deferred, and the reason is the game: Proton runs PoE as an Xwayland
  client, so our X11 dependency is not stronger than the game's, and going native would trade
  capability away. Revisit only if PoE itself stops being an Xwayland client. The full argument,
  and the one Wayland backend that was built, measured and removed, are in
  [docs/platform.md](docs/platform.md) and [docs/architecture.md](docs/architecture.md).

## Rules that hold everywhere

Each is a summary of something a doc argues out. Where they disagree, the doc wins — but do not
violate one of these on the strength of not having read it.

- **Never issue a GGG request outside `trade::request`.** The shared rate limiter is a hard
  requirement, not a courtesy. poe.ninja and the currency-exchange CDN are *different hosts with
  different rules* and deliberately do not go through it. → trade-layer, external-apis
- **Do not go back to `SDL_GetClipboardText()`**, do not clear the clipboard before a copy, and do
  not build a purely passive clipboard watcher. All three were tried and measured; each fails in a
  way that reads as a hang. → platform, architecture
- **Numbers must never go through the C locale.** Parse with `std::from_chars`; `LC_NUMERIC` is
  forced to `C` for formatting and `LC_TIME` explicitly is not. → architecture
- **Pin numbers to real captures**, never to another tool's output or a screenshot of one. Ask the
  maintainer for a capture of the concrete case instead. → testing
- **Where two records share a wording, refuse to guess.** A confident wrong price is the failure
  this codebase is built to avoid; say what was not searched instead. → item-layer, roadmap
- **`NORMALIZATION.md` in the data repo is normative** for `data/stat_normalize`. A divergence does
  not crash, it silently misprices. → data-layer
- **Never write over a live bundle.** Install writes a fresh directory and flips `current` by
  rename; Windows will not replace a memory-mapped file. → data-layer
- **`ppc_core` links no SDL3, no ImGui, no X11 and no libcurl.** The parser and every pricing layer
  must stay testable headless. → architecture
- **Failure is silent.** Past the copy timeout, or when what was copied does not parse, the check is
  dropped and nothing opens — an overlay narrating its own plumbing is noise over a game, and the
  debug log has the detail. → architecture
- **Focus is a gate, never something to take.** The one sanctioned exception is pulling the game out
  of the foreground to make Wine release the clipboard. → architecture
- **`PRIVACY.md` enumerates every outbound request and every file written**, so a new host, a new
  cache file or anything new in the debug log is a change to that document as much as to the code.
  It is the one doc that goes stale silently. → conventions
- **The maintainer is `JIRPOS`** — the alias in every published file, and git's `user.name` is not
  to be changed (GPG-signed commits). The repo is public. → conventions
- **Comments:** doc comments on public API; inline comments **only for the non-obvious**. No
  narration of what the code plainly says. → conventions

## Build & test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # first run fetches + builds SDL3 (slow, cached after)
cmake --build build -j
ctest --test-dir build                         # all tests
ctest --test-dir build -R <name> -V            # a single test
./build/PathOfPriceCheck                       # run (add PPC_DEV_OVERLAY=1 to see the UI w/o PoE)
```

Linux needs SDL3's dev headers and libcurl; Windows needs only MSVC, and CI validates that build on
every push. The full package list, the sanitizer invocation, the dev environment variables and the
fixture-regeneration rules are in [docs/testing.md](docs/testing.md).
