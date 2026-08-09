#include "update/install.hpp"

#include <cstdlib>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace ppc::update {
namespace {

#ifdef _WIN32
/// The installer's `InstallDir`, or empty when this machine has no installed copy.
fs::path installed_dir() {
    HKEY key{};
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\PathOfPriceCheck", 0, KEY_QUERY_VALUE,
                        &key) != ERROR_SUCCESS)
        return {};
    wchar_t buf[MAX_PATH]{};
    DWORD bytes = sizeof(buf) - sizeof(wchar_t); // room for a terminator RegGetValue may omit
    DWORD type = 0;
    const LSTATUS rc =
        ::RegQueryValueExW(key, L"InstallDir", nullptr, &type, reinterpret_cast<BYTE*>(buf),
                           &bytes);
    ::RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_SZ) return {};
    return fs::path(buf);
}
#endif

} // namespace

fs::path exe_path() {
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(),
                                             static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            return fs::path(buf);
        }
        buf.resize(buf.size() * 2); // truncated: ERROR_INSUFFICIENT_BUFFER
    }
#else
    std::error_code ec;
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path{} : p;
#endif
}

Flavour detect_flavour() {
#ifdef _WIN32
    const fs::path self = exe_path();
    if (self.empty()) return Flavour::Unknown;
    const fs::path dir = installed_dir();
    if (!dir.empty()) {
        std::error_code ec;
        if (fs::equivalent(dir, self.parent_path(), ec) && !ec) return Flavour::WinInstalled;
    }
    return Flavour::WinPortable;
#else
    // Set by the AppImage runtime to the .AppImage file itself, which is the path desktop
    // integration knows and therefore the only path an update may land on.
    if (const char* img = std::getenv("APPIMAGE"); img && *img) return Flavour::AppImage;
    const fs::path self = exe_path();
    if (self.empty()) return Flavour::Unknown;
    // A build tree is not something to update over: the next `cmake --build` would undo it,
    // and a developer running from `build/` has not asked for a release binary.
    if (self.parent_path().filename() == "build") return Flavour::Unknown;
    return Flavour::LinuxBinary;
#endif
}

bool install_dir_writable() {
    const fs::path self = exe_path();
    if (self.empty()) return false;
    const fs::path probe = self.parent_path() / ".ppc-write-probe";
    std::error_code ec;
    fs::remove(probe, ec); // a leftover from a killed run would otherwise answer for us
    const bool ok = std::ofstream(probe, std::ios::binary).good();
    fs::remove(probe, ec);
    return ok;
}

Method method_for(Flavour f) {
    switch (f) {
    case Flavour::WinInstalled: return Method::RunInstaller;
    case Flavour::WinPortable:
    case Flavour::AppImage:
    case Flavour::LinuxBinary: return Method::Swap;
    case Flavour::Unknown: break;
    }
    return Method::None;
}

fs::path old_path_for(const fs::path& target) {
    // Appended rather than substituted for the extension: an asset name carries the version,
    // so the last dot in `…-0.3.42-linux-x64` is not an extension at all.
    fs::path p = target;
    p += ".old";
    return p;
}

bool apply_swap(const fs::path& staged, const fs::path& target, std::string* err) {
    const auto fail = [err](std::string msg) {
        if (err) *err = std::move(msg);
        return false;
    };
    std::error_code ec;
    if (!fs::is_regular_file(staged, ec)) return fail("nothing staged to apply");

    // Before the rename, never after: a file that becomes the executable must already be one,
    // or a crash between the two steps leaves an installation that cannot start.
#ifndef _WIN32
    fs::permissions(staged,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
    if (ec) return fail("cannot make the staged file executable: " + ec.message());
#endif

#ifdef _WIN32
    // A running image cannot be overwritten, but it can be renamed out of the way.
    const fs::path old = old_path_for(target);
    fs::remove(old, ec);
    fs::rename(target, old, ec);
    if (ec) return fail("cannot move the running program aside: " + ec.message());
    fs::rename(staged, target, ec);
    if (ec) {
        std::error_code back;
        fs::rename(old, target, back); // put it back rather than leave nothing at the path
        return fail("cannot move the update into place: " + ec.message());
    }
#else
    // Renaming over a running binary is fine here: the old inode stays alive for as long as it
    // is mapped, and the directory entry is what changes.
    fs::rename(staged, target, ec);
    if (ec) {
        // Most often a staging directory on a different filesystem than the install, where
        // rename cannot work at all. Copy, then rename within the destination directory, so the
        // step that replaces the target is still atomic.
        const fs::path tmp = target.parent_path() / (target.filename().string() + ".new");
        fs::remove(tmp, ec);
        fs::copy_file(staged, tmp, fs::copy_options::overwrite_existing, ec);
        if (ec) return fail("cannot place the update beside the program: " + ec.message());
        fs::permissions(tmp,
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add, ec);
        fs::rename(tmp, target, ec);
        if (ec) {
            std::error_code rm;
            fs::remove(tmp, rm);
            return fail("cannot move the update into place: " + ec.message());
        }
        fs::remove(staged, ec);
    }
#endif
    return true;
}

void sweep_old(const fs::path& target) {
    std::error_code ec;
    fs::remove(old_path_for(target), ec);
}

} // namespace ppc::update
