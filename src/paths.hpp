#pragma once

#include <filesystem>

namespace ppc {

/// %APPDATA%\PathOfPriceCheck | $XDG_CONFIG_HOME/PathOfPriceCheck | ~/.config/PathOfPriceCheck
std::filesystem::path config_dir();

/// %LOCALAPPDATA%\PathOfPriceCheck | $XDG_CACHE_HOME/PathOfPriceCheck | ~/.cache/PathOfPriceCheck
std::filesystem::path cache_dir();

/// mkdir -p, ignoring the error. False if the directory still isn't there afterwards.
bool ensure_dir(const std::filesystem::path& p);

} // namespace ppc
