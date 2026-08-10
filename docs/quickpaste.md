# QuickPaste

<!-- Developer notes for PathOfPriceCheck. Loaded on demand; see ../CLAUDE.md for the map. -->

A hotkey (**Alt+V** by default) opens a list of saved snippets at the cursor. Pick one — by click
or by the number key beside it — and its text goes on the clipboard, the popup closes, and the
game gets the focus back. The user pastes it themselves.

Three files and one seam: the model is `src/quickpaste.{hpp,cpp}` (`ppc_core`), the popup is
`src/screens/quickpaste_screen.cpp`, the list is edited in Settings' **QuickPaste** tab
(`quickpaste_tab` in `settings_screen.cpp`), and the write goes through
`clipboard_set_text` in [platform.md](platform.md)'s clipboard seam.

## What it will not do

**Nothing presses Ctrl+V**, and this is a decision rather than a gap. The application's one
sanctioned reason to touch the game's focus is prising the clipboard out of Wine
([architecture.md](architecture.md)); typing into whatever window happens to be in front is a
different promise, and a mistimed one lands in a chat box the user was not looking at. What the
tool guarantees is that the text is *on the clipboard* when the popup closes. The public plan
([../ROADMAP.md](../ROADMAP.md)) says the same thing in the user's words, and says the keystroke
may come back later as an option.

## The nine slots

`kMaxActivePastes` is 9, and it is a limit on the **keyboard, not on storage**. A tenth entry
would have no key to press, and a list longer than a glance has already spent the time it saves
at a vendor window. So the list is unbounded and each entry carries `enabled`; `active_pastes()`
returns the enabled ones in list order, capped at nine, and slot *n* of that answer is the key
that picks it.

Everywhere the ceiling is reached, the answer is **a control that is not available rather than an
error to read**: the tenth checkbox is disabled, not refused; a new paste is created enabled when
there is room and disabled when there is not, because one that silently displaced an existing
slot would be worse than one with no number yet. `limit_enabled()` applies the same rule when the
config file is *read*, since that file is hand-editable and a run drawing keys nobody can press
is not a state worth having.

## Picking by number is scancodes

`paste_slot_for()` in `app.cpp` reads `SDL_Scancode`, not the keycode. **The digits are printed
on the number row only on a US layout** — on a Czech one the same physical keys produce
`ěščřžýáíé` — so a keycode test would leave the feature unusable by number for most of Europe. A
scancode is the key's *position*, which is what "the second one along" means and what the digit
drawn in the square stands for. The keypad answers the same slots.

That is also why the popup **claims the keyboard focus** (`overlay_take_keyboard_focus`, as
Settings and the range editor do): our window is override-redirect, so without it every keystroke
goes to the game. It takes the X server's input focus and not the window manager's activation —
`active=` stays on the game — which is why it is not the thing
[architecture.md](architecture.md) forbids.

## Where the window goes

`App::place_overlay` sizes it from `quickpaste_size(entries)` — **declared, not measured**, for a
harder reason than Settings' fixed size: placement happens before the frame that could measure
it, so a height taken from the last frame would place the popup for the previous list every time.
The constants live in `quickpaste_screen.cpp` beside the code that draws to them.

Position is the cursor sampled **at hotkey time** (`paste_x_`/`paste_y_`), for the same reason
`side_` is sampled there: by the time anything is placed, the hand has moved. It opens to the
right of the cursor and downwards from it, then is clamped into the game window — which is what
turns "downwards" into "upwards" near the bottom edge, and into somewhere between the two in the
middle. There is deliberately no rule that picks a direction: a rule and a clamp would disagree
in exactly the cases that matter.

It dismisses like a price check — Escape, the hotkey again, a click outside it
(`poll_click_away`, whose panel rectangle is simply the whole window here) or the game taking
focus back.

## The clipboard write

`clipboard_set_text` is a platform seam of ours, never `SDL_SetClipboardText`. On X11 there is no
clipboard to put something *in*: a selection is a live window answering `SelectionRequest`, so a
write is a promise to still be there when the paste happens — after the popup has closed and the
user has clicked into a chat box. Hence a thread with its own `Display` that owns `CLIPBOARD` for
the life of the process; the main thread hands text over under a mutex and pokes a self-pipe,
because **the Display is touched only by that thread** (the rule the hotkey listener follows, and
the same Xlib abort behind it).

