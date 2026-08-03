#include "data/install.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

#include <nlohmann/json.hpp>

#include "paths.hpp"
#include "util/sha256.hpp"

namespace fs = std::filesystem;

namespace ppc::data {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool write_file(const fs::path& p, std::string_view bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

bool bytes_match(std::string_view bytes, std::string_view expected_sha256) {
    return sha256_hex(bytes) == lower(std::string(expected_sha256));
}

fs::path BundleStore::staging_dir(std::string_view version) const {
    return root_ / (".tmp-" + std::string(version));
}

fs::path BundleStore::version_dir(std::string_view version) const {
    return root_ / std::string(version);
}

std::string BundleStore::current_version() const {
    // Read and close immediately: `current` must never be a mapped file, or Windows could
    // not replace it.
    std::string v = read_file(root_ / "current");
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' '))
        v.pop_back();
    // It names a directory, so it is subject to the same rules as an asset name.
    return is_safe_asset_name(v) ? v : std::string();
}

fs::path BundleStore::current_dir() const {
    const std::string v = current_version();
    if (v.empty()) return {};
    std::error_code ec;
    const fs::path d = version_dir(v);
    return fs::is_directory(d, ec) ? d : fs::path();
}

bool BundleStore::stage(std::string_view version, const ManifestFile& f,
                        std::string_view bytes, std::string* err) const {
    const auto fail = [err](std::string msg) {
        if (err) *err = std::move(msg);
        return false;
    };
    if (!is_safe_asset_name(f.name)) return fail("unsafe asset name: " + f.name);
    if (bytes.size() != f.size)
        return fail(f.name + ": expected " + std::to_string(f.size) + " bytes, got " +
                    std::to_string(bytes.size()));
    if (!bytes_match(bytes, f.sha256)) return fail(f.name + ": sha256 mismatch");

    const fs::path dir = staging_dir(version);
    if (!ensure_dir(dir)) return fail("cannot create " + dir.string());
    if (!write_file(dir / f.name, bytes)) return fail("cannot write " + f.name);
    return true;
}

bool BundleStore::commit(const Manifest& m, std::string* err) const {
    const auto fail = [err](std::string msg) {
        if (err) *err = std::move(msg);
        return false;
    };
    const fs::path staging = staging_dir(m.data_version);
    std::error_code ec;
    if (!fs::is_directory(staging, ec)) return fail("nothing staged for " + m.data_version);

    // Re-verify from disk. Staging checked the bytes in flight; this catches a short write
    // or a file that was disturbed between staging and commit.
    for (const ManifestFile& f : m.files) {
        const fs::path p = staging / f.name;
        const std::string content = read_file(p);
        if (content.size() != f.size) return fail(f.name + ": missing or truncated in staging");
        if (!bytes_match(content, f.sha256)) return fail(f.name + ": sha256 mismatch on commit");
    }

    nlohmann::json j;
    j["schema_version"] = m.schema_version;
    j["data_version"] = m.data_version;
    j["generated_at"] = m.generated_at;
    j["game_patch"] = m.game_patch;
    if (!write_file(staging / "manifest.json", j.dump(2) + "\n"))
        return fail("cannot write manifest.json");

    const fs::path final_dir = version_dir(m.data_version);
    if (fs::exists(final_dir, ec)) fs::remove_all(final_dir, ec);
    fs::rename(staging, final_dir, ec);
    if (ec) return fail("cannot move staging into place: " + ec.message());

    // Flip `current` by writing a sibling and renaming over it: atomic on POSIX, and
    // MoveFileExW(MOVEFILE_REPLACE_EXISTING) under MSVC's implementation.
    const fs::path tmp = root_ / "current.tmp";
    if (!write_file(tmp, m.data_version + "\n")) return fail("cannot write current.tmp");
    fs::rename(tmp, root_ / "current", ec);
    if (ec) return fail("cannot flip current: " + ec.message());
    return true;
}

void BundleStore::prune(std::string* removed_summary) const {
    std::error_code ec;
    if (!fs::is_directory(root_, ec)) return;
    const std::string keep = current_version();

    std::string removed;
    for (const fs::directory_entry& e : fs::directory_iterator(root_, ec)) {
        if (ec) return;
        if (!e.is_directory(ec)) continue;
        const std::string name = e.path().filename().string();
        if (name == keep) continue;
        // Everything else is either an abandoned staging directory or a superseded bundle
        // that nothing has mapped yet, because prune runs before anything is opened.
        fs::remove_all(e.path(), ec);
        if (!ec) {
            if (!removed.empty()) removed += ", ";
            removed += name;
        }
    }
    fs::remove(root_ / "current.tmp", ec);
    if (removed_summary) *removed_summary = removed;
}

} // namespace ppc::data
