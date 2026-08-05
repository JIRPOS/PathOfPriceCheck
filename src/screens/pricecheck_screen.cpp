#include "screens/pricecheck_screen.hpp"

#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <imgui.h>

#include "app.hpp"
#include "screens/item_view.hpp"

namespace ppc {
namespace {

constexpr ImVec4 kDim(0.55f, 0.55f, 0.55f, 1.0f);
constexpr ImVec4 kWarn(0.90f, 0.55f, 0.25f, 1.0f);

std::string format_number(double v, int dp) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*f", dp, v);
    return buf;
}

/// "80 to 89", "at least 42", or nothing at all for a mod with no roll to filter on.
std::string bounds_text(const std::optional<double>& min, const std::optional<double>& max,
                        int dp) {
    if (min && max) {
        if (*min == *max) return format_number(*min, dp);
        return format_number(*min, dp) + " to " + format_number(*max, dp);
    }
    if (min) return format_number(*min, dp) + "+";
    // Not "up to": the filter asks for -11 and everything past it, and "up to -11" reads to
    // half the world as the range between zero and -11.
    if (max) return format_number(*max, dp) + " or lower";
    return {};
}

/// The same, in the compact form the stat rows use: "[77-90]" for a bounded search, "42+" for
/// a search that only has a floor.
std::string range_text(const std::optional<double>& min, const std::optional<double>& max,
                       int dp) {
    if (min && max && *min != *max)
        return "[" + format_number(*min, dp) + "-" + format_number(*max, dp) + "]";
    if (min && max) return "[" + format_number(*min, dp) + "]";
    return bounds_text(min, max, dp);
}

/// The trade site's own colours for the two halves of the mod pool, which is where the user
/// reads them everywhere else.
constexpr ImVec4 kPrefix(0.85f, 0.36f, 0.36f, 1.0f);
constexpr ImVec4 kSuffix(0.45f, 0.62f, 0.95f, 1.0f);
constexpr ImVec4 kBounds(0.78f, 0.78f, 0.92f, 1.0f);

struct Code {
    std::string text;
    ImVec4 colour = kBounds;
};

/// Where a modifier came from, as short as it goes: "P2" is a tier-2 prefix, "S1" a tier-1
/// suffix, "R" a crafted one. Empty without Advanced Mod Descriptions, which is the only thing
/// that says which side of the pool a roll is from. A crafted mod is still a prefix or a
/// suffix, so the colour says which even though the letter no longer does.
Code affix_code(const item::Modifier& m) {
    Code c;
    if (m.type == data::ModType::Crafted) c.text = "R";
    else if (m.affix == item::Affix::Prefix) c.text = "P";
    else if (m.affix == item::Affix::Suffix) c.text = "S";
    else return {};
    if (m.affix == item::Affix::Prefix) c.colour = kPrefix;
    else if (m.affix == item::Affix::Suffix) c.colour = kSuffix;
    if (const int n = m.tier ? m.tier : m.rank) c.text += std::to_string(n);
    return c;
}

/// One code per modifier folded into the filter — two life rolls are searched as their total
/// but are still two affixes, and the user is picking which to keep.
std::vector<Code> affix_codes(const item::Item& it, const item::StatFilter& f) {
    std::vector<Code> out;
    if (Code c = affix_code(it.mods[f.mod_index]); !c.text.empty()) out.push_back(std::move(c));
    for (const size_t i : f.merged)
        if (Code c = affix_code(it.mods[i]); !c.text.empty()) out.push_back(std::move(c));
    return out;
}

void draw_strategy_picker(App& app, const item::Item& it, item::SearchPlan& plan) {
    ImGui::TextColored(kDim, "Search as");
    ImGui::SameLine();
    // A rolled item can be worth more as a base than as the sum of its mods — a fractured
    // mod, a good influence, a high item level — and only the user knows which they meant.
    if (it.rarity == item::Rarity::Magic || it.rarity == item::Rarity::Rare) {
        for (const item::Strategy s : {item::Strategy::Modifiers, item::Strategy::BaseItem}) {
            const bool on = plan.strategy == s;
            if (ImGui::RadioButton(std::string(item::to_string(s)).c_str(), on) && !on)
                app.set_strategy(s);
            ImGui::SameLine();
        }
        ImGui::NewLine();
    } else {
        ImGui::TextUnformatted(std::string(item::to_string(plan.strategy)).c_str());
    }

    std::string target;
    if (!plan.name.empty()) target = plan.name;
    if (!plan.type.empty()) target += (target.empty() ? "" : ", ") + plan.type;
    if (!plan.category.empty()) target += (target.empty() ? "" : " — ") + plan.category;
    if (!target.empty()) ImGui::TextColored(kDim, "%s", target.c_str());
}

/// One filter row: a toggle, where the modifier came from, what it asks for, and the wording.
/// The codes and bounds go first because they are the part being compared; the wording wraps
/// under them.
void draw_filter_row(int id, bool& enabled, std::span<const Code> codes, const std::string& bounds,
                     const std::string& text, const std::string& note) {
    ImGui::PushID(id);
    ImGui::Checkbox("", &enabled);
    ImGui::PopID();
    ImGui::SameLine();
    const float indent = ImGui::GetCursorPosX();
    for (const Code& c : codes) {
        ImGui::TextColored(c.colour, "%s", c.text.c_str());
        ImGui::SameLine();
    }
    if (!bounds.empty()) {
        ImGui::TextColored(kBounds, "%s", bounds.c_str());
        ImGui::SameLine();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    if (!note.empty()) {
        ImGui::SetCursorPosX(indent);
        ImGui::TextColored(kDim, "%s", note.c_str());
    }
    ImGui::PopTextWrapPos();
}

void draw_filters(const item::Item& it, item::SearchPlan& plan) {
    for (size_t i = 0; i < plan.numerics.size(); ++i) {
        item::NumericFilter& f = plan.numerics[i];
        draw_filter_row(static_cast<int>(i), f.enabled, {}, bounds_text(f.min, f.max, f.dp),
                        f.label, f.note);
    }
    for (size_t i = 0; i < plan.stats.size(); ++i) {
        item::StatFilter& f = plan.stats[i];
        const std::vector<Code> codes = affix_codes(it, f);
        std::string bounds = range_text(f.min, f.max, f.dp);
        // What the unique itself can roll, which the clipboard only prints with Advanced Mod
        // Descriptions on. A point range says nothing a reader cannot see.
        if (f.unique_min && f.unique_max && *f.unique_min != *f.unique_max)
            bounds += " of " + range_text(f.unique_min, f.unique_max, f.dp);
        std::string note;
        if (f.type != data::ModType::Explicit) note = data::trade_prefix(f.type);
        // Why this one is ticked on a unique whose other modifiers are not: the item picked
        // it out of a pool, so it is what separates this copy from every other.
        if (f.pooled) {
            if (!note.empty()) note += " \xe2\x80\x94 ";
            note += f.pool_hint.empty() ? "one of several possible modifiers" : f.pool_hint;
        }
        draw_filter_row(static_cast<int>(1000 + i), f.enabled, codes, bounds,
                        strip_roll_ranges(f.text), note);
    }
}

} // namespace

void draw_pricecheck_screen(App& app) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Price check", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

