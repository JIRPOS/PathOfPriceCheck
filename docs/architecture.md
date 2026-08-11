# Architecture

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

This is the shell — the event loop, the copy path, focus, placement and the debug log. The layers
it drives have docs of their own and are read separately: [data-layer.md](data-layer.md),
[item-layer.md](item-layer.md), [trade-layer.md](trade-layer.md), [ninja.md](ninja.md),
[exchange.md](exchange.md), [localisation.md](localisation.md). The per-OS seams underneath are
[platform.md](platform.md).

Pipeline: **hotkey → auto-copy → clipboard → parse → identify → price → render**. `App` (`src/app.cpp`)
owns the SDL event loop and a `Screen` state machine `{ Hidden, PriceCheck, Settings, QuickPaste }`
— the last of which is the paste list and is [quickpaste.md](quickpaste.md), the only screen that
does not involve the copy path at all. Price-check
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
re-export. The first `nudge_clipboard_handover` was *not* enough here and the log proves it: it
fired, moved the X input focus (`input=0x9a00037 active=0x8400001`), and Wine did not re-assert —
**the game never stopped being `_NET_ACTIVE_WINDOW`**, because the thing it took focus onto was our
override-redirect panel, which no window manager manages and none can therefore make active. That
is the diagnosis the current nudge is built on rather than a reason to stop: it now asks the WM to
activate a managed window of its own, which does move `active=` (measured, both ways).
Captured in `ppc-20260805-162746.log`, checks `ECJG` (the race)
and `3NDN` (the stuck state). A drop to no owner is deliberately **not** counted by
`clipboard_stamp()`: it is the opposite of a write, and counting it made an empty read look like a
successful copy of something that "is not an item".

**Reading the Wayland side instead does not fix it — measured, so do not try again.** A second
clipboard backend on `ext-data-control-v1` (the protocol a Wayland clipboard manager uses, and the
only way for an unfocused client to read the selection at all) was built to find out, and the
answer is no: the wedge reproduces step for step (start → check works → copy a URL in a Wayland
browser → the next check gets nothing → focus out of the game and back → it works again).
The reason is that **the bug is upstream of both readers**. The Wayland application's copy makes
KWin's bridge take the X selection on its behalf; Wine loses it, still believes it owns it, and so
never publishes the in-game copy to X at all. There is nothing on either side to read, and the poke
reaches the bridge — which serves the browser's URL, not the item. Nothing is read wrongly (the
stamp gate is why the symptom is silence rather than a price check of a URL); the item is simply
never published. **Only Wine re-acquiring the selection recovers it, and only a WM-level activation
change makes Wine do that** — which is the same conclusion `nudge_clipboard_handover` reached from
the X11 side, now confirmed from a second, independent protocol. The backend was **removed** once
it had answered: it read the selection correctly and cost a `libwayland-client` dependency to tell
us nothing the X11 path did not already say. Two things it did establish are worth keeping. The
poke is a fact about the **owner** and not about the reading protocol — with it skipped on the
theory that a pushed selection needs no asking, nothing worked at all, because the party that has
to be woken is Wine and Wine only listens over X11. And every in-game copy reaches the compositor
as **clear-then-offer**, not as one change, which is why `clipboard_stamp()` not counting a
cleared selection is load-bearing rather than a corner case.

What is left in its place is the **diagnosis**: `clipboard_wedge_note` says so on the give-up line
whenever the owner answered a format list, since a live, responsive owner that never re-asserted
during the whole check is by definition not the game's clipboard. That is the one thing about this
failure the app can state truthfully, and it is what saves the next investigation from starting
over. Captured in `ppc-20260809-011007.log`: check `S36Q` gave up at 2014ms with the owner serving
`application/x-kde-onlyReplaceEmpty`, and `GM54` — the very next check, after the user alt-tabbed
by hand — read the item in 3ms.
The note still tells the user to alt-tab even though `nudge_clipboard_handover` now asks the
window manager for exactly that at 350ms, and it is not stale: reaching the give-up line *is* the
statement that the automatic one did not work, and a hand alt-tab is a focus-out of arbitrary
length against a 250ms hold. Read the two `[copy]` lines above the note before concluding
anything about a capture — they say whether the activation moved at all.

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
saw at the alt-tab; kept as a 350ms backstop for when asking doesn't work, and it has to be the
**window manager's** idea of focus, not the server's). Clearing the clipboard
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

