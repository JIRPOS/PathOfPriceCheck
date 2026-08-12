#include "mapcheck/verdict.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ppc::mapcheck {
namespace {

constexpr std::array<std::string_view, 4> kIds{"unrated", "safe", "dangerous", "deadly"};

/// Names MS-DOS claimed and Windows still refuses, with or without an extension.
constexpr std::string_view kReserved[]{"CON", "PRN", "AUX", "NUL",  "COM1", "COM2", "COM3",
                                       "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1",
                                       "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8",
                                       "LPT9"};

bool forbidden(unsigned char c) {
    return c < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' ||
           c == '|' || c == '?' || c == '*' || c == 0x7F;
}

std::optional<double> number_or_none(const json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_number()) return std::nullopt;
    return j[key].get<double>();
}

} // namespace

std::string_view verdict_id(Verdict v) {
    const size_t i = static_cast<size_t>(v);
    return i < kIds.size() ? kIds[i] : kIds[0];
}

Verdict verdict_from_id(std::string_view s) {
    for (size_t i = 0; i < kIds.size(); ++i)
        if (s == kIds[i]) return static_cast<Verdict>(i);
    return Verdict::Unrated;
}

void Tally::add(Verdict v) {
    switch (v) {
    case Verdict::Safe: ++safe; break;
    case Verdict::Dangerous: ++dangerous; break;
    case Verdict::Deadly: ++deadly; break;
    case Verdict::Unrated: ++unrated; break;
    }
}

Outlook assess(const Tally& t) {
    const int n = t.total();
    if (n == 0) return Outlook::NoMods;
    // One is enough, whatever the other modifiers say: this is the verdict the whole feature
    // exists to carry, and averaging it away would be the confident wrong answer.
    if (t.deadly > 0) return Outlook::Fatal;
    // Doubled rather than divided, so an odd count needs no rule about which way it rounds.
    if (t.safe * 2 > n) return Outlook::Safe;
    // Half the map is a question mark and the rest is fine — a map worth running and worth
    // reading, which no count of safe against dangerous can express.
    if (t.unrated * 2 >= n && t.dangerous == 0) return Outlook::Unrated;
    return t.safe > t.dangerous ? Outlook::Likely : Outlook::Careful;
}

std::string affix_key(std::vector<std::string> refs) {
    std::erase_if(refs, [](const std::string& r) { return r.empty(); });
    std::sort(refs.begin(), refs.end());
    refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
    std::string out;
    for (const std::string& r : refs) {
        if (!out.empty()) out += kKeySep;
        out += r;
    }
    return out;
}

std::vector<std::string> affix_refs(std::string_view key) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= key.size() && !key.empty()) {
        const size_t end = key.find(kKeySep, i);
        out.emplace_back(key.substr(i, end == std::string_view::npos ? end : end - i));
        if (end == std::string_view::npos) break;
        i = end + 1;
    }
    return out;
}

void Profile::reindex() {
    index_.clear();
    index_.reserve(ratings_.size());
    for (const auto& [key, rating] : ratings_) index_.push_back({affix_refs(key), rating});
}

void Profile::put(std::string key, Rating r) {
    ratings_[std::move(key)] = r;
    reindex();
}

const Rating* Profile::rating_of(const std::vector<std::string>& refs) const {
    if (refs.empty()) return nullptr;
    // Sorted once, so each candidate is a linear walk rather than a search per wording.
    std::vector<std::string> have = refs;
    std::sort(have.begin(), have.end());

    const Rating* best = nullptr;
    size_t best_len = 0;
    Verdict tied = Verdict::Unrated;
    for (const Entry& e : index_) {
        if (e.refs.size() > have.size()) continue;
        if (!std::includes(have.begin(), have.end(), e.refs.begin(), e.refs.end())) continue;
        if (e.refs.size() > best_len) {
            best_len = e.refs.size();
            best = &e.rating;
            tied = e.rating.verdict;
        } else if (e.refs.size() == best_len) {
            // Two keys of equal length both covering this affix: neither is the more particular
            // statement, so take the one the reader would least like to be surprised by.
            tied = worse_of(tied, e.rating.verdict);
            if (tied != best->verdict) best = &e.rating;
        }
    }
    return best;
}

Verdict Profile::verdict_of(const std::vector<std::string>& refs) const {
    const Rating* r = rating_of(refs);
    return r ? r->verdict : Verdict::Unrated;
}

