#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "data/install.hpp"
#include "data/manifest.hpp"
#include "util/sha256.hpp"

namespace fs = std::filesystem;
using namespace ppc::data;

namespace {

fs::path scratch_root(const char* name) {
    const fs::path p = fs::temp_directory_path() / "ppc-install-test" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

std::string manifest_json(const std::string& version, const std::string& payload) {
    return R"({"schema_version":1,"data_version":")" + version +
           R"(","generated_at":"2026-08-03T00:00:00Z","game_patch":"3.29.1.2","files":[)"
           R"({"name":"en-items.ndjson","sha256":")" + ppc::sha256_hex(payload) +
           R"(","size":)" + std::to_string(payload.size()) +
           R"(,"encoding":"none","url":"https://example.invalid/en-items.ndjson"}]})";
}

} // namespace

TEST_CASE("asset names that would escape the cache directory are rejected") {
    CHECK(is_safe_asset_name("en-items.ndjson"));
    CHECK(is_safe_asset_name("manifest.json"));
    CHECK(is_safe_asset_name("a_b-c.1"));

    CHECK_FALSE(is_safe_asset_name(""));
    CHECK_FALSE(is_safe_asset_name("../etc/passwd"));
    CHECK_FALSE(is_safe_asset_name("a/../../b"));
    CHECK_FALSE(is_safe_asset_name("/etc/passwd"));
    CHECK_FALSE(is_safe_asset_name("..hidden"));
    CHECK_FALSE(is_safe_asset_name(".hidden"));
    CHECK_FALSE(is_safe_asset_name("C:\\windows\\system32"));
    CHECK_FALSE(is_safe_asset_name("dir/file.txt")); // no separators at all
    CHECK_FALSE(is_safe_asset_name("back\\slash"));
    CHECK_FALSE(is_safe_asset_name("space file"));
    CHECK_FALSE(is_safe_asset_name("http://x/y"));
}

TEST_CASE("a dataset this build does not read is still downloaded and installed") {
    // The updater is manifest-driven on purpose: the publisher adds assets within a schema
    // version and they arrive without an app release — fetched, hashed and installed whether
    // or not this build knows what they are. `en-items-base.index.bin` is the current example,
    // published and installed while only the unidentified-unique work will read it.
    const std::string payload = "{\"name\":\"Watcher's Eye\"}\n";
    const std::string body =
        R"({"schema_version":1,"data_version":"v1","files":[{"name":"en-unique-mods.ndjson",)"
        R"("sha256":")" + ppc::sha256_hex(payload) + R"(","size":)" +
        std::to_string(payload.size()) +
        R"(,"encoding":"none","url":"https://example.invalid/en-unique-mods.ndjson"}]})";

    Manifest m;
    std::string err;
    REQUIRE_MESSAGE(parse_manifest(body, m, &err), err);
    REQUIRE(m.find("en-unique-mods.ndjson") != nullptr);

    BundleStore store(scratch_root("unknown-asset"));
    REQUIRE_MESSAGE(store.stage("v1", m.files[0], payload, &err), err);
    REQUIRE_MESSAGE(store.commit(m, &err), err);
    CHECK(fs::exists(store.version_dir("v1") / "en-unique-mods.ndjson"));
}

TEST_CASE("a good manifest parses") {
    Manifest m;
    std::string err;
    REQUIRE_MESSAGE(parse_manifest(manifest_json("20260803.1", "hello"), m, &err), err);
    CHECK(m.data_version == "20260803.1");
    CHECK(m.files.size() == 1);
    CHECK(m.find("en-items.ndjson") != nullptr);
    CHECK(m.find("nope") == nullptr);
    CHECK(m.total_bytes() == 5);
}

TEST_CASE("manifests this build will not act on are refused") {
    Manifest m;
    std::string err;

    CHECK_FALSE(parse_manifest("not json", m, &err));
    CHECK_FALSE(parse_manifest("{}", m, &err));
    // A future schema must not be half-understood.
    CHECK_FALSE(parse_manifest(R"({"schema_version":2,"data_version":"1","files":[]})", m, &err));
    // No files is not a valid bundle.
    CHECK_FALSE(parse_manifest(R"({"schema_version":1,"data_version":"1","files":[]})", m, &err));

    const auto one = [](const std::string& body) {
        return std::string(R"({"schema_version":1,"data_version":"1","files":[)") + body + "]}";
    };
    const std::string sha(64, 'a');
    CHECK_FALSE(parse_manifest(one(R"({"name":"../x","sha256":")" + sha +
                                   R"(","size":1,"url":"https://x/y"})").c_str(), m, &err));
    CHECK_FALSE(parse_manifest(one(R"({"name":"x","sha256":"short","size":1,"url":"https://x/y"})"),
                               m, &err));
    CHECK_FALSE(parse_manifest(one(R"({"name":"x","sha256":")" + sha +
                                   R"(","size":0,"url":"https://x/y"})").c_str(), m, &err));
    // Plain http would let anyone on the path swap the bundle.
    CHECK_FALSE(parse_manifest(one(R"({"name":"x","sha256":")" + sha +
                                   R"(","size":1,"url":"http://x/y"})").c_str(), m, &err));
    // An encoding we cannot decode must be refused, not guessed at.
    CHECK_FALSE(parse_manifest(one(R"({"name":"x","sha256":")" + sha +
                                   R"(","size":1,"encoding":"gzip","url":"https://x/y"})").c_str(),
                               m, &err));
    // Absurd sizes must not be allowed to fill the disk.
    CHECK_FALSE(parse_manifest(one(R"({"name":"x","sha256":")" + sha +
                                   R"(","size":999999999,"url":"https://x/y"})").c_str(),
                               m, &err));
}

