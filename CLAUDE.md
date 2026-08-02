# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

**Greenfield / not yet scaffolded.** As of this writing the repo is empty — this document is the
architectural plan agreed with the maintainer, not a description of existing code. When you scaffold
files, keep this file in sync with reality (replace "planned" sections with real commands/paths).

## What this is

A native, lightweight **Path of Exile price-check overlay**, in the spirit of
[Awakened PoE Trade](https://github.com/SnosMe/awakened-poe-trade) but with **no Electron / no
wrapper runtime**. The user hovers an item in-game and presses the copy hotkey; PoE writes the item
text to the clipboard; the tool parses it, queries prices, and draws an overlay with the result.

### Locked technical decisions (do not relitigate without asking)

- **Language:** plain C++20. (Rust and Fil-C were considered and rejected: Fil-C is Linux-only so it
  can't build the Windows target — it may return later purely as an optional *hardened Linux build
  variant*, never as the primary/only toolchain.)
- **Build:** CMake. Clang/GCC on Linux, MSVC or Clang on Windows. `-fsanitize=address,undefined` in
  debug builds.
- **Overlay UI:** [Dear ImGui](https://github.com/ocornut/imgui) rendered on a **transparent,
  click-through, always-on-top** window via **SDL2 + OpenGL**.
- **HTTP:** libcurl. **JSON:** nlohmann/json (swap parse-hot paths to simdjson only if profiling says so).
- **Cross-platform target:** Windows + Linux **X11 first**. Wayland is a later stretch goal — it
  blocks arbitrary global hotkeys and click-through overlays without compositor portals / evdev
  access, so do not gate v1 on it.

## Where the real difficulty is

Parsing and HTTP are the easy 80%. The hard, platform-specific 20% is **global input capture** and
the **overlay window**, and both require per-OS native code regardless of anything else. Isolate that
code behind narrow interfaces so the cross-platform core never sees `#ifdef _WIN32`.

- **Global hotkey + read-clipboard-on-copy:** Windows `RegisterHotKey` / low-level keyboard hook;
  Linux/X11 `XGrabKey`. The tool does not read the game — it reacts to the clipboard changing after
  the user's in-game copy. Debounce and only act on clipboard payloads that look like PoE item text.
- **Transparent overlay:** Windows layered window (`WS_EX_LAYERED | WS_EX_TRANSPARENT |
  WS_EX_TOPMOST`); Linux/X11 an override-redirect or dock-hinted window with an ARGB visual and a
  running compositor. Click-through must toggle off while the user interacts with the panel.

Suggested seams (keep these as abstract interfaces with per-OS implementations under `platform/`):
`IHotkeyListener`, `IClipboard`, `IOverlayWindow`.

## Conventions

- **Comments:** doc comments (Doxygen `///`) on public API; inline comments **only for the
  non-obvious** — hacks, surprising behavior, workarounds, protocol quirks. No narration of what the
  code plainly says.
- **Commit messages / PRs:** precise, not verbose. State what changed and why; skip the essay.

## Architecture (planned module boundaries)

Pipeline: **hotkey → clipboard → parse → identify → price → render**.

- **Item parser** — the domain core. PoE clipboard text is UTF-8 with sections split by lines of
  `--------`. First section holds `Item Class`, `Rarity`, name, and base type; later sections hold
  implicit/explicit mods, item level, requirements, sockets, corruption, influences, etc. Turn this
  into a structured `Item`. This module must be **pure and heavily unit-tested** from real clipboard
  captures — it has no I/O and is where most bugs will live.
- **Item classifier** — decides the pricing strategy from the parsed item: uniques / currency /
  divination cards / fragments / cluster jewels → poe.ninja bulk pricing; rare/modded gear → a
  constructed trade-site stat query.
- **Trade query builder + client** — see "PoE trade API" below. Maps parsed mods to GGG **stat
  hashes** and builds the search JSON, then runs the two-step search→fetch flow.
- **poe.ninja client** — reference pricing for categorized items; see below.
- **Rate limiter** — a single shared component every outbound GGG request goes through. Non-optional;
  see "Rate limits".
- **Static data cache** — GGG's stat/item/league metadata, fetched once and cached on disk with a TTL.
- **Overlay UI** — ImGui panels; presentation only, no network or parsing logic.

## External APIs — the load-bearing domain knowledge

We are **not** registering the application, so only **publicly accessible** endpoints are usable, and
we must send a descriptive `User-Agent` identifying the tool + contact, per GGG policy.

### PoE trade API (two-step, both rate-limited)

1. `POST https://www.pathofexile.com/api/trade/search/<league>` with the query JSON → returns a
   search `id` and a list of result hashes.
2. `GET https://www.pathofexile.com/api/trade/fetch/<comma-separated-ids>?query=<id>` — **fetch in
   batches of at most 10 ids** → returns listing + price detail.

Mods are not sent as text; they are **stat hashes** (e.g. `explicit.stat_1509134228`). The mapping
comes from static-data endpoints that must be fetched and cached:
`.../api/trade/data/stats`, `.../api/trade/data/items`, `.../api/trade/data/leagues`.

### poe.ninja

Docs: https://poe.ninja/docs/api. Two overview endpoints:
- `GET https://poe.ninja/api/data/currencyoverview?league=<league>&type=<Currency|Fragment>`
- `GET https://poe.ninja/api/data/itemoverview?league=<league>&type=<UniqueWeapon|DivinationCard|...>`

Cache aggressively (prices move slowly relative to how often a user hovers items).

### Rate limits — treat as a hard requirement

GGG returns rate-limit state in response **headers** (`X-Rate-Limit-Rules`, per-policy
`X-Rate-Limit-<policy>` giving `hits:period:window` triplets, `X-Rate-Limit-<policy>-State`, and
`Retry-After` on 429). All GGG traffic must pass through the shared rate limiter, which parses these
headers, tracks each active window, and **proactively delays** rather than reactively eating 429s.
Never fire the search→fetch flow without going through it.

## Build & test (planned — update once CMake exists)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build                       # all tests
ctest --test-dir build -R <name> -V          # a single test
./build/<binary>                             # run the tool
```

Debug builds should enable `-fsanitize=address,undefined`. The item parser must be runnable and
tested without any windowing or network dependency.
