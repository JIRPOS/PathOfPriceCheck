#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "data/index.hpp"
#include "data/lexicon.hpp"
#include "data/mapped_file.hpp"
#include "data/types.hpp"

namespace ppc::data {

/// One immutable snapshot of an installed bundle.
///
/// Files are memory-mapped and individual records are parsed the first time they are hit,
/// so a loaded bundle costs about a megabyte resident and no startup time. Swapping in a
/// newer bundle is a `shared_ptr` assignment; the old mapping goes away when the last
/// reader drops its reference, which is why callers must hold one for the duration of a
/// frame rather than re-reading `App::data()` mid-render.
class GameData {
public:
    /// Opens `dir` (a versioned directory in the cache). Returns null and fills `err` if
    /// any required file is missing or malformed.
    static std::shared_ptr<GameData> open(const std::filesystem::path& dir,
                                          std::string_view lang, std::string* err);

    /// Every stat whose wordings include `normalized`, in file order.
    /// fnv1a32 collides, so candidates are re-verified before being returned.
    std::vector<const Stat*> find_stats(std::string_view normalized) const;

    /// The single stat matching `normalized` that is searchable as `type`.
    ///
    /// Filtering by mod type is the primary disambiguator: explicit / implicit / fractured
    /// / crafted / enchant variants share a wording and differ only by trade namespace, and
    /// the parsed section already told us which one this is. Null when nothing matches, or
    /// when the choice is ambiguous.
    const Stat* find_stat(std::string_view normalized, ModType type) const;

    const Stat* find_stat_by_ref(std::string_view ref) const;

    /// Every base with this display name, in file order — the three Two-Stone Rings, or a
    /// unique colliding with a base name. The caller disambiguates on the returned fields.
    std::vector<const BaseType*> find_bases(Namespace ns, std::string_view name) const;

    /// Every base whose English `refName` is `ref` — how to name a record the clipboard did
    /// not print, and the one lookup that means the same thing in every language.
    ///
    /// A bundle with no ref index answers through the name index instead, which is right for
    /// an English one (the two names are the same string) and the only thing available on a
    /// bundle published before the index shipped.
    std::vector<const BaseType*> find_bases_by_ref(Namespace ns, std::string_view ref) const;

    /// Every unique that drops on `base`, in file order — which is all an **unidentified**
    /// unique states about itself: the clipboard prints the base line and no name at all.
    ///
    /// Empty is two different answers and the caller has to keep them apart: a base nothing
    /// drops on, and a bundle published before this index existed (`has_unique_bases()`).
    std::vector<const BaseType*> find_uniques_on_base(std::string_view base) const;

    /// False for a bundle carrying no base → uniques index, where an unidentified unique
    /// cannot be identified at all rather than being one nothing drops on.
    bool has_unique_bases() const { return items_base_index_.valid(); }

    /// What this unique can roll, or null. `name` is the same string `find_bases` is keyed
    /// on, so it is the name the clipboard already gave us.
    ///
    /// Null is normal, not an error: the source does not cover every unique and lags a league
    /// launch, and a bundle published before this dataset existed carries none of it. The
    /// caller degrades to what a printed range can prove, never to a wrong filter.
    const UniqueMods* find_unique_mods(std::string_view name) const;

    /// False for a bundle published before the per-unique modifier data existed, which is
    /// what tells "this unique has no record" apart from "nothing here has one".
    bool has_unique_mods() const { return unique_mods_name_index_.valid(); }

    /// Every modifier `domain`'s pool can spawn, in file order — the whole set, whether or not
    /// anything is holding one. This is the one lookup here that starts from no item.
    ///
    /// Empty is two different answers, as it is for the unique indices: a domain the bundle
    /// publishes no pool for, and a bundle published before the dataset existed
    /// (`has_mod_pools()`). Parsing is one pass over the file, memoised, since a pool is only
    /// ever asked for whole.
    std::span<const PoolMod* const> mod_pool(int domain) const;

    /// The entries in `domain`'s pool that print `normalized`, which is how a wording resolved
    /// off an item finds what the pool says about it. Usually one; two where a prefix and a
    /// suffix word the same thing, which the game does 42 times in the map pool alone.
    ///
    /// **Never a gate.** An empty answer means the pool does not mention this wording, which is
    /// normal — see `PoolMod`.
    std::vector<const PoolMod*> find_pool_mods(int domain, std::string_view normalized) const;

    /// False for a bundle published before the mod-pool dataset existed, which is what tells
    /// "this domain has no pool" apart from "nothing here has one".
    bool has_mod_pools() const { return mod_pools_ref_index_.valid(); }

    /// Which pool an item resolved to `base` and printing `item_class` rolls from, or 0.
    ///
    /// The base is asked first and the class only answers where it cannot: trade lists all 491
    /// maps under one entry whose game row is a stand-in in the stackable-currency domain, so a
    /// map's own record states no domain and its class is what knows the answer. The other way
    /// round would be wrong — a class holding genuinely different things (Jewels covers two
    /// domains) publishes none.
    int mod_domain_for(const BaseType* base, std::string_view item_class) const;

