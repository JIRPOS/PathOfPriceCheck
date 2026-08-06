#include "data/game_data.hpp"

#include <array>
#include <fstream>

#include <nlohmann/json.hpp>

#include "util/fnv1a.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ppc::data {
namespace {

constexpr std::array<std::string_view, static_cast<size_t>(ModType::Count)> kPrefixes{
    "explicit", "implicit", "fractured", "enchant", "crafted", "veiled", "pseudo",
    "scourge", "crucible", "sanctum", "delve", "ultimatum", "imbued", "mercenary"};

constexpr std::array<std::string_view, 6> kNamespaces{
    "ITEM", "UNIQUE", "GEM", "DIVINATION_CARD", "CAPTURED_BEAST", "AREA"};

std::optional<std::pair<int, int>> read_range(const json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 2) return std::nullopt;
    return std::pair<int, int>{(*it)[0].get<int>(), (*it)[1].get<int>()};
}

UniqueMod read_unique_mod(const json& j) {
    UniqueMod m;
    m.mod = j.value("mod", std::string());
    m.implicit = j.value("implicit", false);
    if (const auto fs = j.find("filters"); fs != j.end() && fs->is_array()) {
        for (const json& f : *fs) {
            if (!f.is_object()) continue;
            UniqueModFilter uf;
            uf.ref = f.value("ref", std::string());
            uf.trade_id = f.value("tradeId", std::string());
            if (const auto r = f.find("range"); r != f.end() && r->is_array()) {
                for (const json& pair : *r)
                    if (pair.is_array() && pair.size() == 2)
                        uf.ranges.emplace_back(pair[0].get<double>(), pair[1].get<double>());
            }
            m.filters.push_back(std::move(uf));
        }
    }
    return m;
}

} // namespace

std::string_view to_string(Namespace ns) { return kNamespaces[static_cast<size_t>(ns)]; }

std::optional<Namespace> namespace_from_string(std::string_view s) {
    for (size_t i = 0; i < kNamespaces.size(); ++i)
        if (kNamespaces[i] == s) return static_cast<Namespace>(i);
    return std::nullopt;
}

std::string_view trade_prefix(ModType t) { return kPrefixes[static_cast<size_t>(t)]; }

std::optional<ModType> mod_type_from_prefix(std::string_view s) {
    for (size_t i = 0; i < kPrefixes.size(); ++i)
        if (kPrefixes[i] == s) return static_cast<ModType>(i);
    return std::nullopt;
}

const StatMatcher* Stat::matcher_for(std::string_view normalized) const {
    for (const StatMatcher& m : matchers)
        if (m.string == normalized) return &m;
    return nullptr;
}

std::shared_ptr<GameData> GameData::open(const fs::path& dir, std::string_view lang,
                                         std::string* err) {
    const auto fail = [err](std::string msg) -> std::shared_ptr<GameData> {
        if (err) *err = std::move(msg);
        return nullptr;
    };

    auto gd = std::shared_ptr<GameData>(new GameData);
    const std::string p = std::string(lang) + "-";

    struct Want {
        MappedFile& file;
        std::string name;
    };
    const std::array<Want, 5> wants{{
        {gd->stats_nd_, p + "stats.ndjson"},
        {gd->items_nd_, p + "items.ndjson"},
        {gd->stats_matcher_idx_, p + "stats-matcher.index.bin"},
        {gd->stats_ref_idx_, p + "stats-ref.index.bin"},
        {gd->items_name_idx_, p + "items-name.index.bin"},
    }};
    for (const Want& w : wants)
        if (!w.file.open(dir / w.name)) return fail("cannot map " + w.name);

    if (!gd->stats_matcher_index_.attach(gd->stats_matcher_idx_.data(),
                                         gd->stats_matcher_idx_.size()) ||
        !gd->stats_ref_index_.attach(gd->stats_ref_idx_.data(), gd->stats_ref_idx_.size()) ||
        !gd->items_name_index_.attach(gd->items_name_idx_.data(), gd->items_name_idx_.size()))
        return fail("malformed index (not a whole number of rows)");

    // Optional, and missing on every bundle published before the dataset existed: a unique
    // then falls back to what a printed range can prove, which is what the app did before.
    // Both files or neither — an index without its ndjson resolves to offsets into nothing.
    if (gd->unique_mods_nd_.open(dir / (p + "unique-mods.ndjson")) &&
        gd->unique_mods_name_idx_.open(dir / (p + "unique-mods-name.index.bin")))
        gd->unique_mods_name_index_.attach(gd->unique_mods_name_idx_.data(),
                                           gd->unique_mods_name_idx_.size());

    // Small table, read eagerly.
    std::ifstream cls(dir / "item-classes.ndjson");
    if (!cls) return fail("cannot read item-classes.ndjson");
    for (std::string line; std::getline(cls, line);) {
        if (line.empty()) continue;
        const json j = json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) continue;
        ItemClass ic;
        ic.item_class = j.value("itemClass", std::string());
        ic.id = j.value("id", std::string());
        ic.trade_category = j.value("tradeCategory", std::string());
        if (!ic.item_class.empty()) gd->classes_.emplace(ic.item_class, std::move(ic));
    }
    if (gd->classes_.empty()) return fail("item-classes.ndjson is empty");

    std::ifstream mf(dir / "manifest.json");
    if (mf) {
        const json j = json::parse(mf, nullptr, false);
        if (!j.is_discarded()) {
            gd->data_version_ = j.value("data_version", std::string());
            if (const auto s = j.find("source"); s != j.end() && s->is_object())
                gd->unique_mods_attribution_ =
                    s->value("unique_mods_attribution", std::string());
        }
    }
    return gd;
}

