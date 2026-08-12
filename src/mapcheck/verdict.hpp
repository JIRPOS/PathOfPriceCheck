#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Map check's model: what a player decided about a modifier, and the table those decisions
/// live in.
///
/// This half is `ppc_core` — no ImGui, no filesystem. The popup is
/// `screens/mapcheck_screen`, the settings page is the Map Check tab, and the files these
/// tables are read from and written to are `MapCheckService`.
namespace ppc::mapcheck {

/// What the user decided about one modifier.
///
/// **Ordered worst-last on purpose.** `worse_of` is a `max`, which is the whole of "lead with
/// the worst verdict on the map" and of how a modifier printing several stats reports one
/// answer. `Unrated` is zero because it is the *absence* of a decision rather than a fourth
/// one — it is what every modifier starts as, and it is why the table grows by being used
/// instead of having to be filled in first.
enum class Verdict : uint8_t { Unrated = 0, Safe, Dangerous, Deadly };

std::string_view verdict_id(Verdict v);       ///< the word the file stores, "safe"
Verdict verdict_from_id(std::string_view s);  ///< `Unrated` for anything it does not know

constexpr Verdict worse_of(Verdict a, Verdict b) { return a > b ? a : b; }

/// The next verdict a click walks to: unrated → safe → dangerous → deadly → unrated.
constexpr Verdict next_verdict(Verdict v) {
    return v == Verdict::Deadly ? Verdict::Unrated : static_cast<Verdict>(uint8_t(v) + 1);
}

/// How the modifiers on the map in hand came out. Implicits are not in it — they are printed
/// and never rated — and neither is anything that resolved to no stat, since there is nothing
/// for a verdict to attach to.
struct Tally {
    int safe = 0, dangerous = 0, deadly = 0, unrated = 0;
    int total() const { return safe + dangerous + deadly + unrated; }

    void add(Verdict v);
};

/// What the map as a whole is worth saying, which is the line the popup leads with.
///
/// Severity, not a count: the same order as `Verdict`, so the strongest thing true of the map
/// is what it reports. `Safe` covers two sentences rather than two outlooks, because a map that
/// is mostly safe with a few unrated modifiers is still a map you can run — it just has a
/// footnote.
enum class Outlook : uint8_t {
    NoMods,   ///< nothing rolled; a white map, or one whose lines all failed to resolve
    Unrated,  ///< half or more unrated and nothing worse than safe under them
    Safe,     ///< more than half rated safe
    Likely,   ///< more safe than dangerous
    Careful,  ///< as many dangerous as safe, or more
    Fatal     ///< one deadly modifier is enough
};

/// The strongest true statement about `t`, in the order they are checked: a deadly modifier
/// first, because one of them decides the map on its own; then the two majorities; then the
/// balance between safe and dangerous.
Outlook assess(const Tally& t);

/// One row of a table.
///
/// `min`/`max` are **parsed and written back and never shown**. The practice this feature
/// copies — map regexes — has no notion of a threshold, and asking for one on each of a few
/// hundred modifiers is the UI 0.7 exists to avoid. They are in the format from the first
/// version because a reader that accepts both shapes costs one branch now, and teaching every
/// user's file a new shape later costs a migration.
struct Rating {
    Verdict verdict = Verdict::Unrated;
    std::optional<double> min, max;
};

/// The identity a verdict attaches to: **an affix**, written as the set of stat wordings it
/// grants, sorted and joined.
///
/// **Keyed on the stat records' `ref`s**, never on printed lines: a wording is
/// language-dependent the moment a localised bundle exists, and two records sharing one are
/// something `find_stat` already refuses to guess between. So a modifier is resolved first and
/// the verdict attaches to what it resolved to.
///
/// **A set and not one wording**, because a wording is not an affix. 21 of the pool's wordings
/// sit on more than one affix — `Monsters cannot be Stunned` is granted by `Unwavering` and by
/// `of the Juggernaut`, which are not the same decision — and 50 affixes grant more than one.
/// One verdict per wording cannot express either, and rating them together is what made rating
/// one affix silently change others.
///
/// Sorted, so the key does not depend on the order the pool happens to list the stats in. A
/// one-wording affix keys as that wording alone, which is what a table written before this
/// already holds — so an old file reads correctly rather than needing a migration.
/// What joins the wordings in a key. A unit separator, because it is the one byte that cannot
/// occur in one: the wordings are game text.
inline constexpr char kKeySep = '\x1f';

std::string affix_key(std::vector<std::string> refs);
std::vector<std::string> affix_refs(std::string_view key);

/// A named table of affix → verdict.
class Profile {
public:
    Profile() = default;
    explicit Profile(std::string name) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }
    void rename(std::string name) { name_ = std::move(name); }

