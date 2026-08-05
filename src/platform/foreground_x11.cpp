#include "platform/foreground.hpp"
#include "platform/platform.hpp"

#include <cstdio>
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

} // namespace ppc
