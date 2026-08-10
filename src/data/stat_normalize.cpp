#include "data/stat_normalize.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <optional>

// See NORMALIZATION.md in the data repository — that document is normative and this file
// must reproduce it exactly. Divergence does not crash: it silently fails to match a mod,
// so the price check succeeds with the wrong answer. normalize_test checks this against the
// conformance vectors shipped in every data release.

namespace ppc::data {
namespace {

constexpr size_t kMaxTokens = 4;

/// Which token indices stay '#', most generic first, indexed by token count.
constexpr std::array<uint32_t, 1> kMasks0{0b0000};
constexpr std::array<uint32_t, 2> kMasks1{0b0001, 0b0000};
constexpr std::array<uint32_t, 4> kMasks2{0b0011, 0b0001, 0b0010, 0b0000};
// Three tokens deliberately has no empty mask: a wording with three numbers and none
// placeheld is not a form the data contains.
constexpr std::array<uint32_t, 7> kMasks3{0b0111, 0b0110, 0b0101, 0b0011,
                                          0b0100, 0b0010, 0b0001};
constexpr std::array<uint32_t, 11> kMasks4{0b1111, 0b1110, 0b1101, 0b1011, 0b0111, 0b1100,
                                           0b1010, 0b1001, 0b0110, 0b0101, 0b0011};

std::span<const uint32_t> masks_for(size_t n) {
    switch (n) {
    case 0: return kMasks0;
    case 1: return kMasks1;
    case 2: return kMasks2;
    case 3: return kMasks3;
    default: return kMasks4;
    }
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

std::optional<double> to_double(std::string_view s) {
    // from_chars does not accept a leading '+' for floating point, and mod text is full of
    // them ("+42 to maximum Life"). Strip it before parsing rather than silently yielding 0.
    if (!s.empty() && s.front() == '+') s.remove_prefix(1);
    if (s.empty()) return std::nullopt;
    double v = 0;
    const auto* first = s.data();
    const auto* last = s.data() + s.size();
    const auto res = std::from_chars(first, last, v);
    if (res.ec != std::errc{} || res.ptr != last) return std::nullopt;
    return v;
}

int fraction_digits(std::string_view s) {
    const size_t dot = s.find('.');
    if (dot == std::string_view::npos) return 0;
    return static_cast<int>(s.size() - dot - 1);
}

} // namespace

std::string strip_empty_parens(std::string_view line) {
    std::string out;
    out.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '(' && i + 1 < line.size() && line[i + 1] == ')') {
            ++i;
            continue;
        }
        out.push_back(line[i]);
    }
    return out;
}

std::string strip_named_ranges(std::string_view line) {
    std::string out;
    out.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '(') {
            out.push_back(line[i]);
            continue;
        }
        const size_t close = line.find(')', i);
        // A range that follows a number is that number's own, numeric bounds or not.
        const char prev = out.empty() ? '\0' : out.back();
        if (close == std::string_view::npos || is_digit(prev) || prev == ')') {
            out.push_back(line[i]);
            continue;
        }
        const std::string_view inner = line.substr(i + 1, close - i - 1);
        // The same split as a numeric range: one arbitrary character, then the separator.
        const size_t sep = inner.size() > 1 ? inner.find('-', 1) : std::string_view::npos;
        if (sep == std::string_view::npos || sep + 1 >= inner.size() ||
            to_double(inner.substr(0, sep)) || to_double(inner.substr(sep + 1))) {
            out.push_back(line[i]);
            continue;
        }
        if (prev == ' ') out.pop_back();
        i = close;
    }
    return out;
}

