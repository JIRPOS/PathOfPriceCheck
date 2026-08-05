# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

The overlay, Settings, the league list, the **static game-data layer** and the **item layer**
(parse → resolve → price-relevant numbers → search plan, plus the game-styled tooltip) are built and
tested, including the **per-unique modifier data** that decides which of a unique's modifiers are
worth searching on. The **trade query builder + client and the rate limiter are the next piece of
work**: the plan the item layer produces is exactly the input they need.

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
  smoother Wayland path later. True transparency + click-through is a later step; today it's an opaque
  panel.)
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
  `clipboard_changed()` is the same argument for the *write* signal: X11 selects XFixes
  `SetSelectionOwnerNotify` on its own requestor window and latches it, so a copy is observed the
  moment ownership is asserted rather than after a `TARGETS` round trip the game may never answer
  (SDL's `SDL_EVENT_CLIPBOARD_UPDATE` waits for that reply — see the copy watch under Architecture);
  Win32 uses `GetClipboardSequenceNumber`, whose first call only establishes a baseline. It is
  latching, so callers arm it by calling it and discarding the result, and it must be polled every
  tick or a write between two polls is lost. Without XFixes it reports *no* change, never a
  permanent yes — a permanent yes would vouch for the stale clipboard a failed copy leaves behind.
  Verified against the same stub owner re-asserting ownership over byte-identical text: fires once
  per re-copy, silent otherwise.
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
- **Commit messages / PRs:** precise, not verbose. State what changed and why; skip the essay.

## Architecture

Pipeline: **hotkey → auto-copy → clipboard → parse → identify → price → render**. `App` (`src/app.cpp`)
owns the SDL event loop and a `Screen` state machine `{ Hidden, PriceCheck, Settings }`. Price-check
hotkey → `simulate_copy()` → watch the clipboard → show. Watching means a 100ms poll of
`clipboard_text()` plus `clipboard_changed()`, our own write detector;
`SDL_EVENT_CLIPBOARD_UPDATE` is kept only as a third accelerator, because SDL raises it not on
the owner change but once the new owner answers the `TARGETS` conversion SDL fires in response —
a fullscreen game mid-frame answers late or never, so SDL stays silent through a copy that plainly
happened. Accepted text must
look like item text (`Item Class:`/`Rarity:`) — never fall back to the pre-copy clipboard, which is
whatever the user last copied anywhere and reads as a successful but wrong price check.
**Text byte-identical to the pre-copy clipboard needs `clipboard_changed()` to vouch for it**, and
that is not an edge case: re-checking the same item produces the same bytes, and with only SDL's
event to go on the panel hung until the user alt-tabbed out of the game and back, which is what
finally made the game answer SDL's probe. `clipboard_changed()` must be **armed before**
`simulate_copy()`, which blocks for the length of a human keypress that the copy can land inside.
Text that
lands goes through `App::accept_clipboard` → parse, resolve, derive, plan (see "The item layer"). Past the
2.5s deadline the panel says so but keeps watching; the game's X11 handover can land seconds late.
`PPC_DEBUG_COPY=1` traces the whole timeline. The overlay is
**dismiss-on-focus-loss**: once shown it stays until you click away, hit Escape, the X button, or the
toggle hotkey — keeping logical state in sync with what's visible (a stale "still open" state was the
two-press bug).

**Focus is a gate, never something to take.** The hotkeys are grabbed system-wide, so `handle_action()`
drops any action fired while PoE is not the foreground window — otherwise they go off in the user's
browser. The lone exception is the Settings hotkey while Settings is open: that panel holds the
keyboard focus itself, so the game *can't* be foreground, and the hotkey has to be able to close it.
The idle "● PPC" marker follows the same rule and unmaps whenever the game isn't in front, so it
never floats over other applications. In the other direction we never force focus: the copy path used
to call `focus_game_window()` on a window it had just confirmed was foreground, and `XSetInputFocus`
on the toplevel can land somewhere Wine didn't put it. Focus is handed back to the game on close
**only** if `overlay_.has_focus()` — i.e. only focus we took ourselves.

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