**Claiming the keyboard is a smaller thing than claiming the foreground**, and three places do it:
Settings, for its text fields; the filter list's range editor, for its two boxes; and the paste
popup, for its number keys. All three go through `App::take_keyboard`, which is
`overlay_take_keyboard_focus` — `XSetInputFocus` on our own override-redirect window — it
moves `input=` and leaves `active=` on the game, which is why it is useless for prising the
clipboard out of Wine (below) and exactly right here. Without it a text field on a price check
looks live and receives nothing, because the WM will not focus the window it is drawn on. None of
them hands the focus back on closing the widget: for a price check the game regaining focus *is*
the dismiss, so returning it would close the panel out from under the edit. `set_screen` hands it
back when the screen closes, through `give_keyboard_back`.

**Claiming it once is not enough, and `overlay_.has_focus()` is not the record of having claimed
it.** Two reported bugs came out of that pair. A screen that lives on the keyboard has to
*re-*claim it whenever the game comes back to the front, because the window manager will not hand
the focus to a window it does not manage — without that, alt-tabbing to a browser and back left
Settings on screen, apparently live, receiving nothing (`App::reclaim_keyboard`, run from the
placement poll and, up to 400ms sooner, from a click into the panel). And handing it back cannot
be gated on SDL's `has_focus()` alone, which lags the `XSetInputFocus` that caused it: a paste
popup dismissed briskly reached the hand-back before SDL had registered the focus we had taken
ourselves, so the game never got a focus change — and that focus change is what makes Wine re-read
the clipboard, which is why the first paste served the *previous* one. `took_keyboard_` is our own
record of the call, and `give_keyboard_back` still checks the foreground first, so it can never
become a focus steal from a third application.

**A drag that leaves the window is reconciled against the physical mouse** (`Overlay::sync_held_mouse`,
run between the backend's `NewFrame` and ImGui's). The overlay is never wider than it needs to be
and the range slider is meant to be pulled past its own ends, so drags leave it routinely — and
nothing guarantees the release comes back, because SDL's X11 capture is a **no-op whenever XInput2
owns the pointer** (`X11_CaptureMouse` returns early unless the window also holds a grab). The
button-up then lands on whatever is under the cursor and ImGui never hears it, leaving the widget
grabbed and following the mouse long after the button was let go. So while any button is down the
global state wins: the position is fed in as window coordinates (which also keeps the drag tracking
outside the window) and a button the OS reports as up is released. It costs an extra
`SDL_GetGlobalMouseState` only during drags, and `poll_click_away` already makes that call every
frame.

Taking the game **out of the foreground** is the one sanctioned exception, and it exists for
exactly one reason: it is the only thing that makes Wine let go of the clipboard (above).
`nudge_clipboard_handover` fires **once per check, at 350ms, only while the game is still in
front**, and never in dev mode. Do not promote it to unconditional. A healthy clipboard — any
Windows machine, a native X11 game — answers the first poll and never reaches the grace period,
and pulling the game out of the foreground mid-fight for a copy that was not late is a worse bug
than the one it fixes. Its own `[copy]` log line, and the `input=`/`active=` fields `focus_info()`
puts on every poll line, are what says whether it worked.

**It is the window manager's focus and not the X server's**, and that distinction is the whole
history of this function: it used to call `overlay_take_keyboard_focus`, which is
`XSetInputFocus` on our own override-redirect window, and the log shows `input=` moving while
`active=` stayed on the game and Wine stayed silent. A window the WM does not manage cannot be
made active, so that version could not have worked. `deactivate_game_window` asks the WM to
activate a throwaway pixel instead — see the seam above for why the source indication and the
timestamp are not optional.

**Every path that starts a handover owes the game back** (`restore_game_activation`, called from
the copy landing, from `abandon_copy`, and from the start of the next check), and it is on a
**250ms hold** rather than left until the 2s timeout: the measured export lands within 160ms of
the game losing the active window or not at all, so a copy still missing after that is not
waiting on this, and holding the game out of the foreground for the rest of the timeout is
purely a cost. This is why `deactivate_game_window` allocates and `activate_game_window`
releases even when it finds no game window — an unmatched pair leaks the helper, and the next
`deactivate_game_window` then refuses.

