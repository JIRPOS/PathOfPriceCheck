#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "data/game_data.hpp"
#include "data/types.hpp"

/// Path of Exile's item-search syntax — the strings players already keep for the map device,
/// and what a tool like poe.re writes.
///
/// **GGG publishes no grammar, but the community has written one down**, and it agrees with
/// what [docs/roadmap.md](../../docs/roadmap.md) fixed for this project before any of this was
/// written. The reference is [Guide:Regex](https://www.poewiki.net/wiki/Guide:Regex), and every
/// rule below is from it rather than inferred:
///
/// - an **unquoted space is a logical AND**, and every search field in the game takes the same
///   syntax, so a string kept for the stash is a string that works here;
/// - **double quotes group a term that contains spaces**, and only that — a quoted term is still
///   expanded as a pattern, so `|` inside quotes is still an alternation and not a literal;
/// - a leading **`!` negates**, inside the quotes or outside them (`!corrupted`, `"!(^str)"`);
/// - **string literals are case-insensitive**;
/// - **`^` and `$` anchor to a printed line**, which is why every term here is asked about each
///   of a modifier's wordings separately rather than about them joined.
///
/// So `"!\d+ e|te of|ents$" pte` is two terms, the first negated.
///
/// The terms themselves are regular expressions, matched here by `std::regex` in its ECMAScript
/// dialect. The game's is a custom engine, so the two can disagree on a corner — lookbehind is
/// the known one, since ECMAScript has none — which costs a proposed verdict the user is about
/// to confirm or reject anyway, and never anything the game does.
///
/// **A term that will not compile matches nothing**, rather than falling back to the literal
/// text it is made of. The fallback was tried and is what made this box two search languages at
/// once: `Damage (` found a substring, `Damage (Fire|Cold)` found a pattern, and nothing on
/// screen said which reading a given term had got. One language, and an unfinished pattern
/// showing an empty list is what the game does too.
namespace ppc::mapcheck {

/// Whether a term asks about the *item* rather than about one of its modifiers.
///
/// The game's search has keywords — `ilvl:84`, `"rarity: rare"`, `ts:`, `"item level: 78"` — and
/// they are questions no modifier wording can answer. Left in, one of them ANDed into a filter
/// empties the list, which is what pasting a real map string would otherwise do here.
///
/// **Recognised by shape, not by a list of the keywords.** A term is item-scope when it opens
/// with `<word>:`; no wording in the pool contains a colon at all, checked against the published
/// bundle, so nothing a modifier could answer is caught by that. The bare keywords are
/// deliberately *not* recognised, because the wiki's own list is partial and the words are real
/// modifier text: `currency` alone appears in 17 wordings and `corrupted` in two, so treating
/// them as keywords would silently swallow the searches most worth typing.
bool asks_about_item(std::string_view term);

/// One term, with the `!` and the quotes taken off.
struct SearchTerm {
    std::string text;
    bool negated = false;
};

/// Split a search string into its terms. An unterminated quote runs to the end of the string,
/// which is what makes a search usable while it is still being typed.
std::vector<SearchTerm> parse_search(std::string_view s);

/// A parsed search, ready to be asked about a modifier.
///
/// Every term is tested against each line **on its own** rather than against the lines joined:
/// these strings were written against a tooltip, where `$` means the end of a printed line, and
/// a join would put that anchor somewhere no term's author has ever seen. A term hits a
/// modifier when it hits any of its lines, which is also the answer to what a modifier printing
/// two to four of them counts as.
///
/// **The two questions below read the same terms differently, and have to.** The game ANDs
/// terms because it is deciding about a whole *map*, where each term can be answered by a
/// different modifier on it. Here the subject is one modifier, and a modifier cannot satisfy
/// two unrelated wanted terms at once — so proposing a verdict asks each term separately, which
/// is also what [ROADMAP.md](../../ROADMAP.md) promises ("every modifier an excluding term hits
/// is proposed dangerous, every one a wanted term hits proposed safe"). Filtering keeps the
/// game's rule, because there the subject really is "show me the rows matching all of this".
class SearchFilter {
public:
    SearchFilter();
    explicit SearchFilter(std::string_view s);
    SearchFilter(SearchFilter&&) noexcept;
    SearchFilter& operator=(SearchFilter&&) noexcept;
    ~SearchFilter();