std::string_view GameData::line_at(const MappedFile& f, uint32_t offset) const {
    // A valid file from a buggy builder can still point past the end; the sha256 says
    // nothing about that. One comparison is cheaper than the crash.
    if (offset >= f.size()) return {};
    std::string_view v = f.view().substr(offset);
    const size_t nl = v.find('\n');
    return nl == std::string_view::npos ? v : v.substr(0, nl);
}

const Stat* GameData::stat_at(uint32_t offset) const {
    if (const auto it = stat_cache_.find(offset); it != stat_cache_.end())
        return it->second.get();

    const std::string_view line = line_at(stats_nd_, offset);
    if (line.empty()) return nullptr;
    const json j = json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return nullptr;

    auto s = std::make_unique<Stat>();
    s->ref = j.value("ref", std::string());
    s->better = j.value("better", 1);
    s->dp = j.value("dp", 0);
    if (const auto m = j.find("matchers"); m != j.end() && m->is_array()) {
        for (const json& e : *m) {
            StatMatcher sm;
            sm.string = e.value("string", std::string());
            sm.negate = e.value("negate", false);
            if (const auto v = e.find("value"); v != e.end() && v->is_number())
                sm.value = v->get<double>();
            if (!sm.string.empty()) s->matchers.push_back(std::move(sm));
        }
    }
    if (const auto t = j.find("trade"); t != j.end() && t->is_object()) {
        s->inverted = t->value("inverted", false);
        if (const auto ids = t->find("ids"); ids != t->end() && ids->is_object()) {
            for (const auto& [prefix, arr] : ids->items()) {
                const auto mt = mod_type_from_prefix(prefix);
                if (!mt || !arr.is_array()) continue;
                for (const json& id : arr)
                    if (id.is_string())
                        s->ids[static_cast<size_t>(*mt)].push_back(id.get<std::string>());
            }
        }
    }
    return stat_cache_.emplace(offset, std::move(s)).first->second.get();
}

const BaseType* GameData::base_at(uint32_t offset) const {
    if (const auto it = base_cache_.find(offset); it != base_cache_.end())
        return it->second.get();

    const std::string_view line = line_at(items_nd_, offset);
    if (line.empty()) return nullptr;
    const json j = json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return nullptr;

    auto b = std::make_unique<BaseType>();
    b->name = j.value("name", std::string());
    b->ref_name = j.value("refName", b->name);
    b->ns = namespace_from_string(j.value("namespace", std::string("ITEM")))
                .value_or(Namespace::Item);
    b->trade_disc = j.value("tradeDisc", std::string());
    b->metadata_id = j.value("metadataId", std::string());
    b->w = j.value("w", 0);
    b->h = j.value("h", 0);
    b->drop_level = j.value("dropLevel", 0);
    if (const auto c = j.find("craftable"); c != j.end() && c->is_object()) {
        b->category = c->value("category", std::string());
        b->corrupted = c->value("corrupted", false);
    }
    if (const auto u = j.find("unique"); u != j.end() && u->is_object())
        b->unique_base = u->value("base", std::string());
    if (const auto a = j.find("armour"); a != j.end() && a->is_object()) {
        b->armour = read_range(*a, "ar");
        b->evasion = read_range(*a, "ev");
        b->energy_shield = read_range(*a, "es");
        b->ward = read_range(*a, "ward");
    }
    return base_cache_.emplace(offset, std::move(b)).first->second.get();
}

