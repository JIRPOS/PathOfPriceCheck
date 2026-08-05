#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "paths.hpp"
#include "util/debug_log.hpp"

namespace fs = std::filesystem;

namespace {

fs::path scratch() {
    const fs::path p = fs::temp_directory_path() / "ppc-debug-log-test";
    std::error_code ec;
    fs::remove_all(p, ec);
#ifdef _WIN32
    _putenv_s("LOCALAPPDATA", p.string().c_str());
#else
    setenv("XDG_CACHE_HOME", p.string().c_str(), 1);
#endif
    return p;
}

std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("the log is off until it is turned on") {
    scratch();
    CHECK_FALSE(ppc::debug::enabled());
    ppc::debug::log("this must not open a file");
    CHECK(ppc::debug::log_path().empty());
}

TEST_CASE("enabling writes a file that carries the check id and the exact bytes") {
    scratch();
    ppc::debug::set_enabled(true);
    REQUIRE(ppc::debug::enabled());
    const std::string id = ppc::debug::begin_check();
    CHECK(id.size() == 4);
    CHECK(ppc::debug::check_id() == id);

    // Trailing whitespace and CRLF are exactly what a digest and base64 are here to preserve:
    // a log that trims them cannot answer "did the game hand us the same bytes twice".
    ppc::debug::log_text("clipboard", "Rarity: Rare\r\n  \n");
    const fs::path path = ppc::debug::log_path();
    REQUIRE_FALSE(path.empty());
    const std::string body = read_all(path);
    CHECK(body.find("[" + id + "]") != std::string::npos);
    CHECK(body.find("17 bytes") != std::string::npos);
    CHECK(body.find("clipboard.b64: UmFyaXR5OiBSYXJlDQogIAo=") != std::string::npos);
    ppc::debug::set_enabled(false);
}

TEST_CASE("each price check gets its own id") {
    scratch();
    ppc::debug::set_enabled(true);
    std::set<std::string> ids;
    for (int i = 0; i < 200; ++i) ids.insert(ppc::debug::begin_check());
    // Not a uniqueness guarantee — 20 bits of id — but consecutive ones must differ, which is
    // what a user comparing two checks in a row is relying on.
    CHECK(ids.size() > 190);
    ppc::debug::set_enabled(false);
}

TEST_CASE("digests distinguish texts that differ only in whitespace") {
    CHECK(ppc::debug::digest("a b") != ppc::debug::digest("a  b"));
    CHECK(ppc::debug::digest("x\n") != ppc::debug::digest("x"));
    CHECK(ppc::debug::digest("x") == ppc::debug::digest("x"));
}

TEST_CASE("old runs are pruned, the newest are kept") {
    const fs::path root = scratch();
    const fs::path dir = root / "PathOfPriceCheck" / "logs";
    ppc::ensure_dir(dir);
    for (int i = 0; i < 14; ++i) {
        char name[64];
        std::snprintf(name, sizeof name, "ppc-20200101-0000%02d.log", i);
        std::ofstream(dir / name) << "old\n";
    }
    ppc::debug::set_enabled(true);
    ppc::debug::set_enabled(false);

    int count = 0;
    bool kept_newest = false;
    for (const fs::directory_entry& e : fs::directory_iterator(dir)) {
        ++count;
        kept_newest = kept_newest || e.path().filename() == "ppc-20200101-000013.log";
    }
    CHECK(count == 10);
    CHECK(kept_newest);
}
