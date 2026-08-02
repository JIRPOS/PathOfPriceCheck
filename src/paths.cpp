#include "paths.hpp"

#include <cstdlib>

namespace fs = std::filesystem;

namespace ppc {
namespace {

/// Every lookup re-reads the environment rather than caching in a static: tests point
/// XDG_CONFIG_HOME/XDG_CACHE_HOME at a scratch directory per case.
fs::path env_dir(const char* primary, const char* home_var, const char* home_child) {
    if (const char* v = std::getenv(primary); v && *v) return fs::path(v) / "PathOfPriceCheck";
    fs::path base(".");
    if (home_var) {
        const char* home = std::getenv(home_var);
        base = home ? fs::path(home) : fs::path(".");
    }
    if (home_child) base /= home_child;
    return base / "PathOfPriceCheck";
}

} // namespace

fs::path config_dir() {
#ifdef _WIN32
    return env_dir("APPDATA", nullptr, nullptr);
#else
    return env_dir("XDG_CONFIG_HOME", "HOME", ".config");
#endif
}

fs::path cache_dir() {
#ifdef _WIN32
    // LOCALAPPDATA, not APPDATA: the data bundle is machine-local and has no business
    // following a roaming profile between machines.
    return env_dir("LOCALAPPDATA", nullptr, nullptr);
#else
    return env_dir("XDG_CACHE_HOME", "HOME", ".cache");
#endif
}

bool ensure_dir(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    return fs::is_directory(p, ec);
}

} // namespace ppc
