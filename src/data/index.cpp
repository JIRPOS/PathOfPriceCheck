#include "data/index.hpp"

#include <cstring>

#include "util/fnv1a.hpp"

namespace ppc::data {
namespace {

constexpr size_t kRowBytes = 8;

/// The mapping is byte-aligned only, so read fields rather than casting to uint32_t*.
/// memcpy of 4 bytes compiles to a single load; a misaligned cast is undefined behaviour
/// that ubsan flags and some targets fault on.
uint32_t row_hash(const uint8_t* rows, size_t i) {
    uint32_t v;
    std::memcpy(&v, rows + i * kRowBytes, 4);
    return v;
}

uint32_t row_offset(const uint8_t* rows, size_t i) {
    uint32_t v;
    std::memcpy(&v, rows + i * kRowBytes + 4, 4);
    return v;
}

} // namespace

bool HashIndex::attach(const uint8_t* base, size_t bytes) {
    rows_ = nullptr;
    count_ = 0;
    if (!base || bytes == 0 || bytes % kRowBytes != 0) return false;
    rows_ = base;
    count_ = bytes / kRowBytes;
    return true;
}

void HashIndex::lookup(uint32_t hash, std::vector<uint32_t>& out) const {
    if (!rows_) return;
    // Lower bound on the hash column, then walk the equal-hash run.
    size_t lo = 0, hi = count_;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (row_hash(rows_, mid) < hash)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (size_t i = lo; i < count_ && row_hash(rows_, i) == hash; ++i)
        out.push_back(row_offset(rows_, i));
}

void HashIndex::lookup(std::string_view key, std::vector<uint32_t>& out) const {
    lookup(fnv1a32(key), out);
}

} // namespace ppc::data
