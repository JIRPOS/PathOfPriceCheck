#include "platform/clipboard.hpp"

#include <cerrno>
#include <chrono>
#include <climits>

#include <sys/select.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace ppc {
namespace {

using Clock = std::chrono::steady_clock;

struct Ctx {
    Display* d = nullptr;
    Window win = None; ///< requestor; selection data is tied to a window, so we need our own
    Atom clipboard = None, utf8 = None, plain_utf8 = None, incr = None, prop = None;
};

Ctx& ctx() {
    static Ctx c = [] {
        Ctx x;
        x.d = XOpenDisplay(nullptr); // main-thread only
        if (!x.d) return x;
        XSetWindowAttributes attr{};
        x.win = XCreateWindow(x.d, DefaultRootWindow(x.d), -10, -10, 1, 1, 0, CopyFromParent,
                              InputOnly, CopyFromParent, 0, &attr);
        XSelectInput(x.d, x.win, PropertyChangeMask); // INCR chunks arrive as PropertyNotify
        x.clipboard = XInternAtom(x.d, "CLIPBOARD", False);
        x.utf8 = XInternAtom(x.d, "UTF8_STRING", False);
        x.plain_utf8 = XInternAtom(x.d, "text/plain;charset=utf-8", False);
        x.incr = XInternAtom(x.d, "INCR", False);
        x.prop = XInternAtom(x.d, "PPC_CLIPBOARD", False);
        return x;
    }();
    return c;
}

/// Block for the next event of `type` on our window, or until the deadline. Events of other
/// types stay queued.
bool wait_for(Display* d, Window w, int type, XEvent* out, Clock::time_point deadline) {
    const int fd = ConnectionNumber(d);
    for (;;) {
        if (XCheckTypedWindowEvent(d, w, type, out)) return true;
        const auto left = deadline - Clock::now();
        if (left <= std::chrono::seconds(0)) return false;
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(left).count();
        fd_set r;
        FD_ZERO(&r);
        FD_SET(fd, &r);
        timeval tv{static_cast<time_t>(us / 1000000), static_cast<suseconds_t>(us % 1000000)};
        if (select(fd + 1, &r, nullptr, nullptr, &tv) < 0 && errno != EINTR) return false;
    }
}

std::string read_prop(Ctx& c, Atom* type_out) {
    Atom type = None;
    int fmt = 0;
    unsigned long count = 0, after = 0;
    unsigned char* data = nullptr;
    std::string s;
    if (XGetWindowProperty(c.d, c.win, c.prop, 0, INT_MAX / 4, False, AnyPropertyType, &type,
                           &fmt, &count, &after, &data) == Success) {
        if (data && fmt == 8) s.assign(reinterpret_cast<char*>(data), count);
        if (data) XFree(data);
    }
    if (type_out) *type_out = type;
    return s;
}

/// One selection→property round trip for a single target format.
std::string convert(Ctx& c, Atom target, Clock::time_point deadline) {
    XEvent ev;
    // A reply to a request we already gave up on would otherwise be read as this one's.
    while (XCheckTypedWindowEvent(c.d, c.win, SelectionNotify, &ev)) {}
    XDeleteProperty(c.d, c.win, c.prop);
    XConvertSelection(c.d, c.clipboard, target, c.prop, c.win, CurrentTime);
    XFlush(c.d);

    for (;;) {
        if (!wait_for(c.d, c.win, SelectionNotify, &ev, deadline)) return {};
        if (ev.xselection.selection == c.clipboard && ev.xselection.target == target) break;
    }
    if (ev.xselection.property == None) return {}; // owner can't supply this format

    Atom type = None;
    std::string s = read_prop(c, &type);
    if (type != c.incr) {
        XDeleteProperty(c.d, c.win, c.prop);
        return s;
    }

    // INCR: the value is streamed in chunks, each announced by a PropertyNotify once we
    // delete the property to acknowledge the previous one. A zero-length chunk ends it.
    // Item text is far below any server's maximum property size, so this is the cold path.
    std::string out;
    XDeleteProperty(c.d, c.win, c.prop);
    XFlush(c.d);
    for (;;) {
        XEvent pe;
        do {
            if (!wait_for(c.d, c.win, PropertyNotify, &pe, deadline)) return out;
        } while (pe.xproperty.atom != c.prop || pe.xproperty.state != PropertyNewValue);
        std::string chunk = read_prop(c, nullptr);
        XDeleteProperty(c.d, c.win, c.prop);
        XFlush(c.d);
        if (chunk.empty()) return out;
        out += chunk;
    }
}

} // namespace

std::string clipboard_text(int timeout_ms) {
    Ctx& c = ctx();
    if (!c.d) return {};
    if (XGetSelectionOwner(c.d, c.clipboard) == None) return {}; // nothing to ask
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    // STRING last: it's Latin-1, so it mangles anything non-ASCII. The UTF-8 targets are
    // what Wine and every modern toolkit actually offer.
    for (Atom target : {c.utf8, c.plain_utf8, static_cast<Atom>(XA_STRING)}) {
        std::string s = convert(c, target, deadline);
        if (!s.empty()) return s;
    }
    return {};
}

} // namespace ppc
