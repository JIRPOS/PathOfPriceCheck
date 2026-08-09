#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "update/install.hpp"
#include "update/release.hpp"

namespace fs = std::filesystem;
using namespace ppc::update;

namespace {

fs::path scratch_root(const char* name) {
    const fs::path p = fs::temp_directory_path() / "ppc-update-test" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

void write(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary);
    out << text;
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

constexpr const char* kDigest =
    "0000000000000000000000000000000000000000000000000000000000000000";

std::string release_json(const std::string& version, const std::string& assets) {
    return R"({"schema_version":1,"version":")" + version + R"(","assets":[)" + assets + "]}";
}

std::string asset_json(const std::string& name) {
    return R"({"name":")" + name + R"(","url":"https://example.invalid/)" + name +
           R"(","sha256":")" + kDigest + R"(","size":1024})";
}

Version v(int major, int minor, int build) { return Version{major, minor, build}; }

} // namespace

TEST_CASE("a version is three numbers, compared as numbers") {
    Version out;
    REQUIRE(Version::parse("0.3.42", out));
    CHECK(out.major == 0);
    CHECK(out.minor == 3);
    CHECK(out.build == 42);
    CHECK(out.str() == "0.3.42");

    // The whole reason this is not a string compare.
    CHECK(v(0, 3, 9) < v(0, 3, 10));
    CHECK(v(0, 9, 99) < v(0, 10, 0));
    CHECK(v(0, 99, 0) < v(1, 0, 0));
    CHECK_FALSE(v(0, 3, 42) < v(0, 3, 42));
    CHECK_FALSE(v(0, 4, 0) < v(0, 3, 99));
}

TEST_CASE("a version that will not parse is never newer than what is running") {
    Version out;
    // Each of these would otherwise leave `out` at 0.0.0 and take every release as an update.
    CHECK_FALSE(Version::parse("", out));
    CHECK_FALSE(Version::parse("v0.3.42", out));
    CHECK_FALSE(Version::parse("0.3", out));
    CHECK_FALSE(Version::parse("0.3.42.1", out));
    CHECK_FALSE(Version::parse("0.3.x", out));
    CHECK_FALSE(Version::parse("0.3.42 ", out));
    CHECK_FALSE(Version::parse(" 0.3.42", out));
    CHECK_FALSE(Version::parse("0.3.-1", out));
    CHECK_FALSE(Version::parse("0.3.+1", out));
}

TEST_CASE("this build knows its own version") {
    const Version self = running_version();
    // Whatever CMake injected, it has to have parsed — a 0.0.0 here would mean every release
    // published looks newer, for ever.
    CHECK(self.str() == APP_VERSION);
}

TEST_CASE("latest.json is parsed and validated") {
    Release r;
    std::string err;

    REQUIRE(parse_release(release_json("0.3.42", asset_json("PathOfPriceCheck-0.3.42-win64.exe")),
                          r, &err));
    CHECK(r.version.str() == "0.3.42");
    REQUIRE(r.assets.size() == 1);
    CHECK(r.assets[0].size == 1024);
    // Defaulted rather than required: an older generator that omits it still works.
    CHECK(r.notes_url == kReleasesUrl);

    SUBCASE("refused rather than guessed at") {
        CHECK_FALSE(parse_release("", r, &err));
        CHECK_FALSE(parse_release("{", r, &err));
        CHECK_FALSE(parse_release(R"({"schema_version":2,"version":"0.3.42","assets":[]})", r,
                                  &err));
        CHECK_FALSE(parse_release(release_json("nonsense", asset_json("a")), r, &err));
        CHECK_FALSE(parse_release(release_json("0.3.42", ""), r, &err));
    }

    SUBCASE("an asset we would not act on") {
        // Plain http, a short digest, a zero size and one larger than anything published.
        CHECK_FALSE(parse_release(
            R"({"schema_version":1,"version":"0.3.42","assets":[{"name":"a","url":"http://x/a","sha256":")" +
                std::string(kDigest) + R"(","size":1}]})",
            r, &err));
        CHECK_FALSE(parse_release(
            R"({"schema_version":1,"version":"0.3.42","assets":[{"name":"a","url":"https://x/a","sha256":"abc","size":1}]})",
            r, &err));
        CHECK_FALSE(parse_release(
            R"({"schema_version":1,"version":"0.3.42","assets":[{"name":"a","url":"https://x/a","sha256":")" +
                std::string(kDigest) + R"(","size":0}]})",
            r, &err));
        CHECK_FALSE(parse_release(
            R"({"schema_version":1,"version":"0.3.42","assets":[{"name":"a","url":"https://x/a","sha256":")" +
                std::string(kDigest) + R"(","size":999999999999}]})",
            r, &err));
    }
}