Verdict Profile::exact(const std::vector<std::string>& refs) const {
    const auto it = ratings_.find(affix_key(refs));
    return it == ratings_.end() ? Verdict::Unrated : it->second.verdict;
}

bool Profile::set(const std::vector<std::string>& refs, Verdict v) {
    const std::string key = affix_key(refs);
    if (key.empty()) return false;
    const auto it = ratings_.find(key);
    if (v == Verdict::Unrated) {
        if (it == ratings_.end()) return false;
        ratings_.erase(it);
        reindex();
        return true;
    }
    if (it != ratings_.end()) {
        if (it->second.verdict == v) return false;
        it->second.verdict = v;
        reindex();
        return true;
    }
    ratings_.emplace(key, Rating{v, std::nullopt, std::nullopt});
    reindex();
    return true;
}

std::string sanitize_profile_name(std::string_view name) {
    std::string out;
    out.reserve(std::min(name.size(), kMaxProfileName));
    for (const char ch : name) {
        if (out.size() >= kMaxProfileName) break;
        out += forbidden(static_cast<unsigned char>(ch)) ? '_' : ch;
    }
    // Windows drops trailing dots and spaces without saying so, which would make two names that
    // do not look alike open the same file.
    const auto trim = [](char c) { return c == ' ' || c == '.'; };
    while (!out.empty() && trim(out.front())) out.erase(out.begin());
    while (!out.empty() && trim(out.back())) out.pop_back();
    if (out.empty()) return out;

    std::string stem = out.substr(0, out.find('.'));
    for (char& c : stem) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const std::string_view r : kReserved)
        if (stem == r) return "_" + out;
    return out;
}

std::string profile_to_json(const Profile& p) {
    json j;
    j["profile"] = p.name();
    // An array of rows rather than an object keyed by wording, because the key is now a *set* of
    // them and JSON has no such key. Joining them into one string would work and would be
    // unreadable: this file is hand-editable and a wording is exactly what somebody searches it
    // for, so each stays a string of its own on a line of its own.
    json v = json::array();
    for (const auto& [key, r] : p.ratings()) {
        json row{{"verdict", std::string(verdict_id(r.verdict))}, {"mods", affix_refs(key)}};
        if (r.min) row["min"] = *r.min;
        if (r.max) row["max"] = *r.max;
        v.push_back(std::move(row));
    }
    j["verdicts"] = v;
    return j.dump(2) + "\n";
}

Profile profile_from_json(std::string name, std::string_view text) {
    Profile p(std::move(name));
    try {
        const json j = json::parse(text);
        if (!j.contains("verdicts")) return p;
        const json& v = j["verdicts"];
        // The object form is what tables written before the key became a set hold, and a
        // one-wording affix keys as that wording alone — so those rows mean today exactly what
        // they meant then and are read rather than migrated.
        if (v.is_object()) {
            for (const auto& [ref, value] : v.items()) {
                Rating r;
                if (value.is_string()) {
                    r.verdict = verdict_from_id(value.get<std::string>());
                } else if (value.is_object()) {
                    r.verdict = verdict_from_id(value.value("verdict", std::string()));
                    r.min = number_or_none(value, "min");
                    r.max = number_or_none(value, "max");
                }
                // Not a row: an unrated affix is one nothing was said about, and writing it back
                // would grow the file by every modifier somebody clicked past.
                if (r.verdict == Verdict::Unrated) continue;
                p.put(affix_key({ref}), r);
            }
            return p;
        }
        if (!v.is_array()) return p;
        for (const json& row : v) {
            if (!row.is_object() || !row.contains("mods") || !row["mods"].is_array()) continue;
            std::vector<std::string> refs;
            for (const json& m : row["mods"])
                if (m.is_string()) refs.push_back(m.get<std::string>());
            Rating r;
            r.verdict = verdict_from_id(row.value("verdict", std::string()));
            r.min = number_or_none(row, "min");
            r.max = number_or_none(row, "max");
            if (r.verdict == Verdict::Unrated) continue;
            std::string key = affix_key(std::move(refs));
            if (key.empty()) continue;
            p.put(std::move(key), r);
        }
    } catch (...) {
        // Hand-editable, so a stray comma must cost the ratings and never the run.
    }
    return p;
}

} // namespace ppc::mapcheck
