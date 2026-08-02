#include "platform/overlay_native.hpp"

#include <SDL3/SDL.h>

#include <X11/Xlib.h>
#include <X11/extensions/shape.h>

namespace ppc {
namespace {

Display* x_display(SDL_Window* w) {
    return static_cast<Display*>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(w), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr));
}

Window x_window(SDL_Window* w) {
    return static_cast<Window>(SDL_GetNumberProperty(
        SDL_GetWindowProperties(w), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
}

} // namespace

void overlay_set_click_through(SDL_Window* w, bool passthrough) {
    Display* dpy = x_display(w);
    Window win = x_window(w);
    if (!dpy || !win) return;
    if (passthrough)
        // An empty input region means the window catches no events; they hit the game.
        XShapeCombineRectangles(dpy, win, ShapeInput, 0, 0, nullptr, 0, ShapeSet, Unsorted);
    else
        XShapeCombineMask(dpy, win, ShapeInput, 0, 0, None, ShapeSet); // restore full input
    XFlush(dpy);
}

void overlay_set_unmanaged(SDL_Window* w, bool unmanaged) {
    Display* dpy = x_display(w);
    Window win = x_window(w);
    if (!dpy || !win) return;
    XSetWindowAttributes attrs;
    attrs.override_redirect = unmanaged ? True : False;
    XChangeWindowAttributes(dpy, win, CWOverrideRedirect, &attrs);
    XFlush(dpy);
}

void overlay_take_keyboard_focus(SDL_Window* w) {
    Display* dpy = x_display(w);
    Window win = x_window(w);
    if (!dpy || !win) return;
    XSetInputFocus(dpy, win, RevertToParent, CurrentTime); // WM won't focus an OR window
    XFlush(dpy);
}

} // namespace ppc
