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
- **Overlay UI:** [Dear ImGui](https://github.com/ocornut/imgui) on a borderless, always-on-top
  **SDL3 + OpenGL** window. (SDL3, not SDL2 — first-class transparent/always-on-top window flags and a
  smoother Wayland path later. True transparency + click-through is a later step; today it's an opaque
  panel.)
- **Dependencies:** CMake **FetchContent** builds SDL3 + ImGui + nlohmann/json from source (pinned
  tags in `CMakeLists.txt`), so CI needs no system packages beyond Linux dev headers (see Build).
- **HTTP (later):** libcurl. **JSON:** nlohmann/json. **Clipboard:** SDL3's `SDL_GetClipboardText()` —
  cross-platform, so *not* a platform seam.
- **Cross-platform target:** Windows + Linux **X11 first**. Wayland is a later stretch goal — it
  blocks arbitrary global hotkeys and click-through overlays without compositor portals / evdev
  access, so do not gate v1 on it.

## Where the real difficulty is

Parsing and HTTP are the easy 80%. The hard, platform-specific 20% is **global hotkey capture** and
**foreground-window detection** — both need per-OS native code. They live behind narrow headers in
`src/platform/` with X11 and Win32 implementations, so the cross-platform core never sees `#ifdef`.
The only two seams (clipboard and windowing come free from SDL3):

- **`platform/hotkeys.hpp` — `HotkeyListener`:** system-wide hotkeys. X11 `XGrabKey` on root from a
  dedicated thread (grabbed with all NumLock/CapsLock combos). The `Display` is touched **only** by
  that thread; the main thread signals rebind/quit via a **self-pipe** watched with `select()` — never
  cross-thread Xlib (that combo aborts with `xcb_xlib_threads_sequence_lost`). Win32 uses
  `RegisterHotKey` on a thread with its own message loop (rebind via `PostThreadMessage`). The callback
  fires on the OS thread and is marshaled to the main loop as an SDL user event.
- **`platform/foreground.hpp` — `foreground_title_contains()`:** X11 reads `_NET_ACTIVE_WINDOW` +
  `_NET_WM_NAME`; Win32 `GetForegroundWindow` + `GetWindowTextW`. Matched against a configurable title
  (default "Path of Exile") to decide whether to auto-copy.
- **`platform/input_sim.hpp` — `simulate_copy()`:** synthesizes Ctrl+C to the focused window so the
  price-check hotkey grabs the hovered item itself (no manual copy). X11 uses `XTestFakeKeyEvent`
  (libXtst); Win32 uses `SendInput`.
- **`platform/platform.hpp` — `platform_init()`:** one-time init (X11 calls `XInitThreads`).

Key naming is canonical strings ("D", "Space", "F5"); `key_name_from_sdl` (capture), the X11 keysym
lookup, and the Win32 VK lookup each translate them. OS hotkey APIs are **side-agnostic** on
modifiers, so "LShift" registers as "Shift".

## Conventions

- **Comments:** doc comments (Doxygen `///`) on public API; inline comments **only for the
  non-obvious** — hacks, surprising behavior, workarounds, protocol quirks. No narration of what the
  code plainly says.
- **Commit messages / PRs:** precise, not verbose. State what changed and why; skip the essay.

## Architecture

Pipeline: **hotkey → auto-copy → clipboard → parse → identify → price → render**. `App` (`src/app.cpp`)
owns the SDL event loop and a `Screen` state machine `{ Hidden, PriceCheck, Settings }`. Price-check
hotkey → `simulate_copy()` (if PoE focused) → poll clipboard for the change → show. The overlay is
**dismiss-on-focus-loss**: once shown it stays until you click away, hit Escape, the X button, or the
toggle hotkey — keeping logical state in sync with what's visible (a stale "still open" state was the
two-press bug). A **system-tray icon** (SDL3 `SDL_Tray`, cross-platform) provides Exit. `Overlay` wraps
the SDL3+GL+ImGui window; `Config` persists to JSON. `PPC_DEV_OVERLAY=1` opens Settings and disables
dismiss-on-blur for local dev.

The parser/pricing modules below are **not built yet** — they're the planned next layers. The
price-check screen currently just dumps the raw clipboard text where the parser will slot in.

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

## Build & test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # first run fetches + builds SDL3 (slow, cached after)
cmake --build build -j
ctest --test-dir build                         # all tests
ctest --test-dir build -R <name> -V            # a single test
./build/PathOfPriceCheck                       # run (add PPC_DEV_OVERLAY=1 to see the UI w/o PoE)
```

SDL3 builds from source, so **Linux needs dev headers**: `libx11-dev libxext-dev libxrandr-dev
libxcursor-dev libxi-dev libxfixes-dev libxkbcommon-dev libwayland-dev wayland-protocols
libgl1-mesa-dev libegl1-mesa-dev libasound2-dev libpulse-dev libdbus-1-dev libudev-dev` (the CI
workflows install exactly these). Windows needs only MSVC. The CI still validates the Windows build on
every push/PR — trust it for the Win32 platform code, which can't be compiled locally here.

`-fsanitize=address,undefined` for debug builds is not wired into CMake yet. The item parser (when it
lands) must be runnable and tested without any windowing or network dependency.
