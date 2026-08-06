#include "config.hpp"

#include <cctype>
#include <fstream>

#include <nlohmann/json.hpp>

#include "paths.hpp"

using json = nlohmann::json;

namespace ppc {

// Hand-rolled rather than <regex>, which costs ~100KB of binary and seconds of compile
// time for a pattern this trivial.
NameCheck check_account_name(std::string_view s) {
    if (s.empty()) return NameCheck::Empty;
    const size_t hash = s.find('#');
    if (hash == std::string_view::npos || hash == 0 || hash + 1 == s.size())
        return NameCheck::Malformed;
    for (char ch : s.substr(0, hash))
        if (!std::isalnum(static_cast<unsigned char>(ch))) return NameCheck::Malformed;
    for (char ch : s.substr(hash + 1))
        if (!std::isdigit(static_cast<unsigned char>(ch))) return NameCheck::Malformed;
    return NameCheck::Ok;
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
    c.auto_search = j.value("auto_search", c.auto_search);
    if (const std::string s = j.value("listing_status", c.listing_status); trade::valid_status(s))
        c.listing_status = s;
    if (const int n = j.value("result_count", c.result_count); trade::valid_result_count(n))
        c.result_count = n;
    c.panel_width = j.value("panel_width", c.panel_width);
    c.stash_edge = j.value("stash_edge", c.stash_edge);
    c.inventory_edge = j.value("inventory_edge", c.inventory_edge);
    c.status_right = j.value("status_right", c.status_right);
    c.status_bottom = j.value("status_bottom", c.status_bottom);
    c.debug_log = j.value("debug_log", c.debug_log);
    return c;
}

bool Config::save() const {
    ensure_dir(config_dir());
    json j;
    j["league"] = league;
    j["account_name"] = account_name;
    j["poe_window_title"] = poe_window_title;
    j["hotkeys"]["price_check"] = to_string(price_check);
    j["hotkeys"]["settings"] = to_string(settings);
    j["auto_search"] = auto_search;
    j["listing_status"] = listing_status;
    j["result_count"] = result_count;
    j["panel_width"] = panel_width;
    j["stash_edge"] = stash_edge;
    j["inventory_edge"] = inventory_edge;
    j["status_right"] = status_right;
    j["status_bottom"] = status_bottom;
    j["debug_log"] = debug_log;
    std::ofstream out(path());
    if (!out) return false;
    out << j.dump(2) << "\n";
    return true;
}

} // namespace ppc
