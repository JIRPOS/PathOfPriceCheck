#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// The paste list: saved snippets a hotkey puts on the clipboard.
///
/// This half is the model — the list, which of it the popup can offer, and how a body reads on
/// one line. It is `ppc_core`, so none of it knows about ImGui or the clipboard: the popup is
/// `screens/quickpaste_screen`, the write is `platform/clipboard`'s `clipboard_set_text`.
namespace ppc {

/// One saved snippet: a name to recognise it by, and the text that goes on the clipboard.
/// The body is multi-line and the heading is not — the popup draws the heading whole and the
/// body as the single line `paste_preview` makes of it.
struct Paste {
    std::string heading;
    std::string body;
    /// Whether it takes one of the nine slots the popup offers. Storage is unlimited; the
    /// number keys are not (see `kMaxActivePastes`).
    bool enabled = true;
};

/// How many pastes the popup can hold at once. **A limit on the keyboard, not on storage**:
/// picking by number is the whole point of the feature, a tenth entry would have no key to
/// press, and a list longer than a glance has already spent what it saves.
inline constexpr size_t kMaxActivePastes = 9;

/// How many entries the popup would draw right now.
size_t enabled_pastes(const std::vector<Paste>& list);

/// Indices into `list` of the pastes the popup offers, in list order. Never more than
/// `kMaxActivePastes` — the popup's slot *n* is `active_pastes(...)[n]`, which is also the
/// number key that picks it.
std::vector<size_t> active_pastes(const std::vector<Paste>& list);

/// Turn off everything enabled past the ninth. For loading a config file, which is
/// hand-editable: a file claiming twelve active pastes is not a reason to draw a popup with
/// keys nobody can press. Returns how many it turned off.
size_t limit_enabled(std::vector<Paste>& list);

/// Move the entry at `from` to sit at `to`, shifting the rest along. Out-of-range or equal
/// indices are a no-op and answer false, which is what makes it safe to call from a drag that
/// has run off the end of the list.
bool move_paste(std::vector<Paste>& list, size_t from, size_t to);

/// The body as one line: every run of whitespace — newlines included — collapsed to a single
/// space, the ends trimmed, and the result cut to `max_chars` characters with an ellipsis. The
/// cut is on a UTF-8 boundary, so it can never split a character in half.
///
/// `max_chars` is a bound on the work the popup does, not the width it draws at: the row is
/// clipped to the pixels it actually has (`ellipsize` in the screen). A body of a hundred
/// kilobytes must not turn into a hundred kilobytes of text measurement.
std::string paste_preview(std::string_view body, size_t max_chars = 160);

} // namespace ppc
