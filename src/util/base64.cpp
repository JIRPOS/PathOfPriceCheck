#include "util/base64.hpp"

#include <array>
#include <cstdint>

namespace ppc {
namespace {

constexpr char kAlpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// Reverse alphabet: index by byte, -1 for anything that is not a base64 digit.
constexpr std::array<int8_t, 256> reverse_alphabet() {
    std::array<int8_t, 256> t{};
    for (int8_t& v : t) v = -1;
    for (int i = 0; i < 64; ++i) t[static_cast<uint8_t>(kAlpha[i])] = static_cast<int8_t>(i);
    return t;
}

constexpr std::array<int8_t, 256> kReverse = reverse_alphabet();

bool is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

} // namespace

std::string base64_encode(std::string_view in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = (static_cast<uint8_t>(in[i]) << 16) |
                           (static_cast<uint8_t>(in[i + 1]) << 8) | static_cast<uint8_t>(in[i + 2]);
        out += kAlpha[(v >> 18) & 63];
        out += kAlpha[(v >> 12) & 63];
        out += kAlpha[(v >> 6) & 63];
        out += kAlpha[v & 63];
    }
    if (i < in.size()) {
        uint32_t v = static_cast<uint32_t>(static_cast<uint8_t>(in[i])) << 16;
        const bool two = i + 1 < in.size();
        if (two) v |= static_cast<uint32_t>(static_cast<uint8_t>(in[i + 1])) << 8;
        out += kAlpha[(v >> 18) & 63];
        out += kAlpha[(v >> 12) & 63];
        out += two ? kAlpha[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::optional<std::string> base64_decode(std::string_view in) {
    std::string out;
    out.reserve(in.size() / 4 * 3);
    uint32_t acc = 0;
    int bits = 0;
    bool padded = false;
    for (const unsigned char c : in) {
        if (is_space(c)) continue;
        if (c == '=') { // only ever the tail; digits after it are a corrupt payload
            padded = true;
            continue;
        }
        if (padded) return std::nullopt;
        const int8_t d = kReverse[c];
        if (d < 0) return std::nullopt;
        acc = (acc << 6) | static_cast<uint32_t>(d);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    // A leftover of 6 bits is one digit on its own, which encodes nothing.
    if (bits >= 6) return std::nullopt;
    return out;
}

} // namespace ppc
