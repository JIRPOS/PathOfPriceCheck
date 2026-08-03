#include "screens/item_view.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include <imgui.h>

namespace ppc {
namespace {

using item::Element;
using item::Item;
using item::Modifier;
using item::Rarity;

// The game's own palette, brightened where its darkest values (fire is #960000) would be
// unreadable on the overlay's background rather than on the item frame's.
constexpr ImU32 kColNormal = IM_COL32(200, 200, 200, 255);
constexpr ImU32 kColMagic = IM_COL32(136, 136, 255, 255);
constexpr ImU32 kColRare = IM_COL32(255, 255, 119, 255);
constexpr ImU32 kColUnique = IM_COL32(214, 129, 62, 255);
constexpr ImU32 kColGem = IM_COL32(27, 162, 155, 255);
constexpr ImU32 kColCurrency = IM_COL32(170, 158, 130, 255);
constexpr ImU32 kColQuest = IM_COL32(74, 230, 58, 255);

constexpr ImU32 kColMod = IM_COL32(136, 136, 255, 255);
constexpr ImU32 kColCrafted = IM_COL32(184, 218, 242, 255); ///< crafted and enchanted
constexpr ImU32 kColFractured = IM_COL32(163, 141, 109, 255);
constexpr ImU32 kColScourge = IM_COL32(255, 110, 255, 255);
constexpr ImU32 kColCorrupted = IM_COL32(210, 0, 0, 255);
constexpr ImU32 kColLabel = IM_COL32(127, 127, 127, 255);
constexpr ImU32 kColValue = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kColAugmented = IM_COL32(136, 136, 255, 255);
constexpr ImU32 kColInfo = IM_COL32(128, 128, 128, 255);
constexpr ImU32 kColFlavour = IM_COL32(175, 96, 37, 255);
constexpr ImU32 kColUnparsed = IM_COL32(150, 150, 150, 255);

constexpr ImU32 kColFire = IM_COL32(230, 64, 64, 255);
constexpr ImU32 kColCold = IM_COL32(102, 153, 255, 255);
constexpr ImU32 kColLightning = IM_COL32(255, 215, 0, 255);
constexpr ImU32 kColChaos = IM_COL32(210, 96, 208, 255);

ImU32 rarity_colour(Rarity r) {
    switch (r) {
        case Rarity::Magic: return kColMagic;
        case Rarity::Rare: return kColRare;
        case Rarity::Unique: return kColUnique;
        case Rarity::Gem: return kColGem;
        case Rarity::Currency: return kColCurrency;
        case Rarity::DivinationCard: return kColGem;
        case Rarity::Quest: return kColQuest;
        default: return kColNormal;
    }
}

ImU32 element_colour(Element e) {
    switch (e) {
        case Element::Fire: return kColFire;
        case Element::Cold: return kColCold;
        case Element::Lightning: return kColLightning;
        case Element::Chaos: return kColChaos;
        default: return kColValue;
    }
}

ImU32 mod_colour(data::ModType t) {
    switch (t) {
        case data::ModType::Crafted:
        case data::ModType::Enchant:
        case data::ModType::Veiled: return kColCrafted;
        case data::ModType::Fractured: return kColFractured;
        case data::ModType::Scourge: return kColScourge;
        default: return kColMod;
    }
}

struct Segment {
    std::string text;
    ImU32 colour = kColValue;
};

/// One centred line built from differently coloured runs. The game centres everything in a
/// tooltip; a run too long for the panel falls back to left-aligned wrapping, because a
/// centred wrap is unreadable at this width.
void draw_segments(const std::vector<Segment>& segs) {
    float width = 0;
    for (const Segment& s : segs)
        width += ImGui::CalcTextSize(s.text.c_str(), s.text.c_str() + s.text.size()).x;

    const float avail = ImGui::GetContentRegionAvail().x;
    const bool fits = width <= avail;
    if (fits) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - width) * 0.5f);

    for (size_t i = 0; i < segs.size(); ++i) {
        if (i) ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_Text, segs[i].colour);
        if (!fits) ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(segs[i].text.c_str(), segs[i].text.c_str() + segs[i].text.size());
        if (!fits) ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
}

void draw_line(std::string_view text, ImU32 colour) {
    draw_segments({{std::string(text), colour}});
}

/// A dimmer rule than ImGui's own; the game separates a tooltip's blocks with a thin line.
void draw_rule() {
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(90, 90, 90, 160));
    ImGui::Separator();
    ImGui::PopStyleColor();
}

std::string format_number(double v, int dp) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", dp, v);
    return buf;
}

std::string format_range(const item::DamageRange& r) {
    const int dp = r.min == static_cast<int>(r.min) && r.max == static_cast<int>(r.max) ? 0 : 1;
    if (r.min == r.max) return format_number(r.min, dp);
    return format_number(r.min, dp) + "-" + format_number(r.max, dp);
}

