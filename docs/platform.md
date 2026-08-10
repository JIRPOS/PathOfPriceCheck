# Where the real difficulty is

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

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
  `deactivate_game_window()` / `activate_game_window()` are the pair that gives the game a
  **window-manager-level** focus-out, which is the only kind Wine acts on (see the copy path in
  [architecture.md](architecture.md)). X11 maps a 1×1 undecorated utility window and sends the WM an `_NET_ACTIVE_WINDOW`
  client message naming it, then another naming the game. Two things are load-bearing and both
  were measured: the **source indication must be 2** (pager / direct user action) — with 1
  (application) KWin's focus-stealing prevention grants the activation *out*, because that
  window has just mapped, and refuses the one *back*, which strands the user off the game; and
  the message must carry a **real server timestamp**, taken from a zero-length property append
  on our own window, since `CurrentTime` is refused outright. Round trip 10ms each way, and the
  helper is undecorated via `_MOTIF_WM_HINTS` or the WM frames one pixel with a titlebar and the
  handover becomes a box flashing over the game. Windows needs none of it — its clipboard is
  written by the copy itself — so `deactivate_game_window()` is `false` there and the caller
  does nothing.
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
  see the copy watch in [architecture.md](architecture.md)); Win32 uses `GetClipboardSequenceNumber`. **It is a value,
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
- **`platform/clipboard.hpp` — `clipboard_set_text(text)`:** the write, for QuickPaste. Not
  `SDL_SetClipboardText`, and the reason is not the read path's: **X11 has no clipboard to put
  something into.** A selection is a live window answering `SelectionRequest`, so a write is a
  promise to still be there when the paste happens — which here is Wine asking, after the popup
  has closed. So the owner is a window on a thread of its own with its own `Display`, started on
  the first write and never stopped; the main thread hands text over under a mutex and pokes a
  self-pipe, because **that Display is touched only by that thread** (the hotkey listener's rule,
  and the same abort behind it). It answers `TARGETS`, `TIMESTAMP`, `UTF8_STRING`,
  `text/plain;charset=utf-8`, `text/plain` and `STRING`, takes ownership with a **real server
  timestamp** (the zero-length property append, as the handover does — ICCCM wants an owner able
  to answer `TIMESTAMP` truthfully), and drops the text on `SelectionClear` rather than serving
  something it no longer owns. `STRING` is served the same UTF-8 bytes on purpose: it is
  nominally Latin-1, but everything that can read UTF-8 asks for `UTF8_STRING` first and refusing
  `STRING` leaves the rest with nothing. **The call blocks until ownership is asserted** (a
  condition variable, bounded at 250ms): the caller hands the focus back to the game immediately
  afterwards and Wine re-reads the selection around that focus change, so returning before the
  server has us as the owner is a first paste of the previous clipboard — measured, reported, and
  the reason this is not fire-and-forget. **No INCR on this side** — one `XChangeProperty`, hence
  `kMaxClipboardWrite` (64KB) and an editor that will not store more. Windows is the ordinary
  `OpenClipboard`/`CF_UNICODETEXT` write, retried while another application holds the lock.
  → [quickpaste.md](quickpaste.md)
- **`clipboard_wedge_note` in `App` concludes from the owner's behaviour, never from its
  identity** — and that is the second attempt, the first two having been wrong in ways worth
  recording so nobody rebuilds them. It says the one thing the app can state truthfully about
  the sticky wedge: the selection belongs to something other than the game and the copy was
  never published, so alt-tab out and back. It fires **only on the give-up line**, from two
  facts already in hand there: nothing asserted ownership during the whole check (that *is* the
  give-up condition) and the owner answered a format list, so it exists and responds. A live,
  responsive owner that never re-asserted is not the game's clipboard. Naming this in the log is
  what stops the next report of it starting from scratch.
  **Comparing `_NET_WM_PID` cannot work, measured**: the owner during the captured failures is
  KWin's own selection window, which advertises no pid and no `WM_CLASS` at all (`xprop` on it
  returns one KDE-private property), so an owner-vs-game pid check silently never fires. That
  is why `window_desc` warns that everything it prints may be missing.
  **Fingerprinting the format list against Wine's would be a third guess**: every capture of
  Wine's own formats came through the Wayland bridge rather than off the X selection, so there
  is nothing verified to match. The list is logged beside the note and is usually
  self-describing about who *did* have it — `chromium/x-source-url` is the browser,
  `application/x-kde-onlyReplaceEmpty` the clipboard manager — which is a hint for a reader and
  deliberately not a rule for the code.
  It cannot fire earlier than give-up, and an attempt to log it at injection was reverted: the
  evidence that settles it is `clipboard_targets`, a **real conversion request**, and issuing
  one at injection would perturb the handover being measured. The owner window id is still
  logged there, because across every capture it is the field that predicts the outcome and it
  costs no round trip — but nothing may be concluded from it, since neither Wine's clipboard
  window nor KWin's identifies itself.
- **`platform/single_instance.hpp` — `InstanceLock`:** one running copy per user. `flock` on
  `<cache>/PathOfPriceCheck.lock`; a session-local named mutex (`Local\`, not `Global\`) on
  Windows. A **kernel lock and not a pid file**, because the operating system releases it when the
  process dies — a pid file has to be removed by the process that died, and the recovery ("is 4711
  still us?") races pid reuse in the one direction that matters, refusing to start. flock belongs
  to the open file description rather than to the process, which is also what makes it testable
  in-process: two claims collide without spawning anything. **"Could not evaluate" counts as
  held** — an unwritable cache directory or a filesystem with no locking must not turn a rare
  annoyance into an app that will not launch with nothing on screen saying why; only a lock
  positively observed to be somebody else's refuses. Claimed in `App::run` **before the debug log
  is opened**, or a stray second launch would start a log file and prune the ten kept, pushing the
  run being diagnosed out of the window. The rejection is the one launch failure said **out loud**
  (`SDL_ShowSimpleMessageBox`, exit 0): silence is a rule about the overlay, which is noise over a
  game, while this is something the user just did on their desktop and the answer is that they
  already have what they asked for. What makes it worth having is that two copies both grab the
  global hotkeys — X11 gives a passive grab to whoever asked first, so the *newly launched* copy
  silently does nothing, which reads as a broken hotkey — and keep two unsynchronised copies of the
  rate limiter's state, which is how a client walks into a lockout.
- **`platform/platform.hpp` — `platform_init()`:** one-time init (X11 calls `XInitThreads`).

Key naming is canonical strings ("D", "Space", "F5"); `key_name_from_sdl` (capture), the X11 keysym
lookup, and the Win32 VK lookup each translate them. OS hotkey APIs are **side-agnostic** on
modifiers, so "LShift" registers as "Shift".
