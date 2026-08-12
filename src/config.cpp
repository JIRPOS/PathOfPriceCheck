#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "mapcheck/verdict.hpp"
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

namespace {

void read_into(Config& c, const json& j) {
    c.league = j.value("league", c.league);
    c.account_name = j.value("account_name", c.account_name);
    c.client_language = j.value("client_language", c.client_language);
    c.ui_language = j.value("ui_language", c.ui_language);
    c.poe_window_title = j.value("poe_window_title", c.poe_window_title);
    if (j.contains("hotkeys")) {
        const auto& h = j["hotkeys"];
        if (h.contains("price_check")) c.price_check = parse_hotkey(h["price_check"].get<std::string>());
        if (h.contains("settings")) c.settings = parse_hotkey(h["settings"].get<std::string>());
        if (h.contains("quick_paste"))
            c.quick_paste = parse_hotkey(h["quick_paste"].get<std::string>());
        if (h.contains("map_check")) c.map_check = parse_hotkey(h["map_check"].get<std::string>());
    }
    if (j.contains("map_check")) {
        const auto& m = j["map_check"];
        if (m.contains("profiles") && m["profiles"].is_array())
            for (const auto& p : m["profiles"])
                if (p.is_string()) {
                    // Sanitised on the way in as well as on the way out: this file is
                    // hand-editable and a name here is a file name, so a slash in one would be
                    // a profile that can be listed and never opened.
                    std::string name = mapcheck::sanitize_profile_name(p.get<std::string>());
                    if (!name.empty()) c.map_profiles.push_back(std::move(name));
                }
        c.map_profile = mapcheck::sanitize_profile_name(m.value("profile", std::string()));
    }
    if (j.contains("pastes") && j["pastes"].is_array()) {
        for (const auto& p : j["pastes"]) {
            if (!p.is_object()) continue;
            c.pastes.push_back(Paste{p.value("heading", std::string()),
                                     p.value("body", std::string()), p.value("enabled", true)});
        }
        // Clamped rather than taken, like the range percentages above: a hand-edited file
        // claiming twelve active pastes would otherwise draw slots with no key to press.
        limit_enabled(c.pastes);
    }
    c.auto_search = j.value("auto_search", c.auto_search);
    if (const std::string s = j.value("listing_status", c.listing_status); trade::valid_status(s))
        c.listing_status = s;
    if (const int n = j.value("result_count", c.result_count); trade::valid_result_count(n))
        c.result_count = n;
    if (j.contains("range_match")) {
        const auto& r = j["range_match"];
        item::RangeMatch& rm = c.range_match;
        rm.min_mode = item::bound_mode_from_id(r.value("min_mode", std::string()), rm.min_mode);
        rm.max_mode = item::bound_mode_from_id(r.value("max_mode", std::string()), rm.max_mode);
        // Clamped rather than taken: this file is hand-editable, and a negative percentage is
        // an interval whose ends have swapped, i.e. a filter nothing can ever match.
        rm.min_pct = std::clamp(r.value("min_pct", rm.min_pct), 0.0, 100.0);
        rm.max_pct = std::clamp(r.value("max_pct", rm.max_pct), 0.0, 100.0);
    }
    c.panel_width = j.value("panel_width", c.panel_width);
    c.stash_edge = j.value("stash_edge", c.stash_edge);
    c.inventory_edge = j.value("inventory_edge", c.inventory_edge);
    c.status_right = j.value("status_right", c.status_right);
    c.status_bottom = j.value("status_bottom", c.status_bottom);
    c.reduce_transparency = j.value("reduce_transparency", c.reduce_transparency);
    c.auto_update = j.value("auto_update", c.auto_update);
    c.debug_log = j.value("debug_log", c.debug_log);
}

} // namespace

Config Config::load() {
    Config c;
    std::ifstream in(path());
    if (!in) return c;
    try {
        json j;
        in >> j;
        read_into(c, j);
    } catch (...) {
        // Malformed, or a field holding a type this does not expect — an object where a string
        // belongs throws as surely as a truncated file does. This file is hand-editable, so
        // neither may be the reason the program will not start. What was read before the throw
        // stands; everything after it keeps its default.
    }
    return c;
}

bool Config::save() const {
    ensure_dir(config_dir());
    json j;
    j["league"] = league;
    j["account_name"] = account_name;
    j["client_language"] = client_language;
    j["ui_language"] = ui_language;
    j["poe_window_title"] = poe_window_title;
    j["hotkeys"]["price_check"] = to_string(price_check);
    j["hotkeys"]["settings"] = to_string(settings);
    j["hotkeys"]["quick_paste"] = to_string(quick_paste);
    j["hotkeys"]["map_check"] = to_string(map_check);
    j["map_check"]["profiles"] = map_profiles;
    j["map_check"]["profile"] = map_profile;
    // Written even when empty, so the file says the feature exists and where its entries go.
    j["pastes"] = json::array();
    for (const Paste& p : pastes)
        j["pastes"].push_back({{"heading", p.heading}, {"body", p.body}, {"enabled", p.enabled}});
    j["auto_search"] = auto_search;
    j["listing_status"] = listing_status;
    j["result_count"] = result_count;
    j["range_match"]["min_mode"] = std::string(item::bound_mode_id(range_match.min_mode));
    j["range_match"]["max_mode"] = std::string(item::bound_mode_id(range_match.max_mode));
    j["range_match"]["min_pct"] = range_match.min_pct;
    j["range_match"]["max_pct"] = range_match.max_pct;
    j["panel_width"] = panel_width;
    j["stash_edge"] = stash_edge;
    j["inventory_edge"] = inventory_edge;
    j["status_right"] = status_right;
    j["status_bottom"] = status_bottom;
    j["reduce_transparency"] = reduce_transparency;
    j["auto_update"] = auto_update;
    j["debug_log"] = debug_log;
    std::ofstream out(path());
    if (!out) return false;
    out << j.dump(2) << "\n";
    return true;
}

} // namespace ppc
