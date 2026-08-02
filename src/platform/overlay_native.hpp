#pragma once

struct SDL_Window;

namespace ppc {

/// Toggle input passthrough (click-through). When `passthrough` is true, pointer
/// and keyboard events fall through the overlay to whatever is beneath it, so the
/// transparent overlay can sit on top of the game permanently without stealing input.
void overlay_set_click_through(SDL_Window* w, bool passthrough);

/// Mark the overlay as an unmanaged / override-redirect window so the compositor
/// stacks it above everything (as it does tooltips), including fullscreen windows.
/// On X11 this must be set while the window is hidden, before it is first shown.
/// The window then receives no WM focus — see overlay_take_keyboard_focus.
/// No-op where the window manager already floats it (Win32).
void overlay_set_unmanaged(SDL_Window* w, bool unmanaged);

/// Force keyboard focus onto the overlay. An override-redirect window gets no focus
/// from the WM, so screens that need typing (Settings) must claim it directly.
void overlay_take_keyboard_focus(SDL_Window* w);

} // namespace ppc
