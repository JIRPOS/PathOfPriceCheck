#include "platform/hotkeys.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <mutex>
#include <thread>

#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

namespace ppc {
namespace {

int ignore_xerror(Display*, XErrorEvent*) { return 0; }

unsigned int x11_modmask(Mod m) {
    unsigned int mask = 0;
    if (has(m, Mod::Ctrl)) mask |= ControlMask;
    if (has(m, Mod::Shift)) mask |= ShiftMask;
    if (has(m, Mod::Alt)) mask |= Mod1Mask;
    if (has(m, Mod::Super)) mask |= Mod4Mask;
    return mask;
}

KeySym keysym_from_name(const std::string& name) {
    if (name.size() == 1) {
        char c = name[0];
        if (std::isalpha(static_cast<unsigned char>(c))) {
            char lower = static_cast<char>(std::tolower(c));
            return XStringToKeysym(std::string(1, lower).c_str());
        }
        if (std::isdigit(static_cast<unsigned char>(c))) return XStringToKeysym(name.c_str());
    }
    static const std::pair<const char*, const char*> map[] = {
        {"Space", "space"}, {"Enter", "Return"}, {"Escape", "Escape"}, {"Tab", "Tab"},
        {"Backspace", "BackSpace"}, {"Delete", "Delete"}, {"Insert", "Insert"}, {"Home", "Home"},
        {"End", "End"}, {"PageUp", "Prior"}, {"PageDown", "Next"}, {"Up", "Up"}, {"Down", "Down"},
        {"Left", "Left"}, {"Right", "Right"},
    };
    for (auto& [k, v] : map)
        if (name == k) return XStringToKeysym(v);
    if (name.size() >= 2 && name[0] == 'F') return XStringToKeysym(name.c_str()); // F1..F12
    return NoSymbol;
}

// Grab each combo also with lock modifiers so it fires regardless of NumLock/CapsLock.
const unsigned int kLockMasks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
const unsigned int kRelevantMods = ShiftMask | ControlMask | Mod1Mask | Mod4Mask;

// The Display is owned exclusively by the internal thread. The main thread only
// pokes a self-pipe (never Xlib), which avoids the multi-threaded-Xlib abort.
class X11Hotkeys final : public HotkeyListener {
public:
    explicit X11Hotkeys(std::function<void(Action)> cb) : cb_(std::move(cb)) {
        dpy_ = XOpenDisplay(nullptr);
        if (!dpy_) return;
        XSetErrorHandler(ignore_xerror);
        // Without this a held key repeats as KeyRelease/KeyPress pairs, and wait_for_release
        // would mistake the first repeat for the user letting go.
        XkbSetDetectableAutoRepeat(dpy_, True, nullptr);
        root_ = DefaultRootWindow(dpy_);
        if (pipe(pipe_) != 0) return;
        fcntl(pipe_[0], F_SETFL, O_NONBLOCK);
        running_ = true;
        thread_ = std::thread([this] { run(); });
    }

    ~X11Hotkeys() override {
        if (!dpy_) return;
        running_ = false;
        poke();
        if (thread_.joinable()) thread_.join();
        if (pipe_[0] >= 0) close(pipe_[0]);
        if (pipe_[1] >= 0) close(pipe_[1]);
        XCloseDisplay(dpy_);
    }

    bool rebind(const std::vector<std::pair<Hotkey, Action>>& b) override {
        if (!dpy_) return false;
        {
            std::lock_guard lk(mu_);
            pending_ = b;
            have_pending_ = true;
        }
        poke();
        return true;
    }

private:
    struct Grab { unsigned int keycode, mods; Action action; };

    void poke() {
        char c = 1;
        ssize_t n = write(pipe_[1], &c, 1);
        (void)n;
    }

