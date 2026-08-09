#include "platform/foreground.hpp"
#include "platform/platform.hpp"

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace ppc {
namespace {

int ignore_xerror(Display*, XErrorEvent*) { return 0; } // windows can vanish mid-query

Display* display() {
    static Display* d = [] {
        Display* dpy = XOpenDisplay(nullptr);
        if (dpy) XSetErrorHandler(ignore_xerror);
        return dpy;
    }();
    return d;
}

Window active_window(Display* d) {
    Atom prop = XInternAtom(d, "_NET_ACTIVE_WINDOW", True);
    if (prop == None) return 0;
    Atom type;
    int format;
    unsigned long n = 0, bytes = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(d, DefaultRootWindow(d), prop, 0, 1, False, AnyPropertyType, &type,
                           &format, &n, &bytes, &data) != Success ||
        !data)
        return 0;
    Window w = n ? *reinterpret_cast<Window*>(data) : 0;
    XFree(data);
    return w;
}

std::string window_title(Display* d, Window w) {
    if (!w) return {};
    std::string title;
    Atom net_name = XInternAtom(d, "_NET_WM_NAME", True);
    Atom utf8 = XInternAtom(d, "UTF8_STRING", True);
    if (net_name != None && utf8 != None) {
        Atom type;
        int format;
        unsigned long n = 0, bytes = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(d, w, net_name, 0, 1024, False, utf8, &type, &format, &n, &bytes,
                               &data) == Success &&
            data) {
            title.assign(reinterpret_cast<char*>(data), n);
            XFree(data);
        }
    }
    if (title.empty()) { // fall back to legacy WM_NAME
        char* name = nullptr;
        if (XFetchName(d, w, &name) && name) {
            title = name;
            XFree(name);
        }
    }
    return title;
}

std::vector<Window> client_list(Display* d) {
    Atom prop = XInternAtom(d, "_NET_CLIENT_LIST", True);
    if (prop == None) return {};
    Atom type;
    int format;
    unsigned long n = 0, bytes = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(d, DefaultRootWindow(d), prop, 0, 4096, False, XA_WINDOW, &type, &format,
                           &n, &bytes, &data) != Success ||
        !data)
        return {};
    Window* w = reinterpret_cast<Window*>(data);
    std::vector<Window> out(w, w + n);
    XFree(data);
    return out;
}

bool window_geometry(Display* d, Window w, int& x, int& y, int& width, int& height) {
    XWindowAttributes a;
    if (!XGetWindowAttributes(d, w, &a)) return false;
    Window child;
    int rx = 0, ry = 0;
    // Attributes' x/y are relative to the WM frame; translate the origin to the root.
    XTranslateCoordinates(d, w, DefaultRootWindow(d), 0, 0, &rx, &ry, &child);
    x = rx;
    y = ry;
    width = a.width;
    height = a.height;
    return a.map_state == IsViewable;
}

Window matching_window(Display* d, const std::string& needle) {
    for (Window w : client_list(d)) {
        std::string t = window_title(d, w);
        if (!t.empty() && t.find(needle) != std::string::npos) return w;
    }
    return 0;
}

/// One throwaway pixel the window manager can be asked to activate. Kept between the two
/// halves of a handover and destroyed by `activate_game_window`.
Window g_handover = 0;

/// A real server timestamp, obtained the standard way: a zero-length property append on our own
/// window, whose `PropertyNotify` carries the server's clock. KWin discards an activation
/// request carrying `CurrentTime` or a stale time, so this is not optional — but it is a round
/// trip on the main loop, so it is bounded and falls back rather than waiting.
Time server_time(Display* d, Window w) {
    XChangeProperty(d, w, XInternAtom(d, "_PPC_TIMESTAMP", False), XA_ATOM, 32, PropModeAppend,
                    nullptr, 0);
    XSync(d, False);
    for (int i = 0; i < 20; ++i) { // ~20ms; in practice the reply is already queued
        XEvent e;
        if (XCheckTypedWindowEvent(d, w, PropertyNotify, &e)) return e.xproperty.time;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return CurrentTime;
}

/// The EWMH "please activate this window" message.
///
/// `data.l[0]` is the source indication and it decides everything: **1 (application) is
/// refused** by KWin's focus-stealing prevention for any window that did not just map — the
/// activation out succeeds and the one back does not, which strands the user off the game. 2
/// (pager, i.e. a direct user action) is honoured both ways, measured at 10ms each. It is also
/// the truthful one: this only ever runs because the user pressed the price-check hotkey.
void ask_activate(Display* d, Window w, Time when) {
    XEvent e{};
    e.xclient.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = 2;
    e.xclient.data.l[1] = static_cast<long>(when);
    e.xclient.data.l[2] = 0;
    XSendEvent(d, DefaultRootWindow(d), False, SubstructureRedirectMask | SubstructureNotifyMask,
               &e);
    XFlush(d);
}

} // namespace

