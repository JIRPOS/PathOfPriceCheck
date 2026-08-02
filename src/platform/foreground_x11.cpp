#include "platform/foreground.hpp"
#include "platform/platform.hpp"

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

} // namespace

void platform_init() { XInitThreads(); }

bool foreground_title_contains(const std::string& needle) {
    Display* d = display();
    if (!d) return false;
    std::string t = window_title(d, active_window(d));
    return !t.empty() && t.find(needle) != std::string::npos;
}

} // namespace ppc
