#include "report/report.hpp"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string_view>

#include <nlohmann/json.hpp>

#include "data/types.hpp"
#include "util/base64.hpp"

namespace ppc::report {
namespace {

/// The deployed relay. Public on purpose: it is an endpoint, not a credential — see the header.
constexpr const char* kRelay = "https://ppc-reports.jirpos.workers.dev/report";

std::string num(double v, int dp = 2) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", dp, v);
    std::string s = buf;
    // Trailing zeros on a number nobody asked to be precise are noise in a column of them.
    if (s.find('.') != std::string::npos) {
        s.erase(s.find_last_not_of('0') + 1);
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

std::string opt_num(const std::optional<double>& v, int dp = 2) {
    return v ? num(*v, dp) : std::string("-");
}

std::string opt_int(const std::optional<int>& v) {
    return v ? std::to_string(*v) : std::string("-");
}

const char* yn(bool b) { return b ? "yes" : "no"; }

/// An interval as a filter states it: `[77,90]`, `[77,]`, `[,90]`, `[]`.
std::string interval(const std::optional<double>& lo, const std::optional<double>& hi, int dp) {
    std::string s = "[";
    if (lo) s += num(*lo, dp);
    s += ",";
    if (hi) s += num(*hi, dp);
    return s + "]";
}

void heading(std::string& out, const char* title) {
    if (!out.empty()) out += "\n";
    out += "== ";
    out += title;
    out += " ==\n";
}

void describe_item(std::string& out, const item::Item& it) {
    heading(out, "Item");
    out += "class: " + (it.item_class.empty() ? std::string("-") : it.item_class) + "\n";
    out += "rarity: " + std::string(item::to_string(it.rarity)) + "\n";
    if (!it.name.empty()) out += "name: " + it.name + "\n";
    out += "base line: " + it.base_type + "\n";
    // The two differ exactly when a magic item's affixes were stripped off the base line, which
    // is one of the things a "priced the wrong item" report is usually about.
    if (it.base_name != it.base_type) out += "base name: " + it.base_name + "\n";
    if (!it.type_line.empty()) out += "type line: " + it.type_line + "\n";
    out += std::string("identified: ") + yn(it.identified) + "  corrupted: " + yn(it.corrupted) +
           "  mirrored: " + yn(it.mirrored) + "  synthesised: " + yn(it.synthesised) +
           "  fractured: " + yn(it.fractured_item) + "  foulborn: " + yn(it.foulborn) + "\n";
    out += "item level: " + opt_int(it.item_level) + "  quality: " + opt_int(it.quality);
    if (it.map_tier) out += "  map tier: " + std::to_string(*it.map_tier);
    if (it.gem_level) out += "  gem level: " + std::to_string(*it.gem_level);
    out += "\n";
    if (it.socket_count)
        out += "sockets: " + it.sockets + " (" + std::to_string(it.socket_count) + " sockets, " +
               std::to_string(it.link_count) + " links)\n";
    if (!it.influences.empty()) {
        out += "influences:";
        for (const item::Influence i : it.influences) out += " " + std::string(item::to_string(i));
        out += "\n";
    }
}

void describe_resolved(std::string& out, const item::Item& it) {
    heading(out, "Resolved");
    if (it.base)
        out += "base record: " + it.base->name + " [ref " + it.base->ref_name + "] category " +
               (it.base->category.empty() ? "-" : it.base->category) +
               (it.base->trade_disc.empty() ? "" : " disc " + it.base->trade_disc) + "\n";
    else
        out += "base record: NONE — the bundle has no base type under this name\n";
    if (it.unique_entry)
        out += "unique record: " + it.unique_entry->name + " on " + it.unique_entry->unique_base +
               "\n";
    if (!it.unique_candidates.empty()) {
        out += "unidentified, " + std::to_string(it.unique_candidates.size()) +
               " unique(s) drop on this base:";
        for (const data::BaseType* u : it.unique_candidates) out += " " + u->name + ";";
        out += "\n";
    }
}

void describe_properties(std::string& out, const item::Item& it) {
    if (it.properties.empty()) return;
    heading(out, "Properties");
    for (const item::Property& p : it.properties)
        out += (p.label.empty() ? std::string("(prose)") : p.label) + ": " + p.value +
               (p.augmented ? "  (augmented)" : "") + (p.num ? "  num " + num(*p.num) : "") + "\n";
}

/// One modifier and what it resolved to. **The interesting line is the one that says NO MATCH**:
/// a wording nothing in the bundle recognises is the single most common thing behind "this priced
/// wrong", and it is the reason the whole dump exists.
void describe_mods(std::string& out, const item::Item& it) {
    heading(out, "Modifiers");
    if (it.mods.empty()) out += "(none)\n";
    for (size_t i = 0; i < it.mods.size(); ++i) {
        const item::Modifier& m = it.mods[i];
        out += "[" + std::to_string(i) + "] " + std::string(data::trade_prefix(m.type));
        if (m.affix == item::Affix::Prefix) out += " prefix";
        else if (m.affix == item::Affix::Suffix) out += " suffix";
        if (!m.affix_name.empty()) out += " \"" + m.affix_name + "\"";
        if (!m.generation.empty()) out += " gen \"" + m.generation + "\"";
        if (m.tier) out += " tier " + std::to_string(m.tier);
        if (m.rank) out += " rank " + std::to_string(m.rank);
        if (!m.qualifier.empty()) out += " " + m.qualifier;
        if (m.roll_incr != 0) out += " incr " + num(m.roll_incr) + "%";
        if (m.added_unique) out += " added-to-unique";
        if (!m.advanced) out += " (no info line)";
        if (m.continuation) out += " (continuation)";
        out += "\n";
        for (const std::string& line : m.lines) out += "    | " + line + "\n";
        if (!m.match) {
            out += "    -> NO MATCH\n";
            continue;
        }
        const data::StatMatch& s = *m.match;
        out += "    -> stat \"" + (s.stat ? s.stat->ref : std::string("?")) + "\"";
        if (s.stat) {
            const std::vector<std::string>& ids = s.stat->trade_ids(s.mod_type);
            out += " id " + (ids.empty() ? std::string("NONE — matched but not searchable")
                                         : ids.front());
        }
        out += " value " + num(s.value, s.stat ? s.stat->dp : 2);
        out += " bounds " + interval(s.min, s.max, s.stat ? s.stat->dp : 2);
        if (s.lines_consumed != 1) out += " over " + std::to_string(s.lines_consumed) + " lines";
        if (s.negated) out += " negated";
        if (s.legacy) out += " legacy";
        if (s.unscalable) out += " unscalable";
        out += "\n";
    }
}

void describe_derived(std::string& out, const item::Derived& d) {
    heading(out, "Derived");
    out += "pdps " + opt_num(d.pdps) + "  edps " + opt_num(d.edps) + "  cdps " + opt_num(d.cdps) +
           "  dps " + opt_num(d.dps) + "\n";
    out += "at q20: pdps " + opt_num(d.pdps_q20) + "  dps " + opt_num(d.dps_q20) + "  ar " +
           opt_int(d.armour_q20) + "  ev " + opt_int(d.evasion_q20) + "  es " +
           opt_int(d.energy_shield_q20) + "  ward " + opt_int(d.ward_q20) + "\n";
    out += "base percentile: " + opt_num(d.base_pct, 3) + "\n";
}

void describe_plan(std::string& out, const item::SearchPlan& plan) {
    heading(out, "Search plan");
    out += "strategy: " + std::string(item::to_string(plan.strategy)) + "\n";
    out += "category: " + (plan.category.empty() ? "-" : plan.category) +
           "  name: " + (plan.name.empty() ? "-" : plan.name) +
           "  type: " + (plan.type.empty() ? "-" : plan.type) + "  rarity: " + plan.rarity;
    if (!plan.discriminator.empty()) out += "  disc: " + plan.discriminator;
    out += "\n";

    for (const item::OptionFilter& f : plan.options)
        out += std::string("option ") + (f.enabled ? "[x] " : "[ ] ") + f.key + "=" + f.option +
               (f.shown ? "  (shown)" : "") + "\n";
    for (const item::ChoiceGroup& g : plan.choices)
        out += "choice: " + g.label + (g.note.empty() ? "" : " — " + g.note) + "\n";
    if (!plan.choices.empty()) out += "chosen: " + std::to_string(plan.choice) + "\n";
    for (const item::NumericFilter& f : plan.numerics)
        out += std::string("numeric ") + (f.enabled ? "[x] " : "[ ] ") + f.key + " " +
               interval(f.min, f.max, f.dp) + (f.hidden ? " hidden" : "") + "  \"" + f.label +
               "\"\n";
    for (const item::StatFilter& f : plan.stats) {
        out += std::string("stat ") + (f.enabled ? "[x] " : "[ ] ") +
               (f.id.empty() ? "(no id)" : f.id) + " " + interval(f.min, f.max, f.dp);
        if (f.roll_min || f.roll_max) out += " of " + interval(f.roll_min, f.roll_max, f.dp);
        if (f.negated) out += " absent";
        if (f.hidden) out += " hidden";
        if (f.pooled) out += " pooled";
        if (f.tiered) out += " tiered";
        if (f.inverted) out += " inverted";
        if (f.choice) out += " choice " + std::to_string(*f.choice);
        if (!f.merged.empty()) out += " +" + std::to_string(f.merged.size()) + " merged";
        out += "  \"" + f.text + "\"\n";
    }
    for (const std::string& n : plan.notes) out += "note: " + n + "\n";
}

} // namespace

std::string relay_url() {
    if (const char* env = std::getenv("PPC_REPORT_URL"); env && *env) return env;
    return kRelay;
}

std::string describe(const item::Item& it, const item::Derived& d, const item::SearchPlan& plan) {
    std::string out;
    describe_item(out, it);
    describe_resolved(out, it);
    describe_properties(out, it);
    describe_mods(out, it);
    describe_derived(out, d);
    describe_plan(out, plan);
    if (!it.unparsed.empty()) {
        heading(out, "Lines this tool did not understand");
        for (const std::string& l : it.unparsed) out += l + "\n";
    }
    return out;
}

std::string to_json(const Report& r) {
    nlohmann::json meta = nlohmann::json::object();
    // Only the fields that have something to say: the relay caps each at 64 characters and an
    // empty one is noise in a Discord embed.
    if (!r.meta.version.empty()) meta["version"] = r.meta.version;
    if (!r.meta.os.empty()) meta["os"] = r.meta.os;
    if (!r.meta.league.empty()) meta["league"] = r.meta.league;
    if (!r.meta.bundle.empty()) meta["bundle"] = r.meta.bundle;

    nlohmann::json j = nlohmann::json::object();
    j["item"] = r.item;
    j["parse"] = r.parse;
    if (!r.comment.empty()) j["comment"] = r.comment;
    if (!meta.empty()) j["meta"] = meta;
    if (!r.png.empty()) j["screenshot_png_b64"] = base64_encode(r.png);
    return j.dump();
}

Outcome read_response(long status, const std::string& body, const std::string& transport) {
    Outcome o;
    if (!transport.empty()) {
        o.error = "Could not reach the report relay: " + transport;
        return o;
    }
    // The relay states its own reason for every refusal, and it is a better message than any
    // status-code table here could be. A body that is not the JSON it promised is the only case
    // this has to invent words for.
    std::string reason;
    std::string id;
    if (const nlohmann::json j = nlohmann::json::parse(body, nullptr, false); j.is_object()) {
        if (const auto e = j.find("error"); e != j.end() && e->is_string()) reason = e->get<std::string>();
        if (const auto i = j.find("id"); i != j.end() && i->is_string()) id = i->get<std::string>();
    }
    if (status >= 200 && status < 300 && !id.empty()) {
        o.ok = true;
        o.id = id;
        return o;
    }
    o.error = !reason.empty() ? reason
                              : "The relay answered " + std::to_string(status) + " and said nothing";
    return o;
}

} // namespace ppc::report
