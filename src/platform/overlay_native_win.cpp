#include "platform/overlay_native.hpp"

#include <SDL3/SDL.h>
#include <windows.h>

namespace ppc {
namespace {

HWND hwnd_of(SDL_Window* w) {
    return static_cast<HWND>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}

} // namespace

void overlay_set_click_through(SDL_Window* w, bool passthrough) {
    HWND hwnd = hwnd_of(w);
    if (!hwnd) return;
    LONG ex = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (passthrough)
        ex |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
    else
        ex &= ~WS_EX_TRANSPARENT;
    SetWindowLong(hwnd, GWL_EXSTYLE, ex);
}

void overlay_set_unmanaged(SDL_Window*, bool) {
    // A layered always-on-top window already floats over borderless fullscreen on Win32.
}

void overlay_take_keyboard_focus(SDL_Window* w) {
    HWND hwnd = hwnd_of(w);
    if (!hwnd) return;
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
}

} // namespace ppc
