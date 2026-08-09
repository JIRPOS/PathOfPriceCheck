#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <utility>

#include "platform/single_instance.hpp"

namespace fs = std::filesystem;

namespace {

void set_env(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v ? v : "");
#else
    setenv(k, v, 1);
#endif
}

/// The POSIX lock is a file under the cache directory, so the whole test runs against a
/// scratch one rather than the user's — a stale `PathOfPriceCheck.lock` in `~/.cache` from a
/// test run is a file the app then contends with for real. `cache_dir()` re-reads the
/// environment on every call, which is what makes this work.
fs::path scratch() {
    const fs::path p = fs::temp_directory_path() / "ppc-single-instance-test";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    set_env("XDG_CACHE_HOME", p.string().c_str());
    set_env("LOCALAPPDATA", p.string().c_str());
    return p;
}

// Distinct per case: the Windows half is a kernel object keyed on this name alone and ignores
// the scratch directory entirely, so two cases sharing a key would collide there and nowhere
// else — a failure that reproduces on one platform out of two.
constexpr const char* kKeyA = "ppc-test-instance-a";
constexpr const char* kKeyB = "ppc-test-instance-b";
constexpr const char* kKeyC = "ppc-test-instance-c";

} // namespace

TEST_CASE("a second claim on the same key is refused") {
    scratch();
    ppc::InstanceLock first(kKeyA);
    REQUIRE(first.held());

    // flock belongs to the open file description, not to the process, so this collides even
    // though it is the same process — which is the whole reason this is testable in-process.
    ppc::InstanceLock second(kKeyA);
    CHECK_FALSE(second.held());
}

TEST_CASE("releasing lets the next claim through") {
    scratch();
    {
        ppc::InstanceLock first(kKeyB);
        REQUIRE(first.held());
    }
    ppc::InstanceLock second(kKeyB);
    CHECK(second.held());
}

TEST_CASE("different keys do not contend") {
    scratch();
    ppc::InstanceLock a(kKeyB);
    ppc::InstanceLock b(kKeyC);
    CHECK(a.held());
    CHECK(b.held());
}

TEST_CASE("moving carries the claim and leaves nothing behind") {
    scratch();
    ppc::InstanceLock first(kKeyA);
    REQUIRE(first.held());

    ppc::InstanceLock moved = std::move(first);
    CHECK(moved.held());
    CHECK_FALSE(first.held()); // NOLINT(bugprone-use-after-move) — the point of the test

    // The moved-from object must not have released on the way out, or the claim would be gone.
    ppc::InstanceLock other(kKeyA);
    CHECK_FALSE(other.held());
}

TEST_CASE("a default-constructed lock holds nothing and releases nothing") {
    scratch();
    ppc::InstanceLock none;
    CHECK_FALSE(none.held());

    ppc::InstanceLock real(kKeyA);
    CHECK(real.held());
}

#ifndef _WIN32
TEST_CASE("the lock file names the process holding it") {
    const fs::path dir = scratch();
    ppc::InstanceLock lock(kKeyA);
    REQUIRE(lock.held());
    CHECK(fs::exists(dir / "PathOfPriceCheck" / (std::string(kKeyA) + ".lock")));
}
#endif
