#include "mapcheck/filter.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <regex>

namespace ppc::mapcheck {

bool asks_about_item(std::string_view term) {
    for (size_t i = 0; i < term.size(); ++i) {
        const char c = term[i];
        if (c == ':') return i > 0;
        // Letters and spaces only, so a pattern that merely contains a colon — `(?:…)`, a
        // character class — is not mistaken for a keyword.
        if (!std::isalpha(static_cast<unsigned char>(c)) && c != ' ') return false;
    }
    return false;
}

std::vector<SearchTerm> parse_search(std::string_view s) {
    std::vector<SearchTerm> out;
    size_t i = 0;
    while (i < s.size()) {
        if (std::isspace(static_cast<unsigned char>(s[i]))) { ++i; continue; }
        SearchTerm t;
        // Outside the quotes as well as inside: a generator writes `"!a|b"` and a player types
        // `!"a b"`, and both mean the same thing.
        if (s[i] == '!') { t.negated = true; ++i; }
        if (i < s.size() && s[i] == '"') {
            const size_t end = s.find('"', ++i);
            // Unterminated runs to the end rather than being dropped: the string is being
            // typed, and a filter that empties itself on the opening quote is unusable.
            t.text = std::string(s.substr(i, end == std::string_view::npos ? end : end - i));
            i = end == std::string_view::npos ? s.size() : end + 1;
        } else {
            const size_t end = std::min(s.find(' ', i), std::min(s.find('\t', i), s.find('\n', i)));
            t.text = std::string(s.substr(i, end == std::string_view::npos ? end : end - i));
            i = end == std::string_view::npos ? s.size() : end;
        }
        if (!t.negated && !t.text.empty() && t.text.front() == '!') {
            t.negated = true;
            t.text.erase(t.text.begin());
        }
        if (!t.text.empty()) out.push_back(std::move(t));
    }
    return out;
}

struct SearchFilter::Impl {
    struct Term {
        std::regex re;
        bool valid = true; ///< false when the term would not compile, and it then hits nothing
        bool negated = false;

        bool hits(std::span<const std::string> lines) const;
    };
    std::vector<Term> terms;
    size_t set_aside = 0;
};

bool SearchFilter::Impl::Term::hits(std::span<const std::string> lines) const {
    if (!valid) return false;
    for (const std::string& line : lines) {
        // A pattern that compiles can still exhaust the stack on a pathological input;
        // std::regex reports that as an exception, and one term is not worth the run.
        try {
            if (std::regex_search(line, re)) return true;
        } catch (const std::regex_error&) {
        }
    }
    return false;
}

SearchFilter::SearchFilter() : impl_(std::make_unique<Impl>()) {}

SearchFilter::SearchFilter(std::string_view s) : impl_(std::make_unique<Impl>()) {
    for (SearchTerm& t : parse_search(s)) {
        if (asks_about_item(t.text)) {
            ++impl_->set_aside;
            continue;
        }
        Impl::Term term;
        term.negated = t.negated;
        try {
            term.re = std::regex(t.text, std::regex::ECMAScript | std::regex::icase |
                                             std::regex::optimize);
        } catch (const std::regex_error&) {
            // Nothing, rather than the literal text it is made of: see the header. A half-typed
            // pattern emptying the list is the same thing the game's own box does, and it is
            // the price of the box having one syntax instead of two.
            term.valid = false;
        }
        impl_->terms.push_back(std::move(term));
    }
}

SearchFilter::SearchFilter(SearchFilter&&) noexcept = default;
SearchFilter& SearchFilter::operator=(SearchFilter&&) noexcept = default;
SearchFilter::~SearchFilter() = default;

bool SearchFilter::empty() const { return impl_->terms.empty(); }
size_t SearchFilter::size() const { return impl_->terms.size(); }
size_t SearchFilter::set_aside() const { return impl_->set_aside; }

bool SearchFilter::matches(std::span<const std::string> lines) const {
    for (const Impl::Term& t : impl_->terms)
        if (t.hits(lines) == t.negated) return false;
    return true;
}

SearchFilter::Hit SearchFilter::classify(std::span<const std::string> lines) const {
    bool wanted = false;
    for (const Impl::Term& t : impl_->terms) {
        if (!t.hits(lines)) continue;
        if (t.negated) return Hit::Unwanted; // the strongest thing a search string says
        wanted = true;
    }
    return wanted ? Hit::Wanted : Hit::None;
}

std::string render_wording(std::string_view ref, std::optional<double> value) {
    if (!value || ref.find('#') == std::string_view::npos) return std::string(ref);
    char buf[32];
    // Trailing zeros off: the game prints "20% increased", never "20.00% increased", and a
    // term ending in `0%$` would match the wrong one of the two.
    if (*value == static_cast<long long>(*value))
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(*value));
    else
        std::snprintf(buf, sizeof buf, "%.2f", *value);
    std::string out;
    out.reserve(ref.size() + 8);
    for (const char c : ref) {
        if (c == '#') out += buf;
        else out += c;
    }
    return out;
}

std::string printed_wording(const data::Stat* rec, std::string_view ref,
                            std::optional<double> value) {
    if (!rec || rec->matchers.empty()) return render_wording(ref, value);
    // Below zero the game says the stat the other way round, which is exactly what a `negate`
    // matcher is. At or above it, the plain wording.
    const bool below = value && *value < 0;
    for (const data::StatMatcher& m : rec->matchers) {
        if (m.negate != below) continue;
        return render_wording(m.string, below ? std::optional<double>(-*value) : value);
    }
    return render_wording(ref, value);
}

std::string display_wording(const data::Stat* rec, const data::PoolStat& s) {
    if (!rec || !s.min || !s.max || *s.min >= 0 || *s.max >= 0) return s.ref;
    for (const data::StatMatcher& m : rec->matchers)
        if (m.negate) return m.string;
    return s.ref;
}

namespace {

/// Every line one wording can print, at either end of what it rolls.
void push_wordings(std::vector<std::string>& out, const data::PoolStat& s, const data::Stat* rec) {
    out.push_back(printed_wording(rec, s.ref, s.max));
    if (!s.min || s.min == s.max) return;
    std::string lo = printed_wording(rec, s.ref, s.min);
    if (lo != out.back()) out.push_back(std::move(lo));
}

} // namespace

std::vector<std::string> matchable_lines(const data::PoolMod& m,
                                         std::span<const data::Stat* const> recs) {
    std::vector<std::string> out;
    out.reserve(m.stats.size() * 2 + 1);
    for (size_t i = 0; i < m.stats.size(); ++i)
        push_wordings(out, m.stats[i], i < recs.size() ? recs[i] : nullptr);
    if (!m.name.empty()) out.push_back(m.name);
    return out;
}

std::vector<std::string> matchable_lines(const data::PoolMod& m, const data::GameData* gd) {
    std::vector<const data::Stat*> recs;
    recs.reserve(m.stats.size());
    for (const data::PoolStat& s : m.stats)
        recs.push_back(gd ? gd->find_stat_by_ref(s.ref) : nullptr);
    return matchable_lines(m, std::span<const data::Stat* const>(recs));
}

} // namespace ppc::mapcheck
