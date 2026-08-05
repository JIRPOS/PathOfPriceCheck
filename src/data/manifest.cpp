#include "data/manifest.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ppc::data {

bool is_safe_asset_name(std::string_view name) {
    if (name.empty() || name.size() > 128) return false;
    if (name.front() == '.' || name.front() == '/' || name.front() == '\\') return false;
    if (name.find("..") != std::string_view::npos) return false;
    if (name.find(':') != std::string_view::npos) return false; // drive letter or scheme
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

const ManifestFile* Manifest::find(std::string_view name) const {
    for (const ManifestFile& f : files)
        if (f.name == name) return &f;
    return nullptr;
}

uint64_t Manifest::total_bytes() const {
    uint64_t n = 0;
    for (const ManifestFile& f : files) n += f.size;
    return n;
}

bool parse_manifest(std::string_view json_text, Manifest& out, std::string* err) {
    const auto fail = [err](std::string msg) {
        if (err) *err = std::move(msg);
        return false;
    };

    const json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail("manifest is not valid JSON");

    out = Manifest{};
    out.schema_version = j.value("schema_version", 0);
    if (out.schema_version != kSupportedSchemaVersion)
        return fail("unsupported manifest schema v" + std::to_string(out.schema_version) +
                    " (this build understands v" + std::to_string(kSupportedSchemaVersion) +
                    ")");

    out.data_version = j.value("data_version", std::string());
    if (out.data_version.empty()) return fail("manifest has no data_version");
    out.generated_at = j.value("generated_at", std::string());
    out.game_patch = j.value("game_patch", std::string());
    if (const auto s = j.find("source"); s != j.end() && s->is_object())
        out.unique_mods_attribution = s->value("unique_mods_attribution", std::string());

    const auto files = j.find("files");
    if (files == j.end() || !files->is_array() || files->empty())
        return fail("manifest lists no files");

    for (const json& f : *files) {
        if (!f.is_object()) return fail("malformed file entry");
        ManifestFile mf;
        mf.name = f.value("name", std::string());
        mf.sha256 = f.value("sha256", std::string());
        mf.size = f.value("size", uint64_t{0});
        mf.encoding = f.value("encoding", std::string("none"));
        mf.url = f.value("url", std::string());

        // This name becomes a path. Everything else here is a sanity check; this one is a
        // security boundary.
        if (!is_safe_asset_name(mf.name)) return fail("unsafe asset name: " + mf.name);
        if (mf.sha256.size() != 64) return fail("bad sha256 for " + mf.name);
        if (mf.size == 0 || mf.size > kMaxFileBytes)
            return fail("implausible size for " + mf.name);
        if (mf.url.rfind("https://", 0) != 0) return fail("non-https url for " + mf.name);
        // Refuse rather than guess: an encoding we cannot decode would install garbage that
        // passes no hash check we could perform.
        if (mf.encoding != "none") return fail("unsupported encoding '" + mf.encoding +
                                               "' for " + mf.name);
        out.files.push_back(std::move(mf));
    }

    if (out.total_bytes() > kMaxTotalBytes) return fail("manifest total size is implausible");
    return true;
}

} // namespace ppc::data
