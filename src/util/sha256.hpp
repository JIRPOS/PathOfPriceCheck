#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ppc {

/// Streaming SHA-256, so a multi-megabyte download is hashed as it is written to disk
/// rather than buffered whole and hashed afterwards.
class Sha256 {
public:
    Sha256() { reset(); }
    void reset();
    void update(const void* data, size_t len);
    void update(std::string_view s) { update(s.data(), s.size()); }
    std::array<uint8_t, 32> digest(); ///< finalises; the object must be reset to reuse
    std::string hex();                ///< lowercase, 64 chars

private:
    void compress(const uint8_t block[64]);

    uint32_t state_[8]{};
    uint64_t bits_ = 0;
    uint8_t buf_[64]{};
    size_t buffered_ = 0;
};

std::string sha256_hex(std::string_view s);

} // namespace ppc
