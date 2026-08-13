#include "mapcheck/rate.hpp"

#include <algorithm>
#include <map>

#include "mapcheck/filter.hpp"

namespace ppc::mapcheck {
namespace {

/// Everything the popup rates.
///
/// **Implicits are in it.** They were left out on the argument that an implicit is what the base
/// came with rather than what it rolled — which is true of a Nightmare map saying it is one, and
/// false of the Vaal corruption implicits, which roll, which the pool carries as generation 5,
/// and which are rateable in Settings. Leaving them off the map was an inconsistency the design
/// admitted to and paid for in a line the reader could see and not decide about.
///
/// Enchantments stay out — nothing that opens in the map device carries one — and so does
/// `Pseudo`, which is not a printed line at all.
bool rateable_type(data::ModType t) {
    switch (t) {
    case data::ModType::Implicit:
    case data::ModType::Explicit:
    case data::ModType::Fractured:
    case data::ModType::Crafted:
    case data::ModType::Veiled:
    case data::ModType::Scourge:
    case data::ModType::Crucible:
        return true;
    default:
        return false;
    }
}

} // namespace

int map_domain_of(const item::Item& it, const data::GameData* gd) {
    if (gd) {
        const int d = gd->mod_domain_for(it.base, it.item_class);
        if (d) return d;
    }
    // The bundle said nothing — it predates the field, or the base did not resolve. The
    // clipboard still did, and these are the same items read from the other side.
    if (it.is_chart()) return kChartDomain;
    if (it.is_heist()) return kHeistDomain;
    if (it.is_map() || it.is_logbook() || it.is_map_fragment()) return kMapDomain;
    return 0;
}

bool is_rateable_item(const item::Item& it, const data::GameData* gd) {
    const int d = map_domain_of(it, gd);
    return std::find(std::begin(kDomains), std::end(kDomains), d) != std::end(kDomains);
}

std::vector<std::string> pool_key_refs(const data::PoolMod& m) {
    std::vector<std::string> out;
    out.reserve(m.stats.size());
    for (const data::PoolStat& s : m.stats)
        if (!s.ref.empty()) out.push_back(s.ref);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<PoolGroup> pool_groups(const data::GameData& gd) {
    std::vector<PoolGroup> out;
    std::map<std::string, size_t, std::less<>> at;
    // `kDomains` in order, so the first entry to claim a key is the map's where a chart shares
    // it — which is what makes the row show a map's wording without anything having to ask.
    for (const int domain : kDomains)
        for (const data::PoolMod* m : gd.mod_pool(domain)) {
            std::vector<std::string> refs = pool_key_refs(*m);
            if (refs.empty()) continue;
            const auto [it, fresh] = at.emplace(affix_key(refs), out.size());
            if (!fresh) {
                out[it->second].all.push_back(m);
                continue;
            }
            out.push_back(PoolGroup{m, {m}, std::move(refs)});
        }
    return out;
}

std::vector<std::string> group_lines(const PoolGroup& g, const data::GameData* gd) {
    std::vector<std::string> out;
    for (const data::PoolMod* m : g.all) {
        std::vector<std::string> lines = matchable_lines(*m, gd);
        for (std::string& l : lines)
            if (std::find(out.begin(), out.end(), l) == out.end()) out.push_back(std::move(l));
    }
    return out;
}

std::vector<std::string> pool_refs_for(const std::vector<std::string>& printed, int domain,
                                       const data::GameData* gd) {
    if (printed.empty() || !gd) return printed;
    std::vector<std::string> want = printed;
    std::sort(want.begin(), want.end());

    std::vector<std::string> best, sorted;
    for (const data::PoolMod* m : gd->mod_pool(domain)) {
        if (m->stats.size() < want.size()) continue;
        sorted.clear();
        for (const data::PoolStat& s : m->stats)
            if (!s.ref.empty()) sorted.push_back(s.ref);
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        if (!std::includes(sorted.begin(), sorted.end(), want.begin(), want.end())) continue;
        // The smallest entry that covers what was printed: a bigger one would be a different
        // affix that happens to grant these as well.
        if (!best.empty() && best.size() <= sorted.size()) continue;
        best = sorted;
    }
    return best.empty() ? printed : best;
}

std::vector<Row> rate(const item::Item& it, const Store& store, const data::GameData* gd) {
    const int domain = map_domain_of(it, gd);
    std::vector<Row> rows;
    for (const item::Modifier& m : it.mods) {
        if (!rateable_type(m.type)) continue;
        // The second and later stats of one affix join the row the first opened — that grouping
        // is the whole of what makes a verdict about an affix rather than about a wording, and
        // only Advanced Mod Descriptions supplies it. Without it every line opens its own row,
        // which is right for the affixes that grant one.
        if (m.continuation && !rows.empty()) {
            Row& r = rows.back();
            r.mods.push_back(&m);
            if (m.match && m.match->stat) r.refs.push_back(m.match->stat->ref);
            continue;
        }
        Row r;
        r.mods.push_back(&m);
        // The record's identity, never the printed line: see `Profile`.
        if (m.match && m.match->stat) r.refs.push_back(m.match->stat->ref);
        rows.push_back(std::move(r));
    }
    for (Row& r : rows) {
        if (!r.rateable()) continue;
        r.refs = pool_refs_for(r.refs, domain, gd);
        r.verdict = store.verdict_of(r.refs);
    }
    return rows;
}

Tally tally(const std::vector<Row>& rows) {
    Tally t;
    // A row nothing could be keyed on counts as unrated rather than being left out: it is a
    // modifier on the map and the reader has not decided about it, which is exactly what
    // unrated means. Leaving it out would make a map of unreadable lines look fully rated.
    for (const Row& r : rows) t.add(r.verdict);
    return t;
}

} // namespace ppc::mapcheck