`App::place_overlay()` gives each screen its own geometry: Settings is a 640×720 dialog centered over
the game, the paste popup is sized to its own list and placed at the cursor sampled when its hotkey
fired (see [quickpaste.md](quickpaste.md)), price-check is a **full-height panel docked beside the item's own frame** — right of the
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
bundle's version — in outlined yellow at half opacity over the mana globe, which is where the game
itself has nothing to say. `Config::status_right`/`status_bottom` place its centre, as offsets from
the game window's bottom-right corner ÷ its height (the same reasoning as the frame edges), and they
are config-file-only. Horizontally that centre is the globe's; vertically it sits below it, in the
globe's lower half, so that the third line — the one an available update adds — still lands on the
glass instead of on the frame. `place_overlay` sizes the window to the text for that screen, so the idle
overlay is a 200×48 rectangle rather than a dialog-sized one nothing is drawn into.

**Settings is four tabs** — General, Price check, QuickPaste, Application — between a fixed header (the title
and the close disc) and a fixed footer (Save). `kTabs` in `settings_screen.cpp` pairs each name with
the function that draws it; `App::settings_tab()` holds which one is open, because the screen is a
free function rebuilt every frame. The strip is buttons, not `ImGui::BeginTabBar`: the game marks
the open tab by lighting its *name*, and ImGui has no colour for a selected tab's label. Only the
open tab draws, so only it is measured — see the height rule below.

**Settings** lays every row out on one grid via `row()` in `settings_screen.cpp` — ImGui draws a
control's own label to its *right*, which is why nothing passes a visible label. `row_label()` is
the left column on its own, for the rows that build their control by hand. League is a combo
fed by `LeagueService` from `/api/trade/data/leagues`, cached 24h under `cache_dir()`; the payload
repeats each id per realm so it is filtered to `pc`, hardcoded because this binary can only be
driven by a PC client. **Language** is two rows of the same shape and for the same reason: a
configured value the list does not have is still selectable, or opening Settings would silently
change it. Client language is offered from `GameData::languages()` and says on the row that it
lands on the next run; Interface takes effect at once, since nothing is cached on it. Two
invariants: the dropdown is never empty (fallback → cache → fetch), and
the configured league is never lost — it is the combo preview and is appended as a selectable when
a fetch does not contain it, which is exactly what happens on league-launch day. No request is made
unless Settings is opened. `poe_window_title` is config-file-only, deliberately not in the UI.
**Filter ranges** is two rows of the same shape (`bound_row`), one per side of the interval every
modifier's filter opens to — see `item/range_match`. The percentage box beside each is *disabled*
rather than hidden for the two modes that do not read it: the dialog is sized to hold every section
without scrolling, and a row whose height depends on its own value makes that a moving target.

**Its size is declared, not measured** — `kSettingsW`×`kSettingsH`, 640×720, the same for every
tab, **capped at the game's own height**. It used to be measured: the form reported what the last
row left on the cursor and the window resized to it between frames. Tabs ended that. Each tab
measures differently, so the dialog jumped under the pointer on every tab click, which is a worse
failure than the scrollbar the measuring was there to avoid; 720 clears the tallest tab and still
fits a 768-tall game window. Because the rows scroll **inside a child**, the header, the tab strip
and the Save row sit outside it and stay put when they do — a form whose Save button scrolls away
is the failure that shape exists to prevent. Adding a section needs no new number; adding one that
overflows 720 needs this one raised.

**The two paths the dialog names go through `path_line()`** — the config file beside Save, and the
debug log under Diagnostics. Both are drawn through `paths::display_path`, which folds the home
directory back into `~` or `%APPDATA%`, and both open their **folder** on a click
(`paths::file_url` → `SDL_OpenURL`). Three reasons, and they all point the same way: the line has
one line of room and an absolute path does not reliably fit it; this dialog is the one people
screenshot, and an absolute path names the person who took it; and a `.json` or a `.log` is a file
a desktop may have no handler for, while its folder is a thing every file manager opens. The whole
path is still in the hover, which is where the shortening gives back what it took.

**The look is `src/ui/theme.cpp`** — the game's palette and control shapes, sampled off its Options
dialog: near-black frames under hairline brown borders, headings and the title in the small-caps
face, the left column tinted where the value beside it is not, and orange for check marks, slider
grabs and the open tab. It is app-side and never `ppc_core`, and it is a **scope** rather than the
global style — `ui::Theme` pushes for the window that opens it, because so far only Settings is
styled. `Config::reduce_transparency` is the one part of it that reaches further: `App` calls
`ui::set_opaque_windows()` once per frame, outside every screen, so that a screen which pushes its
own window colour cannot pop the base value back over it. It is opacity and not a blur because
there is no backdrop to sample — the overlay composites over another process's window, and what is
behind our pixels never reaches our framebuffer.

