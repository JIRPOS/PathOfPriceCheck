#pragma once

#include <string>

#include "platform/input.hpp"

namespace ppc {

struct Config {
    std::string league = "Standard";
    std::string account_name;                       // optional
    std::string poe_window_title = "Path of Exile"; // focus-detection match

    Hotkey price_check{Mod::Ctrl, "D"};
    Hotkey settings{Mod::Shift, "Space"};

    static std::string path(); ///< platform config-file path
    static Config load();      ///< load from disk, or defaults if absent/invalid
    bool save() const;
};

} // namespace ppc
