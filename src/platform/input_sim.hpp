#pragma once

namespace ppc {

/// Synthesize a Ctrl+C keystroke to the currently focused window, so the game
/// copies the hovered item to the clipboard without the user pressing it.
void simulate_copy();

} // namespace ppc
