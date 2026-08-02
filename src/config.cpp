#include "config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ppc {

static fs::path config_dir() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    fs::path base = appdata ? fs::path(appdata) : fs::path(".");
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    fs::path base;
    if (xdg && *xdg) {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        base = fs::path(home ? home : ".") / ".config";
    }
#endif
    return base / "PathOfPriceCheck";
}

std::string Config::path() { return (config_dir() / "config.json").string(); }

Config Config::load() {
    Config c;
    std::ifstream in(path());
    if (!in) return c;
    json j;
    try {
        in >> j;
    } catch (...) {
        return c; // keep defaults on malformed config
    }
    c.league = j.value("league", c.league);
    c.account_name = j.value("account_name", c.account_name);
    c.poe_window_title = j.value("poe_window_title", c.poe_window_title);
    if (j.contains("hotkeys")) {
        const auto& h = j["hotkeys"];
        if (h.contains("price_check")) c.price_check = parse_hotkey(h["price_check"].get<std::string>());
        if (h.contains("settings")) c.settings = parse_hotkey(h["settings"].get<std::string>());
    }
    c.panel_width = j.value("panel_width", c.panel_width);
    c.stash_edge = j.value("stash_edge", c.stash_edge);
    c.inventory_edge = j.value("inventory_edge", c.inventory_edge);
    return c;
}

bool Config::save() const {
    std::error_code ec;
    fs::create_directories(config_dir(), ec);
    json j;
    j["league"] = league;
    j["account_name"] = account_name;
    j["poe_window_title"] = poe_window_title;
    j["hotkeys"]["price_check"] = to_string(price_check);
    j["hotkeys"]["settings"] = to_string(settings);
    j["panel_width"] = panel_width;
    j["stash_edge"] = stash_edge;
    j["inventory_edge"] = inventory_edge;
    std::ofstream out(path());
    if (!out) return false;
    out << j.dump(2) << "\n";
    return true;
}

} // namespace ppc
