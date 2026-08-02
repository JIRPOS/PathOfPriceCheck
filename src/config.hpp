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

    // Price-check panel placement. The game's UI scales with window *height*, so the stash
    // and inventory frames sit at a fixed fraction of it regardless of aspect ratio. The two
    // are mirror images — tuned by hand they came out at 0.617 and 0.616 — but they stay
    // separate knobs because GGG moves these between leagues. Eyeballing them off a
    // screenshot is not accurate to the pixel; adjust in Settings against the live game.
    int panel_width = 460;         ///< price-check panel width, px
    float stash_edge = 0.615f;     ///< stash frame's right edge, ÷ game height
    float inventory_edge = 0.615f; ///< inventory frame's left edge from the right, ÷ game height

    static std::string path(); ///< platform config-file path
    static Config load();      ///< load from disk, or defaults if absent/invalid
    bool save() const;
};

} // namespace ppc