Seven SDL user event types are registered as one contiguous block: hotkey `Action`, league result,
data-updater state, trade result, poe.ninja result, currency-exchange result, application-updater
state. Async results are **not** routed through `Action` — `handle_action()` gates on
the game being foreground and would silently swallow them whenever PoE is not in front.

**Both updaters are re-checked from the hotkey, not from a timer.** `refresh_checks()` runs on
every action that gets past that gate and starts a check for whichever of the bundle and the
release was last checked more than `kRecheckIntervalMs` (30 minutes) ago — so the interval is a
floor and not a schedule: a copy left running overnight asks for nothing, and every request it does
make lands beside something the user was about to look at anyway. Whatever comes back is news for
the *next* press, never for the one that triggered it, which is the only reason this can sit on the
copy path at all. The two clocks are separate so that **Check now** on one Settings row does not
postpone the other's re-check, and the release check is additionally skipped while
`Status::has_news()` — re-checking there would find the same version and take the notice down for
the length of it. A bundle that lands mid-price-check is handled where it always was, by
`take_ready_bundle()` re-resolving the item in hand.

## The debug log (`src/util/debug_log.cpp`)

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
the SDL3+GL+ImGui window; `Config` persists to JSON. **`Config::load` reads every field inside
one `try`**, not just the parse: `config.json` is hand-editable and nlohmann throws as readily on
a field of the wrong type as on a truncated file, so an object where a string belongs would
otherwise be an uncaught exception before the first window — a config the user can only fix by
deleting it. What was read before the throw stands and everything after it keeps its default,
which is the same posture as the clamping the numeric fields already do. **`SDL_HINT_VIDEO_ALLOW_SCREENSAVER` is set
back on**: SDL disables the screensaver at video init on the assumption that it is running a game,
and on Linux that is an `org.freedesktop.ScreenSaver` inhibit — reason "Playing a game" — held for
the life of the process, so an application that sits in the tray all day stopped the machine from
sleeping. The game does its own inhibiting; we are a desktop app. `PPC_DEV_OVERLAY=1` opens Settings and disables
dismiss-on-blur for local dev; add `PPC_DEV_ITEM=<file>` to open the price-check panel on a captured
clipboard instead, `PPC_DEV_PASTE=1` to open the paste popup at the pointer, or `PPC_DEV_IDLE=1` to keep the idle status marker up (it otherwise only ever
appears while the game is the window in front). `PPC_DEV_UPDATE_URL=<url>` points the update check
at a `latest.json` of your own, which is the only way to see its three notice surfaces before a
release publishes one — see [updater.md](updater.md). `PPC_REPORT_URL=<url>` points the bug
reporter at a relay of your own, which is how its two outcomes are seen without posting into the
real channel — see [reporting.md](reporting.md).

The Windows binary is **GUI-subsystem** (`WIN32_EXECUTABLE`, entered at `WinMain` in `src/main.cpp`):
a console-subsystem build pops a console window beside an application whose whole UI is an overlay
and a tray icon. Nothing user-facing goes to stdout — `PPC_DEBUG_COPY`'s traces have nowhere to go
there, which is what the debug log is for.

**Icon** (`src/icon.cpp`): `assets/popc_icon.png` embedded as its own bytes in the generated
`src/icon_data.inc` and read at startup with SDL3's own `SDL_LoadPNG_IO` — no image library, no
runtime asset. One surface feeds both the tray and `SDL_SetWindowIcon`, which is why it has to be
pixels at runtime and cannot come from the Windows resource: `SDL_CreateTray` takes a surface.
The embedded copy is the 128px master **downscaled to 64**, since the tray draws it at 16 or 32
and a window icon at rather less than 128; the master stays the source of the `.ico`, where the
big entries are the ones Explorer uses.

The Windows executable icon is separate: `assets/popc_icon.ico` via an `.rc` resource configured
from `assets/app.rc.in`. That script also carries the executable's **`VERSIONINFO`** — publisher,
product, description and both version strings, spelled exactly as
[packaging/PathOfPriceCheck.iss](../packaging/PathOfPriceCheck.iss) spells them. `FILEVERSION` wants
four numbers where `APP_VERSION` has three, so the fourth is a constant 0.

