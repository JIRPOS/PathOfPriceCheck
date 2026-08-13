#pragma once

#include <string>
#include <vector>

#include "data/game_data.hpp"
#include "item/item.hpp"
#include "mapcheck/store.hpp"
#include "mapcheck/verdict.hpp"

namespace ppc::mapcheck {

/// The three pools map check reads: the map device's, the charts', and the heist areas'.
///
/// One list rather than a hard-coded name anywhere, because everything above it is written per
/// domain — the settings page, the lookups, the store. A fourth pool costs an entry here.
///
/// **Order is precedence**, and the map comes first: where two pools word one affix the same, the
/// row is drawn with the map's wording and its affix name. Heist is last for no reason but that
/// it arrived last — it shares nothing with the chart pool that the map pool does not also share.
inline constexpr int kMapDomain = 5;
inline constexpr int kChartDomain = 39;
inline constexpr int kHeistDomain = 22;
inline constexpr int kDomains[]{kMapDomain, kChartDomain, kHeistDomain};

/// Which pool this item rolls from, or 0 when nothing says.
///
/// `GameData::mod_domain_for` is the answer where the bundle has one. A bundle published before
/// the field existed says 0 about everything, and rather than making the hotkey silently do
/// nothing on it, the parser's own reading of the item stands in — the same items, decided from
/// the clipboard instead of from the data.
int map_domain_of(const item::Item& it, const data::GameData* gd);

/// True for an item that rolls from one of `kDomains`: everything that opens in the map device —
/// ordinary, nightmare and Originator maps, unique maps, expedition logbooks and invitations —
/// plus charts, and heist contracts and blueprints.
///
/// The gate on the hotkey, and the only one there is. It keeps a ring's modifiers out of a map
/// profile — the rating table is keyed on stats, so nothing would stop them going in.
///
/// **A bundle with no pool for the domain still passes.** The gate is about the item, and the
/// pool is a convenience: an affix that resolves to a stat can be rated whether or not anything
/// describes the pool it came from. What such a bundle costs is the expansion in
/// `pool_refs_for` — a verdict set against a printed line alone, which the pool would have keyed
/// on the affix's whole wording set. Those two keys are not each other, so ratings made on a
/// bundle predating a domain's pool are not found again once it arrives.
bool is_rateable_item(const item::Item& it, const data::GameData* gd);

/// One line of the popup's rateable list: **an affix** the item printed, and what the profile in
/// use says about it.
struct Row {
    /// The modifiers this affix printed, in order — one for an ordinary affix, several for one
    /// that grants several stats. Never empty.
    std::vector<const item::Modifier*> mods;
    /// The stat records' `ref`s the verdict is keyed on. **Empty when nothing resolved**, and
    /// such a row is drawn and cannot be rated: there is nothing for a verdict to attach to, and
    /// a printed line is not a key — it is language-dependent and two records can share one.
    std::vector<std::string> refs;
    Verdict verdict = Verdict::Unrated;

    bool rateable() const { return !refs.empty(); }
    const item::Modifier* mod() const { return mods.empty() ? nullptr : mods.front(); }
};

/// Every affix the popup rates, in the order the item printed them.
///
/// **Implicits are in it**, and enchantments are not. See `rateable_type`: the Vaal corruption
/// implicits roll, the pool carries them, and a line that can be rated in Settings and not on the
/// map in front of you is the worse half of both rules.
///
/// **Grouped by affix, which is what Advanced Mod Descriptions makes possible.** The parser
/// marks the second and later stats of one affix `continuation`, so `of the Juggernaut`'s three
/// lines are one row and one decision. Without that setting an item is a flat list of lines with
/// nothing saying where an affix ends, and each line stands alone — which still rates every
/// single-wording affix correctly, and is why this degrades rather than fails.
///
/// `gd` is consulted to turn the affix's *printed* wordings into the pool entry's full set,
/// since an affix can grant stats the item does not print — the `#% more Currency found in Area`
/// on every Nightmare-map modifier is never on the tooltip. Without that step a verdict set in
/// Settings could never be found again from a map.
std::vector<Row> rate(const item::Item& it, const Store& store, const data::GameData* gd);

/// The pool entry's full wording set for an affix that printed `refs`, or `refs` unchanged when
/// nothing in the pool covers them.
///
/// The smallest superset wins. `Impaling` is two pool entries — the ordinary one granting only
/// `Monsters' Attacks have #% chance to Impale on Hit`, and the Nightmare one granting that plus
/// a reflect mechanic plus more currency — and a map printing the one line means the ordinary
/// one. A map printing both means the other, which no smaller entry can cover.
std::vector<std::string> pool_refs_for(const std::vector<std::string>& printed, int domain,
                                       const data::GameData* gd);

/// A pool entry's own wording set — what rating it in Settings keys on, and what
/// `pool_refs_for` resolves a map's printed affix to.
std::vector<std::string> pool_key_refs(const data::PoolMod& m);

/// One row of the settings pool browser: an **affix**, and every pool entry that grants
/// exactly its set of wordings.
///
/// A map and a chart word 42 modifiers identically and roll them from pools of their own, so
/// one affix arrives here as two entries differing only in their ranges — `Resistant` is
/// `10-25` chaos on a map and `0-40` on a chart. The verdict store keys on the sorted ref set
/// and has no domain in it, so two such entries can never hold different verdicts: rating
/// either one rates both. **82 of 270 entries are in that position**, and two rows that must
/// always agree are one decision drawn twice.
struct PoolGroup {
    /// What the row draws. The first domain in `kDomains` with an entry, so a map's wording
    /// and affix name win over a chart's for the six groups where the two disagree.
    const data::PoolMod* mod = nullptr;
    /// Every entry sharing the key, `mod` among them. A search is tested against all of them,
    /// so a term naming a number hits if *either* pool's range would print it.
    std::vector<const data::PoolMod*> all;
    /// The affix key: sorted, deduplicated, never empty.
    std::vector<std::string> refs;
};

/// The pool browser's rows, in the order the pools list them, `kDomains` first to last.
///
/// An entry keyed on nothing — no stat carries a `ref` — is left out: there is no verdict to
/// attach to it and no row worth drawing.
std::vector<PoolGroup> pool_groups(const data::GameData& gd);

/// Every line a search term may be tested against for one group: its entries' `matchable_lines`
/// unioned, in order, without repeats.
std::vector<std::string> group_lines(const PoolGroup& g, const data::GameData* gd);

/// How those rows came out, for `assess`.
Tally tally(const std::vector<Row>& rows);

} // namespace ppc::mapcheck
