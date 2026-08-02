#include "platform/input_sim.hpp"

#include <windows.h>

namespace ppc {

void simulate_copy() {
    INPUT in[4] = {};
    for (auto& e : in) e.type = INPUT_KEYBOARD;
    in[0].ki.wVk = VK_CONTROL;
    in[1].ki.wVk = 'C';
    in[2].ki.wVk = 'C';
    in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    in[3].ki.wVk = VK_CONTROL;
    in[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, in, sizeof(INPUT));
}

} // namespace ppc
