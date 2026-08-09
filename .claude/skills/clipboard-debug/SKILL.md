---
name: clipboard-debug
description: Diagnose a PathOfPriceCheck price check that hung, timed out, showed a stale clipboard, or silently showed nothing. Use whenever the copy path is implicated — Ctrl+D does nothing, the panel shows the previously copied text, the first check after launch hangs until the user alt-tabs, or an item only appears on the second press.
---

# Debugging the copy path

**Instrument, do not guess.** This path has cost more sessions than any other in the project, and
every fix that was reasoned from first principles made it worse. The app writes a debug log for
exactly this; start there.

## 1. Get the evidence before touching code

Ask the user for two things and wait for them:

1. The **debug log** — `debug_log` must be on (a Settings checkbox under Diagnostics, off by
   default). Files are `<cache>/logs/ppc-<date>-<time>.log`, one per run, newest ten kept; on Linux
   that is `~/.cache/PathOfPriceCheck/logs/`.
2. The **four-character check id** printed in the panel's footer (clicking it copies it). Every
   press of the price-check hotkey mints one and it tags every line the check wrote, so "check
   S36Q hung" names a span of the file.

Then read that span — including the two `[copy]` lines above the give-up line, which say whether
the automatic handover moved the active window at all.

## 2. What the fields mean

- `input=` / `active=` — the X server's focus and the window manager's `_NET_ACTIVE_WINDOW`. **They
  are different things and the distinction is the whole history of this bug**: Wine only reacts to a
  WM-level focus change, so `input=` moving while `active=` stays on the game means the nudge did
  nothing.
- The clipboard **stamp** changes when and only when something writes the clipboard. Equal before
  and after means nothing was copied, whatever the clipboard holds. A drop to *no owner* is
  deliberately not counted — it is the opposite of a write.
- `clipboard_owner_info()` is server-side only and safe to call mid-handover.
  `clipboard_targets()` is a **real conversion request** to the owner and can change what it does
  next — it is asked once, on the give-up line. Suspect it first whenever turning the log on changes
  the behaviour being logged.
- The clipboard contents go in whole as base64 plus an FNV-1a-64 digest, because the
  UTF-8-vs-Latin-1 difference is exactly what the two-texts problem turns on.

## 3. The two known failures

**Wine only speaks when spoken to.** PoE under Wine does not publish its copy to the X selection on
its own — measured at ~13s with no external prompting. Each poll of a pending copy therefore issues
one fire-and-forget `TARGETS` request (`clipboard_poke`) whose reply is never read; the ownership
change lands 0–2ms after it. A purely passive watcher waits forever.

**The KWin wedge, which is not ours to fix.** A copy made in a *Wayland* application while the game
runs makes KWin's Xwayland bridge take the selection; Wine loses it, still believes it owns it, and
never publishes again. There is nothing to poke and nothing to read. Only a real **window-manager**
focus change out of the game recovers it. The app states this on the give-up line
(`clipboard_wedge_note`) when the owner answered a format list — a live, responsive owner that never
re-asserted is by definition not the game's clipboard.

## 4. Do not re-try these

Each was built, measured and reverted. Re-proposing one is the actual regression risk.

- `SDL_GetClipboardText()` — its X11 backend can return `""` for the life of the process, and its
  wait seizes the selection and destroys the real clipboard.
- **Clearing the clipboard before the copy** — tried as a way to detect a fresh write; made it
  strictly worse.
- A **byte comparison against a pre-copy snapshot**, a latching write detector, a "copying…" state,
  an overdue state. All answered a question the stamp answers at the source; re-checking the same
  item produces identical bytes, which no comparison can see.
- A **native Wayland clipboard backend** (`ext-data-control-v1`). It was built, it read the
  selection correctly, and the wedge reproduced step for step, because the bug is upstream of both
  readers. Removed.
- `XSetInputFocus` on our own override-redirect window as the nudge — a window the WM does not
  manage cannot be made active, so it cannot work.
- Re-pressing a modifier the user is already holding during injection: a fake press is cancelled
  only by a fake release, and a wedged server silently breaks Alt+Tab and every hotkey.

## 5. Then

Read [docs/platform.md](../../../docs/platform.md) for the seams and
[docs/architecture.md](../../../docs/architecture.md) for the copy path and the focus rules before
changing either. `PPC_DEBUG_COPY=1` traces the same timeline to stderr on Linux; on the
GUI-subsystem Windows binary it has nowhere to go, which is what the log file is for.
