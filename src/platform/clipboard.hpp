#pragma once

#include <cstdint>
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

/// An opaque number identifying what is on the clipboard. It changes when — and only when —
/// something writes the clipboard. **Compare for equality, never for order:** the X11 half is
/// built on a server time that wraps.
///
/// This answers the whole stale-vs-fresh question at the source, and it is why the copy path
/// needs no snapshot of the previous clipboard, no byte comparison and no special case for
/// re-checking the same item: identical bytes with a new stamp are a real copy, and any bytes
/// at all with the old stamp are what the previous owner is still serving.
///
/// Cheap enough to call every frame, and it cannot perturb a handover: X11 reads the
/// `selection_timestamp` the server pushes with each XFixes ownership change, so nothing is
/// ever asked of the owner; Windows uses `GetClipboardSequenceNumber()`. Zero means no write
/// has been seen this run, which compares correctly — a later write differs from it.
///
/// Not `SDL_EVENT_CLIPBOARD_UPDATE`, for the same reason `clipboard_text` is ours: SDL raises
/// that event not on the ownership change but once the new owner answers a `TARGETS`
/// conversion SDL fires in response, and a fullscreen game mid-frame answers late or never.
uint64_t clipboard_stamp();

/// Ask the current owner to render the clipboard, without reading it. Costs one conversion
/// request and waits for nothing.
///
/// **This is what makes a copy appear at all under Wine.** Wine's clipboard manager is lazy: it
/// holds the selection but does not render the Windows clipboard behind it until somebody asks,
/// and it publishes by *re-asserting ownership* — so a purely passive watcher (XFixes, or SDL's
/// event) waits forever for a publish that only its own asking would cause. Measured: the
/// ownership change lands 0-2ms after this request, every time.
///
/// Cheap enough to call on every poll of a pending copy, and deliberately separate from
/// `clipboard_text` — the answer we want is the stamp moving, not whatever the owner is still
/// serving from the previous copy. No-op on Windows, where the clipboard is already rendered.
void clipboard_poke();

/// Diagnostics only: who owns the clipboard right now, as "0x<window> class 'X' pid N".
/// Server-side queries exclusively — it never asks the owner for anything, so it cannot
/// perturb a handover in flight and is safe to call while waiting for one.
std::string clipboard_owner_info();

/// Diagnostics only: the formats the owner says it can supply, as a space-separated list.
/// Unlike `clipboard_owner_info` this *is* a round trip to the owner and can therefore
/// change what it does next; only ever call it from a debug path.
///
/// **Every non-answer is parenthesised** — `(no owner)`, `(no reply)`, `(refused)`, `(none)`,
/// `(no display)` — and a real list never is, so a leading `(` is the whole test for "the owner
/// did not answer". That distinction carries weight: an owner that answers exists and responds,
/// which is half of what tells the sticky clipboard wedge from an ordinary failed copy.
std::string clipboard_targets(int timeout_ms);

} // namespace ppc
