#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ppc::data {

/// Reader for a published `*.index.bin`.
///
/// Layout: a flat little-endian array of `[u32 fnv1a32(key), u32 byte_offset]` rows sorted
/// ascending by hash. Little-endian is declared, not detected — both targets are x86-64.
///
/// Unlike Awakened's format, colliding keys are kept as consecutive rows rather than one
/// silently overwriting the other, so a lookup returns a *run* of candidate offsets. The
/// caller must still re-verify each candidate's key: fnv1a32 collides, and a run can mix
/// distinct keys.
class HashIndex {
public:
    /// False if the blob is empty or not a whole number of 8-byte rows.
    bool attach(const uint8_t* base, size_t bytes);

    bool valid() const { return rows_ != nullptr; }
    size_t size() const { return count_; }

    /// Appends every offset whose row hash matches. Does not clear `out`.
    void lookup(uint32_t hash, std::vector<uint32_t>& out) const;
    void lookup(std::string_view key, std::vector<uint32_t>& out) const;

private:
    const uint8_t* rows_ = nullptr; ///< unaligned: the mapping offers no alignment guarantee
    size_t count_ = 0;
};

} // namespace ppc::data