**Settings** lays every row out on one grid via `row()` in `settings_screen.cpp` — ImGui draws a
control's own label to its *right*, which is why nothing passes a visible label. League is a combo
fed by `LeagueService` from `/api/trade/data/leagues`, cached 24h under `cache_dir()`; the payload
repeats each id per realm so it is filtered to `pc`, hardcoded because this binary can only be
driven by a PC client. Two invariants: the dropdown is never empty (fallback → cache → fetch), and
the configured league is never lost — it is the combo preview and is appended as a selectable when
a fetch does not contain it, which is exactly what happens on league-launch day. No request is made
unless Settings is opened. `poe_window_title` is config-file-only, deliberately not in the UI.

Three SDL user event types are registered as one contiguous block: hotkey `Action`, league result,
data-updater state. Async results are **not** routed through `Action` — `handle_action()` gates on
the game being foreground and would silently swallow them whenever PoE is not in front.

A **system-tray icon** (SDL3 `SDL_Tray`, cross-platform) provides Exit. `Overlay` wraps
the SDL3+GL+ImGui window; `Config` persists to JSON. `PPC_DEV_OVERLAY=1` opens Settings and disables
dismiss-on-blur for local dev; add `PPC_DEV_ITEM=<file>` to open the price-check panel on a captured
clipboard instead.

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

**`ppc_core`** is the static library holding everything that needs neither a window nor a network,
so it can be unit-tested headless: `paths`, `config`, `leagues`, `platform/input`, `util/`, all of
`item/`, and all of `data/` except the updater. The rule is that `ppc_core` links no SDL3, no ImGui,
no X11 and no libcurl. Tests use doctest and link only `ppc_core`.

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
  weapon bases.
- **`item/plan`** — `SearchPlan`: strategy, category/name/type, corruption, influences, stat filters
  and numeric filters, plus **`notes` for everything deliberately left out**. Strategy decides what
  matters: `Modifiers` (magic/rare) enables every mod and bounds it by the tier it rolled when
  Advanced Mod Descriptions gave a range; `BaseItem` (white, or a rare the user switches over)
  searches the base with item level and influences and enables only fractured mods and non-inherent
  implicits; `Unique` searches the name and enables a roll the **per-unique modifier data** says comes
  from a pool (see below), a roll a range proves is variable, any mod *added* to the unique —
  `{ Foulborn Unique Modifier }`, i.e. `Modifier::added_unique()`,
  which not every copy of that unique carries — and anything the player *crafted onto this copy*
  (`added_to_copy`: enchant, crafted, fractured, scourge, veiled, crucible). An enchant costs
  currency and is most of what an enchanted copy sells for, so leaving it out prices a different
  item. A `Maps` item class is `Unsupported`: a map is not
  priced on its mods, and pricing one as a rare would search for gear carrying map mods.
  An unbounded filter asks for "no worse than this", and **worse is not always smaller**: a mod
  the game prints negative is better the more negative it is (an eldritch implicit applying `-11%`
  to Cold Resistance — its magnitude comes from the currency tier, so the clipboard prints no range
  to bound it with), and so is a stat the bundle marks `better: -1`. Both get the roll as a
  **maximum**. The sign is what carries the direction for the rest, because the canonical wording
  already does — "#% reduced Mana Cost" is stored as a negative increase. It reads wrong only for a
  negative roll of a stat that also rolls positive, i.e. a resistance penalty, which is a drawback
  on a unique rather than something a buyer searches for.
  Two rules that are easy to get wrong: trade indexes **repeated stats as their total**, so
  `merge_same_stat` sums two life rolls into 104–117 rather than filtering twice; and an
  added-damage mod is indexed as **the average of its two numbers** while every other multi-number
  wording is indexed on its **first** ("15% chance to Unnerve … for 4 seconds" is searched on 15,
  not on 9.5) — hence `StatMatch::roll_bounds` being per roll.
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
  filter list. `UniqueMods::unlisted` — a pool stated in prose but never
  enumerated — becomes a note, so the app says what it is leaving out.
  [UNIQUE-MODS.md](UNIQUE-MODS.md) is the dataset's contract, including what it does not cover.

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
line, the base percentile under the last defence line. A filter row leads with where its modifier
came from and what it asks for: `P2 [77-90]` is a tier-2 prefix, `S1` a suffix, `R` crafted, one code
per modifier `merge_same_stat` folded in (`StatFilter::merged`). The code is **coloured by which
half of the pool it came from** — red prefix, blue suffix, as the trade site does it — which is also
what says whether a crafted `R` is a prefix or a suffix, since its letter no longer can. The
item and its plan live on `App`, alongside **the bundle snapshot they were resolved against** —
`item_data_` is held separately from `data_` because the updater swaps that from its own thread and
the item holds raw pointers into it.

