#pragma once

#include <filesystem>
#include <string>

#include "update/release.hpp"

namespace ppc::update {

/// Where the running executable lives. Empty only if the operating system will not say, which
/// no supported platform does in practice.
std::filesystem::path exe_path();

/// How this copy got onto the disk. On Windows the answer is the `InstallDir` value the
/// installer wrote under `HKCU\Software\PathOfPriceCheck`: equal to our own directory means we
/// are that install, anything else means a portable copy sitting somewhere of its own. On Linux
/// `$APPIMAGE` names the AppImage that mounted us.
///
/// `Unknown` is the answer for a build tree or a distribution package, and nothing is ever
/// applied to one — a package manager owns those files.
Flavour detect_flavour();

/// True when the directory holding `target` can be written. Probed by creating and deleting a
/// file rather than by reading permissions, because the permission bits are not the whole answer
/// on either platform. A 64-bit process gets a clean refusal from `Program Files`; there is no
/// UAC file virtualization to be fooled by.
///
/// `target` is the file an update would replace, never `exe_path()` — inside a mounted AppImage
/// those are different files, and the running binary's directory is a read-only squashfs that
/// answers no for every AppImage there is.
bool install_dir_writable(const std::filesystem::path& target);

/// What a flavour needs done to it, decided once so the worker and the UI cannot disagree.
enum class Method {
    None,        ///< nothing can be applied here; the notice points at the release page
    Swap,        ///< replace the executable in place
    RunInstaller ///< hand the staged installer the job
};

Method method_for(Flavour f);

/// Replaces the running executable with `staged`.
///
/// Windows renames the running image aside first: a running .exe cannot be overwritten, but it
/// can be renamed, and the leftover is cleared by `sweep_old()` on the next start. POSIX renames
/// straight over the target, which succeeds against a running binary because the inode is
/// unlinked rather than written. Either way the last step is a rename, so a machine that loses
/// power mid-update still has one whole executable at the path — never a half-written one.
///
/// For an AppImage `target` is `$APPIMAGE` at its own path, because desktop integration keys on
/// that path and a new filename is a duplicate launcher entry.
///
/// False with `err` filled on failure, having left the installation as it was found.
bool apply_swap(const std::filesystem::path& staged, const std::filesystem::path& target,
                std::string* err);

/// Deletes the `.old` executable a previous swap left behind. Called at startup, before
/// anything else; failure is not worth reporting, since the next start tries again.
void sweep_old(const std::filesystem::path& target);

/// The path `apply_swap` renames the running executable to.
std::filesystem::path old_path_for(const std::filesystem::path& target);

} // namespace ppc::update
