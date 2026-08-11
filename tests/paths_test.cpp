#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdlib>
#include <fstream>

#include "paths.hpp"

namespace fs = std::filesystem;

namespace {

void set_env(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v ? v : "");
#else
    if (v)
        setenv(k, v, 1);
    else
        unsetenv(k);
#endif
}

} // namespace

#ifndef _WIN32
TEST_CASE("XDG variables select the directory") {
    set_env("XDG_CONFIG_HOME", "/tmp/ppc-cfg");
    set_env("XDG_CACHE_HOME", "/tmp/ppc-cache");
    CHECK(ppc::config_dir() == fs::path("/tmp/ppc-cfg/PathOfPriceCheck"));
    CHECK(ppc::cache_dir() == fs::path("/tmp/ppc-cache/PathOfPriceCheck"));
}

TEST_CASE("unset XDG falls back under HOME") {
    set_env("XDG_CONFIG_HOME", nullptr);
    set_env("XDG_CACHE_HOME", nullptr);
    set_env("HOME", "/home/tester");
    CHECK(ppc::config_dir() == fs::path("/home/tester/.config/PathOfPriceCheck"));
    CHECK(ppc::cache_dir() == fs::path("/home/tester/.cache/PathOfPriceCheck"));
}

// Empty is not the same as set: an exported-but-blank XDG_CONFIG_HOME must not resolve
// to "/PathOfPriceCheck".
TEST_CASE("empty XDG is treated as unset") {
    set_env("XDG_CONFIG_HOME", "");
    set_env("HOME", "/home/tester");
    CHECK(ppc::config_dir() == fs::path("/home/tester/.config/PathOfPriceCheck"));
}

TEST_CASE("config and cache never collide") {
    set_env("XDG_CONFIG_HOME", nullptr);
    set_env("XDG_CACHE_HOME", nullptr);
    set_env("HOME", "/home/tester");
    CHECK(ppc::config_dir() != ppc::cache_dir());
}

TEST_CASE("display_path folds the home directory away") {
    set_env("HOME", "/home/tester");
    CHECK(ppc::display_path("/home/tester/.config/PathOfPriceCheck/config.json") ==
          "~/.config/PathOfPriceCheck/config.json");
    // A trailing slash on HOME must not leave "~//".
    set_env("HOME", "/home/tester/");
    CHECK(ppc::display_path("/home/tester/.config") == "~/.config");
}

TEST_CASE("display_path leaves anything not under home alone") {
    set_env("HOME", "/home/tester");
    // Somewhere else entirely — an XDG variable pointed outside the home directory.
    CHECK(ppc::display_path("/srv/ppc/config.json") == "/srv/ppc/config.json");
    // The prefix matches but the boundary does not: a sibling directory is not inside it.
    CHECK(ppc::display_path("/home/tester2/x") == "/home/tester2/x");
    // The home directory itself: nothing follows it, so there is nothing to shorten.
    CHECK(ppc::display_path("/home/tester") == "/home/tester");
    set_env("HOME", nullptr);
    CHECK(ppc::display_path("/home/tester/x") == "/home/tester/x");
}
#endif

TEST_CASE("file_url escapes what a URL reserves") {
    // The separator, the drive colon and the unreserved set survive; a space does not.
    CHECK(ppc::file_url("/home/t/My Games/config.json") ==
          "file:///home/t/My%20Games/config.json");
    CHECK(ppc::file_url("/home/t/a-b_c.d~e") == "file:///home/t/a-b_c.d~e");
    CHECK(ppc::file_url("/home/t/100%").ends_with("/100%25"));
    // Whatever the platform's separator, the URL's is a forward slash, and the drive letter
    // gets the third one rather than being read as a hostname.
    CHECK(ppc::file_url(fs::path("C:") / "Users" / "t").starts_with("file:///"));
}

TEST_CASE("ensure_dir creates nested directories and is idempotent") {
    const fs::path root = fs::temp_directory_path() / "ppc-paths-test";
    std::error_code ec;
    fs::remove_all(root, ec);

    const fs::path nested = root / "a" / "b";
    CHECK(ppc::ensure_dir(nested));
    CHECK(fs::is_directory(nested));
    CHECK(ppc::ensure_dir(nested)); // already there

    fs::remove_all(root, ec);
}

TEST_CASE("ensure_dir reports failure when the path is a file") {
    const fs::path root = fs::temp_directory_path() / "ppc-paths-test-file";
    std::error_code ec;
    fs::remove_all(root, ec);
    ppc::ensure_dir(root.parent_path());
    { std::ofstream(root.string()) << "x"; }

    CHECK_FALSE(ppc::ensure_dir(root));

    fs::remove_all(root, ec);
}