TEST_CASE("each flavour picks its own asset, and never an archive") {
    const std::string assets =
        asset_json("PathOfPriceCheck-0.3.42-win64-setup.exe") + "," +
        asset_json("PathOfPriceCheck-0.3.42-win64.exe") + "," +
        asset_json("PathOfPriceCheck-0.3.42-win64.zip") + "," +
        asset_json("PathOfPriceCheck-0.3.42-linux-x64.AppImage") + "," +
        asset_json("PathOfPriceCheck-0.3.42-linux-x64.tar.gz") + "," +
        asset_json("PathOfPriceCheck-0.3.42-linux-x64");

    Release r;
    std::string err;
    REQUIRE(parse_release(release_json("0.3.42", assets), r, &err));

    REQUIRE(pick_asset(r, Flavour::WinInstalled));
    CHECK(pick_asset(r, Flavour::WinInstalled)->name == "PathOfPriceCheck-0.3.42-win64-setup.exe");
    // The portable flavour takes the bare .exe, not the .zip it is also published in: nothing
    // here reads an archive container.
    REQUIRE(pick_asset(r, Flavour::WinPortable));
    CHECK(pick_asset(r, Flavour::WinPortable)->name == "PathOfPriceCheck-0.3.42-win64.exe");
    REQUIRE(pick_asset(r, Flavour::AppImage));
    CHECK(pick_asset(r, Flavour::AppImage)->name == "PathOfPriceCheck-0.3.42-linux-x64.AppImage");
    REQUIRE(pick_asset(r, Flavour::LinuxBinary));
    CHECK(pick_asset(r, Flavour::LinuxBinary)->name == "PathOfPriceCheck-0.3.42-linux-x64");

    // A build tree or a distribution package: there is nothing to hand it.
    CHECK(pick_asset(r, Flavour::Unknown) == nullptr);
}

TEST_CASE("a release with nothing for this flavour is not an error") {
    Release r;
    std::string err;
    REQUIRE(parse_release(release_json("0.3.42", asset_json("PathOfPriceCheck-0.3.42-win64.exe")),
                          r, &err));
    CHECK(pick_asset(r, Flavour::AppImage) == nullptr);
    CHECK(pick_asset(r, Flavour::LinuxBinary) == nullptr);
}

TEST_CASE("what each flavour may have done to it") {
    CHECK(method_for(Flavour::WinInstalled) == Method::RunInstaller);
    CHECK(method_for(Flavour::WinPortable) == Method::Swap);
    CHECK(method_for(Flavour::AppImage) == Method::Swap);
    CHECK(method_for(Flavour::LinuxBinary) == Method::Swap);
    // Never touched: a package manager owns those files.
    CHECK(method_for(Flavour::Unknown) == Method::None);
}

TEST_CASE("the .old path is appended, not substituted for an extension") {
    // The naive replace_extension() would cut this at the last dot, which is inside the
    // version rather than at an extension.
    CHECK(old_path_for("/opt/PathOfPriceCheck-0.3.42-linux-x64") ==
          fs::path("/opt/PathOfPriceCheck-0.3.42-linux-x64.old"));
    CHECK(old_path_for("/opt/PathOfPriceCheck.exe") == fs::path("/opt/PathOfPriceCheck.exe.old"));
}

TEST_CASE("the swap replaces the target and leaves it whole") {
    const fs::path root = scratch_root("swap");
    const fs::path target = root / "PathOfPriceCheck";
    const fs::path staged = root / "staged";
    write(target, "old binary");
    write(staged, "new binary");

    std::string err;
    REQUIRE(apply_swap(staged, target, &err));
    CHECK(read(target) == "new binary");
    // Consumed, so a second start cannot apply the same release twice.
    CHECK_FALSE(fs::exists(staged));

#ifndef _WIN32
    // Executable before the rename, never after: a crash between the two steps would otherwise
    // leave an installation that cannot start.
    CHECK((fs::status(target).permissions() & fs::perms::owner_exec) != fs::perms::none);
#endif
}

TEST_CASE("a swap with nothing staged changes nothing") {
    const fs::path root = scratch_root("nostage");
    const fs::path target = root / "PathOfPriceCheck";
    write(target, "old binary");

    std::string err;
    CHECK_FALSE(apply_swap(root / "absent", target, &err));
    CHECK_FALSE(err.empty());
    CHECK(read(target) == "old binary");
}

TEST_CASE("sweep removes the leftover and tolerates its absence") {
    const fs::path root = scratch_root("sweep");
    const fs::path target = root / "PathOfPriceCheck.exe";
    write(target, "running");
    write(old_path_for(target), "superseded");

    sweep_old(target);
    CHECK_FALSE(fs::exists(old_path_for(target)));
    CHECK(fs::exists(target)); // and not the one still in use

    sweep_old(target); // nothing to remove, and nothing to report
}

TEST_CASE("the running executable is locatable and its directory is writable") {
    // Both underpin every decision above: without a path there is nothing to replace, and
    // the writability probe is what tells a Program Files install from a normal one.
    const fs::path self = exe_path();
    REQUIRE_FALSE(self.empty());
    CHECK(fs::exists(self));
    CHECK(install_dir_writable()); // the test binary's own build directory
}
