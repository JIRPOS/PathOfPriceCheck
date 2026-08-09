#pragma once

#include <string>

namespace ppc {

/// True if the OS foreground/active window's title contains `needle`.
/// Used to gate the overlay so it only reacts while Path of Exile is focused.
bool foreground_title_contains(const std::string& needle);

/// The foreground/active window's title, empty when there is none. Diagnostics: knowing the
/// title that did *not* match is the difference between a misconfigured `poe_window_title`
/// and the game genuinely not being in front.
std::string foreground_title();

/// Who holds the keyboard right now, as one line, for the copy log. Diagnostics only, but
/// cheap enough to sample on every poll — which is the point: the WM's active-window property
/// lags the server's real input focus, so at one-sample-per-second the two are the same event
/// and it is impossible to say whether a late clipboard handover happened *before* the game
/// lost focus or *because* it did.
std::string focus_info();

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

/// Take the *window manager's* active window off the game, onto one throwaway pixel of ours.
///
/// A different thing from `focus_game_window`'s opposite: that moves the X input focus, which
/// the server owns, and Wine does not act on it — measured, the game stayed
/// `_NET_ACTIVE_WINDOW` throughout and never published its copy. Wine re-exports the clipboard
/// on a **WM-level** activation change, so this asks the WM for one. Undone by
/// `activate_game_window`, which every caller owes: leaving the game deactivated is the same
/// bug as an alt-tab nobody undid.
///
/// False where there is nothing to do (Windows, whose clipboard needs none of this) or where
/// the request could not be made.
bool deactivate_game_window();

/// Ask the window manager to make the game active again — the WM's own decision, not the
/// server's, so it is not `focus_game_window`. Also releases whatever
/// `deactivate_game_window` allocated, so it is safe (and required) to call after a failed one.
void activate_game_window(const std::string& needle);

} // namespace ppc
