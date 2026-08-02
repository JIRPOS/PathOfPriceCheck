#include "platform/hotkeys.hpp"

#include <atomic>
#include <cctype>
#include <mutex>
#include <thread>

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

class X11Hotkeys final : public HotkeyListener {
public:
    explicit X11Hotkeys(std::function<void(Action)> cb) : cb_(std::move(cb)) {
        dpy_ = XOpenDisplay(nullptr);
        if (!dpy_) return;
        XSetErrorHandler(ignore_xerror);
        root_ = DefaultRootWindow(dpy_);
        self_ = XCreateSimpleWindow(dpy_, root_, 0, 0, 1, 1, 0, 0, 0);
        wakeup_ = XInternAtom(dpy_, "PPC_HOTKEY_WAKEUP", False);
        running_ = true;
        thread_ = std::thread([this] { run(); });
    }

    ~X11Hotkeys() override {
        if (!dpy_) return;
        running_ = false;
        send_wakeup(kQuit);
        if (thread_.joinable()) thread_.join();
        XDestroyWindow(dpy_, self_);
        XCloseDisplay(dpy_);
    }

    bool rebind(const std::vector<std::pair<Hotkey, Action>>& b) override {
        if (!dpy_) return false;
        {
            std::lock_guard lk(mu_);
            pending_ = b;
            have_pending_ = true;
        }
        send_wakeup(kRebind);
        return true;
    }

private:
    struct Grab { unsigned int keycode, mods; Action action; };
    enum WakeKind : long { kRebind = 1, kQuit = 2 };

    void send_wakeup(long kind) {
        XClientMessageEvent ev{};
        ev.type = ClientMessage;
        ev.window = self_;
        ev.message_type = wakeup_;
        ev.format = 32;
        ev.data.l[0] = kind;
        XSendEvent(dpy_, self_, False, 0, reinterpret_cast<XEvent*>(&ev));
        XFlush(dpy_);
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

    void run() {
        XEvent ev;
        while (running_) {
            XNextEvent(dpy_, &ev);
            if (ev.type == KeyPress) {
                unsigned int st = ev.xkey.state & kRelevantMods;
                for (auto& g : grabs_)
                    if (g.keycode == ev.xkey.keycode && g.mods == st) {
                        cb_(g.action);
                        break;
                    }
            } else if (ev.type == ClientMessage &&
                       static_cast<Atom>(ev.xclient.message_type) == wakeup_) {
                if (ev.xclient.data.l[0] == kQuit) break;
                apply_pending();
            }
        }
    }

    std::function<void(Action)> cb_;
    Display* dpy_ = nullptr;
    Window root_ = 0, self_ = 0;
    Atom wakeup_ = 0;
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