    /// What was decided about the affix granting exactly `refs`.
    ///
    /// **The most specific decision that covers it.** A stored key applies when its wordings are
    /// all present, so rating the one-wording affix `Monsters cannot be Stunned` also speaks for
    /// `of the Juggernaut`, which grants that and two more — but only until the Juggernaut is
    /// rated in its own right, at which point the longer key wins because it is the more
    /// particular statement. Equal-length keys that both apply fall back to `worse_of`, since a
    /// reader who called something deadly is not served by being shown the milder half.
    Verdict verdict_of(const std::vector<std::string>& refs) const;
    const Rating* rating_of(const std::vector<std::string>& refs) const;

    /// Returns whether this changed anything, which is what the throttled save watches.
    /// Setting `Unrated` **erases** the row: unrated is the absence of a decision, and a file
    /// listing every affix somebody once clicked past and back is a file of nothing.
    bool set(const std::vector<std::string>& refs, Verdict v);
    /// What this table itself says about that exact affix, ignoring anything a shorter key
    /// would lend it. This is what a control shows, so that pressing it is the only thing that
    /// ever changes it.
    Verdict exact(const std::vector<std::string>& refs) const;

    size_t rated() const { return ratings_.size(); }
    const std::map<std::string, Rating, std::less<>>& ratings() const { return ratings_; }
    void put(std::string key, Rating r);

private:
    /// A stored key, split once so that a lookup is not re-parsing the whole table.
    struct Entry {
        std::vector<std::string> refs;
        Rating rating;
    };

    std::string name_;
    std::map<std::string, Rating, std::less<>> ratings_;
    std::vector<Entry> index_;
    void reindex();
};

/// A name that can be a file. Everything Windows or POSIX refuses becomes `_`, the ends are
/// trimmed of the spaces and dots Windows silently drops, a reserved DOS device name is given
/// a leading `_`, and the result is cut to `kMaxProfileName`. Empty out means there was nothing
/// left to keep, which is what the dialog disables its Create button on.
///
/// Substituted rather than dropped, so two names that do not look alike cannot collapse into
/// one file: `a/b` and `ab` are different profiles and stay that way.
///
/// The sanitised form **is** the name: it is what the file is called and what the dropdown
/// shows, so there is never a mapping between the two to get wrong.
std::string sanitize_profile_name(std::string_view name);

/// How long a profile name may be. A file-name limit, not a display one — 255 bytes is the
/// usual ceiling and this leaves room for the directory and the `.json`.
inline constexpr size_t kMaxProfileName = 64;

/// The table as the file holds it. A rating with no bound is written as the bare word, since
/// that is every row this version can produce and it keeps a hand-edited file readable.
std::string profile_to_json(const Profile& p);

/// The inverse. `name` is the profile's name — taken from the file it was read from rather
/// than from anything inside it, so renaming the file renames the profile.
///
/// Anything malformed reads as an empty table rather than throwing: this file is hand-editable
/// and a stray comma is not a reason for a hotkey to stop working.
Profile profile_from_json(std::string name, std::string_view text);

} // namespace ppc::mapcheck
