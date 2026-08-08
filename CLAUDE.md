# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

The overlay, Settings, the league list, the **static game-data layer**, the **item layer**
(parse → resolve → price-relevant numbers → search plan, plus the game-styled tooltip) and the
**trade search** (query builder, two-step client, shared rate limiter, results in the panel) are
built and tested, including the **per-unique modifier data** that decides which of a unique's
modifiers are worth searching on, the **map search**, which asks for none of the things a
rolled item's search does, and the **gem search**, which asks for three things and no
modifier at all. **poe.ninja reference pricing** — uniques, gems, currency and
base types, the going rates a stat query cannot give — is built too, and so is the **in-game
currency exchange feed** (GGG's own hourly digests of the market currency and fragments actually
trade on, which is why those items have no trade search at all) — together with the bundle's
record of **which items trade there at all**, which is what tells an item nobody traded this hour
from one that is not sold there.

Keep this file in sync with reality; sections describing unbuilt layers say so explicitly.

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
  smoother Wayland path later. The window **is** transparent — `SDL_WINDOW_TRANSPARENT` with an alpha
  framebuffer cleared to 0, so only what ImGui draws is painted; **click-through is the part still
  missing**, so the window's whole rectangle still swallows mouse input. That is why the window is
  never made larger than it needs to be, and why the price-check gutter below is sized to what is
  free rather than to what is wanted.)
- **Dependencies:** CMake **FetchContent** builds SDL3 + ImGui + nlohmann/json + doctest from source
  (pinned tags in `CMakeLists.txt`), so CI needs no system packages beyond Linux dev headers and
  libcurl (see Build).
