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

    /// "Item Class: Rings" -> the trade `category` option, e.g. "accessory.ring".
    /// Empty when the class has no trade category, which is not an error.
    std::string_view trade_category_for(std::string_view item_class) const;
    const ItemClass* item_class(std::string_view item_class) const;

    std::string_view data_version() const { return data_version_; }
    size_t stat_count() const { return stats_matcher_index_.size(); }

private:
    /// Parses the ndjson line at `offset`, memoising it. Records are stable for the
    /// lifetime of this GameData, so callers may hold the returned pointer.
    const Stat* stat_at(uint32_t offset) const;
    const BaseType* base_at(uint32_t offset) const;
    std::string_view line_at(const MappedFile& f, uint32_t offset) const;

    MappedFile stats_nd_, items_nd_;
    MappedFile stats_matcher_idx_, stats_ref_idx_, items_name_idx_;
    HashIndex stats_matcher_index_, stats_ref_index_, items_name_index_;

    // Parsed on demand. mutable because lookups are logically const.
    mutable std::unordered_map<uint32_t, std::unique_ptr<Stat>> stat_cache_;
    mutable std::unordered_map<uint32_t, std::unique_ptr<BaseType>> base_cache_;

    // Small enough (90 rows) that parsing it up front beats indexing it.
    std::unordered_map<std::string, ItemClass> classes_;
    std::string data_version_;
};

} // namespace ppc::data
