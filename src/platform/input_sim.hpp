#pragma once

namespace ppc {

/// Synthesize a Ctrl+C keystroke to the currently focused window, so the game
/// copies the hovered item to the clipboard without the user pressing it.
///
/// The game performs the clipboard write itself — Ctrl+C is a PoE keybind, not an
/// OS operation — so injecting a real input event is the only way to trigger it.
///
/// Blocks for up to ~250ms waiting for the user to let go of the price-check hotkey
/// first; injecting while its modifiers are still down gives the game a polluted
/// combo (and on X11 the keyboard is still grabbed, see hotkeys_x11.cpp).
void simulate_copy();

} // namespace ppc
