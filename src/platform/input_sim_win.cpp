#include "platform/input_sim.hpp"

#include <chrono>
#include <thread>
#include <vector>

#include <windows.h>

namespace ppc {
namespace {

using namespace std::chrono_literals;

constexpr auto kStep = 16ms;
constexpr auto kReleaseTimeout = 250ms;

const WORD kModVks[] = {VK_LCONTROL, VK_RCONTROL, VK_LSHIFT, VK_RSHIFT,
                        VK_LMENU,    VK_RMENU,    VK_LWIN,   VK_RWIN};

void key(WORD vk, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    // Carry the scan code too (without KEYEVENTF_SCANCODE, so wVk still drives it):
    // games reading lParam or raw input reject events with a zero scan code.
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    if (vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN)
        in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &in, sizeof(INPUT));
}

std::vector<WORD> held_modifiers() {
    std::vector<WORD> held;
    for (WORD vk : kModVks)
        if (GetAsyncKeyState(vk) & 0x8000) held.push_back(vk);
    return held;
}

} // namespace

void simulate_copy() {
    // The price-check hotkey itself holds modifiers down (Ctrl+D by default). Sending
    // Ctrl+C on top of them hands the game a polluted combo, and releasing our own Ctrl
    // afterwards would desync a Ctrl the user is still physically holding. Wait for a
    // clean keyboard instead — a hotkey tap clears in a few tens of ms.
    std::vector<WORD> held;
    for (auto waited = 0ms; waited < kReleaseTimeout; waited += kStep) {
        held = held_modifiers();
        if (held.empty()) break;
        std::this_thread::sleep_for(kStep);
    }
    // Still down (key repeat, or the user is leaning on it): release them ourselves. Not
    // pressed back afterwards — a modifier stuck *down* in the game is far worse than one
    // the game briefly thinks is up.
    for (WORD vk : held) key(vk, false);
    if (!held.empty()) std::this_thread::sleep_for(kStep);

    for (auto [vk, down] : {std::pair<WORD, bool>{VK_CONTROL, true},
                            {'C', true},
                            {'C', false},
                            {VK_CONTROL, false}}) {
        key(vk, down);
        std::this_thread::sleep_for(kStep);
    }
}

} // namespace ppc
