#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include "util/fnv1a.hpp"
#include "util/sha256.hpp"

using ppc::fnv1a32;
using ppc::Sha256;
using ppc::sha256_hex;

TEST_CASE("fnv1a32 reference vectors") {
    CHECK(fnv1a32("") == 0x811C9DC5u);
    CHECK(fnv1a32("a") == 0xE40C292Cu);
    CHECK(fnv1a32("foobar") == 0xBF9CF968u);
}

// Produced by the builder's own Python fnv1a32 over real index keys. If these drift, the
// client cannot find anything in the published indices — and would fail by returning no
// match rather than by crashing, which is why they are pinned here.
TEST_CASE("fnv1a32 agrees with the builder on real keys") {
    CHECK(fnv1a32("# to maximum Life") == 1765587445u);
    CHECK(fnv1a32("#% increased Physical Damage") == 4081531279u);
    CHECK(fnv1a32("ITEM::Two-Stone Ring") == 1221230806u);
    CHECK(fnv1a32("No Physical Damage") == 738583476u);
}

TEST_CASE("fnv1a32 is usable in a constant expression") {
    static_assert(fnv1a32("") == 0x811C9DC5u);
    static_assert(fnv1a32("a") == 0xE40C292Cu);
}

TEST_CASE("sha256 NIST vectors") {
    CHECK(sha256_hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("sha256 handles the length-field boundary") {
    // 55 and 56 bytes straddle the point where the padding needs a second block.
    CHECK(sha256_hex(std::string(55, 'a')) ==
          "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(sha256_hex(std::string(56, 'a')) ==
          "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(sha256_hex(std::string(64, 'a')) ==
          "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

// The download path feeds the hasher in whatever chunks the socket delivers, so a chunked
// update must produce the same digest as one big one.
TEST_CASE("streaming in odd chunks matches a single update") {
    const std::string data(1 << 20, 'x');
    const std::string once = sha256_hex(data);

    Sha256 h;
    for (size_t off = 0, n = 1; off < data.size(); off += n, n = n * 3 + 1) {
        h.update(data.data() + off, std::min(n, data.size() - off));
    }
    CHECK(h.hex() == once);
}

TEST_CASE("reset makes the object reusable") {
    Sha256 h;
    h.update("abc");
    CHECK(h.hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    h.reset();
    h.update("abc");
    CHECK(h.hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