void platform_init() { XInitThreads(); }

bool foreground_title_contains(const std::string& needle) {
    Display* d = display();
    if (!d) return false;
    std::string t = window_title(d, active_window(d));
    return !t.empty() && t.find(needle) != std::string::npos;
}

std::string foreground_title() {
    Display* d = display();
    if (!d) return {};
    return window_title(d, active_window(d));
}

std::string focus_info() {
    Display* d = display();
    if (!d) return "no display";
    Window focus = None;
    int revert = 0;
    XGetInputFocus(d, &focus, &revert);
    const Window active = active_window(d);
    char buf[128];
    // The input focus is the server's own state and moves first; _NET_ACTIVE_WINDOW is the
    // WM's story about it and can lag by a frame or two. Both, so the order is readable.
    std::snprintf(buf, sizeof buf, "input=0x%lx active=0x%lx", focus, active);
    return std::string(buf) + " '" + window_title(d, active) + "'";
}

GameWindow find_game_window(const std::string& needle) {
    GameWindow g;
    Display* d = display();
    if (!d) return g;
    Window active = active_window(d);
    for (Window w : client_list(d)) {
        std::string t = window_title(d, w);
        if (t.empty() || t.find(needle) == std::string::npos) continue;
        if (!window_geometry(d, w, g.x, g.y, g.w, g.h)) continue;
        g.present = true;
        g.focused = (w == active);
        break;
    }
    return g;
}

void focus_game_window(const std::string& needle) {
    Display* d = display();
    if (!d) return;
    if (Window w = matching_window(d, needle)) {
        XSetInputFocus(d, w, RevertToParent, CurrentTime);
        // Sync, not flush: simulate_copy() injects on a *different* X connection, so
        // without a round trip the keystroke can beat the focus change to the server.
        XSync(d, False);
    }
}

bool deactivate_game_window() {
    Display* d = display();
    if (!d || g_handover) return false;
    // No property on the root means no EWMH window manager, and nothing to ask.
    if (XInternAtom(d, "_NET_ACTIVE_WINDOW", True) == None) return false;

    XSetWindowAttributes attrs{};
    attrs.override_redirect = False; // the WM must *manage* it, or it cannot be made active
    attrs.event_mask = PropertyChangeMask;
    g_handover = XCreateWindow(d, DefaultRootWindow(d), 0, 0, 1, 1, 0, CopyFromParent, InputOutput,
                               CopyFromParent, CWOverrideRedirect | CWEventMask, &attrs);
    XStoreName(d, g_handover, "PathOfPriceCheck clipboard handover");
    const Atom utility = XInternAtom(d, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    XChangeProperty(d, g_handover, XInternAtom(d, "_NET_WM_WINDOW_TYPE", False), XA_ATOM, 32,
                    PropModeReplace, reinterpret_cast<const unsigned char*>(&utility), 1);
    const Atom states[2] = {XInternAtom(d, "_NET_WM_STATE_SKIP_TASKBAR", False),
                            XInternAtom(d, "_NET_WM_STATE_SKIP_PAGER", False)};
    XChangeProperty(d, g_handover, XInternAtom(d, "_NET_WM_STATE", False), XA_ATOM, 32,
                    PropModeReplace, reinterpret_cast<const unsigned char*>(states), 2);
    // Undecorated, or the window manager frames one pixel with a titlebar and the handover is a
    // box flashing over the game. With this, KWin reparents it to a 1x1 frame at 0,0.
    const struct {
        unsigned long flags, functions, decorations;
        long input_mode;
        unsigned long status;
    } mwm{2, 0, 0, 0, 0}; // MWM_HINTS_DECORATIONS, none of them
    const Atom mwm_atom = XInternAtom(d, "_MOTIF_WM_HINTS", False);
    XChangeProperty(d, g_handover, mwm_atom, mwm_atom, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&mwm), 5);
    XMapWindow(d, g_handover);
    XSync(d, False);

    ask_activate(d, g_handover, server_time(d, g_handover));
    return true;
}

void activate_game_window(const std::string& needle) {
    Display* d = display();
    if (!d) return;
    if (Window w = matching_window(d, needle)) {
        // The timestamp comes off the handover window while it still exists — it is the only
        // window of ours on this connection. Without one there is nothing to take a server
        // clock from, and CurrentTime is what KWin refuses.
        ask_activate(d, w, g_handover ? server_time(d, g_handover) : CurrentTime);
        XSync(d, False); // the request must reach the WM before the window it names goes away
    }
    if (g_handover) {
        XDestroyWindow(d, g_handover);
        g_handover = 0;
        XFlush(d);
    }
}

} // namespace ppc
