#include "update/release.hpp"

#include <charconv>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ppc::update {
namespace {

/// The suffix each flavour's asset carries, as `.github/workflows/release.yml` names them.
/// The Windows pair is the whole reason this is a lookup rather than a platform #ifdef: one
/// binary serves both, and which file it should fetch is not known until run time.
///
/// **Never the .zip or the .tar.gz**, though those are what a person downloads: what gets
/// applied has to be a file this can swap into place, and nothing here reads an archive
/// container. The release therefore publishes the bare executable beside each archive, which
/// is also a download worth having on its own.
std::string_view asset_suffix(Flavour f) {
    switch (f) {
    case Flavour::WinInstalled: return "win64-setup.exe";
    case Flavour::WinPortable: return "win64.exe";
    case Flavour::AppImage: return "linux-x64.AppImage";
    case Flavour::LinuxBinary: return "linux-x64";
    case Flavour::Unknown: break;
    }
    return {};
}

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(),
                                                  suffix) == 0;
}

bool is_hex_digest(std::string_view s) {
    if (s.size() != 64) return false;
    for (char c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!ok) return false;
    }
    return true;
}

} // namespace

std::string_view to_string(Flavour f) {
    switch (f) {
    case Flavour::WinInstalled: return "installed";
    case Flavour::WinPortable: return "portable";
    case Flavour::AppImage: return "appimage";
    case Flavour::LinuxBinary: return "binary";
    case Flavour::Unknown: break;
    }
    return "unknown";
}

bool Version::parse(std::string_view s, Version& out) {
    Version v;
    int* const fields[3] = {&v.major, &v.minor, &v.build};
    size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        if (i > 0) {
            if (pos >= s.size() || s[pos] != '.') return false;
            ++pos;
        }
        const char* const first = s.data() + pos;
        // from_chars, never strtol: a version is a number and numbers do not go through the C
        // locale here. It rejects a leading '+' and any whitespace strtol would have taken, but
        // *not* a leading '-' — so negatives are refused here rather than compared as older.
        const auto [ptr, ec] = std::from_chars(first, s.data() + s.size(), *fields[i]);
        if (ec != std::errc{} || ptr == first || *fields[i] < 0) return false;
        pos = static_cast<size_t>(ptr - s.data());
    }
    if (pos != s.size()) return false;
    out = v;
    return true;
}

std::string Version::str() const {
    return std::to_string(major) + '.' + std::to_string(minor) + '.' + std::to_string(build);
}

Version running_version() {
    Version v;
    // A build whose APP_VERSION does not parse would otherwise compare as 0.0.0 and take every
    // release as an update, in a loop. Left at 0.0.0 deliberately is the same thing, which is
    // why the caller checks `parse` on the release rather than trusting this.
    Version::parse(APP_VERSION, v);
    return v;
}

bool parse_release(std::string_view json_text, Release& out, std::string* err) {
    const auto fail = [err](std::string msg) {
        if (err) *err = std::move(msg);
        return false;
    };

    const json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail("latest.json is not valid JSON");

    out = Release{};
    out.schema_version = j.value("schema_version", 0);
    if (out.schema_version != kSupportedSchemaVersion)
        return fail("unsupported release schema v" + std::to_string(out.schema_version));

    const std::string ver = j.value("version", std::string());
    if (!Version::parse(ver, out.version)) return fail("bad version '" + ver + "'");
    out.notes_url = j.value("notes_url", std::string(kReleasesUrl));

    const auto assets = j.find("assets");
    if (assets == j.end() || !assets->is_array() || assets->empty())
        return fail("release lists no assets");

    for (const json& a : *assets) {
        if (!a.is_object()) return fail("malformed asset entry");
        Asset as;
        as.name = a.value("name", std::string());
        as.url = a.value("url", std::string());
        as.sha256 = a.value("sha256", std::string());
        as.size = a.value("size", uint64_t{0});

        if (as.name.empty()) return fail("asset with no name");
        // Nothing here becomes a path — the staged file is named by us — so the checks that
        // matter are the ones that decide whether we will execute what arrives.
        if (as.url.rfind("https://", 0) != 0) return fail("non-https url for " + as.name);
        if (!is_hex_digest(as.sha256)) return fail("bad sha256 for " + as.name);
        if (as.size == 0 || as.size > kMaxAssetBytes)
            return fail("implausible size for " + as.name);
        out.assets.push_back(std::move(as));
    }
    return true;
}

const Asset* pick_asset(const Release& r, Flavour f) {
    const std::string_view suffix = asset_suffix(f);
    if (suffix.empty()) return nullptr;
    for (const Asset& a : r.assets)
        if (ends_with(a.name, suffix)) return &a;
    return nullptr;
}

} // namespace ppc::update