**The write blocks until the selection is actually ours**, and that is the fix for the bug this
shipped with: `pick_paste` hands the keyboard focus straight back to the game the moment it
returns, and **Wine re-reads the X selection around a focus change**. Posting the text to the
owner thread and returning meant the focus went back while Wine still owned the selection — so
the first paste was of the *previous* clipboard and only the next one, after some later event had
made Wine look again, came out right. Reported from the game as "it only works on the second
try". The wait is two round trips on our own connection (measured at 0.1-0.7ms) against a 250ms
bound that exists only so a wedged server cannot hold the main loop.

The same argument is why `give_keyboard_back` does not simply test `overlay_.has_focus()`: SDL's
view of our own focus lags the `XSetInputFocus` that caused it, so a popup dismissed briskly
could skip handing the focus back at all — and on this path that focus change is the thing that
makes Wine look. `took_keyboard_` is our own record of the call; the foreground check beside it
is what stops it becoming a focus steal when the user has alt-tabbed away.

Two more consequences worth knowing:

- **There is no INCR on the write side.** A value goes over in one `XChangeProperty`, so
  `kMaxClipboardWrite` (64KB) is the ceiling, and the editor disables **Done** past it rather than
  storing something that cannot be served. A paste is a chat line or a map regex; nothing that
  belongs here comes near it.
- **A price check in flight is dropped first.** Its stamp would move on our own write and the
  copy would be read as an item that is not one. `handle_action` calls `abandon_copy` before
  opening the popup.

## Ordering, editing, deleting

The Settings list is **read-only apart from arranging it**: enable, drag to reorder, edit,
delete. A field that is typed into is a field that has to be finished before anything else can be
clicked, and the body is multi-line — so writing happens in a modal dialog on a **draft**, which
Done copies back and Cancel drops. Nothing persists until Settings' own Save, which is also what
makes an accidental delete recoverable: close Settings without saving.

Reorder is a drag on the grip glyph, applied *after* the loop that drew the list (`PasteAction`)
— a delete changes the vector being walked, and a move needs the height of a row that has not
been drawn yet. `move_paste` answers false for a move that cannot happen.

**The drag is measured in pixels of travel, not in rows crossed**, and this is the whole of what
makes it hold still. A row is picked up only once the pointer has covered the full height of the
neighbour it is heading for, and that height is then taken off a tally (`PasteDrag::paid`), so
the row stays under the hand and the move that would put it back needs the same distance again.
Asking instead which row the pointer is *over* — the shape of ImGui's own demo — works only
while every row is the same height. These are two lines and the second one wraps, so a move drops
the pointer back over the row it just came from, that reads as a move the other way, and the list
flickers between the two for as long as the button is held. That is what shipped, and it is what
this replaces. Travel past either end is *forgotten* rather than banked: banked, the hand would
have to give the distance back before the row moved again.

The drag is also **ours to track, not ImGui's**: a row's id is its index, so the frame a move
lands, ImGui is holding the handle of the row that slid into the vacated place, and the drag
would go on shoving whatever kept arriving there. `PasteDrag::index` follows the paste, and the
held colouring is painted on that row rather than on the id ImGui thinks is down — a pressed
handle left behind on a row standing still is the drag appearing to have gone somewhere it has
not.

The row actions are Font Awesome glyphs — grip-lines, pen, trash-can, square-plus — added to
`ui/glyphs.hpp` and `scripts/fetch-glyphs.sh` together, as that header insists. Adding them is
what turned `kOnlyGlyphs` in `fonts.cpp` from a hand-written range into one derived from
`kGlyphCodepoints`: the old range covered U+F00C..U+F0E2 because that was where the first two
glyphs happened to sit, and every one of these four would have baked as nothing.

## Storage

The list lives in `config.json` under `pastes`, one object per entry
(`heading`, `body`, `enabled`), in the order the popup lists them — and in
[../PRIVACY.md](../PRIVACY.md), because it is text the user typed and it is written to disk.