    /// False for a bundle published before the currency-exchange flags existed, which is what
    /// tells "this item does not trade there" apart from "nothing here says either way".
    ///
    /// The same distinction `has_unique_mods()` draws, but it cannot be drawn the same way:
    /// that dataset is a whole file whose absence is the signal, while this one is a boolean on
    /// records the bundle already had, and an absent boolean is indistinguishable from a false
    /// one. So the signal is bundle-level — `source.exchange_items` in the manifest, written
    /// only when the data build actually has a crawl behind it. Reading a missing flag as "does
    /// not trade" would put every currency item back into the empty-panel case the dataset
    /// exists to fix, on every bundle older than it.
    bool has_exchange_flags() const { return exchange_items_ > 0; }

    /// The words this bundle's client prints, for the language it was opened in.
    ///
    /// English unless the bundle carries a `<lang>-lexicon.json`, and English for whatever
    /// that file leaves unsaid — see `Lexicon`. Never absent, so no caller has to decide what
    /// to do without one.
    const Lexicon& lexicon() const { return lexicon_; }
    /// False for a bundle that carries no lexicon for the language it was opened in, i.e. one
    /// whose item text this build can only read because it happens to be English.
    bool has_lexicon() const { return has_lexicon_; }

    /// The languages this bundle declares assets for, off the manifest. What Settings offers
    /// as the client language, since asking for one the bundle does not have simply fails to
    /// open it. Never empty: a manifest that says nothing is read as English-only, which is
    /// what every bundle published so far is.
    const std::vector<std::string>& languages() const { return languages_; }

    /// "Item Class: Rings" -> the trade `category` option, e.g. "accessory.ring".
    /// Empty when the class has no trade category, which is not an error.
    ///
    /// `item-classes.ndjson` is language-neutral and keyed on the *English* printed name, so
    /// a translated client's class is found through the lexicon's `class_id` instead. The
    /// printed name is still tried first: it is what every bundle published so far answers to.
    std::string_view trade_category_for(std::string_view item_class) const;
    const ItemClass* item_class(std::string_view item_class) const;

    std::string_view data_version() const { return data_version_; }
    size_t stat_count() const { return stats_matcher_index_.size(); }
    /// The credit the per-unique modifier data is licensed on, e.g. "poewiki.net, CC BY-NC
    /// 3.0". It travels with the bundle rather than being hardcoded, because it describes
    /// the data that is installed and not the build that renders it.
    std::string_view unique_mods_attribution() const { return unique_mods_attribution_; }

private:
    /// Parses the ndjson line at `offset`, memoising it. Records are stable for the
    /// lifetime of this GameData, so callers may hold the returned pointer.
    const Stat* stat_at(uint32_t offset) const;
    const BaseType* base_at(uint32_t offset) const;
    const UniqueMods* unique_mods_at(uint32_t offset) const;
    const PoolMod* pool_mod_at(uint32_t offset) const;
    std::string_view line_at(const MappedFile& f, uint32_t offset) const;

    MappedFile stats_nd_, items_nd_, unique_mods_nd_, mod_pools_nd_;
    MappedFile stats_matcher_idx_, stats_ref_idx_, items_name_idx_, items_base_idx_,
        items_ref_idx_, unique_mods_name_idx_, mod_pools_ref_idx_;
    HashIndex stats_matcher_index_, stats_ref_index_, items_name_index_, items_base_index_,
        items_ref_index_, unique_mods_name_index_, mod_pools_ref_index_;

    // Parsed on demand. mutable because lookups are logically const.
    mutable std::unordered_map<uint32_t, std::unique_ptr<Stat>> stat_cache_;
    mutable std::unordered_map<uint32_t, std::unique_ptr<BaseType>> base_cache_;
    mutable std::unordered_map<uint32_t, std::unique_ptr<UniqueMods>> unique_mods_cache_;
    mutable std::unordered_map<uint32_t, std::unique_ptr<PoolMod>> pool_mod_cache_;
    /// The whole file grouped by domain, filled on the first `mod_pool()` call. A pool is only
    /// ever wanted entire, and there are a few hundred records, so one pass beats an index.
    mutable std::unordered_map<int, std::vector<const PoolMod*>> pools_by_domain_;
    mutable bool pools_scanned_ = false;

    // Small enough (90 rows) that parsing it up front beats indexing it. Keyed on the
    // English printed class name, which is what the file states; `by_class_id_` is the same
    // rows keyed on the game's internal id, for a client that printed something else.
    std::unordered_map<std::string, ItemClass> classes_;
    std::unordered_map<std::string, const ItemClass*> by_class_id_;
    Lexicon lexicon_ = Lexicon::english();
    bool has_lexicon_ = false;
    std::vector<std::string> languages_{"en"};
    std::string data_version_;
    std::string unique_mods_attribution_;
    /// How many items the data build's crawl found trading on the currency exchange. 0 means
    /// no dataset, never "none trade" — see `has_exchange_flags()`.
    int exchange_items_ = 0;
};

} // namespace ppc::data
