#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "data/index.hpp"
#include "data/mapped_file.hpp"
#include "util/fnv1a.hpp"

namespace fs = std::filesystem;
using ppc::fnv1a32;
using ppc::data::HashIndex;
using ppc::data::MappedFile;

namespace {

/// Build a blob the way the Python emitter does: rows sorted ascending by hash.
std::vector<uint8_t> make_index(std::vector<std::pair<std::string, uint32_t>> pairs) {
    std::vector<std::pair<uint32_t, uint32_t>> rows;
    for (auto& [k, off] : pairs) rows.emplace_back(fnv1a32(k), off);
    std::sort(rows.begin(), rows.end());
    std::vector<uint8_t> blob(rows.size() * 8);
    for (size_t i = 0; i < rows.size(); ++i) {
        std::memcpy(blob.data() + i * 8, &rows[i].first, 4);
        std::memcpy(blob.data() + i * 8 + 4, &rows[i].second, 4);
    }
    return blob;
}

std::vector<uint32_t> find(const HashIndex& idx, std::string_view key) {
    std::vector<uint32_t> out;
    idx.lookup(key, out);
    return out;
}

} // namespace

TEST_CASE("attach rejects a blob that is not whole rows") {
    HashIndex idx;
    const uint8_t buf[9]{};
    CHECK_FALSE(idx.attach(buf, 9));
    CHECK_FALSE(idx.attach(buf, 0));
    CHECK_FALSE(idx.attach(nullptr, 8));
    CHECK_FALSE(idx.valid());
    CHECK(idx.attach(buf, 8));
    CHECK(idx.valid());
}

TEST_CASE("lookup finds keys and misses cleanly") {
    const auto blob = make_index({{"alpha", 0}, {"beta", 40}, {"gamma", 80}});
    HashIndex idx;
    REQUIRE(idx.attach(blob.data(), blob.size()));
    CHECK(idx.size() == 3);

    CHECK(find(idx, "alpha") == std::vector<uint32_t>{0});
    CHECK(find(idx, "beta") == std::vector<uint32_t>{40});
    CHECK(find(idx, "gamma") == std::vector<uint32_t>{80});
    CHECK(find(idx, "delta").empty());
    CHECK(find(idx, "").empty());
}

TEST_CASE("first and last rows are reachable") {
    // A binary search that is off by one at either end is the classic failure here.
    std::vector<std::pair<std::string, uint32_t>> pairs;
    for (int i = 0; i < 200; ++i) pairs.emplace_back("key" + std::to_string(i), i * 8);
    const auto blob = make_index(pairs);
    HashIndex idx;
    REQUIRE(idx.attach(blob.data(), blob.size()));
    for (auto& [k, off] : pairs) CHECK(find(idx, k) == std::vector<uint32_t>{off});
}

TEST_CASE("a colliding hash returns the whole run") {
    // Two distinct keys forced onto one hash: both offsets must come back, because the
    // caller re-verifies. Awakened's format would have dropped one.
    const uint32_t h = fnv1a32("shared");
    std::vector<uint8_t> blob(3 * 8);
    const uint32_t offs[3] = {10, 20, 30};
    const uint32_t hashes[3] = {h - 1, h, h};
    for (int i = 0; i < 3; ++i) {
        std::memcpy(blob.data() + i * 8, &hashes[i], 4);
        std::memcpy(blob.data() + i * 8 + 4, &offs[i], 4);
    }
    HashIndex idx;
    REQUIRE(idx.attach(blob.data(), blob.size()));
    CHECK(find(idx, "shared") == std::vector<uint32_t>{20, 30});
}

TEST_CASE("lookup appends rather than replacing") {
    const auto blob = make_index({{"a", 1}, {"b", 2}});
    HashIndex idx;
    REQUIRE(idx.attach(blob.data(), blob.size()));
    std::vector<uint32_t> out{99};
    idx.lookup("a", out);
    idx.lookup("b", out);
    CHECK(out == std::vector<uint32_t>{99, 1, 2});
}

TEST_CASE("MappedFile maps a real file and reports its bytes") {
    const fs::path p = fs::temp_directory_path() / "ppc-mapped-file-test.bin";
    const std::string payload = "line one\nline two\n";
    { std::ofstream(p, std::ios::binary) << payload; }

    MappedFile mf;
    REQUIRE(mf.open(p));
    CHECK(mf.valid());
    CHECK(mf.size() == payload.size());
    CHECK(mf.view() == payload);

    // Moving must not double-unmap.
    MappedFile moved = std::move(mf);
    CHECK(moved.view() == payload);
    CHECK_FALSE(mf.valid());

    std::error_code ec;
    fs::remove(p, ec);
}

TEST_CASE("MappedFile refuses what it cannot map") {
    MappedFile mf;
    CHECK_FALSE(mf.open(fs::temp_directory_path() / "ppc-does-not-exist.bin"));

    // An empty file has no bytes to map and must be reported as a failure, not as a
    // zero-length success that later reads walk off the end of.
    const fs::path empty = fs::temp_directory_path() / "ppc-empty-test.bin";
    { std::ofstream(empty, std::ios::binary); }
    CHECK_FALSE(mf.open(empty));
    std::error_code ec;
    fs::remove(empty, ec);
}