`PPC_DEV_ITEM=<file>` (with `PPC_DEV_OVERLAY=1`) opens the price-check panel on a captured clipboard,
which is the only way to iterate on it without the game running. Captures live in
`tests/data/examples/` — each `item_N.txt` is a real clipboard capture paired with `item_N.jpeg`, a
screenshot of the same tooltip, which is what the rendering is checked against. `tests/data/items/`
holds the two captures transcribed from a screenshot rather than copied from the game (the rapier and
the Elder bow); prefer a real capture for anything new.

**Pin numbers to those captures, not to another tool's output.** The Q20 DPS formula was chosen
because it reproduced a number read off a screenshot of a reference tool, which turned out to be
unverifiable — and it disagreed with the one real capture that could discriminate. Ask for a capture
of the concrete case instead; the maintainer can reproduce one in-game.

**Numbers must never go through the C locale.** The game writes `1.79`; `strtod` under a `cs_CZ`
`LC_NUMERIC` reads that as `1`, and every DPS number downstream was wrong. Parsing uses
`std::from_chars` (locale-independent by definition) and `App::run` forces `LC_NUMERIC=C` for
formatting — *after* the tray and window exist, since SDL's X11 backend (XIM) and the tray's GTK both
call `setlocale(LC_ALL, "")` during init and would undo an earlier attempt.

### Still to build

- **Trade query builder + client** — see "PoE trade API" below. Serialises a `SearchPlan` into the
  search JSON (`StatFilter::inverted` is applied here, not earlier) and runs the two-step
  search→fetch flow.
- **poe.ninja client** — bulk/reference pricing for the categories a stat query cannot price:
  currency, fragments, divination cards, and uniques. `Strategy::Currency` / `Strategy::Gem` exist
  and currently only say they are not implemented.
- **Rate limiter** — a single shared component every outbound GGG request goes through. Non-optional;
  see "Rate limits".
- **Offering the pool modifiers the item does *not* have.** Reading the per-unique data is built
  (above); the other half of what it is for is not. A Watcher's Eye search is worth little without
  being able to add "and also has Discipline energy-shield-on-hit" — `ref` gives the wording to show,
  `tradeId` the filter to send and `range` the bounds to seed. That needs a `StatFilter` not tied to a
  `mod_index` and a way to pick one in the UI. Filters the record carries **without** a `tradeId` (428
  ambiguous wordings, 695 with no id at all) belong in that list too: display them, never search them.
- **Unidentified uniques** — the clipboard says only the base, and several uniques can share one
  (an unidentified Watcher's Eye is worth several divines more at high item level). The user has to
  pick from the base's uniques, ideally showing their art; the plan reports the gap as a note today.
  The bundle now carries **`en-items-base.index.bin`**, base → the uniques that drop on it, which is
  the lookup this needs; the candidates' mods come from `en-unique-mods.ndjson`.
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
- **Pseudo mods** — trade's `pseudo.*` totals (total resistances, total life) are not built; mods are
  matched verbatim. The bundle does carry the ids (`pseudo.pseudo_total_cold_resistance` and the
  rest), so this is a plan-layer job, not a data one.
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
