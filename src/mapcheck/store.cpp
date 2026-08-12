#include "mapcheck/store.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

#include "paths.hpp"

namespace fs = std::filesystem;

namespace ppc::mapcheck {
namespace {

const Profile& empty_profile() {
    static const Profile p;
    return p;
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

fs::path profiles_dir() { return config_dir() / "map-profiles"; }

void Store::open(const fs::path& dir, const std::vector<std::string>& listed,
                 std::string_view current) {
    dir_ = dir;
    names_.clear();
    profiles_.clear();
    dirty_.clear();
    current_.clear();

    // What is actually there. A name is the file's stem, so renaming the file renames the
    // profile and there is never a mapping between the two to get wrong.
    std::vector<std::string> found;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".json") continue;
        std::string name = sanitize_profile_name(e.path().stem().string());
        if (!name.empty()) found.push_back(std::move(name));
    }
    std::sort(found.begin(), found.end());

    // The config's order first, for the names it still has files for; then whatever else is in
    // the directory, so a table shared between machines by hand appears without an edit.
    const auto take = [&](const std::string& name) {
        if (std::find(names_.begin(), names_.end(), name) != names_.end()) return;
        names_.push_back(name);
        profiles_.push_back(profile_from_json(name, read_file(dir_ / (name + ".json"))));
    };
    for (const std::string& n : listed)
        if (std::binary_search(found.begin(), found.end(), n)) take(n);
    for (const std::string& n : found) take(n);

    // A profile is what every rating is written into, so there is always one — and on a first
    // run, that means making it here rather than asking the user to before the feature works.
    if (names_.empty()) {
        create(kDefaultProfile);
        return;
    }
    if (!current.empty()) select(current);
    if (current_.empty()) current_ = names_.front();
}

void Store::select(std::string_view name) {
    if (std::find(names_.begin(), names_.end(), name) == names_.end()) return;
    current_ = name;
}

const Profile& Store::profile() const {
    const auto it = std::find(names_.begin(), names_.end(), current_);
    if (it == names_.end()) return empty_profile();
    return profiles_[static_cast<size_t>(it - names_.begin())];
}

size_t Store::rated_in(std::string_view name) const {
    const auto it = std::find(names_.begin(), names_.end(), name);
    return it == names_.end() ? 0
                              : profiles_[static_cast<size_t>(it - names_.begin())].rated();
}

Verdict Store::verdict_of(const std::vector<std::string>& refs) const {
    return profile().verdict_of(refs);
}

Verdict Store::exact(const std::vector<std::string>& refs) const {
    return profile().exact(refs);
}

void Store::set(const std::vector<std::string>& refs, Verdict v) {
    const auto it = std::find(names_.begin(), names_.end(), current_);
    if (it == names_.end()) return; // no profile, nowhere to put it
    Profile& p = profiles_[static_cast<size_t>(it - names_.begin())];
    if (p.set(refs, v)) mark(current_);
}

bool Store::create(std::string_view name, std::string_view copy_from) {
    const std::string clean = sanitize_profile_name(name);
    if (clean.empty()) return false;
    if (std::find(names_.begin(), names_.end(), clean) != names_.end()) return false;

    Profile p(clean);
    if (const auto src = std::find(names_.begin(), names_.end(), copy_from); src != names_.end())
        for (const auto& [ref, r] : profiles_[static_cast<size_t>(src - names_.begin())].ratings())
            p.put(ref, r);

    names_.push_back(clean);
    profiles_.push_back(std::move(p));
    current_ = clean;
    write(clean); // now, not on the throttle: see the header
    return true;
}

bool Store::remove(std::string_view name) {
    const auto it = std::find(names_.begin(), names_.end(), name);
    if (it == names_.end()) return false;
    const size_t i = static_cast<size_t>(it - names_.begin());
    const std::string gone = names_[i];

    // Before the file goes, or a flush riding on the throttle would write it straight back.
    dirty_.erase(gone);
    names_.erase(names_.begin() + static_cast<ptrdiff_t>(i));
    profiles_.erase(profiles_.begin() + static_cast<ptrdiff_t>(i));
    std::error_code ec;
    fs::remove(dir_ / (gone + ".json"), ec);

    if (names_.empty()) {
        create(kDefaultProfile); // there is always one to rate into
        return true;
    }
    // The neighbour, so deleting down a list leaves the selection where the hand is.
    if (current_ == gone) current_ = names_[std::min(i, names_.size() - 1)];
    return true;
}

void Store::mark(const std::string& name) {
    dirty_.insert(name);
    touched_ = std::chrono::steady_clock::now();
}

void Store::write(const std::string& name) {
    const auto it = std::find(names_.begin(), names_.end(), name);
    if (it == names_.end()) return;
    if (!ensure_dir(dir_)) return;
    std::ofstream out(dir_ / (name + ".json"), std::ios::binary);
    if (!out) return;
    out << profile_to_json(profiles_[static_cast<size_t>(it - names_.begin())]);
}

void Store::flush() {
    for (const std::string& name : dirty_) write(name);
    dirty_.clear();
}

void Store::tick() {
    if (dirty_.empty()) return;
    if (std::chrono::steady_clock::now() - touched_ < kWriteDelay) return;
    flush();
}

} // namespace ppc::mapcheck