    ImGui::PushFont(app.fonts().bold, 0.0f);
    ImGui::TextUnformatted("Price check");
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - 34);
    if (ImGui::Button("X", ImVec2(24, 0))) app.close_overlay();
    ImGui::Separator();

    // Copy the snapshot once: the updater can swap the bundle in from its own thread
    // between two reads, and the second would dangle.
    const std::shared_ptr<data::GameData> gd = app.game_data();
    if (!gd) {
        const data::DataUpdater::Status st = app.data_status();
        if (st.state == data::DataUpdater::State::Failed)
            ImGui::TextColored(kWarn, "Pricing data unavailable (%s)", st.error.c_str());
        else if (st.bytes_total)
            ImGui::TextDisabled("Pricing data is downloading (%.1f / %.1f MB)\xe2\x80\xa6",
                                st.bytes_done / 1e6, st.bytes_total / 1e6);
        else
            ImGui::TextDisabled("Pricing data is downloading\xe2\x80\xa6");
        ImGui::Separator();
    }

    const std::string& clip = app.clipboard_text();
    const item::Item* it = app.item();
    if (app.copy_late()) {
        // Still watching — the game's clipboard handover sometimes lands seconds late.
        ImGui::TextDisabled("Waiting for the game to hand over the clipboard\xe2\x80\xa6");
    } else if (app.copying()) {
        ImGui::TextDisabled("Copying item\xe2\x80\xa6");
    } else if (clip.empty()) {
        ImGui::TextDisabled("Clipboard is empty. Hover an item in-game and press the price-check hotkey.");
    } else if (!it) {
        ImGui::TextColored(kWarn, "This does not parse as an item:");
        ImGui::PushFont(app.fonts().small_caps, 0.0f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(clip.c_str(), clip.c_str() + clip.size());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
    } else {
        ImGui::BeginChild("item", ImVec2(0, 0), ImGuiChildFlags_None);
        draw_item_tooltip(*it, app.derived(), app.fonts());

        item::SearchPlan& plan = app.plan();
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Separator();
        if (!gd) {
            ImGui::TextDisabled("No pricing data yet, so nothing has been matched.");
        } else {
            draw_strategy_picker(app, *it, plan);
            ImGui::Dummy(ImVec2(0, 4));
            draw_filters(*it, plan);
            // A dropped filter has to be visible: silently searching without it reads as a
            // successful price check on an item that is not this one.
            ImGui::PushTextWrapPos(0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(kWarn));
            for (const std::string& n : plan.notes)
                ImGui::TextUnformatted(("\xe2\x80\xa2 " + n).c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::TextDisabled("Searching is not wired up yet.");
        }
        ImGui::EndChild();
    }

    ImGui::End();
}

} // namespace ppc
