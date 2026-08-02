#pragma once

#include <string>

namespace ppc {

/// True if the OS foreground/active window's title contains `needle`.
/// Used to gate the overlay so it only reacts while Path of Exile is focused.
bool foreground_title_contains(const std::string& needle);

/// A located game window and its on-screen geometry (global/screen coordinates).
struct GameWindow {
    bool present = false; ///< a window whose title contains the needle exists
    bool focused = false; ///< that window is the active/foreground window
    int x = 0, y = 0;     ///< top-left, in global screen coordinates
    int w = 0, h = 0;     ///< size in pixels
};

/// Find the game window by title substring so the overlay can position itself over
/// it. Returns `present=false` when no match exists (game not running yet).
GameWindow find_game_window(const std::string& needle);

/// Give keyboard focus back to the game window. Used before a simulated copy (so the
/// synthetic Ctrl+C reaches the game, not our overlay) and when a panel closes.
void focus_game_window(const std::string& needle);

} // namespace ppc
