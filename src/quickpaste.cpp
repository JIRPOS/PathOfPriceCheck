#include "quickpaste.hpp"

#include <algorithm>
#include <cctype>

namespace ppc {
namespace {

bool space(unsigned char c) { return std::isspace(c) != 0; }

/// Byte offset of the `n`th character, or the whole string when it has fewer. Continuation
/// bytes (10xxxxxx) are not characters, which is the entire rule.
size_t utf8_offset(std::string_view s, size_t n) {
    size_t chars = 0, i = 0;
    for (; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) continue;
        if (chars == n) return i;
        ++chars;
    }
    return i;
}

} // namespace

size_t enabled_pastes(const std::vector<Paste>& list) {
    return static_cast<size_t>(std::count_if(list.begin(), list.end(),
                                             [](const Paste& p) { return p.enabled; }));
}

std::vector<size_t> active_pastes(const std::vector<Paste>& list) {
    std::vector<size_t> out;
    for (size_t i = 0; i < list.size() && out.size() < kMaxActivePastes; ++i)
        if (list[i].enabled) out.push_back(i);
    return out;
}

size_t limit_enabled(std::vector<Paste>& list) {
    size_t seen = 0, turned_off = 0;
    for (Paste& p : list) {
        if (!p.enabled) continue;
        if (seen < kMaxActivePastes) {
            ++seen;
            continue;
        }
        p.enabled = false;
        ++turned_off;
    }
    return turned_off;
}

bool move_paste(std::vector<Paste>& list, size_t from, size_t to) {
    if (from == to || from >= list.size() || to >= list.size()) return false;
    Paste moved = std::move(list[from]);
    list.erase(list.begin() + static_cast<ptrdiff_t>(from));
    list.insert(list.begin() + static_cast<ptrdiff_t>(to), std::move(moved));
    return true;
}

std::string paste_preview(std::string_view body, size_t max_chars) {
    std::string out;
    bool gap = false;
    for (const char ch : body) {
        if (space(static_cast<unsigned char>(ch))) {
            gap = !out.empty();
            continue;
        }
        if (gap) out += ' ';
        gap = false;
        out += ch;
        // One character past the budget, so a body that only just fits is not given an
        // ellipsis it does not need.
        if (out.size() > max_chars * 4) break; // 4 bytes is the longest UTF-8 character
    }
    const size_t cut = utf8_offset(out, max_chars);
    if (cut < out.size()) {
        out.resize(cut);
        out += "\xe2\x80\xa6";
    }
    return out;
}

} // namespace ppc