- **Game data:** never baked into the binary. Built and published by the separate public repo
  **[JIRPOS/PathOfPriceCheck-Data](https://github.com/JIRPOS/PathOfPriceCheck-Data)** and downloaded
  at runtime, so a new league needs a data build rather than a new release.
- **HTTP:** libcurl behind `src/net/http.hpp`. System package where one exists; a static
  Schannel build via FetchContent on Windows, so the release stays a single `.exe` with no DLL
  beside it. That path also fetches zlib and hands it to curl through `ZLIB::ZLIB` — gzip is
  required, not `AUTO`, so the trade-data endpoints never silently fall back to identity on a
  machine that happens to lack the library. Do not re-add a `CURL::libcurl` alias: curl declares
  that name itself. **JSON:** nlohmann/json. **Tests:** doctest. **Clipboard:** a platform seam of our own
  (`platform/clipboard.hpp`) — SDL3's `SDL_GetClipboardText()` was tried and abandoned, see below.
- **Cross-platform target:** Windows + Linux **X11 first**. Wayland is a later stretch goal — it
  blocks arbitrary global hotkeys and click-through overlays without compositor portals / evdev
  access, so do not gate v1 on it.

## Where the real difficulty is

Parsing and HTTP are the easy 80%. The hard, platform-specific 20% is **global hotkey capture** and
**foreground-window detection** — both need per-OS native code. They live behind narrow headers in
`src/platform/` with X11 and Win32 implementations, so the cross-platform core never sees `#ifdef`.
The seams (windowing still comes free from SDL3; the clipboard did not — see below):

- **`platform/hotkeys.hpp` — `HotkeyListener`:** system-wide hotkeys. X11 `XGrabKey` on root from a
  dedicated thread (grabbed with all NumLock/CapsLock combos). The `Display` is touched **only** by
  that thread; the main thread signals rebind/quit via a **self-pipe** watched with `select()` — never
  cross-thread Xlib (that combo aborts with `xcb_xlib_threads_sequence_lost`). Win32 uses
  `RegisterHotKey` on a thread with its own message loop (rebind via `PostThreadMessage`). The callback
  fires on the OS thread and is marshaled to the main loop as an SDL user event.
  X11 fires on **key release, not press**: a passive `XGrabKey` activates into a real keyboard grab
  that would swallow our own injected Ctrl+C, and the game must not get the hotkey's letter and C at
  once. So `dispatch()` holds the grab until the `KeyRelease` arrives, then ungrabs and fires — which
  also swallows auto-repeat. Needs `XkbSetDetectableAutoRepeat`, or the first repeat looks like a release.
- **`platform/foreground.hpp` — `foreground_title_contains()`:** X11 reads `_NET_ACTIVE_WINDOW` +
  `_NET_WM_NAME`; Win32 `GetForegroundWindow` + `GetWindowTextW`. Matched against a configurable title
  (default "Path of Exile") to decide whether to auto-copy.
- **`platform/input_sim.hpp` — `simulate_copy()`:** synthesizes Ctrl+C to the focused window so the
  price-check hotkey grabs the hovered item itself (no manual copy). X11 uses `XTestFakeKeyEvent`
  (libXtst); Win32 uses `SendInput`. The chord must look like a *human* keypress or the game ignores
  it: a ~16ms tap falls between two of the game's input samples, so C is held `PPC_COPY_HOLD_MS`
  (60) with `PPC_COPY_GAP_MS` (40) either side, each event `XSync`'d separately.
  **Only ever release a key you pressed, and only ever press one that is up.** The hotkey is Ctrl+D,
  so the user is usually holding Ctrl at injection time; send the bare C and ride their modifier
  rather than re-pressing it. A fake press is cancelled *only* by a fake release, so if they let go
  between the `XQueryKeymap` and that release, the server holds Ctrl down with no physical key left
  to clear it — Alt+Tab, Super and our own hotkeys all silently stop matching, and Wine only
  re-syncs the game's modifier state on focus change. (Tapping Ctrl clears a wedged server.)
  Riding their Ctrl has the opposite, benign failure: they drop it mid-injection and the game gets a
  bare C, so re-check after the gap and take over — taking over means we own the release too.
- **`platform/clipboard.hpp` — `clipboard_text(timeout_ms)`:** reads the clipboard ourselves.
  **Do not go back to `SDL_GetClipboardText()`.** Its X11 backend gates every read on
  `_this->clipboard_mime_types`, filled only by a single unretried `TARGETS` probe at `SDL_Init`
  and by XFixes owner-change notifications — so if that probe goes unanswered (fullscreen game
  mid-frame) and the owner never *re-asserts* ownership on later copies, reads return `""` with no
  X traffic for the life of the process. Symptom: a fresh run never sees the item until you click
  out of the game once, then works forever. Its `WaitForSelection` is worse — 1s timeout *per mime
  type*, after which SDL seizes the CLIPBOARD selection and serves empty text, destroying the real
  clipboard. Ours does its own `XConvertSelection` on a private `InputOnly` requestor window,
  trying `UTF8_STRING` → `text/plain;charset=utf-8` → `STRING` against one shared deadline, handles
  `INCR`, and never calls `XSetSelectionOwner`. Win32 is plain `OpenClipboard`/`CF_UNICODETEXT`,
  retried while the lock is held. Verified against a stub owner: live owner <1ms, frozen owner
  returns empty exactly at the deadline, no owner returns instantly.
  `clipboard_stamp()` is the same argument for the *write* signal: X11 selects XFixes
  `SetSelectionOwnerNotify` on its own requestor window and keeps the `selection_timestamp` off
  each one, so a copy is observed the moment ownership is asserted rather than after a `TARGETS`
  round trip the game may never answer (SDL's `SDL_EVENT_CLIPBOARD_UPDATE` waits for that reply —
  see the copy watch under Architecture); Win32 uses `GetClipboardSequenceNumber`. **It is a value,
  not a latch** — the previous design returned a bool and cleared itself, so callers had to arm it
  and poll every tick or lose a write between two polls. A stamp can be read as often or as rarely
  as you like. It pairs a change counter with the timestamp so two writes in the same millisecond
  differ; compare for equality only, the X11 time wraps. Without XFixes it never moves, so every
  copy times out — the honest failure, since a stamp that moved on its own would vouch for the
  stale clipboard a failed copy leaves behind. Verified against a stub owner re-asserting ownership
  over byte-identical text: three re-copies, three distinct stamps.
  **That target list is not cosmetic: the same copy yields two different texts.** Wine publishes
  the Windows clipboard's `CF_UNICODETEXT` as the UTF-8 targets and `CF_TEXT` as `STRING`, and for
  the first ~100ms after a copy PoE has often rendered only the latter — so the UTF-8 conversion
  comes back empty, we fall through to Latin-1, and every em dash in the Advanced Mod Descriptions
  arrives as a plain hyphen (1277 bytes against 1291 for the same item). Polling therefore sees the
  two forms alternate, which is why one press of the hotkey used to show affixes and the next did
  not. `parse_info_line` accepts either separator; **do not "fix" that by trusting the encoding.**
- **`platform/platform.hpp` — `platform_init()`:** one-time init (X11 calls `XInitThreads`).

Key naming is canonical strings ("D", "Space", "F5"); `key_name_from_sdl` (capture), the X11 keysym
lookup, and the Win32 VK lookup each translate them. OS hotkey APIs are **side-agnostic** on
modifiers, so "LShift" registers as "Shift".

## Conventions

- **Comments:** doc comments (Doxygen `///`) on public API; inline comments **only for the
  non-obvious** — hacks, surprising behavior, workarounds, protocol quirks. No narration of what the
  code plainly says.
- **Commit messages / PRs:** precise, not verbose. The **subject is the topic, summarised** — what
  this commit is about, not a list of what it touched. The body is three labelled groups, always
  in this order and only the ones that apply:

  ```text
  ADDED: <one line, one thing>
  ADDED: <…>
  CHANGED: <…>
  REMOVED: <…>
  ```

  One sentence per line, and for **nearly every line that is the whole entry**. Reasoning is the
  exception, not the shape: add it only where the line reads as arbitrary or backwards without it
  — a measurement that decided the design, a rule whose direction is not guessable. A reason under
  every line is the failure mode, and it buries the two that matter.

  Where a line does earn one, it goes on the following lines as bullet points, **with a blank line
  before and after the bullets**. Without them GitHub folds the next `ADDED:` line into the bullet
  as a lazy list continuation, and the body renders as nonsense.

  A **pull request summarises the commits it contains and invents nothing**: take each commit's
  own `ADDED:`/`CHANGED:`/`REMOVED:` lines, pool them, and re-sort into the same three groups.
  Reasons ride along with the lines they belong to. Prefer the commit's wording over a fresh one —
  the PR is a merge of what is already written, not a second telling of it.
- **The maintainer is `JIRPOS`.** Use the GitHub alias in every file — docs, licenses, anything
  published. The legal name goes in no file, here or in the data repo. Git's own `user.name` is a
  separate matter and is **not** to be changed: the commits are GPG-signed, the key is bound to
  that identity, and GitHub renders the commits under the alias anyway.
- **The repo is public.** The public-facing docs are `README.md`, `BUILDING.md`, `PRIVACY.md`,
  `ATTRIBUTION.md`, `EULA.md`, `CONTRIBUTING.md`, `CONTACT.md` and `LICENSE` (MIT), and the
  `User-Agent`'s contact URL points here rather than at the data repo. **`PRIVACY.md` enumerates
  every outbound request and every file written**, so a new host, a new cache file or anything new
  in the debug log is a change to that document as much as to the code — it is the one doc that
  goes stale silently. `CONTRIBUTING.md` says pull requests are not accepted yet.

## Architecture

Pipeline: **hotkey → auto-copy → clipboard → parse → identify → price → render**. `App` (`src/app.cpp`)
owns the SDL event loop and a `Screen` state machine `{ Hidden, PriceCheck, Settings }`. Price-check
hotkey → `simulate_copy()` → wait for the clipboard to be written → parse → show if it's an item.
**Four steps, and they are meant to stay four.** An earlier version grew a pre-copy snapshot, a
byte comparison against it, a latching write detector, `SDL_EVENT_CLIPBOARD_UPDATE` as a third
accelerator, a "copying…" state, an overdue state and a rule for text identical to what was already
there. All of it existed to answer one question — *is what I'm reading the new copy or the old
text?* — and all of it is gone, because `clipboard_stamp()` answers that question at the source.

`clipboard_stamp()` is an opaque number that changes when and only when something writes the
clipboard (X11: the `selection_timestamp` the server pushes with each XFixes ownership change,
paired with a change counter so two writes in one millisecond still differ; Windows:
`GetClipboardSequenceNumber()`). Take it before injecting, compare after. Equal means nothing was
copied, whatever the clipboard holds. Different means a real copy, even if the bytes are identical
— **which is the common case, not an edge case: re-checking the same item produces the same bytes**,
and that is what the byte comparison could never see. It asks the owner for nothing, so it cannot
perturb a handover and is cheap enough to check every frame; the one real read happens once, after
there is something new to read. Compare it for equality only — the X11 half wraps.

**But a stamp only listens, and Wine only speaks when spoken to** — so each poll of a pending copy
also calls `clipboard_poke()`, one fire-and-forget `TARGETS` conversion request whose reply is never
read. Wine's clipboard manager holds the X selection without rendering the Windows clipboard behind
it, and it publishes by *re-asserting ownership*: exactly the event being waited on. A purely passive
watcher therefore waits forever for a publish only its own asking would cause. This is not a guess —
the rewrite that removed the polling read failed five checks out of five, and in every one the
ownership change landed **0-2ms after the diagnostic `clipboard_targets()` on the give-up line**,
tracking our deadline (which varies 166-200ms with the hotkey's release wait) rather than the
keypress. Wine's window id incremented by exactly one per check, i.e. once per probe. `TARGETS`
rather than a text format on purpose: the answer wanted is the stamp moving, not the previous copy
the owner is still serving.

**A separate, sticky failure lives underneath all of this, and it is not ours to fix.** On
Wayland+KWin, a copy made in a *Wayland* application while the game is running arms a three-way
race: the next poke wakes Wine, Wine asserts the X selection with the item, KWin's Xwayland
clipboard bridge asserts over it 2ms later with the Wayland-side content, and 0ms after that the
selection is dropped to **no owner at all**. Now nothing works — there is nobody to poke, nobody to
read, and Wine still believes it owns the selection, so every later copy in the game stays inside
the prefix. Only a real **WM-level** focus change out of the game recovers it, by making Wine
re-export. `nudge_clipboard_handover` is *not* enough here and the log proves it: it fired, moved
the X input focus (`input=0x9a00037 active=0x8400001`), and Wine did not re-assert — the game never
stopped being `_NET_ACTIVE_WINDOW`. Captured in `ppc-20260805-162746.log`, checks `ECJG` (the race)
and `3NDN` (the stuck state). A drop to no owner is deliberately **not** counted by
`clipboard_stamp()`: it is the opposite of a write, and counting it made an empty read look like a
successful copy of something that "is not an item".

Everything else follows: no fallback to the previous clipboard (that's whatever the user last copied
anywhere, and showing it reads as a successful but wrong price check), and **failure is silent**.
Past `kCopyTimeoutMs` (2s), or when what was copied doesn't parse, the check is dropped and nothing
opens — an overlay narrating its own plumbing is noise over a game, and the debug log has the detail.
The panel is opened only once there *is* an item, so it has no waiting or failure states to draw. A
check that starts while a panel is up hides it first: if the new copy then fails silently, leaving
the old panel would read as a price check of the item now under the cursor.

**Why the timeout is needed at all: left alone, PoE under Wine does not publish its copy to the X
selection.** Measured, not inferred — a standalone XFixes watcher, this application not running at
all, a *manual* Ctrl+C: nothing for ~13s, then ownership moved to Wine within 160ms of the alt-tab
away. The debug-log capture `MMHW` shows the same shape for an injected copy (published at
+12506ms; read and parsed 1ms later). Nothing in the injection path shortens that, and ten rounds
of trying were aimed at the wrong layer. Two things do move it, and neither is the copy: **asking**
(`clipboard_poke`, above — the reliable one, 0-2ms) and the game **losing focus**
(`nudge_clipboard_handover` — a focus-out makes Wine export proactively, which is what the watcher
saw at the alt-tab; kept as a 350ms backstop for when asking doesn't work). Clearing the clipboard
before the copy was tried as a third lever and **made it worse**; don't re-add it.
`PPC_DEBUG_COPY=1` traces the whole timeline to stderr, and the **debug log** below records the same
thing plus everything stderr is too narrow for. The overlay is
**dismiss-on-focus-loss**: once shown it stays until you click away, hit Escape, the X button, or the
toggle hotkey — keeping logical state in sync with what's visible (a stale "still open" state was the
two-press bug).

**Focus is a gate, never something to take.** The hotkeys are grabbed system-wide, so `handle_action()`
drops any action fired while PoE is not the foreground window — otherwise they go off in the user's
browser. The lone exception is the Settings hotkey while Settings is open: that panel holds the
keyboard focus itself, so the game *can't* be foreground, and the hotkey has to be able to close it.
The idle status marker follows the same rule and unmaps whenever the game isn't in front, so it
never floats over other applications — with **our own overlay counting as the game being in front**,
because from the user's side it is: closing a panel that had taken the focus leaves it on our
now-empty window for as long as the compositor takes to hand it back, and the marker used to blink
out for exactly that gap. Focusing anything else takes the focus off us too, so the rule still holds. In the other direction we never force focus *onto the game*:
the copy path used to call `focus_game_window()` on a window it had just confirmed was foreground,
and `XSetInputFocus` on the toplevel can land somewhere Wine didn't put it. Focus is handed back to
the game on close **only** if `overlay_.has_focus()` — i.e. only focus we took ourselves.

Taking focus onto **our own** panel is the one sanctioned exception, and it exists for exactly one
reason: it is the only thing that makes Wine let go of the clipboard (above). `nudge_clipboard_handover`
fires **once per check, at 350ms, only while the game is still in front**, and never in dev mode.
Do not promote it to unconditional. A healthy clipboard — any Windows machine, a native X11 game —
answers the first poll and never reaches the grace period, and taking the keyboard mid-fight for a
copy that was not late is a worse bug than the one it fixes. Its own `[copy]` log line, and the
`input=`/`active=` fields `focus_info()` puts on every poll line, are what says whether it worked.

`App::place_overlay()` gives each screen its own geometry: Settings is a 520×680 dialog centered over
the game, price-check is a **full-height panel docked beside the item's own frame** — right of the
stash if the cursor was in the left half of the game window at hotkey time, left of the inventory if
in the right half (`App::cursor_side()`, sampled before the copy; the user has moved on by the time
the clipboard lands). Panels straddling the middle — vendor, quest rewards — have no correct answer,
so the cursor's half just wins. The frame edges are `Config::stash_edge`/`inventory_edge`, fractions
of the game's **height**, not width: PoE scales its UI with height, so those hold on ultrawide. The
two frames are mirror images — hand-tuned against the live game they came out at 0.617 and 0.616, so
both default to **0.615**; they stay separate knobs only because GGG moves the UI between leagues.
They and `panel_width` are sliders in Settings, which is how to set them: eyeballing them off a
screenshot is not accurate to the pixel, and two rounds of doing exactly that both missed.

**The price-check window is wider than the panel**, by a **gutter** on the side the panel
is *not* docked against — right of a stash-side panel, left of an inventory-side one. `App::layout()`
(`PanelLayout`) says where the panel sits inside it and where the gutter is; the panel's `Begin` uses
that instead of the whole viewport. The gutter exists because a hovered listing's item has nowhere
else to go: ImGui clamps every window to the viewport, and the viewport *is* the SDL window, so a
popup wide enough to read used to land on top of the very listings it was there to be compared
against. It is only as wide as the game actually leaves free (capped at the panel's own width), since
the window still swallows mouse input everywhere it covers.

**The item being priced is drawn at the top of that gutter, not at the top of the panel**
(`draw_item_card`), and the panel therefore begins at the filters — *unless there are no filters*,
which is the case for every item the trade site cannot be asked about (currency, a card,
anything the in-game exchange trades). There the whole search half of the panel is gone, so
the item moves back into the column it left and the panel is the item and its reference prices. Every screen is wider than it is
tall, so the panel's column runs out of *height* long before the window runs out of width — at 1080p
and below the item, the filters and the listings were all competing for the same few hundred pixels.
A hovered listing's item stacks underneath it, which is also the comparison the hover is for. Three
consequences worth knowing: on a game window with no room for a readable gutter (`kMinGutter`, 260px)
the card is not drawn at all and the item goes back to the top of the panel, so that path has to keep
working; `poll_click_away` measures against the **panel** rect plus the card's (`App::set_card_height`)
and not the window's, or a click on the game through the empty part of the gutter would not dismiss
while a click on our own card would; and `place_overlay` logs the geometry it chose, which is the
thing to read when the panel lands somewhere unexpected.

The **idle status marker** replaces the old "● PPC" spike: two lines — `PoPC v<version>` and the data
bundle's version — in outlined yellow at half opacity over the middle of the mana globe, which is
where the game itself has nothing to say. `Config::status_right`/`status_bottom` place it, as offsets
from the game window's bottom-right corner ÷ its height (the same reasoning as the frame edges), and
they are config-file-only. `place_overlay` sizes the window to the text for that screen, so the idle
overlay is a 200×48 rectangle rather than a dialog-sized one nothing is drawn into.

**Settings** lays every row out on one grid via `row()` in `settings_screen.cpp` — ImGui draws a
control's own label to its *right*, which is why nothing passes a visible label. League is a combo
fed by `LeagueService` from `/api/trade/data/leagues`, cached 24h under `cache_dir()`; the payload
repeats each id per realm so it is filtered to `pc`, hardcoded because this binary can only be
driven by a PC client. Two invariants: the dropdown is never empty (fallback → cache → fetch), and
the configured league is never lost — it is the combo preview and is appended as a selectable when
a fetch does not contain it, which is exactly what happens on league-launch day. No request is made
unless Settings is opened. `poe_window_title` is config-file-only, deliberately not in the UI.
**Filter ranges** is two rows of the same shape (`bound_row`), one per side of the interval every
modifier's filter opens to — see `item/range_match`. The percentage box beside each is *disabled*
rather than hidden for the two modes that do not read it: the dialog is sized to hold every section
without scrolling, and a row whose height depends on its own value breaks that for whichever mode
happens to be picked. The size is `kSettingsW × kSettingsH` **capped at the game's height**, since a
dialog taller than the screen scrolls whatever that constant says; measure the content against the
window before adding a section rather than guessing at the new number.

Five SDL user event types are registered as one contiguous block: hotkey `Action`, league result,
data-updater state, trade result, poe.ninja result, currency-exchange result. Async results are **not** routed through `Action` — `handle_action()` gates on
the game being foreground and would silently swallow them whenever PoE is not in front.

### The debug log (`src/util/debug_log.cpp`)

The copy path's failures are rare, unreproducible on demand, and invisible after the fact — so
instead of guessing at another fix, it is **instrumented**. `debug_log` in `config.json` (a
Settings checkbox under Diagnostics, off for everyone by default) opens
`<cache>/logs/ppc-<date>-<time>.log`, one file per run, newest ten kept. It is on `ppc_core`, so it
holds no SDL/X11/curl and every layer can log into it.

- **Every press of the price-check hotkey mints a four-character id** (`begin_check`, minted on the
  *hotkey thread* so the lines the hotkey and clipboard layers write before the SDL event is drained
  carry it too). It tags every line and is drawn in the panel's footer, where clicking it copies it
  to the clipboard — the user reporting "check K7F2 hung" is naming a span of the file, and
  transcribing it by hand is the step that would not happen. Its alphabet excludes `0O1I`.
- **Clipboard contents go in whole, as base64** plus an FNV-1a-64 digest, because whitespace and the
  UTF-8-vs-Latin-1 difference are exactly what the two-texts problem turns on and a log that trims
  them cannot answer the question. Repeated reads of the same bytes log the digest only.
- `debug::trace` writes to the log *and* to stderr under `PPC_DEBUG_COPY`; `debug::log` is
  log-file-only, for the loud lines. `debug::tracing()` guards the round trips that exist only to
  be logged.
- `clipboard_owner_info()` is server-side only — safe to call while waiting on a handover.
  `clipboard_targets()` is a **real conversion request to the owner** and can therefore change what
  it does next; it is asked once, on the give-up line. That warning was not theoretical — it turned
  out to be the *only* thing asking, which is how a diagnostic ended up being the fix (see
  `clipboard_poke`). Suspect it first whenever turning the log on changes the behaviour being logged.

A **system-tray icon** (SDL3 `SDL_Tray`, cross-platform) provides Exit. `Overlay` wraps
the SDL3+GL+ImGui window; `Config` persists to JSON. `PPC_DEV_OVERLAY=1` opens Settings and disables
dismiss-on-blur for local dev; add `PPC_DEV_ITEM=<file>` to open the price-check panel on a captured
clipboard instead, or `PPC_DEV_IDLE=1` to keep the idle status marker up (it otherwise only ever
appears while the game is the window in front).

The Windows binary is **GUI-subsystem** (`WIN32_EXECUTABLE`, entered at `WinMain` in `src/main.cpp`):
a console-subsystem build pops a console window beside an application whose whole UI is an overlay
and a tray icon. Nothing user-facing goes to stdout — `PPC_DEBUG_COPY`'s traces have nowhere to go
there, which is what the debug log is for.

**Icon** (`src/icon.cpp`): `assets/popc_icon.png` embedded as a base85 blob in the generated
`src/icon_data.inc` and decoded at startup with SDL3's own `SDL_LoadPNG_IO` — no image library, no
runtime asset. One surface feeds both the tray and `SDL_SetWindowIcon`. The Windows executable icon
is separate: `assets/popc_icon.ico` via an `.rc` resource configured from `assets/app.rc.in`.

**Fonts** (`src/fonts.cpp`): the UI renders in **Fontin**, the typeface the game itself uses. Four
faces (Regular/Bold/Italic/SmallCaps) are embedded in the executable as base85 blobs in the generated
`src/fontin_data.inc` — no runtime asset dependency. Regular is the default; Bold marks panel headers;
**SmallCaps renders item text**, matching the game. SmallCaps is a separate family, *not* an OpenType
`smcp` feature — load-bearing, since ImGui does no shaping or feature substitution. ImGui 1.92 fonts
are dynamically scalable, so it's one `ImFont*` per face at any size: `PushFont(fonts.bold, 22.0f)`.
`$PPC_FONT_DIR` overrides the embedded faces with on-disk TTFs. Fontin's license nominally forbids
redistribution; bundling it is a deliberate maintainer decision — see `assets/fonts/README.md`.
Fontin also has **no Cyrillic, Hangul or CJK, and no `×`** — so `fonts.unicode` is a fifth face for
text we did not write (trade account and character names, which are routinely none of them Latin),
merged at startup from whatever the OS ships: a Latin/Cyrillic base plus a CJK collection, first hit
from a list of well-known paths, falling back to `regular` and its boxes when there is none. Not
embedded, because a CJK collection is ~19MB — several times the whole executable — and dead weight
for everyone whose results are Latin. The files are **mapped, not read** (`data::MappedFile`, with
`FontDataOwnedByAtlas = false`): ImGui 1.92 rasterizes on demand and keeps the bytes for the life of
the atlas, so a face nothing on screen needs costs address space rather than resident memory. The
mappings are a function-local static and must outlive the atlas — and a pointer *into* that vector
does not, because it grows; what survives a reallocation is the address the file was mapped at, so
`FontBytes` copies that out at map time.

**Fontin's `≤` and `≥` are empty outlines, and its cmap does not say so.** Both codepoints map to a
real glyph id whose `glyf` entry is **zero bytes**, so text laid out with them advances the right
width and paints nothing — not even the missing-glyph box that would have made it obvious, which is
how they shipped looking verified. **A cmap entry is not evidence that a font can draw something;
the glyph's own length is.** So `kBorrowedGlyphs` excludes the two from every Fontin source and they
are merged in from a system face, which is what keeps the digits beside them in Fontin.
`ImFontConfig::GlyphExcludeRanges` is what makes that possible at all — a merged source only ever
serves a codepoint no earlier source claims, and Fontin claims these. Three consequences:
the borrowed source carries `kOnlyBorrowed`, the *complement* of those two codepoints, because
ImGui 1.92 loads glyphs on demand and **ignores `GlyphRanges` while doing it** — without the
exclusion the borrowed face would also serve every script Fontin lacks, quietly replacing the boxes
`fonts.unicode` exists to draw properly;
`math_faces()` is its own list and not `system_faces()`, since covering the Latin scripts says
nothing about the mathematical operators (Noto Sans, the usual first hit there, has neither, having
split them into a Noto Sans Math nobody installs by default);
and the result is **asked rather than assumed** — `FindGlyphNoFallback` after loading, which bakes
on demand and answers null only when no source served the codepoint. That answer is
`Fonts::has_comparison_glyphs`, and the price-check panel spells the two out as `>=` and `<=` when
it is false, because a floor of 46 that loses its `≥` reads as an exact match, which is a different
search. `×` (U+00D7) is the same argument and is left alone only because nothing draws one — it is
absent from Fontin's cmap outright, so adding it to `kBorrowedGlyphs` is all it would take.

**`ppc_core`** is the static library holding everything that needs neither a window nor a network,
so it can be unit-tested headless: `paths`, `config`, `leagues`, `platform/input`, `util/` (including
the debug log, which every platform seam writes into), all of `item/`, all of `data/` except the
updater, and all of `trade/`, `ninja/` and `exchange/` except their clients. The rule is that `ppc_core` links
no SDL3, no ImGui, no X11 and no libcurl. Tests use doctest and link only `ppc_core`.

### Static game data (built)

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

### The item layer (built)

`src/item/` turns clipboard text into a structured item, resolves it against the bundle, and says
what a search for it would ask for. Four steps, deliberately separate: only the middle two need a
bundle, and only the third and fourth encode pricing judgement.

- **`item/parse`** — pure, no bundle, no I/O: `parse_item(text) -> optional<Item>`. Sections split on
  `--------`; the header gives `Item Class`, `Rarity` and one or two name lines. Section *kind* is
  decided in a fixed order (flags → `Requirements:` → `Sockets:` → `Note:` → cosmetic → all-property
  → the first mixed block → usage note → bottom prose → gem lines → mods), because PoE prints no
  other marker. Load-bearing bits:
  - A property line is `Label: value` **with no digit in the label** (mod wordings contain colons).
    The property block's first prose line is the item's type, later prose starting with a digit is a
    flask's own effect and anything else is a property the game writes as a sentence ("Lasts 7.20
    Seconds"). That mixed block is only recognised as **section 1**, so a flavour line that happens
    to hold a colon ("simple ethos: why make the effort") cannot become a property. A property line
    is also the evidence that section 1 *is* the property block — except on a **flask**, which
    always has one and prints no `Label: value` line in it **unless it has quality**: an unquality
    flask turned "Lasts 6 Seconds / Consumes 40 of 60 Charges on use / Onslaught" into modifiers,
    so the class carries the rule instead. The type line never holds a number for the same reason.
    A parenthetical in that block is the buff's reminder text ("(Onslaught grants 20% increased
    Attack, Cast, and Movement Speed)") and rides on the property, as a modifier's does.
  - **Prose is not a modifier, and there are three kinds of it.** A rare's own mods can read as
    prose ("Players cannot Regenerate Life"), so flavour text needs a positive signal: a quoted
    block, or the last prose block of a *unique* that already has mods. The usage note underneath
    it ("Right click to drink…", "Place into an item socket…") is recognised by wording, because it
    sits exactly where flavour does — a flask has both, and taking the last section as flavour turned
    Rumi's Concoction's verse into three unmatchable mods. A leading `-` only reads as a negative
    roll with a digit behind it, or the attribution line "-Rumi of the Vaal" is a mod. Everything an
    info line or a mod-type suffix touches is mods, whatever the prose heuristics say.
    Those rules exist to tell a rare's mods from its prose and fire on the *rarity* line, which is
    why **`Item::is_gear()` is false for a map fragment**: a scarab has no modifiers at all, so its
    effect and its verse both used to come back as unrecognised ones. Its first prose block is the
    description and anything after it is flavour — the Maven's Writ prints only a verse and there
    is nothing to tell the two apart, so that one is read as the description.
  - Mod type comes from the ` (implicit)` / ` (crafted)` / … suffix, else from an Advanced Mod
    Descriptions info line's generation words, else Explicit. A flask enchant carries no suffix, so
    on a flask the earlier of two unsuffixed sections is the enchant.
  - The info line's em-dash segments are tags **and** "— 20% Increased", which is a catalyst saying
    it scaled this mod. The clipboard prints the *unscaled* roll and range in that case (`30(20-30)`
    where the tooltip reads 36%), so `roll_incr` is applied to both in `match_stat`. A plain `" - "`
    separates them too, because a Latin-1 clipboard read degrades the em dash — see the clipboard
    seam above. Miss that and the line stays one blob: no tier, no tags, and `generation` never
    ends in "Prefix", so the affix is unknown.
  - Influence lines sometimes arrive glued to the end of the last mod block instead of in a section
    of their own; trailing flag lines are peeled off before the block is parsed as mods.
  - A **gem** has no rolled mods: its stat lines are `inherent_lines`, its skill text `description`.
    Three things are pulled out because they are the whole of what a gem is priced on.
    `gem_level` is the `Level:` in the **property block** — the clipboard prints that label twice
    and the one under `Requirements:` is the character level to socket it, a different number on
    every gem past the first. `transfigured` is a flag line like `Corrupted`. And `vaal_name` is
    the lone-line section heading the second half of a **Vaal gem**, which is two skills in one
    item: the *name* line prints the base skill ("Blight") and only that heading says this is a
    Vaal Blight. `Item::gem_name()` puts the two back together into the one name both markets
    file the gem under — the Vaal skill, or for a transfigured Vaal gem the pair the trade site
    and poe.ninja both write as "Vaal Blight (Blight of Atrophy)".
- **`item/resolve`** — needs the bundle. Two jobs the matcher cannot do alone:
  - **Local vs global.** The bundle keeps a second record for local stats, marked by a **`" (Local)"`
    suffix on the matcher string**; "20% increased Attack Speed" is the weapon's own speed on a
    weapon and the character's anywhere else, and the two have different trade ids. The clipboard
    never says which. `kLocalWordings` lists the 20 wordings that have a local twin and what the item
    must display for it to apply (a weapon, or the defence the wording names). Get this wrong and the
    price check silently searches the wrong stat.
  - **Affix ≠ stat.** An info line groups every line of one affix, but "+34 to Armour" and "+28 to
    maximum Life" from one prefix are two trade filters, so `split_affix` walks the group and lets
    the matcher say how many lines each stat takes (genuine multi-line stats exist — a cluster
    jewel's enchantment). Without info lines the same walk merges hybrid lines instead, driven by
    `StatMatch::lines_consumed`. Every part keeps the affix's name/tier/tags; `continuation` marks
    the ones that must not print the info line again.
  - **The gem's own record** (`resolve_gem`, `Namespace::Gem`), looked up on `Item::gem_name()`.
    What it is there for is `BaseType::trade_name`: **trade files a transfigured gem under the
    skill it alters**, so "Raise Zombie of Falling" is the type `Raise Zombie` with the `alt_y`
    discriminator, and a search naming what the clipboard printed matches *nothing* while the
    bare type matches the unaltered gem — a real, far cheaper item. The discriminator is also
    what tells two records under one key apart, which is the shape a bundle published before
    the display names existed has: three "Vaal Blight" rows, only one of them the plain gem. A
    transfigured gem is exactly the one with a discriminator, so nothing falls back to
    "whichever came first".
  - **A card's own record** (`Namespace::DivinationCard`). Nothing about a card is *searched* — it
    is `Strategy::Currency` and the in-game exchange is the whole answer — but that answer is
    keyed on `BaseType::metadata_id`, which only a resolved base carries, so a card that fell
    through this had no base, no metadata id and therefore no price at all. An essence needed
    nothing: it is an ordinary `Namespace::Item` base and already resolved.
  - **Which uniques an unidentified one could be** (`Item::unique_candidates`, off
    `find_uniques_on_base`). An unidentified unique prints **one** name line and it is the
    base's, so the base is the whole of what the item says about itself and the bundle's
    base → uniques index is the only thing that turns it back into a name. **One candidate is
    not a guess** — that base rolls into exactly that unique — so it is taken, and everything
    downstream plans, prices and searches as if the item had named itself. Several is a
    question only the player looking at the art can answer (`Item::needs_unique_choice`,
    settled by `choose_unique`, which refuses anything outside the candidate list so a pointer
    from the previous item or from a swapped-out bundle can never be what gets searched).
    None is two different facts and `GameData::has_unique_bases()` is what separates them.
- **`item/derive`** — the numbers the game leaves implicit. **Quality scales the base's own inherent
  value and nothing else**: not the flat local rolls added to it, though the local *increases* then
  apply to both. One rule for a weapon's physical damage and for armour / evasion / energy shield /
  ward, differing only in which mods count as local:
  `displayed = (base * (1 + q/100) + flat) * (1 + incr/100)`, inverted by `inherent_roll`.
  Do **not** replace it with the additive `v20 = v * (120 + incr) / (100 + q + incr)`, which is what
  the reference tools compute and what "quality is additive with increased physical damage" describes:
  it recovers a base ~8% too high whenever an item carries both quality and a flat local roll, and on
  the Rift Carapace capture (`examples/item_6`) that puts its energy shield at 316.8 against a
  Twilight Regalia range of 262..302 — outside its own base, so the percentile is lost. The inherent
  form puts it at 293.3, the 78th percentile. That capture is the only real evidence either way; a
  before/after-quality capture of one item with a flat local roll would pin it down.
  Local increases and flat adds are found from the *wording* (`placeholder_form`), so they still count
  when the stat itself is missing from the bundle. **Base percentile** (`Derived::base_pct`) is
  `inherent_roll` placed in the base type's range; a result outside that range means a local mod was
  missed, and then it reports nothing rather than a confident 0%. It is **one number per item, not
  one per defence**: a base rolls a single value and spreads it over the defences it has, so the
  sum of the recovered inherent values goes into the sum of the base's ranges — an armour/energy
  shield hybrid whose two percentiles disagree is showing rounding, not two rolls. A defence with no
  published range makes the two sums incomparable, so there is no percentile at all. There is no
  weapon percentile: the bundle publishes defence ranges for armour bases but no damage ranges for
  weapon bases. It is a **filter and not a remark** — `armour_filters.base_defence_percentile` on
  the trade site — and it is **floored, never rounded**, because the filter is a minimum and a
  78.6th-percentile item asked for at 79 does not match itself. Ticked only on a `BaseItem` plan,
  where the base's roll is what is being bought; on a modifier search the defence totals already
  carry it, and asking the same question twice only drops the listings that answer it once.
- **`item/range_match`** — how wide a filter opens around the roll. It is the one input here that
  is a **setting rather than a fact about the item**, which is why it arrives from outside as a
  `RangeMatch` and `Config` is the only thing that owns one. Each side of the interval is
  `Unbound` (fill nothing), `Exact` (the roll), `Within` (a percentage of it) or `WithinTiered`
  (the same, gated by what the modifier's own tier can roll), and the default is **tier-gated 5%
  on both**: what a buyer wants is a copy that rolled about what theirs did, and a bound outside
  the affix's own tier can only drop the listings that answer the question exactly. Four things
  that are easy to get wrong.
  The window is **rounded outwards at the filter's own last digit** — floor the lower bound, ceil
  the upper — so rounding never asks for a roll the item in hand does not have, and any non-zero
  percentage moves the bound by at least one digit (5% of 20 is exactly 1, 5% of 1 is a twentieth,
  and both still move by one).
  The slack comes off the **magnitude**, so a negative roll widens outwards like a positive one:
  -11 opens to -12..-10 rather than to that pair read backwards.
  **`lower_is_better` swaps which mode governs which side**, because "Minimum" means the bound
  that says *at least this good* and on a modifier the game prints negative that is the upper one.
  It is invisible while the two modes agree — a symmetric window is symmetric either way round —
  which is why the tests state that case with one side `Unbound`.
  And the tier gate **never crosses the roll**: a legacy roll sits outside the range its modifier
  publishes today, and gating to that would ask for a copy of the item that is not this one.
  `WithinTiered` falls back to `Within` where no tier is known at all.
  It seeds **stat filters on every strategy** — the old split, where a `Modifiers` plan took the
  whole tier range and everything else took "no worse than this", is gone; both are now points on
  the same dial. It deliberately does *not* touch the numeric filters: those are thresholds on a
  total ("at least this much armour"), and a maximum on one rules out the strictly better items a
  buyer would still take.
- **`item/plan`** — `SearchPlan`: strategy, category/name/type, corruption, influences, stat filters
  and numeric filters, plus **`notes` for everything deliberately left out**. Strategy decides what
  matters: `Modifiers` (magic/rare) enables every mod and seeds its bounds off the roll it made
  (how wide is `item/range_match`, above), and names no base — **except on a flask**, whose base is
  half of what its mods are worth (the same suffix is a sought-after roll on a Quicksilver Flask
  and nothing on a Ruby one, and trade files every flask under one category, so the `type` is the
  only place to say which). Only ever off a **resolved** base: an unstripped magic name
  ("Surgeon's Quicksilver Flask of the Cheetah") as the `type` matches nothing, which reads as
  nobody selling one, so an unknown base is a note instead. `BaseItem` (white, or a rare the user
  switches over) searches the base with item level and influences and enables only fractured mods and non-inherent
  implicits; `Unique` searches the name and enables a roll the **per-unique modifier data** says comes
  from a pool (see below), a roll a range proves is variable, any mod *added* to the unique —
  `{ Foulborn Unique Modifier }`, i.e. `Modifier::added_unique()`,
  which not every copy of that unique carries — and anything the player *crafted onto this copy*
  (`added_to_copy`: enchant, crafted, fractured, scourge, veiled, crucible). An enchant costs
  currency and is most of what an enchanted copy sells for, so leaving it out prices a different
  item. A `Maps` item class is `Map` at every rarity it prints — see below. A **map
  fragment** (scarab, ember, splinter, invitation) is `Currency` whatever its rarity line says —
  see the poe.ninja section.
  **A number that is not a roll is not a bound.** A fixed modifier says the same thing on every
  copy of itself, and asking the trade site to compare its number asks it to compare a value it
  does not index the stat on — which matches *nothing*, so the price check comes back empty and
  reads as an item nobody wants. Measured, not inferred: a map's Baran implicit ("Item Quantity
  increases amount of Rewards Baran drops by 20% of its value") returned **0 listings with
  `min: 20` against 1705 without it**, and "Area is influenced by The Elder" — whose number is
  not in the clipboard at all, but the constant `StatMatcher::value` substitutes for the
  influence — **0 against 10000**. The filter stays and only its number goes, so the search asks
  for the modifier being present, which is the only thing a fixed modifier can be asked about.
  What says a number is fixed is that the game printed **no range beside it**, and that is only
  evidence on an item that printed ranges *somewhere* — with Advanced Mod Descriptions off
  nothing carries one, and reading their absence as "fixed" would strip the floor off every real
  roll and search a rare for "has a life modifier". Hence `ranges_printed`, asked of the whole
  item, because the setting is a property of the owner rather than of the modifier. A **map**
  needs no such evidence and is exempt: no number one of its implicits or enchants carries is
  ever a roll.
  **A tier or a rank is itself a range**, printed or not, and outranks all of the above: a
  different tier is a different number, so "no worse than what this one gave" is a real question
  even where the tier holds a single value. It is also the only way an **eldritch implicit** can
  say its number moves — its magnitude comes from the tier of the currency that put it there, so
  the clipboard has no range to print and states the rank instead
  (`{ Eater of Worlds Implicit Modifier (Lesser) }` → `Modifier::qualifier`).
  **Where the bundle knows a range the clipboard does not print, the bundle wins**:
  `apply_unique_mods` restores the bound the moment the per-unique data says the modifier rolls,
  because a range is a range whichever source stated it. The one thing that does *not* work in
  reverse is a record calling a modifier fixed — the item's own printed range outranks a record
  about the unique in general.
  A filter with one side left open asks for "no worse than this", and **worse is not always
  smaller**: a mod the game prints negative is better the more negative it is (an eldritch implicit
  applying `-11%` to Cold Resistance — its magnitude comes from the currency tier, so the clipboard
  prints no range to bound it with), and so is a stat the bundle marks `better: -1`. Both put the
  open side at the top, which is what `seed_bounds` swapping the two modes does. The sign is what
  carries the direction for the rest, because the canonical wording
  already does — "#% reduced Mana Cost" is stored as a negative increase. It reads wrong only for a
  negative roll of a stat that also rolls positive, i.e. a resistance penalty, which is a drawback
  on a unique rather than something a buyer searches for.
  Two rules that are easy to get wrong: trade indexes **repeated stats as their total**, so
  `merge_same_stat` sums two life rolls into 104–117 rather than filtering twice; and an
  added-damage mod is indexed as **the average of its two numbers** while every other multi-number
  wording is indexed on its **first** ("15% chance to Unnerve … for 4 seconds" is searched on 15,
  not on 9.5) — hence `StatMatch::roll_bounds` being per roll.
  The **weapon numerics** are the three DPS totals, plus attacks per second and critical strike
  chance — and those last two are ticked **only where the game printed the property augmented**,
  i.e. where a modifier on this copy raised it above the base's own. Every weapon has both numbers
  and on most of them they are the base's, so asking for one rules out nothing but the same weapon
  in somebody else's stash. The augmented marker is the whole of the evidence: the bundle publishes
  no base crit chance or attack speed to compare against.
  **A modifier already inside a searched number is not searched again by name**
  (`unimpose_derived_mods`, off `derived_filter_keys` in `item/derive`). A local roll is not
  something the item has beside its armour — it *is* part of the armour the item displays — so a
  query carrying both the number and the modifier behind it asks one question twice, and the
  second asking is the brittle half: a flat roll and a local increase reach the same armour by
  different routes, and naming this item's route rules out every other way of arriving at the
  number the buyer wants. So the derived value is imposed and the modifier is only offered —
  *left* in the list, not removed. It is conditional on the derived filter actually being
  enabled: on a unique, where the defences and DPS are offered rather than imposed, the modifier
  is all there is to ask about.
  **A fractured roll is the exception and keeps its filter.** It cannot be re-rolled and it is
  what survives every craft the buyer will do afterwards, so *which* modifier reached the number
  is the point of the item rather than an over-constraint on it — and trade indexes it in a
  namespace of its own (`fractured.stat_…`, which `to_filter` already sends off `Modifier::type`,
  alongside the item-level `misc_filters.fractured_item`), so it is a different question from the
  same wording rolled ordinarily. A crafted roll gets no such exemption: a bench craft is
  something any buyer can add.
  **Locality is the whole of it** and is decided in `item/derive` rather than in the data, from
  the same wordings and the same guards `sum_locals` uses — "20% increased Attack Speed" is the
  weapon's own only on a weapon, and "#% increased Energy Shield" the item's own only where the
  item displays energy shield. Attack speed feeds `aps` *and* all three DPS numbers; added
  elemental damage feeds `edps` and `dps` but not `pdps`; "#% increased Elemental Damage" feeds
  none of them, because it never touches what the weapon displays. The base percentile is the one
  derived number a local roll is **not** inside: it is recovered by taking those rolls back out.
  **Everything the site takes as an `{"option": …}` is a `SearchPlan::options` entry** — the
  booleans (corrupted, mirrored, foulborn, identified, blighted), and the closed vocabularies (a
  chart's shape, a Valdo map's payout). One struct, because the wire form is one thing and only the
  source of the string differs; `option_group_for` in `trade/query` is what files each under
  `misc_filters` or `map_filters`.
  The rule for the booleans is one line: the search
  asks the item to be what it is, and it says so out loud only where that is not the ordinary
  answer. `OptionFilter::shown` is the whole of the struct's reason to exist — an uncorrupted,
  unmirrored, unmutated, identified item is what nearly every check is about, so those four are
  imposed with no row at all, and four rows saying nothing is unusual would push the modifiers off
  the panel. The *unusual* value gets the row, because that is the one a buyer might want to widen
  back out: a mirrored item cannot be crafted on, an unidentified one is a different product, a
  corrupted one is a different market. Synthesis and fracturing are asked in one direction only —
  evidence about the copy in hand rather than a choice, and an ordinary item's search has no
  reason to rule out the strictly more constrained copies. **`identified` is not asked of a gem
  or a currency item**, measured rather than assumed: `identified: true` returns **0 listings**
  under `category: gem` and **0** for a Facetor's Lens, against 10000 and 177 without it, because
  trade indexes the flag only for what can be unidentified. `mirrored: false` was checked the
  same way and is safe everywhere.
  **Foulborn is one of those booleans and the site's key for it is `mutated`.** Chayula's
  mutation is a different item at a different price — measured on Tulfall: 3855 listings in all,
  1896 not foulborn and 1960 foulborn, and the mutated ones *cheaper* — so a search that leaves
  it open prices the two markets together and undercuts the copy in hand. Nothing about it is a
  flag line: the game states it as a prefix on the name ("Foulborn Romira's Banquet") and as the
  info line of the modifier it added, and `parse_item` takes either, the name being the half that
  survives Advanced Mod Descriptions being off. `mutated: false` is safe everywhere `mirrored`
  is, checked the same way (655/655 gems, 1299 Facetor's Lenses, 10000 wands and 10000 tier-16
  maps either way), so it is imposed at every strategy even though only a unique can be one.
- **`item/plan`'s three property filters** (`add_property_filters`) are the `misc_filters`
  intervals that come off a **property line** rather than off a modifier, so none has a tier to
  gate against and none gets a window: the number is what this copy has, and all a filter can say
  is "no worse". **Which side that leaves open is the judgement**, and it differs per property.
  **Memory Strands** (1–100) are spent to raise the tier of a modifier a craft adds, so more is
  more of what is being bought — a floor, ticked. **Intangibility** is the opposite: the penalty
  an item accrues from Allflame crafting, the chance the next craft comes back with one outcome
  instead of several, so less is better and it is a **ceiling** — left unticked, since a buyer
  who will not craft on the item does not care what it accrued. **Stored Experience** is the one
  thing telling two copies of a Facetor's Lens apart.
  That last one is why **the Facetor's Lens is the one currency item with a trade search**. Every
  copy holds a different number, so they are listed individually rather than traded by the stack,
  and naming the type is all a search needs — the same shape as the map fragment that prints an
  item level: what says a currency item is not interchangeable is that it prints something no
  other copy of it does. The strategy stays `Currency` (poe.ninja still prices it in the currency
  market, which is the floor under the search) and `trade::searchable` reads the `type` the plan
  filled in, exactly as it does for a gem.
- **`item/plan`'s per-unique join** (`apply_unique_mods`) is what makes a unique searchable at all.
  A unique's modifier can be variable **without printing a range**: Ralakesh's Impatience rolls one of
  three charge mods, each `1..1`, and the clipboard prints that exactly like the four every copy has.
  The bundle's `en-unique-mods.ndjson` says which mods are fixed and which come from a pool, so a
  pooled mod is enabled and labelled with the pool's own prose ("Random charge modifier"), and a mod
  the record calls fixed is now left out *knowingly* instead of with a warning. Three rules:
  **join on the trade id, never on the wording** (wordings are shared by two stat records, which is
  exactly what the ids disambiguate); **never disable** — the item's own printed range outranks a
  record about the unique in general; and **only trust a range that contains the roll**, because the
  bundle carries no `dp` for every stat and a range can arrive 100× the roll it bounds
  (`0.4% of Physical Attack Damage Leeched as Mana` against `40..40` — see `examples/item_3`), which
  would otherwise call a fixed mod variable. Pool membership is a fact about the item rather than a
  number, so it survives that check. A mod the record does not have is reported, never dropped
  silently: it is either something added to this copy or a mod the source has not caught up with, and
  both are what a buyer is searching for. **Except a mod `added_to_copy` covers** — the record
  describes the unique, not what was crafted onto one, so its absence there is by definition and
  "not in the modifier data" reads as a failure to recognise a modifier that is right there in the
  filter list.
  **Reported on the row and not underneath the list** (`StatFilter::caveat`, a hover tooltip):
  the row already names the modifier and shows its box unticked, so a note is that wording a
  second time — three lines of panel each. Triad Grip is the case that proved it: its four
  conversion modifiers are unlisted in the record *and* printed on the item, so both note
  families fired for each and twelve lines went on saying what four unticked boxes had said.
  So `UniqueMods::unlisted` is a caveat on the row wherever the item actually prints that
  modifier, and stays a **note** only for prose with nothing on screen behind it ("4 random
  Charm modifiers") — which is the case the note exists for, since there is no other way to
  say the app is leaving something out.
  [UNIQUE-MODS.md](UNIQUE-MODS.md) is the dataset's contract, including what it does not cover.
- **`item/plan`'s unidentified unique** (`plan_unidentified`) searches the name `item/resolve`
  worked out from the base, plus the **item level** — a floor, ticked, because it is the one
  number an unidentified copy carries and it bounds what the item can still turn out to have
  rolled. The `Unidentified` flag itself needs nothing new: `add_item_flags` already asks the
  item to be what it is, and an unidentified one is exactly the case that flag has a row for.
  Everything else about the item is behind the identification, so there is nothing else to
  carry. Where the base rolls into **several** uniques and the user has not picked one yet,
  there is no name and therefore no search — `App::can_search()` is false, the panel asks
  instead (below) — and the plan states which question is open rather than running a search
  for "some unique of this base", whose cheapest listing would read as this item's price.
  Two notes ride along: the name the app took for itself when a base had a single unique, since
  nothing on the item printed it, and that a reference price is what *identified* copies sell
  for — poe.ninja does not split a unique by that, and an unidentified one is the gamble on the
  rolls rather than the rolls.
- **`item/plan`'s map strategy** (`plan_map`, `add_map_pseudo`) is `Strategy::Map`, and it is the
  one strategy that searches on **none** of an item's affixes. A map's prefixes and suffixes are
  re-rollable with a single Chaos Orb; the buyer is choosing how dangerous a map they want, not
  which affix it has, and a query naming them would find the one copy in the league that rolled
  that set. So they are not filters and **not notes either** — they are left out on purpose, in
  front of the reader (the item card beside the panel), and "unrecognised modifier: Players have
  25% less Accuracy Rating" would charge the check with failing at something it never attempted.
  What is searched instead:
  - **Which area it is.** Every ordinary map now shares the one base type, printed as
    `Map (Tier 16)`, so the tier is the whole of what tells two apart; `parse_header` takes it off
    into `Item::map_tier` because no lookup knows the parenthetical, and `draw_name_plate` puts it
    back on screen. The filter is `map_filters.map_tier` with **min == max**: a tier-16 map is a
    different area from a tier-14 one, not a better one. A map that names its own area instead
    (`Shaper Guardian Map`, `Nightmare Map`) prints no tier and is matched by that name alone.
    A **unique** map is its name plus that tier; its own modifiers are on every copy. The
    `map` **discriminator** the bundle's `Map` record carries is load-bearing here rather than a
    tie-break: a query sending the type as a bare `"Map"` is accepted and matches nothing, which
    reads as an empty market rather than as a search that could not be built — so a bundle
    without that record gets a note instead.
  - **What the map does for you.** `map_iiq` and `map_packsize` on by default, `map_iir` off —
    rarity is a preference, and imposing it drops the cheaper copies of the same map. All three
    come off the game's own property lines.
  - **The four drop bonuses a Maven's chisel adds** — `More Maps`, `More Scarabs`, `More Currency`,
    `More Divination Cards` — which the game also prints as *properties* and which trade has no
    `map_filters` entry for. They are `pseudo.*` stats (`pseudo_map_more_map_drops` and its three
    siblings, which is all of them in `/api/trade/data/stats`), enabled when present.
  - **How many affixes it has, as a total**, and **only on a corrupted map**: six is what every
    rare map has, and eight is what only corruption allows and most of what such a map is worth.
    `pseudo.pseudo_number_of_affix_mods`. The side of the pool is printed only by Advanced Mod
    Descriptions, so with that off there is no count to give and the plan says so rather than
    counting zero. Continuation lines are counted out — one affix can print two.
  - **The implicit and any enchant**, on by default, and **on presence rather than on their
    numbers** — see the bound rule above, which a map is exempt from needing evidence for. The
    implicit is the one modifier a currency cannot re-roll, and it is what names the boss, the
    influence or the memory.
  - **Blight, which is a filter and never a type.** The base line is the only place the clipboard
    says so — `Blighted Map (Tier 12)`, `Blight-ravaged Map (Tier 16)`, no flag line and no
    property — so `parse_header` sets `Item::blighted` / `blight_ravaged` off it and
    `resolve_base` then points the base at the ordinary `Map` every other one shares. Sending
    `"Blighted Map"` as the `type` is *accepted* by the site and matches nothing at all
    (measured: 0 listings against 1449 for the Map base plus `map_filters.map_blighted` at tier
    12), and no bundle carries a base under that name either. The two flags are mutually
    exclusive, so neither is ever asked for in the negative: a blighted map's own search already
    excludes the ravaged ones.

  **A chart is a map under another name and shares the strategy** (`plan_chart`), which is the
  point rather than a shortcut: a Deepwater chart is an area with rolled danger and rolled
  rewards, its prefixes and suffixes are the danger a buyer is choosing among rather than the
  thing being bought, and trade puts it in the same filter group — `map_filters` is titled
  "Map/Chart Filters" for that reason. So the affixes are left out exactly as a map's are, and
  four things are added on top:
  - **Which area it covers.** The game prints it as the leading prose line of the property block
    ("Seafloor Ridges"), i.e. `Item::type_line`, and trade takes it as the **type** — as an
    option carrying the `chart` discriminator, whose value is the area's *internal id*
    (`SeafloorRidges`). The bundle carries those records, but only under that id and with no
    display name anywhere on them, so `chart_area_key` turns the printed name back into one:
    apostrophes go, spaces and hyphens are word breaks, every word is capitalised
    ("Brine King's Domain" → `BrineKingsDomain`, "Clam-infested Shelf" → `ClamInfestedShelf`).
    **That convention is only ever a lookup key** — a record has to come back under it carrying
    the discriminator, or the search falls back to the chart's own base type ("Coral Reef Chart",
    which is a real search and simply a coarser one) plus a note. A wrong guess therefore costs
    breadth, never correctness.
  - **The area's level**, `map_filters.area_level`, **exact** for the same reason a map's tier is:
    a level 83 area is a different area from a level 78 one, not a better one. Measured: 5526
    listings at exactly 83 against 10000+ for the area alone, so it does discriminate.
  - **The shape** — `chart_shape`, whose five ids are `1`–`5` for End/Corner/Straight/Junction/
    Crossing. The game prints the option's own **text**, which is what makes the join possible;
    sending that text answers `{"code":2,"message":"Invalid chart shape"}` and fails the whole
    search, so it is a table in `plan_chart` copied from `/api/trade/data/filters`, the same
    closed-vocabulary argument as `status_options`.
  - **The sulphur** ("Dead Man's Sulphur: +45%") as `chart_sulphur`, a floor and ticked: it is
    the league's own currency and therefore what the area is run for, so the same reasoning as a
    map's quantity rather than as its rarity.

  Its **voyage modifier** needs nothing new — it is an implicit, so the map strategy already
  enables it, **including on a chart nobody has sailed yet**, which prints only the promise of
  one ("Voyage Modifier will be revealed once Charted"). That promise is a real stat with a real
  trade id, not prose, and a buyer choosing between a revealed and an unrevealed chart is
  choosing on exactly it.

  **A Valdo map is the one map that is none of the above** (`plan_map`'s `reward` branch). It is
  bought for the unique it pays out, its quantity and pack size come from unique modifiers rather
  than from an affix roll, and so those are *offered* rather than imposed — they say nothing about
  which Valdo map a buyer wants. What is searched is `map_filters.map_completion_reward`, an option
  over the **unique list**: the game prints the payout as `Reward: Foil Hrimsorrow`, where the foil
  is the reward's own variant, and sending that whole string answers
  `{"code":2,"message":"Unknown reward output provided"}` — which fails the entire search rather
  than widening it. So `find_unique_in` takes the longest run of those words that names a unique
  the bundle knows, and a reward it cannot name becomes a note instead of a guess. The `Reward`
  property is also the marker: no other map prints one.

  The other half is **the only thing anything here is searched on in both directions**. Whether
  dying in the map sends the character to the Void is what a buyer picks on, and a map that voids
  is a different item from one that does not — so the copy in hand decides which question is asked:
  present, and the search asks for it; absent, and it asks for the *absence*. Leaving it open
  prices the two together. That is `StatFilter::negated`, which `build_query` sends as a second
  stat group of type `not` beside the `and` one, carrying an id and no bounds. Measured on the same
  reward: 133 listings that void against 101 that do not.

  Two things this needed elsewhere. `StatFilter::mod_index` is an `optional` now, because a pseudo
  total has no single modifier behind it; anything walking back to `Item::mods` has to check.
  And **`SearchPlan::rarity` carries the trade `rarity` option**, because a unique map is planned
  as a map — reading the option back off the strategy, as `build_query` used to, searched it
  among the rares. It defaults to `nonunique`, since an empty one is a search across both markets
  at once and nothing here ever means that.
- **`item/plan`'s gem strategy** (`plan_gem`) is the shortest search here and the only one whose
  filters are *all* numeric: the name, `misc_filters.gem_level`, `misc_filters.quality`, and the
  corruption every strategy already matches exactly. Everything a gem prints is what the skill
  does and is identical on every copy of it, so there is nothing to turn into a stat filter and
  nothing to leave a note about either.
  **Level and quality are exact — `min == max`, the same reasoning as a map's tier.** A level 21
  gem is not a better level 20 one, it is what the gem sells as; a floor would put 21/23
  corrupted gems into the results for a 20/20 and price a different item. Quality is filtered at
  zero as readily as at twenty, because an unquality gem is a different thing from a 20% one and
  no filter at all prices it as whichever quality is cheapest. Corruption is the hard split
  underneath both: it is what allows level 21 and quality 23 to exist.
  The name is the **record's**, never the printed one — see `item/resolve` above and
  `Item::gem_name()` — and a gem the bundle cannot name gets **no search at all**, only a note.
  That is `trade::searchable` returning false on an empty `type` for this strategy alone: every
  other strategy still has modifiers or a category to fall back on, while a gem falls back to
  every gem in the game at this level, whose cheapest listing would read as this gem's price.
  poe.ninja still prices it, so the check is not empty. In practice this only happens on a
  bundle older than `data-20260807.23`, the release that keys gems on their printed names, and
  then only for transfigured gems.

### The trade layer (built)

`src/trade/` turns a `SearchPlan` into a search on pathofexile.com and back into listings.

- **`trade/query`** — pure, no network: `build_query(plan)` is the search JSON, plus the URLs and
  the response parsers. **`StatFilter::inverted` is applied here and nowhere earlier**, and it flips
  the interval end for end as well as in sign: 77..90 as the game prints it is -90..-77 as the site
  indexes it, so a floor becomes a ceiling. Only ticked filters are sent. `group_for` is the
  contract with `item/plan`'s `NumericFilter::key` — the API nests every filter under a group
  (`misc_filters`, `armour_filters`, `weapon_filters`, `map_filters`) and rejects one filed in the
  wrong place. `option_group_for` is the same contract for `SearchPlan::options`, which go out as
  `{"option": …}` under `misc_filters` or `map_filters`; an **unticked one is not sent at all** —
  whether an option has a row in the panel is the plan's business, and this layer only reads
  `enabled`.
  Whether the search names a `type` is the **plan's** call and this layer sends whatever it was
  given: a `Modifiers` plan leaves it empty (a rare is bought for its mods, and the category
  already says where those can live) **except on a flask**, where it names the base.
- **Which listings to ask for** is `Config::listing_status`, and it defaults to **Instant Buyout**
  (`securable`) rather than the API's older `online`. Not cosmetic: on one real capture the same
  query returned 4 matches In Person against 39 as Instant Buyout, because an offer that can be
  taken without the seller being at their keyboard is what most people now mean by "for sale".
  The five ids and their labels are GGG's own, copied from `status_filters` in
  `/api/trade/data/filters` — a closed vocabulary, so it is a table in `trade/trade.hpp` rather
  than something fetched, and unlike `league` a configured value **is** validated against it on
  load: an id GGG does not know makes every search fail with "Unknown status type".
- **`trade/ratelimit`** — the limits are not guessed at; GGG publishes them in the response headers
  of every request (`X-Rate-Limit-Rules` names the groups, `X-Rate-Limit-<group>` the
  `hits:period:restriction` rules, `-State` the server's own counters). So the first call under a
  policy is spaced against a seeded default and every one after it against measurement. **The
  state header outranks our own tally** rather than adding to it — it counts every client on the
  IP, including the user's browser tab. Time is a parameter, not a call, so the whole thing is
  unit-tested without sleeping.
- **`trade/ratelimit_store` — the limiter survives a restart.** `snapshot`/`restore` state the
  windows as *ages* and the restriction as *time remaining*, so they can be written under one
  clock and read under another; the store converts to absolute wall-clock ms in
  `<cache>/trade-ratelimit.json`, written on every request and every response. Without it,
  closing and reopening the app clears an active restriction it never actually served, and the
  seeded budget gets spent straight back into the lockout — repeatedly hammering through
  restrictions is how a client stops being throttled and starts being blocked. `restore` runs
  **before** `seed`, which then declines to overwrite it, and clamps what it reads (no negative
  window ages, no block over six hours) so a corrupt file cannot wedge the client shut. It is not
  a security boundary — deleting the file resets it — it stops the accidental circumvention,
  which is the one that happens. The decisions still run on `steady_clock`; the wall clock is
  only ever written down and read back.
- **How many listings to fetch** is `Config::result_count`, a Settings dropdown over 10/20/50/100,
  defaulting to **20**. This is a rate-limit choice and not a latency one: every ten listings is
  one more fetch request, and the binding policy (`50:300:300`) allows fifty fetches per five
  minutes before a five-minute lockout — so Top 20 is 25 price checks in that window, Top 50 is
  10, and Top 100 is 5 *and* trips the 16-per-12-seconds rule on two checks in a row. The cost
  also lands where the extra rows help least: only `min(want, total)` is fetched, so a rare with
  four matches costs one request at any setting, and the bill arrives on liquid items where the
  cheapest twenty already set the price. Settings states the cost on the row itself.
- **`trade/client`** — the one place outbound GGG traffic goes, `LeagueService` included. Waits out
  the limiter, issues the request, feeds the headers back in. A debt longer than 30s is returned as
  an error instead of waited on: a price check that lands four minutes late is about an item the
  user has sold. The wait is slept in slices against `cancel_waits()`, so shutdown does not sit out
  a restriction. `run_search` is the two-step flow — POST the query, then GET the first
  the requested number of hashes in batches of **at most ten**, which is a hard API limit. A batch that
  fails after an earlier one succeeded keeps what it has: ten listings are still a price, and the
  error explains the short list rather than replacing it. `fetch_page` is that batching loop on
  its own, which is also what **load more** spends: the search POST returns **100 hashes however
  large `total` is**, so paging deeper costs one /fetch and no search until those hundred run
  out — past which there is nothing to page to and the search has to be narrowed. `fetched`
  tracks hashes *asked for*, not listings received: a listing sold since the search comes back
  as a null element and is dropped, so paging off the listing count would re-fetch what was
  already seen.
- **`TradeService`** (`src/trade_service.cpp`) is `LeagueService`'s twin, for the same reason — every
  member on the main thread, the worker owning only its stack and the payload it hands over through
  the SDL event queue. The query JSON is built **on the main thread** and moved to the worker: the
  plan points into the data bundle the updater can swap at any moment.
- **Currency symbols** come from `/api/trade/data/static` (cached a week under `cache_dir()`), whose
  `image` paths are rooted at `web.poecdn.com`. `IconCache` (`src/icon_cache.cpp`) splits the work
  the way the threads force: the worker downloads (through a disk cache keyed by the URL's sha256,
  so a second launch makes no requests) and decodes to an `SDL_Surface`, and `pump()` uploads to a
  GL texture at the top of a frame, because only the frame loop has the context. `texture()` is
  allowed to answer "not yet" — a price with no symbol still prints its amount — and a URL that
  failed is never retried, or a 404 would be requested every frame forever.

The results are three columns — account, listing age, price — and the table takes **whatever is
left of the panel**, asked for explicitly rather than by bottom-aligning at height 0, because it
sits inside a child that can itself scroll and ImGui's bottom-align is not meaningful in one that
does. Sorting is `price: asc` and the site does it by chaos-equivalent, so a page of chaos prices
and divine ones interleave correctly and are **not** out of order.

The account is the **whole** handle, `Name#1234` — the digits are what tells two players sharing a
name apart — and is drawn in `fonts.unicode` (above), since a Cyrillic or Korean handle is boxes in
Fontin. The price copies the site's own form, `5 x [symbol] Divine Orb`: the symbol arrives off the
CDN in the background, so the currency is **named** as well as pictured and the row reads correctly
before it lands. A lowercase `x` rather than the site's `×`, which Fontin draws as `?`. The
listing's **gold fee** (`listing.fee`, a sibling of `price`, not a field of it) is shown, and
nothing at all when there is none — a tooltip that repeats the number under the cursor is noise,
which is also why the whisper text is no longer one: it is not something the user can act on from a
tooltip. The whole price cell is one hover target (`BeginGroup`/`EndGroup`), or the tip would appear
over the amount but not over the orb beside it.

**Only ever one tooltip per frame.** The fee rides *inside* the item popup below whenever that is
up, and `draw_price`'s own tooltip is suppressed — because `SetTooltip` and `BeginTooltip` build the
same window name, `##Tooltip_%02d` off the same `TooltipOverrideCount`, and only `SetTooltip` bumps
that counter (and only against a *previous frame's* still-active tooltip). Two of them in one frame
therefore `Begin` the same window twice, which appends rather than restarts **and drops
`SetNextWindowPos`/`SetNextWindowSize`**, since those are only honoured on a window's first `Begin`
of the frame. The symptom was the fee line printed inside the item card and the card itself
mis-sized and mis-placed. `AllowOverlap` on the row's `Selectable` is what lets both fire at once:
hovering the price cell hovers the row too.

**Hovering a row draws the seller's own item** over the panel, through the *same* renderer as the
item in hand — because the fetch response carries `item.extended.text`, base64 of the item in the
clipboard format PoE writes on Ctrl+C. So there is no second parser and no second view:
decode (`util/base64`) → `restore_mod_markers` → `parse_item` → `resolve` → `derive` →
`draw_item_tooltip`. **It is not byte-identical to a real copy: the site's renderer leaves the
mod-type markers off.** It writes " (enchant)" but never " (implicit)", " (crafted)" or
" (fractured)" — a fractured mod is simply printed first in the explicit block, with nothing in
the text saying so, and the parser typed every one of them Explicit (a fractured mod in the
explicit blue, an implicit inside the explicit block). The payload does say: each entry of
`implicitMods`/`explicitMods`/… carries a `domain`, and a fractured or crafted mod is listed
**among the explicits**, so the mod's own domain outranks the array it came in. `parse_fetch`
appends the suffix the game would have printed, matching a description against the line verbatim —
a line the site already marked matches nothing and is left alone. Verified against real fetch
responses for four fractured items, a crafted one and two enchanted ones. Parsed lazily
on hover and cached in `App::listing_items_`, resolved against the same pinned `item_data_`
snapshot, and dropped whenever a trade result lands. The row is a `Selectable` with
`SpanAllColumns | AllowOverlap` so the row is the hover target and lights up to say so, while the
price cell's own fee tooltip still sits on top. It is drawn into the **gutter** beside the panel
(above), aligned to the top of its own row but never above the item card the gutter opens with, and
clamped by the height it drew at last frame so a long item does not run off the bottom — one frame
stale, which settles immediately, and there is no way to know the height before drawing it. That
clamp is a `max(min())` rather than `std::clamp`: on a card tall enough to leave less room than the
listing needs, the low bound is above the high one, which `clamp` is not defined for. Both position
and width are set explicitly:
`SetNextWindowPos` overrides a tooltip's follow-the-mouse placement and `SetNextWindowSize`
overrides its auto-fit **per axis**, so `(w, 0)` fixes the width and leaves the height to the item.
The width has to be fixed either way — `draw_item_tooltip` centres every line on
`GetContentRegionAvail()`, which in an auto-sizing window is whatever the last frame happened to be.

Searching is **on a button, not automatic**. `Config::auto_search` exists and defaults off: a
price check the user meant only to read the item with should not spend a request against their
rate limit. **Open in browser** builds the same query and hands it to the site in `?q=`, so it costs
no API call and always matches the filters as they are ticked *now* — the id of a search already run
would open whatever was ticked when it ran. When there is nothing to search the button says **why**
rather than only that: a stack of currency is bought in bulk on the in-game currency exchange and
has nothing a stat query could ask for, so its poe.ninja row is the whole answer and a bare
"Nothing to search" reads as a failure. Where there is no search *at all* — `trade::searchable`
is false, or the exchange feed has a market for it — the buttons, the filters, the strategy picker
and the plan's notes all go together: every one of them is about a query nobody can run, and a note
saying a modifier could not be matched charges a price check with failing at something it never
attempted. What replaces them is the item itself (above) and one line saying where the answer is
instead — see the currency-exchange section below.

Rendering lives in `screens/item_view.cpp` (the game's palette: rarity-coloured name plate, grey
property labels, blue mods, light blue crafted/enchant, tan fractured, magenta scourge, red
corruption, per-element damage colours) and the filter list in `screens/pricecheck_screen.cpp`.
The panel is competing with the game's own tooltip for the same screen, so it prints **less** than
the clipboard does: `strip_roll_ranges` drops the range the game glues to a roll (`+86(77-90)` reads
as `+86`) in both the item text and the filter list, and everything the game prints *about* a
modifier rather than as part of it — what Advanced Mod Descriptions say (affix, tier, tags) and the
reminder text under a wording ("Unnerved enemies take 10% increased Spell Damage") — is a **hover
tooltip** on the modifier rather than lines around it. `draw_hover_tip` is that one place, and a
property uses it too: a utility flask's buff brings reminder text the game likewise keeps out of the
tooltip ("(Onslaught grants 20% increased Attack, Cast, and Movement Speed)"). Nothing is
lost: `Modifier::info_text()` carries the tier's range with it (`(Tier: 2 [77-90])`), a continuation
line repeats its affix because that is where the reader gets *its* range, and every derived number
is a small grey line under the property block it summarises — the DPS totals under the last damage
line, the base percentile under the last defence line.

**An unidentified unique is asked about rather than guessed at** (`draw_unique_choice`), above the
filters, because until it is answered they are filters on nothing. Its candidates come from the
bundle (`item/resolve`) and so does their **artwork**: `BaseType::art` is the path GGG's own CDN
serves the picture at, so `data::item_image_url` builds the URL and `IconCache` fetches it —
**the same picture the game draws, with nothing between the two**. Not poe.ninja: its overviews
carry a `web.poecdn.com` URL too, but only for what is being *sold* this league, which was 1193 of
the bundle's 1526 uniques and 27 of the Cobalt Jewel's 54. Off the bundle it is 1416 and 53.
The **base's inventory footprint** (`BaseType::w`/`h`) is both the size asked of the CDN and the
aspect it is drawn at — squashing a 2×3 body armour into a square is what makes two candidates
hard to tell apart at 46 pixels — and it comes off the base because a unique is not a base type in
the game's data and carries no size of its own. **The art is still never load-bearing**: 110
uniques have no path, an older bundle has none at all, and a download can be in flight, so a
candidate with no picture puts its name in the space the picture would have taken.
Two shapes, and the list's own height picks between them: **one per row with the name** while that
fits half of what is left of the panel, and past it a **grid of artwork alone** with the name on
hover, because fifty rows would push the prices and the item itself off the panel entirely. Each is
a `Selectable` with the picture drawn on top of it — the poe.ninja row's shape, and `AllowOverlap`
is what lets the two overlap — and the strategy picker grows a **change** button beside the name so
a choice, including the one the app took for itself, can be taken back.

**The filter list is a four-column table** (`draw_filters`), so that every row's numbers sit under
the previous row's: the toggle, the wording, where the modifier came from, and what the search asks
for. The **wording is second, straight after the tick**, because it is the only column every row
has something to put in — a pseudo total has no affix behind it and a roll on an item with Advanced
Mod Descriptions off has no code, and a gap between the tick and the text reads as a missing
checkbox. It takes the stretch column; everything else fits its content.

Column three glues the code to the modifier's own range — `P2[77-90]` is a tier-2 prefix, `S1` a
suffix, `R` crafted, `Impl` an implicit, `Frac` a fractured affix — with one code per modifier
`merge_same_stat` folded in (`StatFilter::merged`), joined as `P3+P1` and then dropping the range to
a line below, since it is the pair's total and belongs to neither code alone. An eldritch implicit's
rank (`Modifier::qualifier`) goes on its own line for the same reason a compound's range does: the
column is as wide as its widest row, and `Impl Lesser` on one line sets that width for every
modifier in the list. **The colour is the side of the pool and the letters are what put the modifier
there** — red prefix, blue suffix, as the trade site does it — so the two never compete for the same
four characters: a fractured prefix is a red `Frac`, and what a buyer needs to know about it first
is that it is fractured.

Column four is **what the search asks for**, and it is last rather than beside the code because it
is the one thing here that becomes editable: `46-48` between two bounds, `≥46` for a floor, `≤50`
for a ceiling (**borrowed** glyphs — Fontin's own are blank outlines, see Fonts above — spelled out
as `>=` and `<=` where there was nothing to borrow from), **nothing at all** for a
filter that only asks the modifier to be present, and **`absent`** for one asking that it not be
there (`StatFilter::negated` — a Valdo map that does not void). That last one belongs in this
column and nowhere else: the row is otherwise identical to one asking *for* the modifier, and a
tick beside a wording the item does not have reads backwards. A `misc_filters` boolean puts
**`yes`/`no`** there, and only the flags the plan marks `shown` get a row at all — see
`item/plan`. It is `StatFilter::min`/`max`, while the origin
column is `roll_min`/`roll_max` — what the modifier *can* roll against what the search asks of it,
which is why they are two fields: the range-match setting is exactly the distance between them, so
`[77-90]` beside `81-90` is the 5% window doing its job. The numeric filters share the table, so a defence
and a modifier line their numbers up in the same column. Row pitch is squashed (`CellPadding`,
`ItemSpacing`) and the checkbox is drawn at zero `FramePadding`, i.e. a square the height of a line
of text: at the default it is taller than the wording beside it and sets the pitch for the whole
list. Rows are told apart by **alternating background** (`ImGuiTableFlags_RowBg`) and not by
separators: a modifier can wrap onto three lines and its origin onto two, so what the reader needs
is to see where one row ends — and a rule between every pair would spend a line of height per
filter to say it.

**Why a row is not ticked is a tooltip on the wording** (`StatFilter::caveat`), never a line
under the list. The panel is competing with the game for the same screen, a note repeats a
wording that is one row above it, and the row already carries the whole statement: this modifier,
not searched. What is left under the list is only what has **no row at all** — a modifier nothing
matched, a wording two stats share, a pool the data states but never enumerates — because for
those there is nowhere else for the app to say it is leaving something out.

The item and its plan live on `App`, alongside **the bundle snapshot they were resolved against** —
`item_data_` is held separately from `data_` because the updater swaps that from its own thread and
the item holds raw pointers into it.

`PPC_DEV_ITEM=<file>` (with `PPC_DEV_OVERLAY=1`) opens the price-check panel on a captured clipboard,
which is the only way to iterate on it without the game running. Captures live in
`tests/data/examples/` — each `item_N.txt` is a real clipboard capture paired with `item_N.jpeg`, a
screenshot of the same tooltip, which is what the rendering is checked against. `tests/data/items/`
holds the captures with no screenshot beside them — two transcribed from one (the rapier and the
Elder bow), the map fragments and invitation, the eleven `map-*.txt` maps (every shape one comes in:
tiered rare, corrupted eight-mod, chiselled for extra drops, magic, white, unique, the two that
name their own area instead of printing a tier, a blighted one — whose base line is the only
statement of that — and a Valdo one, which is searched on none of the things any of the others
are), the five `gem-*.txt` gems (a Vaal gem, whose second half is the only thing naming it; a
transfigured one; two supports, one of them with a socket requirement far above its own level; and
one with quality), `card-blazing-fire.txt` and `currency-essence.txt` (the two shapes of thing the
in-game exchange trades in bulk that are neither an orb nor a fragment), and
`currency-chaos-stack.txt`, which is **written rather than captured**: it is a 6000/20 stack, the
case a currency stash tab makes ordinary and nothing else covers. The two
`unique-unidentified-*.txt` are written for the same reason and want replacing with real
captures — they are the two answers a base gives, a Goathide Gloves that could be either of two
uniques and a Cobalt Jewel that could be any of fifty-four, which is what puts the picker into its
grid. The three `chart-*`/
`listing-chart-*` files are every shape a chart comes in — one with Advanced Mod Descriptions on,
one without and carrying the sulphur property, and one unidentified, which prints no name line at
all. Three more cover the
`misc_filters` properties: `currency-facetors-lens.txt` (the one currency item a search can tell
two copies of apart), `memory-strands-boots.txt`, and `listing-intangibility-ring.txt` — which is
captured from a **listing** rather than from the game, and is named so, because the site's
renderer writes the keyword-link markup `[Intangibility|Intangibility]: 8%` where the clipboard
writes a plain label. That `listing-` prefix is what every capture taken from a fetch response
carries, and it means one more thing: the site's renderer leaves the mod-type markers off, so
those files hold the suffix `restore_mod_markers` puts back — which is what the app hands
`parse_item` for a hovered listing, and without it a chart's implicit reads as an affix. `strip_link_markup` in `item/parse` is what drops it, and without that a
hovered listing drew the markup and its property matched nothing anything looks up by name.
Prefer a real capture for anything new.

**Pin numbers to those captures, not to another tool's output.** The Q20 DPS formula was chosen
because it reproduced a number read off a screenshot of a reference tool, which turned out to be
unverifiable — and it disagreed with the one real capture that could discriminate. Ask for a capture
of the concrete case instead; the maintainer can reproduce one in-game.

**Numbers must never go through the C locale.** The game writes `1.79`; `strtod` under a `cs_CZ`
`LC_NUMERIC` reads that as `1`, and every DPS number downstream was wrong. Parsing uses
`std::from_chars` (locale-independent by definition) and `App::run` forces `LC_NUMERIC=C` for
formatting — *after* the tray and window exist, since SDL's X11 backend (XIM) and the tray's GTK both
call `setlocale(LC_ALL, "")` during init and would undo an earlier attempt. **`LC_TIME` goes the
other way**, set from the environment in the same place: a date is written for the reader, not for
the game, so it is written the way their machine writes one. It has to be explicit rather than
inherited because nothing calls `setlocale` at all on Windows — a GUI-subsystem binary gets neither
XIM nor GTK — and the C locale's date is the invariant-looking format this avoids.

### The poe.ninja reference price (built)

`src/ninja/` is the going rate a stat query cannot give. A Divine Orb, a level-21 gem and a
unique whose copies are all alike are worth what the market is paying, which is the one thing
poe.ninja measures and trade does not — and a **white, magic or rare item is priced as its base
type**, which is all poe.ninja knows how to say about one. It is one row under the filters
(`draw_reference_price`): the site's mark, the price, and the week behind it — and the whole row
is a click-through to the item's own page, which holds the variants and the history the row has
to leave out. A map is the only strategy with no row at all.

- **Only the economy endpoints are public API** (https://poe.ninja/docs/api). The builds and
  profile endpoints are explicitly closed to third parties; do not reach for them. The paths moved
  — `poe.ninja/api/data/currencyoverview` is a 404 now, and PoE 1 lives under
  `poe.ninja/poe1/api/economy/`. Two feeds with different payload shapes: `exchange/current/overview`
  is the currency market (id → chaos, plus `core.rates`), `stash/current/item/overview` is what
  individual items are listed at (name, base, variant, `chaosValue`, `sparkLine`).
- **It is not GGG traffic and does not go through `trade::request`.** That limiter serves GGG's
  published per-policy budgets; this is a different host with different rules, honoured differently:
  a **30-minute disk cache** (what poe.ninja sets on its own responses, and PoE 1 overviews only
  refresh every fifteen minutes anyway), a conditional request when that expires so an unchanged
  overview costs a 304, and one request per *category*, not per price check. The docs ask desktop
  apps to proxy through a backend; we have none, so the cache is what stands in for it.
  `ninja/cache` keeps the body verbatim after a one-line JSON header — a gem overview is four
  megabytes and escaping it into a JSON string field would cost more than the download did — and
  `prune` drops what nothing has read in a week, or the cache grows by every league ever played.
- **The divine rate comes from the Divine Orb's own line, not from `core.rates`.** That field is
  the reciprocal rounded to four figures, and converting the divine line's own price with it puts
  a Divine Orb at 0.9995 divine. Priced against itself it is exactly one. `quote()` switches to
  divine at one divine and rounds to what a price is actually said in; `quote_in` forces a
  currency, because both ends of a span have to be in one or "79.4 – 4" is what the pair reads as.
- **The Chaos and Divine Orb are answered with the rate, because their own price is a tautology.**
  The economy is denominated in them: poe.ninja quotes everything in chaos, so a Chaos Orb is one
  chaos, and `quote` converts anything past a divine, so a Divine Orb is one divine. Neither is
  what a player checks either orb for — the rate between the two is, it is one number (the divine
  line's chaos price), and it is the answer for both. `Reference::per` says the price is a *rate*
  and the row draws it as `201 x [chaos] Chaos Orb **per** [divine] Divine Orb`, which reads true
  whichever of the two is in hand. Everything about it comes from the divine line, including the
  sparkline — that trend is the rate's own — and only the click-through follows the item actually
  being checked. With no rate published (an SSF league) there is nothing to state, so both fall
  back to their own price. **Every other currency is priced exactly as poe.ninja reports it**;
  this is the one special case, and it is special because of what the numbers denominate.
- **A unique's variant is read off the modifiers the copy in hand rolled** (`narrow_by_mods`), and
  this is the difference between a right price and a ten-fold wrong one: Ralakesh's Impatience is
  three lines — Power, Endurance, Frenzy — 805, 133 and 75 chaos, and the item says outright which
  it is. poe.ninja's variant *labels* are its own shorthand and are never guessed at, but the
  modifiers behind them are the game's wordings. Two comparisons, because poe.ninja states a
  unique's rolled modifiers as ranges and its fixed ones as plain numbers: a range means only the
  wording is compared (`collapse_ranges` → `data::placeholder_form`, so `+(15-25)%` meets the
  game's `+22%`), while **a number with no range around it is fixed on that variant** — Mageblood's
  "Leftmost 4 Magic Utility Flasks" — and the line is compared verbatim. Mismatches are counted
  *relatively*, never absolutely: the two sources' wordings drift, and a miss every candidate
  shares says nothing. A candidate the source published with **no** modifiers is not scored at all
  and stops the whole comparison, or it would win on a mismatch count of zero — the opposite of
  what its silence means. Where that leaves several (Mageblood's flask count, Voices' passives) the
  row states the **span** and the count rather than picking one; a confident wrong price is the
  failure this whole layer avoids, and the click-through settles it.
- **A rolled item is priced as its base, on three things and only three**: the base, the **item
  level** and the **influences**. poe.ninja carries no quality on a base line, so a 20% quality
  base is priced as any other. The influence set is matched **exactly** — an uninfluenced
  Twilight Regalia at item level 84 is 5 chaos and a Warlord one is 1370, so falling back to the
  wrong influence would be wrong by two orders of magnitude — and it is compared as a *set*,
  because "Shaper/Crusader" is the site's ordering and an item is not priced differently for
  being read backwards. **Searing Exarch and Eater of Worlds are excluded**: they come from an
  implicit rather than from the base, poe.ninja does not split bases by them, and asking for
  them would match nothing (`examples/item_6` is exactly that item). Item level is bracketed —
  82 up, today — so the best bracket the item has reached is what it is worth, and the top one
  is an open end. Below the lowest there is no price, which is a fact about the item rather than
  a gap in the data (`examples/item_7`, item level 78). On a **rare** the note says outright
  that this is the bare base and its modifiers are the search below: without that, a floor price
  reads as the item's price. The base name is the only name matched — a rare's own name is
  randomly generated and a hit on it could only ever be a coincidence, and a wrong price.
- **Which overview to ask** is `keys_for`, off the trade category, and the currency market always
  leads: it carries the rate and the symbols every other price is drawn with. A currency item is
  looked for *in* it before anything else is downloaded — it holds the orbs and catalysts most
  currency checks are about — and only a name it does not have sends us to one of the seventeen
  other exchange overviews, chosen by a keyword on the item's own name. That keyword table exists
  because the clipboard cannot tell a Scarab from an Essence: both are the "Stackable Currency"
  item class.
- **A map item is a bulk good or an item, and the item level is what says which.** "Map Fragments"
  and "Misc Map Items" (`Item::is_map_fragment`) print `Rarity: Normal` because the game has no
  other rarity to print, and pricing one as a *base type* — which is what Normal used to mean here
  — asked poe.ninja a question about crafting bases and got nothing back, so scarabs, embers,
  splinters and invitations had no price at all. The split is the **item level**: one that prints
  none is identical to every other copy, has nothing to filter on and changes hands on the in-game
  currency exchange, so it is `Strategy::Currency`; one that prints an item level can carry a
  rarity and its own quantity/rarity modifiers exactly as a map does, is sold as an item, and falls
  through to the ordinary rule for its rarity. Both classes map to the one trade category
  `map.fragment`, so it says no more than "Stackable Currency" does and the **name** picks the
  overview: the keyword table first (Scarab, Allflame Ember, …), then `Invitation`, then
  `Fragment`. That routing (`ninja::map_item_type`) keys on the **category and name, never the
  strategy**, precisely because the item level moved some of these off `Currency` — asking the
  crafting-base overview about an invitation finds nothing. **`Invitation` is the one map item on
  the stash feed** — an invitation carries an item level, so poe.ninja lists it like an item rather
  than trading it in bulk (`/stash/current/item/overview?type=Invitation`, page slug
  `invitations`). Its item level is not part of the match: poe.ninja publishes one price per
  invitation.
- **A stack is priced as a stack as well as per item.** `Query::stack` comes off the
  "Stack Size: 6000/20" line and takes the **count, never the maximum** — that maximum is what one
  inventory slot holds, and a currency stash tab holds five or ten thousand in a single stack, so
  a count far past it is normal rather than a parse error. `Reference::stack_price` is quoted on
  its own rather than scaled from the unit price, because the two cross the divine line at
  different times: one chaos is one chaos and six thousand of them is 29.8 divine. Left at one for
  an `Ambiguous` price, where a span times a count would be four numbers. It is drawn on **a line of
  its own** under the unit price, which names its currency whether or not that is the unit's: the two
  do not fit the panel's width side by side, and `x6000 = 29.8 x` with the unit only on the row above
  is worse than either.
- **A gem is priced at the nearest tier poe.ninja publishes**, which is rarely the one in hand — it
  lists 1, 20, 20/20, 21/20 corrupted and nothing between. The best of those the gem has already
  reached is a floor on what it is worth, labelled with poe.ninja's own name for it so it is clear
  it is not this gem. Corruption filters first: it is a hard split, not a preference.
  It is looked up under **one** name and no other — `Item::gem_name()`, the same string the
  trade type is resolved from. poe.ninja spells a gem exactly as trade's `data/items` does,
  including "Vaal Blight (Blight of Atrophy)", so there is nothing to fall back *to*: it prices
  both "Blight" and "Vaal Blight", and a Vaal Blight — whose name line says "Blight" — reading
  the printed name would not miss a price, it would find a real line for a different gem at a
  tenth of the value.
- **A league poe.ninja has no economy for answers 200 with an empty payload**, not a 404 — so an
  SSF league parses fine and matches nothing, and that is reported as "tracks no economy for X"
  rather than as a broken response. The website's league slug is **not** the league id: "Hardcore
  Allflame" is `allflamehc`, not `hardcore-allflame` (`league_slug`).
- **`NinjaService`** is the third of the `LeagueService` triplets, with the work split the other
  way round: the worker only *downloads*, because matching an item to a priced line is pure and
  cheap, so it happens on the main thread and the second check of a kind is free. Overviews are
  kept for the life of the process and are about the economy rather than the item they were
  fetched for, so they are installed whatever the panel has moved on to. A check landing on a
  download already in flight cannot wait for it — that one was told to fetch a *different* item's
  categories — so `restart_` repeats it when that one lands.
- The row draws the currency symbol from the **trade** static data where that has been fetched and
  from poe.ninja's own payload where it has not: a reference price must not be the one thing that
  needs a trade request to render. The sparkline is drawn by hand rather than with `PlotLines`,
  which insists on a frame and a background.
- `item/plan` no longer notes that currency and gem pricing "is not implemented yet" — that note
  now sits directly above the price, and read as "this item has no price".
- **A `None` note never names poe.ninja**; it is drawn after the row's own "poe.ninja — " prefix
  and would read twice. The `Priced` and `Ambiguous` notes go in the tooltip instead, where
  naming it is right. The row's note wraps, because these say *why* there is no price and a
  sentence cut off at the panel edge does not.

### The in-game currency exchange (built)

`src/exchange/` is what a stack of currency, a scarab or a fragment is *actually* traded on. The
trade site has nothing to say about any of them — an exchange market is not a listing — so for
those items the search below the panel was never merely empty, it was the wrong question. This is
the right one, and it is GGG's own numbers rather than a third party's reading of them.

- **`https://web.poecdn.com/api/currency-exchange[/<hour>]`** is public and unauthenticated, needs
  no registered application, and is on the **CDN rather than the API host** — so it carries no
  `X-Rate-Limit` policy headers and must not go through `trade::request`, which exists to serve
  budgets this endpoint does not publish. What stands in for a limiter is that **one download
  covers every item in every league**: the cost is per *hour of play*, not per price check.
- **It is purely historical, in hourly digests.** The hour in progress does not exist — asking for
  it answers 404 with a well-formed `{"next_change_id":…,"markets":[]}` — so the freshest possible
  answer is the hour that just ended (`latest_hour`). The feed also publishes a few minutes late
  often enough to matter, so `load_digest` steps back up to `kStepBackHours`. An empty payload is
  never cached: it is "not published yet", not an answer, and `Digest::any_league` is what tells
  that apart from "this league does not trade" (plenty of markets, none ours).
- **A published hour never changes**, which is the whole cache design: the file name *is* the hour,
  there is no etag, no TTL and no freshness decision — the file on disk either is the hour being
  asked for or it is not. The newest `kKeepHours` (2) are kept at ~2MB each; the second exists so
  that stepping back an hour costs no second download.
- **The payload states every item by its `Metadata/Items/...` path and carries no names at all.**
  That is why `data::BaseType::metadata_id` exists — `BaseItemTypes.Id` in the game data *is* that
  path, and the data repo's `emit/items.py` now writes it through. **A bundle without the field
  simply has no exchange prices**, the same shape as `has_unique_mods()`: nothing here may assume
  one is there, and the app asks nothing when the item's base carries no id.
- **Ratios are ordered, not taken as named.** `lowest_ratio`/`highest_ratio` are integer counts of
  the two sides (`{A: 1, B: 50}` is one A for fifty B), so the price of one A in B is B/A — and
  "lowest ratio" is the lowest value of *item over against*, which is the item's **dearest** price.
  Reading the two names as a price band gets it backwards on some markets and right on others,
  because the counts move on both sides; `min`/`max` is what covers both. A market that saw no
  trade is published with zeros, and dividing by that would put a nonsense price on screen rather
  than none.
- **The volume-weighted average is the headline, and the band is the detail under it.**
  `volume_traded` is published for *both* sides of a market, and the two divided
  (`Rate::average`) are the mean ratio every trade in the hour cleared at — not the midpoint of
  a band whose ends one trade can set. Each market is drawn as a summary line and a small table
  beneath it: volume on both sides, then the lowest and the highest the hour saw. **Stock is
  deliberately not shown** — what is standing in the book says how long a sale would take, not
  what the item is worth. Zero volume on either side is no average at all (not a price of zero),
  and then the band itself is the summary.
- **A market is quoted against whichever of the two is worth more**, so one side is always a
  single unit: `4 x Winged Scarab = 1 x Chaos Orb` is how the trade is said in game and
  "0.25 chaos each" is not. The **average** is what decides the direction (`exchange::read`),
  because a band can straddle one — 0.5 – 2 chaos an ember — and then has no direction of its
  own; the top of the band stands in when there is no volume to average. It falls out that a
  Chaos Orb in hand reads `208 x Chaos Orb = 1 x Divine Orb` — the same rate the poe.ninja row
  states for both orbs, from GGG's own book and an hour old rather than half an hour.
- **Every item in the feed is drawn with its own glyph, not just the two denominators.** The
  feed keys on metadata paths and neither symbol source has ever heard of one, so the join is
  the **display name**: `TradeService::image_for_name` over every group of
  `/api/trade/data/static` that carries an image, falling back to
  `NinjaService::icon_for_name` across the overviews already held — the same two sources in the
  same order as every other symbol, and for the same reason (the trade static data is not
  fetched at all until the user's first search). The summary line draws the item's glyph
  **without** its name: it is the one row that has to fit both sides, "Cartography Scarab of
  Corruption" twice does not, and the full name is in every row under it.
- **The hour is stated in the user's own clock and date format** — the digest is addressed by a
  UTC hour, and a reader deciding whether a price is stale should have to do neither timezone
  nor date-order arithmetic. `strftime("%x %H:%M")` on `localtime`, so `App::run` sets
  `LC_TIME` from the environment (the opposite call to the `LC_NUMERIC` one beside it, and
  needed because a GUI-subsystem Windows binary has nothing else calling `setlocale`), and the
  line is drawn in `fonts.unicode` since a locale's date can be Cyrillic or CJK.
- **Only markets against Chaos or Divine are kept.** A scarab-for-essence market is a real market
  and no use to somebody asking what a scarab is worth.
- **A market only appears in an hour it traded in, and the hour cannot say why.** Measured on a
  live hour: 840 of the ~940 Allflame items that trade at all, and 115 of ~200 scarab varieties.
  So silence in the digest is two different facts wearing one face — *this does not trade here*
  and *nobody traded one this hour* — and for a thin item (a Weeping Essence of Greed) the second
  is the normal case. Reading it as the first left such an item with no exchange row, no trade
  search worth running and no poe.ninja price either: an empty panel, which reads as a check that
  gave up.
- **So which items trade there at all is a fact about the item, and it comes from the bundle.**
  `data::BaseType::exchange` is set by the data build, which crawls every hourly digest since
  Settlers launch and keeps the union of the metadata ids any market has ever named — 17.8k hours,
  ~1,000 ids, one boolean on a record that already carries `metadata_id`. The digest still
  supplies the *price*; this supplies the standing answer underneath it.
  **A bundle published before the flag must not read as "does not trade,"** which an absent
  boolean cannot say on its own — so the signal is bundle-level: `source.exchange_items` in the
  manifest, written through by `install` beside the attribution, and read back as
  `GameData::has_exchange_flags()`. Same distinction `has_unique_mods()` draws, drawn differently
  because that dataset is a whole file whose absence is the signal and this one is a field on
  records the bundle already had.
- **The exchange section has three states**, and the middle one is why the flag exists: a market
  this hour draws the table; no market but the item is flagged draws one line saying so; and not
  flagged, or a bundle that cannot say, draws nothing at all. Claiming either way from a bundle
  with no exchange data would be a guess.
  That middle line is **"no trades this hour" only when the hour was actually read** — a digest
  whose fetch failed says nothing about what traded in it, so that case says so instead. The
  distinction is invisible otherwise: a flagged item gets no trade search either way, so stating
  the market was quiet on the strength of our own failed request would leave the user no way to
  tell an idle market from a broken download.
- **An item that trades on the exchange gets no Search, no Open in browser and no filter list.**
  It has no listings for either button to find, and a button that can only ever come back empty
  reads as the item being unsellable rather than as the wrong market having been asked. The
  filters go with them (see the panel layout above): they exist to shape a query nobody can run
  here, and their notes about unmatched modifiers charge the check with a failure it never
  attempted. One line says where the answer is instead.
  **That is keyed off the flag, not off the hour** — `App::trades_on_exchange()`. Keying it off a
  market in the last published hour gave a Weeping Essence of Greed a Search button on every hour
  nobody happened to trade one in, and the message alone would not have fixed that. It takes two
  sources, strongest evidence first: a market this hour *proves* the item trades there whatever
  the bundle says, which is also what keeps this working on a bundle older than the flag.
- **`ExchangeService`** is the fourth of the `LeagueService` family and the smallest, because the
  feed is: one digest in memory at a time, a lookup is a string compare, and the second check of
  the hour is free whatever the item. It is asked about **every** item, not only the ones planned
  as currency — whether a thing trades there is a fact about the market rather than about our
  strategy.

### Still to build

- **Any client language but English.** Every modifier is matched against the wording the client
  printed, and those are language-specific, so a non-English client produces text nothing here
  parses — it does not mis-price, it fails to recognise an item at all. The seam exists and is
  unused: assets are language-prefixed, `GameData::open` takes the language, and `manifest.json`
  declares a `languages` list. What is missing is upstream — the data build fetches only the
  English `stat_descriptions.txt` files and emits one language — plus a setting to pick one.
  Nothing in the schema has to change. **Say so in the README rather than letting it be
  discovered**, which is why it is a Requirements row there.
- **Telling apart the variants a modifier wording cannot.** `narrow_by_mods` resolves a unique
  whose variants differ in *wording*; the ones that differ only in a **number poe.ninja publishes
  for some variants and not others** stay a span. Mageblood is the case: the item prints "Leftmost
  5 Magic Utility Flasks" and the dearest line carries no modifiers at all, so there is nothing to
  compare it against. Matching the number where every candidate does publish one would close most
  of the gap.
- **Offering the pool modifiers the item does *not* have.** Reading the per-unique data is built
  (above); the other half of what it is for is not. A Watcher's Eye search is worth little without
  being able to add "and also has Discipline energy-shield-on-hit" — `ref` gives the wording to show,
  `tradeId` the filter to send and `range` the bounds to seed. That needs a `StatFilter` not tied to a
  `mod_index` and a way to pick one in the UI. Filters the record carries **without** a `tradeId` (428
  ambiguous wordings, 695 with no id at all) belong in that list too: display them, never search them.
- **An unidentified unique's own modifiers.** Which unique it is, is built (above); what a copy
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

The site's map categories are **finer than the item class can be**: `data/filters` publishes
`map.fragment`, `map.scarab`, `map.invitation` and `map.breachstone` as separate options, while
`item-classes.ndjson` maps both "Map Fragments" and "Misc Map Items" onto `map.fragment` — which it
has to, because the clipboard's item class cannot tell a scarab from an invitation. It costs
nothing today (none of them are searched), but a real trade search for invitations needs the split,
and that is a data-repo job or an app-side keyword table like `ninja::map_item_type`.

The **gem** categories are finer too — `gem.activegem`, `gem.supportgem` and `gem.supportgemplus`
against the two classes the clipboard prints — and here it demonstrably does not matter, so do not
add a table for it: an Awakened support gem, which is `gem.supportgemplus` on the site and
`gem.supportgem` in the bundle, returns the same 57 matches under either and under the bare `gem`.
A gem is pinned by its `type`, and the category only ever narrows what that already decided.
The **filters** the gem search needs are all in `misc_filters`: `gem_level`, `quality`, and the
booleans `gem_transfigured` / `gem_vaal` / `gem_imbued`. The two flags are not used and could not
stand in for the discriminator anyway — measured, `type: "Raise Zombie"` with
`gem_transfigured: true` is **0 matches against 365** for the same type with `alt_y`, because a
bare gem type matches only the unaltered skill. There is no `gem_alternate_quality` any more.

### The in-game currency exchange (public, no OAuth)

`GET https://web.poecdn.com/api/currency-exchange[/<realm>][/<id>]`, documented at
<https://www.pathofexile.com/developer/docs/reference>. **Public** — no OAuth, no scope, no
registered application — and on the CDN, so no `X-Rate-Limit` headers and no per-policy budget.
The realm segment defaults to PoE 1 PC, which is what this binary drives.

`<id>` is the **unix timestamp of an hour**, and any hour can be addressed directly — walking from
`next_change_id` is not required. Omit it and you get the *first* hour of history (1722027600,
Settlers launch), which is never what you want. Each digest is ~2MB of every market in every
league: `{league, market_pair: [metadata id, metadata id], volume_traded, lowest_stock,
highest_stock, lowest_ratio, highest_ratio}`, all four maps keyed by the pair's metadata ids.
**No names anywhere** — see `src/exchange/` for what that costs and how it is paid.

### poe.ninja

Docs: <https://poe.ninja/docs/api>. **Only the economy endpoints are public**; the builds and
profile endpoints are closed to third parties and must not be touched. The old
`poe.ninja/api/data/currencyoverview` and `itemoverview` paths are **gone** (404) — PoE 1 is under
`poe.ninja/poe1/api/economy/`, and there are two overview endpoints with different payload shapes:

- `GET .../poe1/api/economy/exchange/current/overview?league=<league>&type=<Currency|Fragment|DivinationCard|Essence|Scarab|…>`
  — the currency market. `lines[]` is `{id, primaryValue (chaos), sparkline}`, joined by `id` to a
  sibling `items[]` for the name and icon; `core.rates.divine` is the chaos↔divine rate.
- `GET .../poe1/api/economy/stash/current/item/overview?league=<league>&type=<UniqueWeapon|SkillGem|…>`
  — what individual items are listed at: `{name, baseType, variant, chaosValue, divineValue,
  links, gemLevel, gemQuality, corrupted, detailsId, sparkLine, explicitModifiers}`.

Item pages are `poe.ninja/poe1/economy/<league-slug>/<category-slug>/<detailsId>`, and the league
slug is not the league id (see `league_slug`). There is no versioning and no SLA: the docs say
outright that breaking changes to these can happen without notice, which is why `parse_overview`
treats every field as optional and an unreadable payload as "no price" rather than an error.

Cache aggressively — 30 minutes, matching what poe.ninja sets on its own responses — send
conditional requests, and identify the app in the User-Agent. See `src/ninja/` above for how.

### Rate limits — treat as a hard requirement

GGG returns rate-limit state in response **headers** (`X-Rate-Limit-Rules`, per-policy
`X-Rate-Limit-<policy>` giving `hits:period:window` triplets, `X-Rate-Limit-<policy>-State`, and
`Retry-After` on 429). All GGG traffic passes through the shared rate limiter
(`trade/ratelimit`, owned by `trade/client`), which parses these headers, tracks each active window,
and **proactively delays** rather than reactively eating 429s. Never issue a GGG request outside
`trade::request`.

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
libgl1-mesa-dev libegl1-mesa-dev libasound2-dev libpulse-dev libdbus-1-dev libudev-dev
libcurl4-openssl-dev` (the CI
workflows install exactly these). Windows needs only MSVC. The CI still validates the Windows build on
every push/PR — trust it for the Win32 platform code, which can't be compiled locally here.

The bundled font data is committed, so a normal build needs nothing extra. To change the typeface:
`./scripts/fetch-fonts.sh` (downloads the TTFs into the gitignored `assets/fonts/`) then
`./scripts/gen-font-data.sh` (rewrites `src/fontin_data.inc`). The icon is the same deal — after
changing `assets/popc_icon.png`, run `./scripts/gen-icon-data.sh` (rewrites `src/icon_data.inc` and
`assets/popc_icon.ico`; needs ImageMagick for the latter).

`-fsanitize=address,undefined` for debug builds is not wired into CMake yet; pass it by hand:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
```

Note that a background job started from a non-interactive shell inherits `SIGINT` **ignored**, so
`kill -INT` will not exercise the shutdown path — launch it in a way that restores the default
disposition, or you will misread "still running" as a hang.

The item parser must be runnable and tested without any windowing or network dependency; that is
what `ppc_core` is for.

### Regenerating the test fixtures

`tests/data/stat-normalization-vectors.ndjson` and `tests/data/bundle/` are slices of a real data
release, committed so the suite runs offline. **`./scripts/slice-test-bundle.py
../PathOfPriceCheck-Data/out` regenerates the bundle slice**; adding a case means adding a key to the
lists at the top of that script, never writing a record by hand. It copies every record verbatim and
rebuilds the indices from the offsets it just wrote, which is the point: the `.index.bin` files
address the ndjson by byte offset, so one extra byte per line silently shifts every record out from
under every lookup and fails as null lookups rather than as a diff. Keep the ndjson **LF and
byte-exact**; `.gitattributes` pins that down and those entries must stay.

The slice has **no `(Local)` stat record**, so the local/global disambiguation in `item/resolve` is
not covered offline — it is verified against an installed bundle by hand. Adding one such record (and
one weapon base) to `STATS`/`ITEMS` in the slicer would close that gap. `tests/data/examples/` and
`tests/data/items/` hold clipboard captures for the parser; those are plain text and need no byte
discipline.

The two chart records are a pair on purpose: `ITEM::Coral Reef Chart` is what the clipboard's
base line says, and `ITEM::SeafloorRidges` is what trade files the chart under — under its
internal id, with the `chart` discriminator and no display name on it at all, which is the whole
reason `chart_area_key` exists.

The slice's four `GEM::` records need a bundle from `data-20260807.23` or later, which is the
release that keys gems on the name the game prints. The transfigured one
(`Raise Zombie of Falling`) is the whole point of that field and is the record to check after
any re-slice: it is the only one carrying `tradeName`, and on an older bundle it does not exist
under that name at all.

Two records in it are there for their **`metadataId`** rather than for anything a search does with
them — `DIVINATION_CARD::The Blazing Fire` and `ITEM::Weeping Essence of Hatred` — because that
field is the only key the in-game exchange states an item by, and it only reaches the app through a
resolved base. They carry the **`exchange`** flag for the same reason, and the slicer copies
`source.exchange_items` out of the source bundle's manifest so the fixture can say the flags are
there to be read: without it `has_exchange_flags()` is false, the flags copied onto the records
read as "unknown", and nothing about them is tested at all. `UNIQUE::Hrimsorrow` is there for the opposite reason: it is what turns a Valdo
map's printed `Foil Hrimsorrow` into a name the trade site accepts. A **blighted** map needs no
record at all, which is itself the point — it resolves against `ITEM::Map` like every other one.

`UNIQUE::Hrimburn` is there to make `ITEM::Goathide Gloves` a base with **two** uniques on it,
which is the whole of what covers an unidentified unique: the gloves are the case only the user
can settle and the Riveted Boots above are the one the app takes for itself. The pair is also what
covers `en-items-base.index.bin` at all, since nothing else in the fixture reads it.

`tests/data/exchange/digest.json` is a slice of one real hourly digest, and every market in it is
there to be dropped or kept for a stated reason: the chaos/divine pair (the rate, read from both
sides), an Allflame ember whose ratio counts move on *both* sides (which is what proves the band
is ordered rather than named), an Awakener's Orb where they do not, a market against neither
denominator, one published all zeros, and one Hardcore Allflame row for the league filter.

`tests/data/ninja/` is the same idea for the reference price: real poe.ninja responses cut down to
the lines a case turns on, kept verbatim so a payload change reads back as a parse failure rather
than as a test that quietly stopped covering anything. Each one is there for a reason — the
currency market for the rate, `unique-armour.json` for a variant the item's own modifiers resolve,
`unique-accessory.json` for one they cannot, `skill-gem.json` for the tiers poe.ninja publishes
against the ones it does not, `base-type.json` for the two bases the captures already cover —
`item_6`'s Twilight Regalia (item level 84, eldritch influences that must be ignored) and
`item_7`'s Infiltrator Mitts (item level 78, under everything poe.ninja publishes) — and the two
map-item feeds, `fragment.json` for the exchange half (where the line's id, `phoenix`, is not its
page slug, `fragment-of-the-phoenix`) and `invitation.json` for the stash half.
