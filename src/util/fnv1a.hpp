#pragma once

#include <cstdint>
#include <string_view>

namespace ppc {

/// FNV-1a, 32-bit. The key function for the published lookup indices — the builder computes
/// the same values in Python, and `fnv1a-vectors.json` in every data release proves it.
constexpr uint32_t fnv1a32(std::string_view s) {
    uint32_t h = 0x811C9DC5u;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 0x01000193u;
    }
    return h;
}

} // namespace ppc
