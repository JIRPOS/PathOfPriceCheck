#pragma once

#include <string>

namespace ppc {

/// Read the system clipboard as UTF-8 text, giving the owner up to `timeout_ms` to answer.
/// Empty on timeout, on an empty clipboard, or when the content isn't text.
///
/// Deliberately not `SDL_GetClipboardText()`: SDL's X11 backend gates reads on a mime-type
/// list it fills from a single unretried `TARGETS` probe at init plus XFixes owner-change
/// notifications, and returns "" with no X traffic when that list is empty — so a clipboard
/// owned by a process that never re-asserts ownership reads as permanently blank. Its
/// timeout path also seizes the CLIPBOARD selection and then serves empty text, destroying
/// the user's real clipboard. Reading the selection ourselves avoids both.
std::string clipboard_text(int timeout_ms);

/// Has the clipboard been written since the last call? Latching: returns true once per
/// change and clears itself, so callers arm it by calling it and discarding the result.
///
/// This is the only evidence that a copy *happened* when the copied text is byte-identical
/// to what was already there — re-checking the same item, which is common. It is ours and
/// not `SDL_EVENT_CLIPBOARD_UPDATE` for the same reason `clipboard_text` is ours: SDL's
/// XFixes handler does not raise that event on the owner change itself, it fires a `TARGETS`
/// conversion and raises the event only when the *reply* lands. A fullscreen game mid-frame
/// answers that probe late or never, so SDL stays silent through a copy that plainly
/// happened, and only speaks up on the focus change that finally makes the game answer.
/// X11 reads XFixes owner-change notifications directly; Windows reads the clipboard
/// sequence number.
bool clipboard_changed();

} // namespace ppc
