#pragma once

#include <filesystem>
#include <string>

namespace ppc {

/// %APPDATA%\PathOfPriceCheck | $XDG_CONFIG_HOME/PathOfPriceCheck | ~/.config/PathOfPriceCheck
std::filesystem::path config_dir();

/// %LOCALAPPDATA%\PathOfPriceCheck | $XDG_CACHE_HOME/PathOfPriceCheck | ~/.cache/PathOfPriceCheck
std::filesystem::path cache_dir();

/// mkdir -p, ignoring the error. False if the directory still isn't there afterwards.
bool ensure_dir(const std::filesystem::path& p);

/// `p` with the part that names this machine's user folded back into what it came from:
/// `~/.config/PathOfPriceCheck/config.json`, `%APPDATA%\PathOfPriceCheck\config.json`.
///
/// **For showing, never for opening** — the result is not a path any API will accept. Every one
/// of these strings ends up on somebody's screenshot, and a home directory is a name; it is also
/// simply shorter, which is what the footer it is drawn in has room for.
std::string display_path(const std::filesystem::path& p);

/// `p` as a `file://` URL, for handing to `SDL_OpenURL`.
///
/// Percent-encoded, because a path may hold spaces and anything else a URL reserves, and given
/// the third slash Windows needs in front of its drive letter.
std::string file_url(const std::filesystem::path& p);

} // namespace ppc
