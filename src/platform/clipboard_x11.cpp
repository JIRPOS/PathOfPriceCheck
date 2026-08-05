#include "platform/clipboard.hpp"

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>

#include <sys/select.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

namespace ppc {
namespace {

using Clock = std::chrono::steady_clock;

/// PPC_DEBUG_COPY=1 also traces the selection side of the copy: who owns the clipboard and
/// what each target conversion actually did. Which of "the game never copied" and "the game
/// copied but we can't read it" happened is otherwise invisible from the app side.
bool trace() {
    static bool on = std::getenv("PPC_DEBUG_COPY") != nullptr;
    return on;
}

struct Ctx {
    Display* d = nullptr;
    Window win = None; ///< requestor; selection data is tied to a window, so we need our own
    Atom clipboard = None, utf8 = None, plain_utf8 = None, incr = None, prop = None,
         targets = None;
    int xfixes_event = 0;      ///< 0 when the extension is missing; see clipboard_changed()
    bool owner_changed = false; ///< latched XFixesSetSelectionOwnerNotify
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
        x.targets = XInternAtom(x.d, "TARGETS", False);
        int evt = 0, err = 0;
        if (XFixesQueryExtension(x.d, &evt, &err)) {
            x.xfixes_event = evt + XFixesSelectionNotify;
            XFixesSelectSelectionInput(x.d, x.win, x.clipboard,
                                       XFixesSetSelectionOwnerNotifyMask);
        }
        return x;
    }();
    return c;
}

/// Drain queued owner-change notifications into the latch. Also called from `clipboard_text`
/// so the queue can't grow behind a caller that only ever reads.
void pump_owner_changes(Ctx& c) {
    if (!c.xfixes_event) return;
    XEvent ev;
    while (XCheckTypedWindowEvent(c.d, c.win, c.xfixes_event, &ev)) {
        auto* fe = reinterpret_cast<XFixesSelectionNotifyEvent*>(&ev);
        if (fe->subtype == XFixesSetSelectionOwnerNotify && fe->selection == c.clipboard) {
            c.owner_changed = true;
            if (trace())
                std::fprintf(stderr, "[copy]   selection owner asserted: 0x%lx\n", fe->owner);
        }
    }
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

/// Atom name for tracing only; the round trip is not worth it on the hot path.
std::string atom_name(Ctx& c, Atom a) {
    char* n = a ? XGetAtomName(c.d, a) : nullptr;
    std::string s = n ? n : "?";
    if (n) XFree(n);
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
        if (!wait_for(c.d, c.win, SelectionNotify, &ev, deadline)) {
            if (trace())
                std::fprintf(stderr, "[copy]   %s: no reply before the deadline\n",
                             atom_name(c, target).c_str());
            return {};
        }
        if (ev.xselection.selection == c.clipboard && ev.xselection.target == target) break;
    }
    if (ev.xselection.property == None) { // owner can't supply this format
        if (trace())
            std::fprintf(stderr, "[copy]   %s: owner refused (format not offered)\n",
                         atom_name(c, target).c_str());
        return {};
    }

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

/// Trace-only: what the owner says it can supply right now. Wine publishes CF_UNICODETEXT
/// and CF_TEXT as different targets and does not necessarily have both rendered, so an empty
/// read with a live owner is a different failure from an owner that answers nothing at all.
void trace_targets(Ctx& c) {
    XEvent ev;
    while (XCheckTypedWindowEvent(c.d, c.win, SelectionNotify, &ev)) {}
    XDeleteProperty(c.d, c.win, c.prop);
    XConvertSelection(c.d, c.clipboard, c.targets, c.prop, c.win, CurrentTime);
    XFlush(c.d);
    const auto deadline = Clock::now() + std::chrono::milliseconds(100);
    for (;;) {
        if (!wait_for(c.d, c.win, SelectionNotify, &ev, deadline)) {
            std::fprintf(stderr, "[copy]   TARGETS: no reply\n");
            return;
        }
        if (ev.xselection.selection == c.clipboard && ev.xselection.target == c.targets) break;
    }
    if (ev.xselection.property == None) {
        std::fprintf(stderr, "[copy]   TARGETS: refused\n");
        return;
    }
    Atom type = None;
    int fmt = 0;
    unsigned long count = 0, after = 0;
    unsigned char* data = nullptr;
    std::string line;
    if (XGetWindowProperty(c.d, c.win, c.prop, 0, 256, False, XA_ATOM, &type, &fmt, &count,
                           &after, &data) == Success &&
        data && fmt == 32) {
        Atom* list = reinterpret_cast<Atom*>(data);
        for (unsigned long i = 0; i < count; ++i) line += " " + atom_name(c, list[i]);
    }
    if (data) XFree(data);
    XDeleteProperty(c.d, c.win, c.prop);
    std::fprintf(stderr, "[copy]   TARGETS:%s\n", line.empty() ? " (none)" : line.c_str());
}

} // namespace

bool clipboard_changed() {
    Ctx& c = ctx();
    if (!c.d) return false;
    pump_owner_changes(c);
    // No XFixes: report no change rather than a change on every call. Callers use this to
    // vouch for text they would otherwise reject, and a permanent "yes" would vouch for
    // everything, including the stale clipboard a failed copy leaves behind.
    bool changed = c.owner_changed;
    c.owner_changed = false;
    return changed;
}

std::string clipboard_text(int timeout_ms) {
    Ctx& c = ctx();
    if (!c.d) return {};
    pump_owner_changes(c);
    const Window owner = XGetSelectionOwner(c.d, c.clipboard);
    if (trace()) {
        // The one fact that separates "the game never copied" from "the game copied but we
        // can't read it": a copy the game actually performed re-asserts the selection.
        static Window last_owner = None;
        static bool seen = false;
        if (!seen || owner != last_owner)
            std::fprintf(stderr, "[copy]   clipboard owner 0x%lx -> 0x%lx\n", last_owner, owner);
        last_owner = owner;
        seen = true;
    }
    if (owner == None) return {}; // nothing to ask
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    // STRING last: it's Latin-1, so it mangles anything non-ASCII. The UTF-8 targets are
    // what Wine and every modern toolkit actually offer.
    for (Atom target : {c.utf8, c.plain_utf8, static_cast<Atom>(XA_STRING)}) {
        std::string s = convert(c, target, deadline);
        if (!s.empty()) return s;
    }
    if (trace()) trace_targets(c);
    return {};
}

} // namespace ppc
