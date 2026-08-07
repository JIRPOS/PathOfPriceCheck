#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "data/index.hpp"
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

    /// "Item Class: Rings" -> the trade `category` option, e.g. "accessory.ring".
    /// Empty when the class has no trade category, which is not an error.
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
    std::string_view line_at(const MappedFile& f, uint32_t offset) const;

    MappedFile stats_nd_, items_nd_, unique_mods_nd_;
    MappedFile stats_matcher_idx_, stats_ref_idx_, items_name_idx_, unique_mods_name_idx_;
    HashIndex stats_matcher_index_, stats_ref_index_, items_name_index_, unique_mods_name_index_;

    // Parsed on demand. mutable because lookups are logically const.
    mutable std::unordered_map<uint32_t, std::unique_ptr<Stat>> stat_cache_;
    mutable std::unordered_map<uint32_t, std::unique_ptr<BaseType>> base_cache_;
    mutable std::unordered_map<uint32_t, std::unique_ptr<UniqueMods>> unique_mods_cache_;

    // Small enough (90 rows) that parsing it up front beats indexing it.
    std::unordered_map<std::string, ItemClass> classes_;
    std::string data_version_;
    std::string unique_mods_attribution_;
    /// How many items the data build's crawl found trading on the currency exchange. 0 means
    /// no dataset, never "none trade" — see `has_exchange_flags()`.
    int exchange_items_ = 0;
};

} // namespace ppc::data
