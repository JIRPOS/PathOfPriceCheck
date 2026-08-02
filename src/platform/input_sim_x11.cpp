#include "platform/input_sim.hpp"

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

namespace ppc {
namespace {

Display* display() {
    static Display* d = XOpenDisplay(nullptr); // main-thread only
    return d;
}

void key(Display* d, KeySym sym, bool down) {
    KeyCode kc = XKeysymToKeycode(d, sym);
    if (kc) XTestFakeKeyEvent(d, kc, down, CurrentTime);
}

} // namespace

void simulate_copy() {
    Display* d = display();
    if (!d) return;
    key(d, XK_Control_L, true);
    key(d, XK_c, true);
    key(d, XK_c, false);
    key(d, XK_Control_L, false);
    XFlush(d);
}

} // namespace ppc