void draw_name_plate(const Item& it, const Fonts& fonts) {
    const ImU32 colour = rarity_colour(it.rarity);
    const float base = ImGui::GetFontSize();

    // The game frames the name in a plate tinted by rarity.
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float lines = it.name.empty() ? 1.0f : 2.0f;
    const float height = base * 1.15f * lines + ImGui::GetStyle().ItemSpacing.y * lines;
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(start.x - 4, start.y - 2),
        ImVec2(start.x + ImGui::GetContentRegionAvail().x + 4, start.y + height),
        (colour & 0x00FFFFFF) | 0x22000000);

    ImGui::PushFont(fonts.small_caps, base * 1.15f);
    if (!it.name.empty()) draw_line(it.name, colour);
    draw_line(it.base_type, colour);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 2));
}

void draw_properties(const Item& it) {
    for (const item::Property& p : it.properties) {
        // Item level gets its own block further down, the way the game prints it.
        if (p.label == "Item Level") continue;
        const ImU32 value_colour = p.augmented ? kColAugmented : kColValue;

        if (p.label == "Elemental Damage" && !it.elemental.empty()) {
            std::vector<Segment> segs{{p.label + ": ", kColLabel}};
            for (size_t i = 0; i < it.elemental.size(); ++i) {
                if (i) segs.push_back({", ", kColLabel});
                segs.push_back({format_range(it.elemental[i]),
                                element_colour(it.elemental[i].element)});
            }
            draw_segments(segs);
            continue;
        }
        if (p.label.empty()) { // prose the game prints among the properties
            draw_line(p.value, kColValue);
            continue;
        }
        draw_segments({{p.label + ": ", kColLabel}, {p.value, value_colour}});
    }
    // A flask's own effect reads as a modifier and the game colours it like one. A gem's stats
    // are the same kind of line but the game prints them under the skill's description.
    if (it.rarity != Rarity::Gem)
        for (const std::string& l : it.inherent_lines) draw_line(l, kColMod);
}

void draw_requirements(const Item& it) {
    if (!it.req.level && !it.req.str && !it.req.dex && !it.req.intelligence) return;
    std::vector<Segment> segs{{"Requires ", kColLabel}};
    if (it.req.level) {
        segs.push_back({"Level ", kColLabel});
        segs.push_back({std::to_string(*it.req.level), kColValue});
    }
    const std::array<std::pair<const std::optional<int>&, const char*>, 3> attrs{
        {{it.req.str, "Str"}, {it.req.dex, "Dex"}, {it.req.intelligence, "Int"}}};
    for (const auto& [value, name] : attrs) {
        if (!value) continue;
        if (segs.size() > 1) segs.push_back({", ", kColLabel});
        segs.push_back({std::to_string(*value) + " ", kColValue});
        segs.push_back({name, kColLabel});
    }
    draw_segments(segs);
}

void draw_sockets(const Item& it) {
    if (it.sockets.empty()) return;
    std::vector<Segment> segs{{"Sockets: ", kColLabel}};
    for (const char c : it.sockets) {
        const ImU32 colour = c == 'R'   ? kColFire
                             : c == 'G' ? IM_COL32(140, 220, 120, 255)
                             : c == 'B' ? kColCold
                             : c == 'W' ? kColValue
                                        : kColLabel;
        segs.push_back({std::string(1, c), colour});
    }
    draw_segments(segs);
}

bool in(std::initializer_list<data::ModType> types, data::ModType t) {
    return std::find(types.begin(), types.end(), t) != types.end();
}

/// Every mod of one of `types`, in the order the clipboard listed them — a fractured or crafted
/// mod sits among the explicits on the item, not after them.
void draw_mods(const Item& it, const Fonts& fonts, std::initializer_list<data::ModType> types) {
    const float base = ImGui::GetFontSize();
    for (const Modifier& m : it.mods) {
        if (!in(types, m.type)) continue;
        // The game prints a modifier added to a unique in magenta, not in the mod blue.
        const ImU32 colour = m.added_unique() ? kColScourge : mod_colour(m.type);
        if (m.advanced && !m.continuation) {
            // The game prints the affix, its tier and its tags above the roll in small grey.
            ImGui::PushFont(fonts.italic, base * 0.82f);
            draw_line(m.info_text(), kColInfo);
            ImGui::PopFont();
        }
        for (const std::string& l : m.lines) draw_line(l, colour);
        ImGui::PushFont(fonts.italic, base * 0.85f);
        for (const std::string& r : m.reminder) draw_line(r, kColInfo);
        ImGui::PopFont();
    }
}

bool has_mods(const Item& it, std::initializer_list<data::ModType> types) {
    for (const Modifier& m : it.mods)
        if (in(types, m.type)) return true;
    return false;
}

} // namespace

