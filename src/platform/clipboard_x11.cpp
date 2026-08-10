#include "platform/clipboard.hpp"

#include <cerrno>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>

#include "util/debug_log.hpp"

namespace ppc {
namespace {

using Clock = std::chrono::steady_clock;

/// The debug log (or PPC_DEBUG_COPY=1) traces the selection side of the copy: who owns the
/// clipboard and what each target conversion actually did. Which of "the game never copied"
/// and "the game copied but we can't read it" happened is otherwise invisible from the app side.
bool trace() { return debug::tracing(); }

struct Ctx {
    Display* d = nullptr;
    Window win = None; ///< requestor; selection data is tied to a window, so we need our own
    Atom clipboard = None, utf8 = None, plain_utf8 = None, incr = None, prop = None,
         targets = None;
    int xfixes_event = 0;   ///< 0 when the extension is missing; see clipboard_stamp()
    uint32_t changes = 0;   ///< ownership changes seen, so two in one millisecond differ
    unsigned long last_change = 0; ///< selection_timestamp of the newest one
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

/// Who a window belongs to, from server-side properties only — no round trip to the client,
/// so this is safe to call about an owner we are in the middle of waiting on.
///
/// **Expect all of it to be missing.** The window that owns the selection during the sticky
/// wedge is KWin's own, which advertises neither `WM_CLASS` nor `_NET_WM_PID`; Wine's clipboard
/// window advertises no more. So this identifies the owner on a good day and is not something to
/// build a decision on — see `clipboard_wedge_note` in `App`, which deliberately concludes from
/// the owner's *behaviour* instead.
std::string window_desc(Display* d, Window w) {
    if (!w) return "none";
    char buf[64];
    std::snprintf(buf, sizeof buf, "0x%lx", w);
    std::string s = buf;
    XClassHint ch{};
    if (XGetClassHint(d, w, &ch)) {
        if (ch.res_class) s += std::string(" class=") + ch.res_class;
        if (ch.res_name) XFree(ch.res_name);
        if (ch.res_class) XFree(ch.res_class);
    }
    Atom pid_atom = XInternAtom(d, "_NET_WM_PID", True);
    Atom type = None;
    int fmt = 0;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    if (pid_atom != None &&
        XGetWindowProperty(d, w, pid_atom, 0, 1, False, XA_CARDINAL, &type, &fmt, &n, &after,
                           &data) == Success &&
        data) {
        if (fmt == 32 && n) s += " pid=" + std::to_string(*reinterpret_cast<unsigned long*>(data));
        XFree(data);
    }
    return s;
}

/// Drain queued owner-change notifications into the stamp. Also called from `clipboard_text`
/// so the queue can't grow behind a caller that only ever reads.
void pump_owner_changes(Ctx& c) {
    if (!c.xfixes_event) return;
    XEvent ev;
    while (XCheckTypedWindowEvent(c.d, c.win, c.xfixes_event, &ev)) {
        auto* fe = reinterpret_cast<XFixesSelectionNotifyEvent*>(&ev);
        if (fe->subtype == XFixesSetSelectionOwnerNotify && fe->selection == c.clipboard) {
            if (trace())
                debug::trace("[copy]   selection owner asserted: %s at t=%lu",
                             window_desc(c.d, fe->owner).c_str(), fe->selection_timestamp);
            // The selection being *dropped* is not a write. It is the opposite: there is now
            // nothing on the clipboard and nobody to ask. Counting it moved the stamp, which
            // read as a successful copy and then logged the empty read as "not an item".
            if (fe->owner == None) continue;
            ++c.changes;
            c.last_change = fe->selection_timestamp;
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
std::string atom_name(Display* d, Atom a) {
    char* n = a ? XGetAtomName(d, a) : nullptr;
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
                debug::trace("[copy]   %s: no reply before the deadline",
                             atom_name(c.d, target).c_str());
            return {};
        }
        if (ev.xselection.selection == c.clipboard && ev.xselection.target == target) break;
    }
    if (ev.xselection.property == None) { // owner can't supply this format
        if (trace())
            debug::trace("[copy]   %s: owner refused (format not offered)",
                         atom_name(c.d, target).c_str());
        return {};
    }

    Atom type = None;
    std::string s = read_prop(c, &type);
    if (type != c.incr) {
        XDeleteProperty(c.d, c.win, c.prop);
        if (trace())
            debug::trace("[copy]   %s: %zu bytes", atom_name(c.d, target).c_str(), s.size());
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

/// What the owner says it can supply right now. Wine publishes CF_UNICODETEXT and CF_TEXT as
/// different targets and does not necessarily have both rendered, so an empty read with a live
/// owner is a different failure from an owner that answers nothing at all. This is a real
/// conversion request, hence diagnostics-only: see the header.
std::string targets_list(Ctx& c, int timeout_ms) {
    XEvent ev;
    while (XCheckTypedWindowEvent(c.d, c.win, SelectionNotify, &ev)) {}
    XDeleteProperty(c.d, c.win, c.prop);
    XConvertSelection(c.d, c.clipboard, c.targets, c.prop, c.win, CurrentTime);
    XFlush(c.d);
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        if (!wait_for(c.d, c.win, SelectionNotify, &ev, deadline)) return "(no reply)";
        if (ev.xselection.selection == c.clipboard && ev.xselection.target == c.targets) break;
    }
    if (ev.xselection.property == None) return "(refused)";
    Atom type = None;
    int fmt = 0;
    unsigned long count = 0, after = 0;
    unsigned char* data = nullptr;
    std::string line;
    if (XGetWindowProperty(c.d, c.win, c.prop, 0, 256, False, XA_ATOM, &type, &fmt, &count,
                           &after, &data) == Success &&
        data && fmt == 32) {
        Atom* list = reinterpret_cast<Atom*>(data);
        for (unsigned long i = 0; i < count; ++i) line += (line.empty() ? "" : " ") +
                                                         atom_name(c.d, list[i]);
    }
    if (data) XFree(data);
    XDeleteProperty(c.d, c.win, c.prop);
    return line.empty() ? "(none)" : line;
}

int ignore_xerror(Display*, XErrorEvent*) { return 0; }

/// The write half: a window that owns the CLIPBOARD selection and answers for it.
///
/// X11 has no clipboard to put something *in*. A selection is a live window that serves
/// `SelectionRequest` on demand, so a write is a promise to still be there when the paste
/// happens — which here is Wine asking, after the popup that made the write has closed and the
/// user has clicked into a chat box. Hence a thread of its own with its own `Display`, started
/// on the first write and never stopped: **the Display is touched only by that thread** (the
/// same rule the hotkey listener follows, and for the same abort), and the main thread hands
/// text over under the mutex and pokes a self-pipe.
///
/// The text goes with the process, as an unowned selection does everywhere on X11 — a clipboard
/// manager that wants to outlive us will have taken a copy of its own.
class SelectionOwner {
public:
    /// Blocks until the selection is actually ours, and **that is not a nicety**. The caller
    /// hands the keyboard focus straight back to the game afterwards, and Wine re-reads the X
    /// selection around a focus change: posting the text and returning meant the focus went back
    /// while Wine still owned the selection, so the first paste was of the *previous* clipboard
    /// and only the next one — after Wine had noticed us — came out right. Reported from the
    /// game, and the whole reason this waits.
    ///
    /// The wait is a handful of milliseconds (two round trips on our own connection) against a
    /// bound that only exists so a wedged server cannot hold the main loop.
    bool set(const std::string& text) {
        if (!start()) return false;
        uint64_t want = 0;
        {
            std::lock_guard lk(mu_);
            pending_ = text;
            want = ++requested_;
        }
        poke();
        std::unique_lock lk(mu_);
        const bool answered = done_.wait_for(lk, std::chrono::milliseconds(kTakeTimeoutMs),
                                             [&] { return taken_ >= want; });
        return answered && owned_;
    }

private:
    /// How long `set` waits for the thread to assert ownership. Generous: the work behind it is
    /// two round trips, and anything near this bound is a server that has stopped answering.
    static constexpr int kTakeTimeoutMs = 250;

    bool start() {
        if (started_) return d_ != nullptr;
        started_ = true;
        d_ = XOpenDisplay(nullptr);
        if (!d_) return false;
        if (pipe(pipe_) != 0) {
            XCloseDisplay(d_);
            d_ = nullptr;
            return false;
        }
        fcntl(pipe_[0], F_SETFL, O_NONBLOCK);
        // A requestor that exits between asking for the selection and being answered leaves us
        // writing a property on a window that is gone — a `BadWindow` whose *default* handler
        // exits the process. The hotkey listener installs the same handler, and it is global
        // rather than per-display, so this is belt and braces; it is here because losing the
        // application to somebody else's Ctrl+V is not a failure to inherit by accident.
        XSetErrorHandler(ignore_xerror);
        XSetWindowAttributes attr{};
        w_ = XCreateWindow(d_, DefaultRootWindow(d_), -10, -10, 1, 1, 0, CopyFromParent, InputOnly,
                           CopyFromParent, 0, &attr);
        // PropertyChange for the timestamp trick below; the selection events themselves are
        // sent to the owner whether or not anything is selected for.
        XSelectInput(d_, w_, PropertyChangeMask);
        clipboard_ = XInternAtom(d_, "CLIPBOARD", False);
        utf8_ = XInternAtom(d_, "UTF8_STRING", False);
        plain_utf8_ = XInternAtom(d_, "text/plain;charset=utf-8", False);
        plain_ = XInternAtom(d_, "text/plain", False);
        targets_ = XInternAtom(d_, "TARGETS", False);
        timestamp_ = XInternAtom(d_, "TIMESTAMP", False);
        stamp_prop_ = XInternAtom(d_, "PPC_OWNER_TIME", False);
        thread_ = std::thread([this] { run(); });
        return true;
    }

    void poke() {
        char c = 1;
        ssize_t n = write(pipe_[1], &c, 1);
        (void)n;
    }

    /// A real server timestamp, from a zero-length property append on our own window. ICCCM
    /// says an owner must be able to answer TIMESTAMP with the time it took the selection at,
    /// and `CurrentTime` leaves it with nothing true to say.
    Time server_time() {
        XChangeProperty(d_, w_, stamp_prop_, XA_ATOM, 32, PropModeAppend, nullptr, 0);
        XFlush(d_);
        XEvent e;
        if (wait_for(d_, w_, PropertyNotify, &e,
                     Clock::now() + std::chrono::milliseconds(kTakeTimeoutMs / 2)))
            return e.xproperty.time;
        return CurrentTime;
    }

    void take_pending() {
        uint64_t seq = 0;
        {
            std::lock_guard lk(mu_);
            if (requested_ == taken_) return;
            served_ = std::move(pending_);
            seq = requested_;
        }
        const auto t0 = Clock::now();
        owned_since_ = server_time();
        XSetSelectionOwner(d_, clipboard_, w_, owned_since_);
        XFlush(d_);
        // Asked rather than assumed, and it is what `set` answers with: taking a selection can
        // fail, and a paste of somebody else's clipboard is worth knowing about in the log.
        const bool ok = XGetSelectionOwner(d_, clipboard_) == w_;
        debug::log("[paste]  put %zu bytes on the clipboard in %lldms (owner taken: %d)",
                   served_.size(),
                   (long long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                                                    t0)
                       .count(),
                   (int)ok);
        {
            std::lock_guard lk(mu_);
            taken_ = seq;
            owned_ = ok;
        }
        done_.notify_all();
    }

    void serve(const XSelectionRequestEvent& req) {
        XSelectionEvent note{};
        note.type = SelectionNotify;
        note.requestor = req.requestor;
        note.selection = req.selection;
        note.target = req.target;
        note.time = req.time;
        // An obsolete requestor sends None and means "put it where the target says".
        const Atom prop = req.property == None ? req.target : req.property;
        note.property = prop;

        if (req.target == targets_) {
            const Atom list[] = {targets_, timestamp_, utf8_, plain_utf8_, plain_, XA_STRING};
            XChangeProperty(d_, req.requestor, prop, XA_ATOM, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(list),
                            static_cast<int>(std::size(list)));
        } else if (req.target == timestamp_) {
            const long t = static_cast<long>(owned_since_);
            XChangeProperty(d_, req.requestor, prop, XA_INTEGER, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(&t), 1);
        } else if (req.target == utf8_ || req.target == plain_utf8_ || req.target == plain_ ||
                   req.target == XA_STRING) {
            // STRING is nominally Latin-1 and this is UTF-8. Served anyway: every requestor
            // that can read one asks for UTF8_STRING first, and refusing STRING outright
            // leaves the ones that only know it with nothing at all.
            XChangeProperty(d_, req.requestor, prop, req.target, 8, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(served_.data()),
                            static_cast<int>(served_.size()));
        } else {
            note.property = None; // we cannot supply that format
        }
        XSendEvent(d_, req.requestor, False, 0, reinterpret_cast<XEvent*>(&note));
        XFlush(d_);
        if (trace())
            debug::trace("[paste]   served %s to 0x%lx: %s", atom_name(d_, req.target).c_str(),
                         req.requestor, note.property == None ? "refused" : "ok");
    }

    void run() {
        const int xfd = ConnectionNumber(d_);
        for (;;) {
            while (XPending(d_)) {
                XEvent ev;
                XNextEvent(d_, &ev);
                if (ev.type == SelectionRequest) {
                    serve(ev.xselectionrequest);
                } else if (ev.type == SelectionClear && ev.xselectionclear.selection == clipboard_) {
                    // Somebody else copied. Drop the text rather than keep serving it: we are
                    // not the owner any more and the next request is not ours to answer.
                    served_.clear();
                    debug::log("[paste]  clipboard taken over by another window");
                }
            }
            fd_set r;
            FD_ZERO(&r);
            FD_SET(xfd, &r);
            FD_SET(pipe_[0], &r);
            const int n = select(std::max(xfd, pipe_[0]) + 1, &r, nullptr, nullptr, nullptr);
            if (n < 0) {
                if (errno == EINTR) continue;
                return;
            }
            if (FD_ISSET(pipe_[0], &r)) {
                char buf[16];
                while (read(pipe_[0], buf, sizeof buf) > 0) {} // drain
                take_pending();
            }
        }
    }

    Display* d_ = nullptr;
    Window w_ = None;
    Atom clipboard_ = None, utf8_ = None, plain_utf8_ = None, plain_ = None, targets_ = None,
         timestamp_ = None, stamp_prop_ = None;
    Time owned_since_ = CurrentTime;
    std::string served_; ///< thread-only: what requests are answered with
    int pipe_[2] = {-1, -1};
    std::thread thread_;
    bool started_ = false;

    std::mutex mu_;
    std::condition_variable done_;
    std::string pending_;   ///< handed to the thread; `requested_` says whether it is new
    uint64_t requested_ = 0; ///< writes asked for, and `taken_` the ones that reached the server
    uint64_t taken_ = 0;
    bool owned_ = false;    ///< the last write actually got the selection
};

/// Leaked on purpose, and never joined: the selection has to stay answerable for as long as the
/// process can be pasted from, and a static whose destructor joins an Xlib thread at exit is a
/// deadlock waiting for a release build to find.
SelectionOwner& owner() {
    static SelectionOwner* o = new SelectionOwner();
    return *o;
}

} // namespace

bool clipboard_set_text(const std::string& text) {
    if (text.size() > kMaxClipboardWrite) {
        debug::log("[paste]  refused %zu bytes: past the %zu-byte ceiling a single property can"
                   " be relied on for",
                   text.size(), kMaxClipboardWrite);
        return false;
    }
    return owner().set(text);
}

void clipboard_poke() {
    Ctx& c = ctx();
    if (!c.d) return;
    if (XGetSelectionOwner(c.d, c.clipboard) == None) return;
    // TARGETS rather than a text format: we want the owner to wake up, not to hand us the
    // previous copy. The reply is never read — it is drained by the next request on this window.
    XEvent ev;
    while (XCheckTypedWindowEvent(c.d, c.win, SelectionNotify, &ev)) {}
    XConvertSelection(c.d, c.clipboard, c.targets, c.prop, c.win, CurrentTime);
    XFlush(c.d);
}

std::string clipboard_owner_info() {
    Ctx& c = ctx();
    if (!c.d) return "no display";
    std::string s = window_desc(c.d, XGetSelectionOwner(c.d, c.clipboard));
    if (!c.xfixes_event) s += " (no XFixes: writes are undetectable)";
    return s;
}

std::string clipboard_targets(int timeout_ms) {
    Ctx& c = ctx();
    if (!c.d) return "(no display)";
    if (XGetSelectionOwner(c.d, c.clipboard) == None) return "(no owner)";
    return targets_list(c, timeout_ms);
}

uint64_t clipboard_stamp() {
    Ctx& c = ctx();
    if (!c.d) return 0;
    pump_owner_changes(c);
    // Without XFixes nothing is ever observed, so the stamp never moves and every copy times
    // out. That is the honest failure: a stamp that changed on its own would vouch for the
    // stale text a failed copy leaves behind, which is a confident wrong price.
    return (static_cast<uint64_t>(c.changes) << 32) | c.last_change;
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
            debug::trace("[copy]   clipboard owner 0x%lx -> %s", last_owner,
                         window_desc(c.d, owner).c_str());
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
    if (trace()) debug::trace("[copy]   TARGETS: %s", targets_list(c, 100).c_str());
    return {};
}

} // namespace ppc
