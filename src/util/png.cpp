#include "util/png.hpp"

#include <cstring>

#include <zlib.h>

namespace ppc {
namespace {

constexpr uint8_t kSignature[]{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

void put_be32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

/// One chunk: length, type, payload, and a CRC over the type and payload but **not** the length.
void chunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& body) {
    put_be32(out, static_cast<uint32_t>(body.size()));
    const size_t crc_from = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    put_be32(out, static_cast<uint32_t>(
                      crc32(crc32(0, nullptr, 0), out.data() + crc_from,
                            static_cast<uInt>(out.size() - crc_from))));
}

/// The raw stream deflate is handed: every row prefixed by its filter type.
///
/// **Sub for every row**, which stores each byte as its difference from the pixel to its left. A
/// panel is mostly flat horizontal runs, and Sub turns those into runs of zeros before deflate
/// ever sees them — the difference on a real capture is several times the file size. Choosing per
/// row would do a little better again and is not worth the code for one picture a user sends by
/// hand.
std::vector<uint8_t> filtered(const uint8_t* rgba, int w, int h) {
    const size_t stride = static_cast<size_t>(w) * 4;
    std::vector<uint8_t> raw(static_cast<size_t>(h) * (stride + 1));
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = rgba + static_cast<size_t>(y) * stride;
        uint8_t* dst = raw.data() + static_cast<size_t>(y) * (stride + 1);
        *dst++ = 1; // Sub
        // The first pixel of a row has nothing to its left, which the format defines as zero.
        for (size_t x = 0; x < stride; ++x) dst[x] = static_cast<uint8_t>(src[x] - (x < 4 ? 0 : src[x - 4]));
    }
    return raw;
}

} // namespace

std::vector<uint8_t> encode_png(const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return {};

    const std::vector<uint8_t> raw = filtered(rgba, w, h);
    uLongf packed = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> idat(packed);
    if (compress2(idat.data(), &packed, raw.data(), static_cast<uLong>(raw.size()),
                  Z_DEFAULT_COMPRESSION) != Z_OK)
        return {};
    idat.resize(packed);

    std::vector<uint8_t> ihdr;
    put_be32(ihdr, static_cast<uint32_t>(w));
    put_be32(ihdr, static_cast<uint32_t>(h));
    ihdr.push_back(8); // bits per channel
    ihdr.push_back(6); // truecolour with alpha
    ihdr.push_back(0); // deflate, the only compression the format has
    ihdr.push_back(0); // adaptive filtering, the only filter method the format has
    ihdr.push_back(0); // not interlaced

    std::vector<uint8_t> out(std::begin(kSignature), std::end(kSignature));
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", idat);
    chunk(out, "IEND", {});
    return out;
}

} // namespace ppc
