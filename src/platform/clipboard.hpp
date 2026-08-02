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

} // namespace ppc
