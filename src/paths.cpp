#include "paths.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

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

/// The remainder of `s` after the directory `base` names, separator and all, or empty if `s` is
/// not inside it. `base` may be null or unset, which is a machine with no such variable.
///
/// A prefix test and nothing cleverer — no symlink resolution, no case folding on Windows. Both
/// strings come from the same environment this process was started with, so they either agree or
/// this is not the directory being named, and getting it wrong shows a full path rather than a
/// short one.
std::string under(const std::string& s, const char* base) {
    if (!base || !*base) return {};
    std::string b(base);
    while (b.size() > 1 && (b.back() == '/' || b.back() == '\\')) b.pop_back();
    if (s.size() <= b.size() || s.compare(0, b.size(), b) != 0) return {};
    const char sep = s[b.size()];
    if (sep != '/' && sep != '\\') return {};
    return s.substr(b.size());
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

std::string display_path(const fs::path& p) {
    const std::string s = p.string();
#ifdef _WIN32
    // USERPROFILE last: it is a prefix of both of the others, so testing it first would fold
    // `%LOCALAPPDATA%\…` into `%USERPROFILE%\AppData\Local\…`, which is longer and says less.
    for (const char* var : {"LOCALAPPDATA", "APPDATA", "USERPROFILE"}) {
        if (std::string sub = under(s, std::getenv(var)); !sub.empty())
            return "%" + std::string(var) + "%" + sub;
    }
#else
    if (std::string sub = under(s, std::getenv("HOME")); !sub.empty()) return "~" + sub;
#endif
    // An XDG variable pointed somewhere else entirely, or there is no home at all. What is left
    // is the path itself: a directory the user named is not one this can improve on.
    return s;
}

std::string file_url(const fs::path& p) {
    // generic_string so Windows' separators are already the URL's, and the third slash so
    // `C:/…` becomes `file:///C:/…` — a drive letter is not a hostname.
    const std::string s = p.generic_string();
    std::string url = "file://";
    if (!s.empty() && s.front() != '/') url += '/';
    for (const unsigned char c : s) {
        // Unreserved, plus the two that have to survive as themselves: the separator and the
        // colon after a drive letter.
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/' ||
            c == ':') {
            url += static_cast<char>(c);
        } else {
            char hex[4];
            std::snprintf(hex, sizeof hex, "%%%02X", c);
            url += hex;
        }
    }
    return url;
}

} // namespace ppc