std::vector<NumberToken> scan_numbers(std::string_view s) {
    std::vector<NumberToken> out;
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const char c = s[i];
        const bool signed_start = c == '+' || c == '-';
        if (!signed_start && !is_digit(c)) {
            ++i;
            continue;
        }
        if (signed_start && (i + 1 >= n || !is_digit(s[i + 1]))) {
            ++i;
            continue;
        }
        // The lookbehind. Without it "1-30" splits into two tokens.
        if (i > 0 && (is_digit(s[i - 1]) || s[i - 1] == ')')) {
            ++i;
            continue;
        }

        NumberToken t;
        t.begin = i;
        if (signed_start) ++i;
        while (i < n && is_digit(s[i])) ++i;
        if (i + 1 < n && s[i] == '.' && is_digit(s[i + 1])) {
            ++i;
            const size_t frac = i;
            while (i < n && is_digit(s[i])) ++i;
            t.decimals = static_cast<int>(i - frac);
        }
        t.value_end = i;
        t.end = i;
        t.value = to_double(s.substr(t.begin, t.value_end - t.begin)).value_or(0.0);

        if (i < n && s[i] == '(') {
            const size_t close = s.find(')', i);
            if (close != std::string_view::npos) {
                const std::string_view inner = s.substr(i + 1, close - i - 1);
                // The minimum takes one arbitrary character, then characters that are
                // neither ')' nor '-'. That exemption is what makes "(-20-10)" read as
                // min -20, max 10 rather than splitting at the leading minus.
                std::string_view lo = inner, hi = inner;
                if (inner.size() > 1) {
                    const size_t sep = inner.find('-', 1);
                    if (sep != std::string_view::npos) {
                        lo = inner.substr(0, sep);
                        hi = inner.substr(sep + 1);
                    }
                }
                t.has_bounds = true;
                t.end = close + 1;
                const auto lo_v = to_double(lo);
                const auto hi_v = to_double(hi);
                if (lo_v && hi_v) {
                    t.numeric_bounds = true;
                    // The game prints an inverse wording's range high to low —
                    // "64(65-60)% reduced Effect of Curses on you during Effect" — and every
                    // consumer reads these as an ordered pair. Left as printed, that becomes a
                    // trade filter wanting at least -60 and at most -65, which matches nothing.
                    t.bound_min = std::min(*lo_v, *hi_v);
                    t.bound_max = std::max(*lo_v, *hi_v);
                    t.decimals = std::max({t.decimals, fraction_digits(lo),
                                           fraction_digits(hi)});
                }
                i = close + 1;
            }
        }
        out.push_back(t);
    }
    return out;
}

std::string apply_candidate(std::string_view line, std::span<const NumberToken> tokens,
                            uint32_t keep) {
    std::string out;
    out.reserve(line.size());
    size_t cursor = 0;
    for (size_t idx = 0; idx < tokens.size(); ++idx) {
        const NumberToken& t = tokens[idx];
        out.append(line.substr(cursor, t.begin - cursor));
        if (keep & (1u << idx)) {
            out.push_back('#');
            if (t.has_bounds && !t.numeric_bounds)
                out.append(line.substr(t.value_end, t.end - t.value_end));
        } else {
            out.append(line.substr(t.begin, t.value_end - t.begin));
        }
        cursor = t.end;
    }
    out.append(line.substr(cursor));
    return out;
}

std::vector<std::string> candidates(std::string_view line) {
    std::vector<std::string> out;
    const auto push_unique = [&out](std::string c) {
        if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(std::move(c));
    };
    const auto enumerate = [&](const std::string& text) {
        std::vector<NumberToken> tokens = scan_numbers(text);
        if (tokens.size() > kMaxTokens) tokens.resize(kMaxTokens);
        for (uint32_t keep : masks_for(tokens.size()))
            push_unique(apply_candidate(text, tokens, keep));
        push_unique(text); // last resort, so a wording with no numbers still resolves
    };

    const std::string text = strip_empty_parens(line);
    enumerate(text);
    // Only then the named-range form, so a wording whose parenthesis is part of it —
    // "Unique Monsters (Blood-Filled Vessel): #" — resolves as printed.
    const std::string named = strip_named_ranges(text);
    if (named != text) enumerate(named);
    return out;
}

std::string placeholder_form(std::string_view line) {
    const std::string text = strip_empty_parens(line);
    const std::vector<NumberToken> tokens = scan_numbers(text);
    const uint32_t all = tokens.size() >= 32 ? ~0u : (1u << tokens.size()) - 1;
    return apply_candidate(text, tokens, all);
}

} // namespace ppc::data