    /// Whether nothing here can be asked about a modifier — which a search made only of
    /// item-scope keywords is, and which has to read the same as an empty box rather than as a
    /// filter matching nothing.
    bool empty() const;
    /// Terms that will be asked.
    size_t size() const;
    /// Terms `asks_about_item` set aside, so a screen can say so instead of leaving the user to
    /// work out why a word they typed made no difference.
    size_t set_aside() const;

    /// **Filtering**, in the game's own reading: every positive term must hit and no negated
    /// term may. This is what the settings list narrows on, and it is why typing two plain
    /// words there means both of them.
    bool matches(std::span<const std::string> lines) const;

    /// Which side of the search a modifier falls on.
    ///
    /// A **negated** term hitting says the player refuses this modifier, which is the strongest
    /// thing a search string states and outranks a positive term that also hit. `None` is the
    /// ordinary answer: most of the pool is not mentioned by any one search.
    enum class Hit : uint8_t { None, Wanted, Unwanted };
    Hit classify(std::span<const std::string> lines) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// `ref` with every `#` replaced by `value`, as the game would print it.
///
/// A search term was written against a printed line and can never match a placeholder, so a
/// pool entry has to be rendered before it can be tested. `value` absent leaves the wording
/// alone, which is right for the wordings that print no number at all.
std::string render_wording(std::string_view ref, std::optional<double> value);

/// The line the game would actually print for this stat at `value` — which is very often not
/// the canonical wording.
///
/// A stat record carries alternative wordings, and one of them may be flagged `negate`: the
/// same stat said the other way round, for a roll below zero. `Players have #% more Defences`
/// rolls `[-30, -25]` and the game therefore *always* prints `Players have 30% less Defences`.
/// Rendering the canonical wording gives `-25% more Defences`, a line no player has ever seen
/// and no search term was ever written against — which is how `s def` misses the single nastiest
/// defence modifier in the pool. 16 of the pool's wordings are in that position, and they are
/// disproportionately the ones a hardcore search string is about.
///
/// This never affected a *verdict*: the popup keys on the stat record's `ref` and the matcher
/// resolves the printed line back to it, so a rating made here is read correctly off a real map
/// either way. It affected only being able to find the modifier and being shown what it says.
std::string printed_wording(const data::Stat* rec, std::string_view ref,
                            std::optional<double> value);

/// The placeholder wording to *show* for a pool entry: the negated alternative where the range
/// cannot produce anything but it, and the canonical one otherwise. A range straddling zero
/// keeps the canonical wording, because either is a line the game may print and the canonical
/// one is the record's identity.
std::string display_wording(const data::Stat* rec, const data::PoolStat& s);

/// Everything a term may be tested against for one pool entry: every one of its wordings
/// rendered at both ends of its range, and the affix name.
///
/// **The whole affix, because the affix is the unit.** A term hitting any one line is a term
/// about the modifier that prints it, and both things this feature does with a hit — narrowing
/// the list, proposing a verdict — are about an affix rather than about a wording. Asking per
/// stat instead was tried, and what it produced was a verdict keyed on one wording: a key that
/// short then speaks for every *other* affix granting the same line, which is the propagation
/// rule working exactly as written on a key that had no business being one wording long.
///
/// **The affix name is in.** With Advanced Mod Descriptions on it is a line of the tooltip the
/// game's own search reads, so a string written there was written knowing it is matchable —
/// leaving it out would silently drop the terms that name one.
///
/// **Both ends of the range, not just the top.** The old rule was the top alone, on the grounds
/// that these strings are written to catch the roll that ends a map — but a negated wording
/// prints the top of `[-30, -25]` as the *smallest* number it can say, so "the top" stops
/// meaning anything. A modifier can roll anywhere in its range, so a term naming a number hits
/// if any roll would say it.
std::vector<std::string> matchable_lines(const data::PoolMod& m, const data::GameData* gd);

/// The same, with the stat records handed over directly rather than looked up — `recs` runs
/// parallel to `m.stats`, and a stat it is too short for is read without one. What the lookup
/// form resolves to, and what a test can build without a bundle behind it.
std::vector<std::string> matchable_lines(const data::PoolMod& m,
                                         std::span<const data::Stat* const> recs);

} // namespace ppc::mapcheck
