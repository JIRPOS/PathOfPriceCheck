#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ppc::data {

/// Bundles this client understands. The publisher only ever adds fields within a version;
/// a genuine break would arrive as a separate manifest document, not a bump here.
inline constexpr int kSupportedSchemaVersion = 1;

/// A hostile or simply buggy manifest must not be able to fill the disk.
inline constexpr uint64_t kMaxFileBytes = 32ull * 1024 * 1024;
inline constexpr uint64_t kMaxTotalBytes = 128ull * 1024 * 1024;

struct ManifestFile {
    std::string name;   ///< also the on-disk filename; validated, never trusted
    std::string sha256; ///< of the decoded bytes
    uint64_t size = 0;  ///< of the decoded bytes
    std::string encoding = "none";
    std::string url;
};

struct Manifest {
    int schema_version = 0;
    std::string data_version;
    std::string generated_at;
    std::string game_patch;
    /// The credit the per-unique modifier data is licensed on. Carried in the bundle rather
    /// than hardcoded here, and written through on install: it describes the data, and an
    /// attribution that only lives in the publisher's repo is not an attribution.
    std::string unique_mods_attribution;
    /// How many items the data build found trading on the in-game currency exchange, and so
    /// whether the `exchange` flag on a base record means anything at all. Written through on
    /// install for the same reason the attribution is: 0 is "this bundle predates the dataset",
    /// which is not the same answer as "this item does not trade there" — see
    /// `GameData::has_exchange_flags()`.
    int exchange_items = 0;
    std::vector<ManifestFile> files;

    const ManifestFile* find(std::string_view name) const;
    uint64_t total_bytes() const;
};

/// `files[].name` is written straight to the filesystem, so it is a security boundary:
/// only `[A-Za-z0-9._-]`, no `..`, no leading separator, no backslash, no drive letter.
bool is_safe_asset_name(std::string_view name);

/// Parses and validates. Returns false and fills `err` on anything it will not act on:
/// an unsupported schema, an unsafe name, an oversized file, a non-https URL, or an
/// encoding this build cannot decode.
bool parse_manifest(std::string_view json_text, Manifest& out, std::string* err);

} // namespace ppc::data
