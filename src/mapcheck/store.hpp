#pragma once

#include <chrono>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "mapcheck/verdict.hpp"

namespace ppc::mapcheck {

/// Where the rating tables live: one `.json` per profile, named after it.
std::filesystem::path profiles_dir();

/// How long a rating waits before it reaches the disk. Long enough that walking a modifier
/// through all four states is one write, short enough that a session ending badly loses at most
/// the last click. Whatever is outstanding is written whenever a screen that can rate closes,
/// so this is a ceiling on batching and never on durability.
inline constexpr std::chrono::milliseconds kWriteDelay{1500};

/// What a first run is given, so that a map check always has somewhere to put a verdict. Not a
/// translated word: it is a file name.
inline constexpr std::string_view kDefaultProfile = "Default";

/// Every rating table on disk, the one in use, and the throttle between a click and a write.
///
/// **The directory is the authority, not the config.** `config.json` records the names and
/// their order because that is where the rest of the application's state lives, but a file
/// dropped in by hand shows up and a name whose file has gone is dropped — which is also what
/// makes creating a profile safe without saving Settings first: the file is written at once and
/// the list catches up on the next save either way.
class Store {
public:
    /// Read `dir`, reconciling it against the names `listed` gives and selecting `current`.
    ///
    /// **Ends with at least one profile.** A directory with nothing in it gets `kDefaultProfile`,
    /// written there and then: a verdict is only ever put into a table, so a popup opening with
    /// no table is one where every click does nothing, and the user has no way to find out why
    /// without going to Settings. Missing directory is not an error — it is a first run.
    void open(const std::filesystem::path& dir, const std::vector<std::string>& listed,
              std::string_view current);

    const std::vector<std::string>& names() const { return names_; }
    /// How many ratings a named table holds, for the line the delete confirmation shows. 0 for
    /// a name there is no table for.
    size_t rated_in(std::string_view name) const;
    const std::string& current() const { return current_; }
    /// No-op for a name there is no table for, so a stale `config.json` cannot leave the popup
    /// rating something that does not exist.
    void select(std::string_view name);

    /// The table in use. Never null: with no profile at all this is an empty one, which reads
    /// as every modifier unrated and accepts no writes.
    const Profile& profile() const;
    /// What the current profile says about the affix granting `refs` — including anything a
    /// shorter key lends it. See `Profile::verdict_of`.
    Verdict verdict_of(const std::vector<std::string>& refs) const;
    /// What it says about that affix and nothing else, which is what a control shows.
    Verdict exact(const std::vector<std::string>& refs) const;
    /// Rate an affix in the current profile. Buffered — see `tick` and `flush`.
    void set(const std::vector<std::string>& refs, Verdict v);

    /// Create `name`, empty or as a copy of `copy_from`, and select it. **Written immediately**:
    /// a profile is a file, and a file that exists only in memory is one a crash turns into a
    /// list entry pointing at nothing. False when the name is unusable or already taken.
    bool create(std::string_view name, std::string_view copy_from = {});

    /// Delete `name` and its file, selecting whatever is left — and putting `kDefaultProfile`
    /// back when that was the last one, for the reason `open` seeds it. Immediate for the same
    /// reason `create` is: a table gone from the list and still on disk comes back on the next
    /// launch, which reads as the delete not having worked.
    bool remove(std::string_view name);

    /// Write every table that has changed. Called when a screen that can rate closes and on the
    /// way out, so nothing depends on the throttle having fired.
    void flush();
    /// Write anything that has been waiting longer than `kWriteDelay`. From the main loop.
    void tick();
    bool dirty() const { return !dirty_.empty(); }

private:
    void mark(const std::string& name);
    void write(const std::string& name);

    std::filesystem::path dir_;
    std::vector<std::string> names_;   ///< display order
    std::vector<Profile> profiles_;    ///< parallel to `names_`
    std::string current_;
    std::set<std::string> dirty_;
    std::chrono::steady_clock::time_point touched_{};
};

} // namespace ppc::mapcheck