    void apply_pending() {
        std::vector<std::pair<Hotkey, Action>> b;
        {
            std::lock_guard lk(mu_);
            if (!have_pending_) return;
            b.swap(pending_);
            have_pending_ = false;
        }
        for (auto& g : grabs_)
            for (unsigned int lock : kLockMasks)
                XUngrabKey(dpy_, g.keycode, g.mods | lock, root_);
        grabs_.clear();
        for (auto& [hk, act] : b) {
            if (!hk.valid()) continue;
            KeySym ks = keysym_from_name(hk.key);
            if (ks == NoSymbol) continue;
            KeyCode kc = XKeysymToKeycode(dpy_, ks);
            if (!kc) continue;
            unsigned int mods = x11_modmask(hk.mods);
            for (unsigned int lock : kLockMasks)
                XGrabKey(dpy_, kc, mods | lock, root_, True, GrabModeAsync, GrabModeAsync);
            grabs_.push_back({kc, mods, act});
        }
        XFlush(dpy_);
    }

    /// Block until the hotkey's own key comes up. The grab is still active, so the release
    /// is guaranteed to reach this connection. Bounded only so a release we somehow never
    /// see can't wedge the thread.
    void wait_for_release(unsigned int keycode) {
        int xfd = ConnectionNumber(dpy_);
        for (int guard = 0; guard < 200; ++guard) { // ~2s
            while (XPending(dpy_)) {
                XEvent ev;
                XNextEvent(dpy_, &ev);
                if (ev.type == KeyRelease && ev.xkey.keycode == keycode) return;
            }
            fd_set r;
            FD_ZERO(&r);
            FD_SET(xfd, &r);
            timeval tv{0, 10000};
            if (select(xfd + 1, &r, nullptr, nullptr, &tv) < 0 && errno != EINTR) return;
        }
    }

    void dispatch(const XKeyEvent& k) {
        unsigned int st = k.state & kRelevantMods;
        for (auto& g : grabs_)
            if (g.keycode == k.keycode && g.mods == st) {
                // A passive XGrabKey *activates* into a real keyboard grab on press and
                // holds it until the keys come up. Two reasons to sit out the hold rather
                // than fire immediately: while the grab is active every keyboard event —
                // including XTest-injected ones — is redirected to us, so a synthetic Ctrl+C
                // would never reach the game; and the game must not be handed the hotkey's
                // own letter and C at the same time. The grab also swallows auto-repeat,
                // so leaning on the hotkey can't spam price checks.
                wait_for_release(k.keycode);
                // Released keys end the grab on their own; this covers the timeout path.
                // UngrabKeyboard also releases a grab that came from GrabKey, and must be
                // called on the connection that owns it — hence doing it here.
                XUngrabKeyboard(dpy_, CurrentTime);
                XFlush(dpy_);
                cb_(g.action);
                break;
            }
    }

    void run() {
        int xfd = ConnectionNumber(dpy_);
        while (running_) {
            while (XPending(dpy_)) {
                XEvent ev;
                XNextEvent(dpy_, &ev);
                if (ev.type == KeyPress) dispatch(ev.xkey);
            }
            fd_set r;
            FD_ZERO(&r);
            FD_SET(xfd, &r);
            FD_SET(pipe_[0], &r);
            int n = select(std::max(xfd, pipe_[0]) + 1, &r, nullptr, nullptr, nullptr);
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (FD_ISSET(pipe_[0], &r)) {
                char buf[16];
                while (read(pipe_[0], buf, sizeof buf) > 0) {} // drain
                if (!running_) break;
                apply_pending();
            }
        }
    }

    std::function<void(Action)> cb_;
    Display* dpy_ = nullptr;
    Window root_ = 0;
    int pipe_[2] = {-1, -1};
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mu_;
    std::vector<std::pair<Hotkey, Action>> pending_;
    bool have_pending_ = false;
    std::vector<Grab> grabs_;
};

} // namespace

std::unique_ptr<HotkeyListener> HotkeyListener::create(std::function<void(Action)> cb) {
    return std::make_unique<X11Hotkeys>(std::move(cb));
}

} // namespace ppc
