#pragma once

#include <cstdint>
#include <string>

namespace ppc {

enum class Mod : uint8_t {
    None  = 0,
    Ctrl  = 1u << 0,
    Shift = 1u << 1,
    Alt   = 1u << 2,
    Super = 1u << 3,
};
constexpr Mod operator|(Mod a, Mod b) { return Mod(uint8_t(a) | uint8_t(b)); }
constexpr Mod operator&(Mod a, Mod b) { return Mod(uint8_t(a) & uint8_t(b)); }
constexpr bool any(Mod m) { return uint8_t(m) != 0; }
constexpr bool has(Mod set, Mod m) { return any(set & m); }

/// A system-wide hotkey. `key` is a canonical name (e.g. "D", "Space", "F5");
/// empty means unbound. The OS hotkey APIs don't distinguish left/right
/// modifiers, so Mod is side-agnostic.
struct Hotkey {
    Mod mods = Mod::None;
    std::string key;
    bool valid() const { return !key.empty(); }
};

enum class Action { PriceCheck, ToggleSettings };

std::string to_string(const Hotkey& h);      ///< e.g. "Ctrl+D"
Hotkey parse_hotkey(const std::string& s);   ///< inverse of to_string

/// Canonical key name for an SDL keycode, or "" if not a bindable key.
/// Arg is an SDL_Keycode, kept as uint32_t to avoid an SDL include here.
std::string key_name_from_sdl(uint32_t sdl_keycode);

} // namespace ppc
