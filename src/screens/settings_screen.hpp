#pragma once

namespace ppc {
class App;
void draw_settings_screen(App& app);

/// Which tab the paste list is on, for the popup's way in to it. A constant rather than a
/// search for the name: the tab strip is a table of function pointers, and the one thing that
/// could go wrong here — this number naming a different tab — is caught by a `static_assert`
/// beside that table.
inline constexpr int kQuickPasteTab = 2;
} // namespace ppc
