#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ppc::update {

/// Where the client looks for the newest release. A fixed release-asset URL, deliberately not
/// `api.github.com`, for the same reason `data::kManifestUrl` gives: unauthenticated API calls
/// are capped at 60 an hour per IP, which a shared address or a restart loop can exhaust. This
/// path is plain CDN, and GitHub's `latest` pointer already skips prereleases.
inline constexpr const char* kLatestUrl =
    "https://github.com/JIRPOS/PathOfPriceCheck/releases/latest/download/latest.json";

/// Where a user is sent when an update exists but cannot be applied here.
inline constexpr const char* kReleasesUrl =
    "https://github.com/JIRPOS/PathOfPriceCheck/releases/latest";

inline constexpr int kSupportedSchemaVersion = 1;

/// An installer is the largest thing published; nothing here should approach it.
inline constexpr uint64_t kMaxAssetBytes = 256ull * 1024 * 1024;

/// How this copy of the application got onto the disk, which decides how it can replace itself.
enum class Flavour {
    Unknown,       ///< a build tree, a distribution package, anything unrecognised: never applied
    WinInstalled,  ///< the .exe the installer put down; updated by running the next installer
    WinPortable,   ///< the .exe out of the .zip; updated by swapping the file
    AppImage,      ///< updated by overwriting $APPIMAGE at its own path
    LinuxBinary,   ///< the tarball's bare binary; updated by swapping the file
};

std::string_view to_string(Flavour f);

/// `MAJOR.MINOR.BUILD`, compared as three numbers rather than as text: "0.3.9" is older than
/// "0.3.10", which a lexicographic compare gets backwards.
struct Version {
    int major = 0, minor = 0, build = 0;

    /// False on anything that is not three non-negative integers — including the empty string
    /// and a leading "v". A version that will not parse is never newer than what is running.
    static bool parse(std::string_view s, Version& out);

    bool operator<(const Version& o) const {
        if (major != o.major) return major < o.major;
        if (minor != o.minor) return minor < o.minor;
        return build < o.build;
    }
    bool operator==(const Version& o) const {
        return major == o.major && minor == o.minor && build == o.build;
    }
    std::string str() const;
};

/// This build's own version, from the APP_VERSION define.
Version running_version();

struct Asset {
    std::string name;
    std::string url;
    std::string sha256; ///< of the bytes as downloaded
    uint64_t size = 0;
};

struct Release {
    int schema_version = 0;
    Version version;
    std::string notes_url;
    std::vector<Asset> assets;
};

/// Parses and validates `latest.json`. False with `err` filled on anything it will not act on:
/// an unsupported schema, an unparseable version, a non-https URL, a bad digest, an implausible
/// size. A release published before this feature existed carries no such file at all, which the
/// caller treats as "nothing to do" rather than as an error.
bool parse_release(std::string_view json_text, Release& out, std::string* err);

/// The asset this flavour would install, or null when the release publishes nothing for it.
/// Matched on the asset-name suffixes the release workflow produces.
const Asset* pick_asset(const Release& r, Flavour f);

} // namespace ppc::update