void draw_item_tooltip(const Item& it, const Fonts& fonts) {
    ImGui::PushFont(fonts.small_caps, 0.0f);
    draw_name_plate(it, fonts);

    if (!it.type_line.empty() || !it.properties.empty() || !it.inherent_lines.empty()) {
        draw_rule();
        if (!it.type_line.empty()) draw_line(it.type_line, kColLabel);
        draw_properties(it);
    }
    if (it.req.level || it.req.str || it.req.dex || it.req.intelligence) {
        draw_rule();
        draw_requirements(it);
    }
    if (!it.description.empty()) {
        draw_rule();
        // A gem's skill description is in its own colour; a currency item's reads as a mod.
        const ImU32 colour = it.rarity == Rarity::Gem ? kColGem : kColMod;
        for (const std::string& l : it.description) draw_line(l, colour);
        if (it.rarity == Rarity::Gem) {
            draw_rule();
            for (const std::string& l : it.inherent_lines) draw_line(l, kColMod);
        }
    }
    if (!it.sockets.empty()) {
        draw_rule();
        draw_sockets(it);
    }
    if (it.item_level) {
        draw_rule();
        draw_segments({{"Item Level: ", kColLabel}, {std::to_string(*it.item_level), kColValue}});
    }

    // Blocks in the order the game prints them, each behind its own rule. Explicit, fractured,
    // crafted and veiled mods share one block, as on the item.
    for (const std::initializer_list<data::ModType> block :
         {std::initializer_list<data::ModType>{data::ModType::Enchant},
          {data::ModType::Implicit},
          {data::ModType::Explicit, data::ModType::Fractured, data::ModType::Crafted,
           data::ModType::Veiled},
          {data::ModType::Scourge},
          {data::ModType::Crucible}}) {
        if (!has_mods(it, block)) continue;
        draw_rule();
        draw_mods(it, fonts, block);
    }

    if (!it.identified) {
        draw_rule();
        draw_line("Unidentified", kColCorrupted);
    }
    if (it.corrupted || it.mirrored || it.split || !it.influences.empty() || it.synthesised ||
        it.fractured_item) {
        draw_rule();
        if (it.synthesised) draw_line("Synthesised Item", kColMod);
        if (it.fractured_item) draw_line("Fractured Item", kColFractured);
        for (const item::Influence i : it.influences)
            draw_line(std::string(item::to_string(i)) + " Item", kColMod);
        if (it.split) draw_line("Split", kColMod);
        if (it.mirrored) draw_line("Mirrored", kColMod);
        if (it.corrupted) draw_line("Corrupted", kColCorrupted);
    }
    if (!it.flavour_text.empty()) {
        draw_rule();
        ImGui::PushFont(fonts.italic, 0.0f);
        for (const std::string& l : it.flavour_text) draw_line(l, kColFlavour);
        ImGui::PopFont();
    }
    if (!it.help_text.empty()) {
        draw_rule();
        for (const std::string& l : it.help_text) draw_line(l, kColLabel);
    }
    for (const std::string& l : it.cosmetic_lines) draw_line(l, kColLabel);
    if (!it.unparsed.empty()) {
        draw_rule();
        for (const std::string& l : it.unparsed) draw_line(l, kColUnparsed);
    }
    if (!it.note.empty())
        draw_segments({{"Note: ", kColLabel}, {it.note, kColValue}});
    ImGui::PopFont();
}

void draw_derived_numbers(const Item& it, const item::Derived& d, const Fonts& fonts) {
    struct Row {
        const char* label;
        std::string value;
    };
    std::vector<Row> rows;
    const auto pct = [](const std::optional<double>& v) {
        return v ? format_number(*v * 100, 0) + "%" : std::string();
    };

    if (d.dps) {
        rows.push_back({"Total DPS", format_number(*d.dps, 1)});
        if (d.pdps) rows.push_back({"Physical DPS", format_number(*d.pdps, 1)});
        if (d.edps) rows.push_back({"Elemental DPS", format_number(*d.edps, 1)});
        if (d.cdps) rows.push_back({"Chaos DPS", format_number(*d.cdps, 1)});
        if (d.dps_q20 && it.quality.value_or(0) != 20)
            rows.push_back({"Total DPS at 20%", format_number(*d.dps_q20, 1)});
    }
    struct Defence {
        const char* label;
        const std::optional<int>& value;
        const std::optional<int>& q20;
        const std::optional<double>& percentile;
    };
    for (const Defence& def : std::initializer_list<Defence>{
             {"Armour", it.armour, d.armour_q20, d.armour_pct},
             {"Evasion", it.evasion, d.evasion_q20, d.evasion_pct},
             {"Energy Shield", it.energy_shield, d.energy_shield_q20, d.energy_shield_pct},
             {"Ward", it.ward, d.ward_q20, d.ward_pct}}) {
        if (!def.value) continue;
        std::string v = std::to_string(*def.value);
        if (def.q20 && *def.q20 != *def.value) v += " (" + std::to_string(*def.q20) + " at 20%)";
        if (def.percentile) v += "  base roll " + pct(def.percentile);
        rows.push_back({def.label, std::move(v)});
    }
    if (rows.empty()) return;

    ImGui::PushFont(fonts.small_caps, 0.0f);
    for (const Row& r : rows) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", r.label);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f);
        ImGui::TextUnformatted(r.value.c_str());
    }
    ImGui::PopFont();
}

} // namespace ppc
