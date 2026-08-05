#include "platform/input_sim.hpp"

#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include "util/debug_log.hpp"

namespace ppc {
namespace {

using namespace std::chrono_literals;

constexpr auto kPoll = 16ms;            // keyboard-state poll interval while waiting
constexpr auto kReleaseTimeout = 400ms; // how long to wait for the hotkey to be let go

/// A 16ms tap is invisible to a game that samples the keyboard once per frame, and a
/// chord assembled inside one frame can be seen as a bare letter. These are the shape of
/// a human keypress instead; tunable because the threshold is the game's, not ours.
int env_ms(const char* name, int fallback) {
    const char* v = std::getenv(name);
    int n = v ? std::atoi(v) : 0;
    return n > 0 ? n : fallback;
}
std::chrono::milliseconds gap() { return std::chrono::milliseconds(env_ms("PPC_COPY_GAP_MS", 40)); }
std::chrono::milliseconds hold() { return std::chrono::milliseconds(env_ms("PPC_COPY_HOLD_MS", 60)); }

bool trace() { return debug::tracing(); }

Display* display() {
    static Display* d = XOpenDisplay(nullptr); // main-thread only
    return d;
}

void key(Display* d, KeyCode kc, bool down) {
    if (kc) XTestFakeKeyEvent(d, kc, down, CurrentTime);
}

/// Keycodes bound to the given modifier map indices. Read from the server rather than
/// hardcoded keysyms so remapped layouts still work. Lock/NumLock are excluded —
/// they're latched, not held, and don't interfere with Ctrl+C.
std::vector<KeyCode> modifier_keycodes(Display* d, std::initializer_list<int> indices) {
    std::vector<KeyCode> out;
    XModifierKeymap* mm = XGetModifierMapping(d);
    if (!mm) return out;
    for (int idx : indices)
        for (int i = 0; i < mm->max_keypermod; ++i)
            if (KeyCode kc = mm->modifiermap[idx * mm->max_keypermod + i]) out.push_back(kc);
    XFreeModifiermap(mm);
    return out;
}

std::vector<KeyCode> held_keys(Display* d, const std::vector<KeyCode>& candidates) {
    char state[32];
    XQueryKeymap(d, state);
    std::vector<KeyCode> held;
    for (KeyCode kc : candidates)
        if (state[kc / 8] & (1 << (kc % 8))) held.push_back(kc);
    return held;
}

/// Where the keystroke will actually land. Under a compositor the X input focus is not
/// necessarily the window the user thinks is active, so it's the first thing to check
/// when an injected chord seems to vanish.
void log_focus(Display* d) {
    Window w = None;
    int revert = 0;
    XGetInputFocus(d, &w, &revert);
    XClassHint ch{};
    const char* cls = "?";
    if (w > 1 && XGetClassHint(d, w, &ch) && ch.res_class) cls = ch.res_class;
    debug::trace("[copy]   x input focus = 0x%lx (%s), revert=%d", w, cls, revert);
    if (ch.res_name) XFree(ch.res_name);
    if (ch.res_class) XFree(ch.res_class);
}

} // namespace

void simulate_copy() {
    Display* d = display();
    if (!d) return;

    const KeyCode c = XKeysymToKeycode(d, XK_c);
    if (!c) return;
    const std::vector<KeyCode> ctrl_keys = modifier_keycodes(d, {ControlMapIndex});
    const std::vector<KeyCode> other_mods =
        modifier_keycodes(d, {ShiftMapIndex, Mod1MapIndex, Mod4MapIndex});

    // Shift/Alt/Super held over the injection turn Ctrl+C into a different chord. The
    // hotkey listener already waited out the hotkey's own key, so this only catches a
    // modifier the user is still leaning on. Ctrl is exempt — see below.
    std::vector<KeyCode> stuck;
    for (auto waited = 0ms; waited < kReleaseTimeout; waited += kPoll) {
        stuck = held_keys(d, other_mods);
        if (stuck.empty()) break;
        std::this_thread::sleep_for(kPoll);
    }
    // Modifiers still down (key repeat, or the user is leaning on it): release them
    // ourselves. We deliberately don't press them back — the server can't tell us whether
    // they're still physically held, and a modifier stuck *down* in the game is far worse.
    for (KeyCode kc : stuck) key(d, kc, false);
    if (!stuck.empty()) {
        XSync(d, False);
        std::this_thread::sleep_for(gap());
    }

    // Ctrl is different: the price-check hotkey *is* Ctrl+D, so the user is probably holding
    // it. Never press a Ctrl we don't own. A fake press is only cancelled by our own fake
    // release, and if the user lets go between the keymap read and that release the server
    // holds Ctrl down with no physical key left to clear it — every shortcut on the desktop
    // silently breaks (Alt+Tab, Super, our own hotkeys) and Wine only re-syncs on focus
    // change. So: if Ctrl is already down, send the bare C and ride the user's modifier;
    // otherwise press our own and release it. `our_ctrl` is set only where we pressed.
    KeyCode our_ctrl = 0;
    const bool user_ctrl = !held_keys(d, ctrl_keys).empty();
    if (!user_ctrl) {
        our_ctrl = XKeysymToKeycode(d, XK_Control_L);
        if (!our_ctrl) return;
        key(d, our_ctrl, true);
        XSync(d, False);
    }
    // Step the events apart: batched into a single server timestamp, Wine can collapse or
    // reorder the press/release pair and the game never sees the chord.
    std::this_thread::sleep_for(gap());
    // Riding the user's Ctrl means they can drop it under us and hand the game a bare C
    // (which opens the character screen). Re-check now that the gap has elapsed and take
    // over if they have — this can't wedge anything, since taking over means we own the
    // release too.
    if (user_ctrl && held_keys(d, ctrl_keys).empty()) {
        our_ctrl = XKeysymToKeycode(d, XK_Control_L);
        if (our_ctrl) {
            key(d, our_ctrl, true);
            XSync(d, False);
            std::this_thread::sleep_for(gap());
        }
    }

    if (trace()) {
        debug::trace("[copy]   inject c=0x%x, ctrl=%s (user held it: %d), forced-release %zu "
                     "mods, gap=%lldms hold=%lldms",
                     c, our_ctrl ? "ours" : "user's", (int)user_ctrl, stuck.size(),
                     (long long)gap().count(), (long long)hold().count());
        log_focus(d);
    }

    key(d, c, true);
    XSync(d, False);
    std::this_thread::sleep_for(hold());
    key(d, c, false);
    XSync(d, False);
    if (our_ctrl) { // exactly the presses we made, never the user's
        std::this_thread::sleep_for(gap());
        key(d, our_ctrl, false);
        XSync(d, False);
    }
}

} // namespace ppc