const UniqueMods* GameData::unique_mods_at(uint32_t offset) const {
    if (const auto it = unique_mods_cache_.find(offset); it != unique_mods_cache_.end())
        return it->second.get();

    const std::string_view line = line_at(unique_mods_nd_, offset);
    if (line.empty()) return nullptr;
    const json j = json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return nullptr;

    auto u = std::make_unique<UniqueMods>();
    u->name = j.value("name", std::string());
    u->base = j.value("base", std::string());
    if (const auto f = j.find("fixed"); f != j.end() && f->is_array())
        for (const json& m : *f)
            if (m.is_object()) u->fixed.push_back(read_unique_mod(m));
    if (const auto ps = j.find("pools"); ps != j.end() && ps->is_array()) {
        for (const json& p : *ps) {
            if (!p.is_object()) continue;
            UniqueModPool pool;
            pool.hint = p.value("hint", std::string());
            pool.implicit = p.value("implicit", false);
            pool.count = read_range(p, "count");
            if (const auto ms = p.find("mods"); ms != p.end() && ms->is_array())
                for (const json& m : *ms)
                    if (m.is_object()) pool.mods.push_back(read_unique_mod(m));
            u->pools.push_back(std::move(pool));
        }
    }
    if (const auto un = j.find("unlisted"); un != j.end() && un->is_array())
        for (const json& s : *un)
            if (s.is_string()) u->unlisted.push_back(s.get<std::string>());
    return unique_mods_cache_.emplace(offset, std::move(u)).first->second.get();
}

const UniqueMods* GameData::find_unique_mods(std::string_view name) const {
    std::string key(to_string(Namespace::Unique));
    key += "::";
    key += name;

    std::vector<uint32_t> offsets;
    unique_mods_name_index_.lookup(key, offsets);
    for (uint32_t off : offsets) {
        const UniqueMods* u = unique_mods_at(off);
        if (u && u->name == name) return u;
    }
    return nullptr;
}

std::vector<const Stat*> GameData::find_stats(std::string_view normalized) const {
    std::vector<uint32_t> offsets;
    stats_matcher_index_.lookup(normalized, offsets);
    std::vector<const Stat*> out;
    for (uint32_t off : offsets) {
        const Stat* s = stat_at(off);
        // Re-verify: fnv1a32 collides, and a run can mix distinct keys.
        if (s && s->matcher_for(normalized)) out.push_back(s);
    }
    return out;
}

const Stat* GameData::find_stat(std::string_view normalized, ModType type) const {
    const std::vector<const Stat*> all = find_stats(normalized);
    const Stat* found = nullptr;
    for (const Stat* s : all) {
        if (!s->has(type)) continue;
        if (found) return nullptr; // ambiguous; the caller needs more context than we have
        found = s;
    }
    return found;
}

const Stat* GameData::find_stat_by_ref(std::string_view ref) const {
    std::vector<uint32_t> offsets;
    stats_ref_index_.lookup(ref, offsets);
    for (uint32_t off : offsets) {
        const Stat* s = stat_at(off);
        if (s && s->ref == ref) return s;
    }
    return nullptr;
}

std::vector<const BaseType*> GameData::find_bases(Namespace ns,
                                                  std::string_view name) const {
    std::string key(to_string(ns));
    key += "::";
    key += name;

    std::vector<uint32_t> offsets;
    items_name_index_.lookup(key, offsets);
    std::vector<const BaseType*> out;
    for (uint32_t off : offsets) {
        const BaseType* b = base_at(off);
        if (b && b->ns == ns && b->name == name) out.push_back(b);
    }
    return out;
}

const ItemClass* GameData::item_class(std::string_view cls) const {
    const auto it = classes_.find(std::string(cls));
    return it == classes_.end() ? nullptr : &it->second;
}

std::string_view GameData::trade_category_for(std::string_view cls) const {
    const ItemClass* ic = item_class(cls);
    return ic ? std::string_view(ic->trade_category) : std::string_view();
}

} // namespace ppc::data
