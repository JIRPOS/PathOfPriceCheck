#include "platform/input.hpp"

#include <array>
#include <utility>

namespace ppc {

static const std::array<std::pair<Mod, const char*>, 4> kMods = {{
    {Mod::Ctrl, "Ctrl"}, {Mod::Shift, "Shift"}, {Mod::Alt, "Alt"}, {Mod::Super, "Super"},
}};

std::string to_string(const Hotkey& h) {
    std::string s;
    for (auto& [m, n] : kMods)
        if (has(h.mods, m)) { s += n; s += '+'; }
    s += h.key;
    return s;
}

Hotkey parse_hotkey(const std::string& s) {
    Hotkey h;
    size_t start = 0;
    while (true) {
        size_t plus = s.find('+', start);
        std::string tok = s.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        if (plus == std::string::npos) { h.key = tok; break; }
        for (auto& [m, n] : kMods)
            if (tok == n) h.mods = h.mods | m;
        start = plus + 1;
    }
    return h;
}

} // namespace ppc
