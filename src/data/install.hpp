#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "data/manifest.hpp"

namespace ppc::data {

/// Layout under the cache root:
///
///     <root>/current              one line: the active data_version
///     <root>/<data_version>/      the installed bundle
///     <root>/.tmp-<version>/      download staging
///
/// Versioned directories are not cosmetic. **Windows will not let you rename over or delete
/// a memory-mapped file**, so a bundle is never written in place over one that may be
/// mapped: a fresh directory is filled, `current` is flipped, and the old directory is
/// removed at the *next* startup when nothing holds it. Do not "simplify" this to an
/// overwrite; it works on Linux and fails only on Windows, which is the worst way to fail.
class BundleStore {
public:
    explicit BundleStore(std::filesystem::path root) : root_(std::move(root)) {}

    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path staging_dir(std::string_view version) const;
    std::filesystem::path version_dir(std::string_view version) const;

    /// The version named by `current`, or empty if there is none.
    std::string current_version() const;
    /// The directory of the active bundle, or empty if there is no usable one.
    std::filesystem::path current_dir() const;

    /// Writes one asset into staging after checking its bytes against the manifest entry.
    /// False on a hash or size mismatch — the bytes are not written.
    bool stage(std::string_view version, const ManifestFile& f, std::string_view bytes,
               std::string* err) const;

    /// Verifies every manifest entry is present and correct in staging, writes the
    /// manifest, moves staging into place, and flips `current`.
    bool commit(const Manifest& m, std::string* err) const;

    /// Removes staging leftovers and every version directory except the current one.
    /// Call at startup, before anything is mapped.
    void prune(std::string* removed_summary = nullptr) const;

private:
    std::filesystem::path root_;
};

/// sha256 of `bytes` as lowercase hex, compared case-insensitively against `expected`.
bool bytes_match(std::string_view bytes, std::string_view expected_sha256);

} // namespace ppc::data