**Looking like software rather than like malware** is what that resource is for, and it is not the
only thing here doing that job. Microsoft's cloud classifier flagged an unsigned release as
`Trojan:Win32/Wacatac.B!ml` — one verdict out of seventy-one engines, the shape of a false positive
— and until the release is code-signed the answer is a collection of cheap signals, each of which
is also defensible on its own:

- the executable declares a publisher and a version (`VERSIONINFO`, above), because one that
  declares neither is a signal in itself;
- it carries an **application manifest** (`assets/app.manifest`, listed as a source so CMake hands
  it to the manifest tool) declaring `asInvoker` and the standard `supportedOS` block. It declares
  no DPI awareness on purpose — SDL sets that at video init, and taking the decision away from SDL
  would move the overlay on a high-DPI display;
- it is linked with **`/guard:cf`**, which MSVC leaves off by default and most shipped Windows
  software has on. `/CETCOMPAT` is deliberately absent: it is a promise about the whole image,
  statically linked SDL, curl and zlib included, and a wrong one crashes rather than degrading;
- **nothing in it is packed or encoded.** The fonts and the icon go in as their own bytes, so a
  scanner reading `.rdata` finds a TTF table directory and a PNG signature. They used to be
  base85-encoded and `stb_compress`ed, which cost a few kilobytes less and looked exactly like a
  packer: an opaque high-entropy blob beside a routine that decodes it into a fresh buffer.

The last of those is a rule and not just a past decision — **do not obfuscate anything to get past
a scanner.** String encryption, packing or anti-debug tricks raise the score sharply and are read
as deliberate evasion, which is a worse verdict than the one being fixed. The genuinely
dropper-shaped behaviour in this program is the updater downloading an executable and running it
([updater.md](updater.md)), and hiding that would be dishonest as well as counterproductive. When a
release does trip the classifier, the remedy is a false-positive report to Microsoft's WDSI
submission portal as a software developer; it clears within a day or two, but it keys on the file
hash, so it recurs per release until the binaries are signed.

**Fonts** (`src/fonts.cpp`): the UI renders in **Fontin**, the typeface the game itself uses. Four
faces (Regular/Bold/Italic/SmallCaps) are embedded in the executable as their own TTF bytes in the
generated `src/fontin_data.inc` — no runtime asset dependency, and see above for why they are not
compressed. ImGui must be told `FontDataOwnedByAtlas = false` for them, as for a mapped file; the
same config must never reach `AddFontFromFileTTF`, which allocates its own buffer and then leaks it.
Regular is the default; Bold marks panel headers;
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

**A handful of UI glyphs are embedded too**, for the buttons a word does not fit on: a subset of
**Font Awesome Free Solid**, merged over every Fontin face out of the generated
`src/glyph_data.inc`. They are named in `ui/glyphs.hpp` and are ordinary text — they take the text
colour, scale with the font size and sit in a `Button` label like any other string. Three things
make this cheap enough to be worth doing rather than drawing the shapes by hand: they live in the
**Private Use Area**, which Fontin does not claim, so there is nothing to exclude on Fontin's side;
the subset is **two codepoints and 1.4KB**, because `scripts/fetch-glyphs.sh` runs `pyftsubset` over
the release rather than bundling the 416KB face; and the same file makes adding a third glyph a
one-line change. Two things follow from the `≤`/`≥` lesson above and are not optional: the subset
carries `kOnlyGlyphs`, the complement of what it holds, because it still ships a `.notdef` that
would otherwise answer for every codepoint nothing else has; and the result is **asked** —
`Fonts::has_glyphs` is `FindGlyphNoFallback` over `ui::kGlyphCodepoints`, so a subset that has
drifted from the header logs and falls back to a letter instead of drawing an empty button. Font
Awesome is scaled to `kGlyphScale` and nudged by `kGlyphNudgeY`, both fractions of the size: it
draws on a square em against a face with descenders, and at parity it stands a touch tall against
the words beside it. Its license, unlike Fontin's, permits redistribution — see
`assets/fonts/README.md`.

**`ppc_core`** is the static library holding everything that needs neither a window nor a network,
so it can be unit-tested headless: `paths`, `config`, `leagues`, `platform/input`,
`platform/single_instance` (a kernel lock needs neither), `util/` (including
the debug log, which every platform seam writes into), all of `item/`, all of `data/` except the
updater, `ui/strings` (our own text is a table, not a widget), and all of `trade/`, `ninja/` and
`exchange/` except their clients. The rule is that `ppc_core` links
no SDL3, no ImGui, no X11 and no libcurl. Tests use doctest and link only `ppc_core`.