TEST_CASE("staging refuses bytes that do not match the manifest") {
    const fs::path root = scratch_root("stage");
    BundleStore store(root);
    Manifest m;
    std::string err;
    REQUIRE(parse_manifest(manifest_json("v1", "hello"), m, &err));
    const ManifestFile& f = m.files.front();

    CHECK_FALSE(store.stage("v1", f, "HELLO", &err)); // right length, wrong bytes
    CHECK(err.find("sha256") != std::string::npos);
    CHECK_FALSE(store.stage("v1", f, "hell", &err));  // wrong length
    CHECK_FALSE(fs::exists(store.staging_dir("v1") / f.name));

    CHECK(store.stage("v1", f, "hello", &err));
    CHECK(fs::exists(store.staging_dir("v1") / f.name));
}

TEST_CASE("commit installs a version and flips current") {
    const fs::path root = scratch_root("commit");
    BundleStore store(root);
    Manifest m;
    std::string err;
    REQUIRE(parse_manifest(manifest_json("v1", "hello"), m, &err));

    CHECK(store.current_version().empty());
    REQUIRE(store.stage("v1", m.files.front(), "hello", &err));
    REQUIRE_MESSAGE(store.commit(m, &err), err);

    CHECK(store.current_version() == "v1");
    CHECK(store.current_dir() == store.version_dir("v1"));
    CHECK(fs::exists(store.version_dir("v1") / "en-items.ndjson"));
    CHECK(fs::exists(store.version_dir("v1") / "manifest.json"));
    CHECK_FALSE(fs::exists(store.staging_dir("v1")));
}

TEST_CASE("commit refuses when staging is incomplete or tampered with") {
    const fs::path root = scratch_root("commit-bad");
    BundleStore store(root);
    Manifest m;
    std::string err;
    REQUIRE(parse_manifest(manifest_json("v1", "hello"), m, &err));

    CHECK_FALSE(store.commit(m, &err)); // nothing staged at all

    REQUIRE(store.stage("v1", m.files.front(), "hello", &err));
    // Disturb the staged file between staging and commit.
    { std::ofstream(store.staging_dir("v1") / "en-items.ndjson", std::ios::binary) << "HELLO"; }
    CHECK_FALSE(store.commit(m, &err));
    CHECK(store.current_version().empty());
}

TEST_CASE("a second version supersedes the first and prune reclaims it") {
    const fs::path root = scratch_root("supersede");
    BundleStore store(root);
    std::string err;

    Manifest v1, v2;
    REQUIRE(parse_manifest(manifest_json("v1", "hello"), v1, &err));
    REQUIRE(parse_manifest(manifest_json("v2", "goodbye"), v2, &err));

    REQUIRE(store.stage("v1", v1.files.front(), "hello", &err));
    REQUIRE(store.commit(v1, &err));
    REQUIRE(store.stage("v2", v2.files.front(), "goodbye", &err));
    REQUIRE(store.commit(v2, &err));

    CHECK(store.current_version() == "v2");
    // v1 survives the swap — it may still be mapped by a running instance.
    CHECK(fs::exists(store.version_dir("v1")));

    std::string removed;
    store.prune(&removed);
    CHECK(removed == "v1");
    CHECK_FALSE(fs::exists(store.version_dir("v1")));
    CHECK(fs::exists(store.version_dir("v2")));
    CHECK(store.current_version() == "v2");
}

TEST_CASE("prune clears abandoned staging directories") {
    const fs::path root = scratch_root("prune-staging");
    BundleStore store(root);
    Manifest m;
    std::string err;
    REQUIRE(parse_manifest(manifest_json("v1", "hello"), m, &err));
    REQUIRE(store.stage("v1", m.files.front(), "hello", &err)); // never committed

    store.prune();
    CHECK_FALSE(fs::exists(store.staging_dir("v1")));
}

TEST_CASE("a garbage current file does not point anywhere") {
    const fs::path root = scratch_root("bad-current");
    BundleStore store(root);
    { std::ofstream(root / "current") << "../../etc\n"; }
    CHECK(store.current_version().empty());
    CHECK(store.current_dir().empty());
}
