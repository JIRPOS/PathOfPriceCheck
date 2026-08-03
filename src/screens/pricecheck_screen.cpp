#include "screens/pricecheck_screen.hpp"

#include <cstdio>
#include <memory>
#include <string>

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
    if (max) return "up to " + format_number(*max, dp);
    return {};
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

/// One filter row: a toggle, what it asks for, and the wording it came from. The bounds go
/// first because they are the part being compared; the wording wraps under them.
void draw_filter_row(int id, bool& enabled, const std::string& bounds, const std::string& text,
                     const std::string& note) {
    ImGui::PushID(id);
    ImGui::Checkbox("", &enabled);
    ImGui::PopID();
    ImGui::SameLine();
    const float indent = ImGui::GetCursorPosX();
    if (!bounds.empty()) {
        ImGui::TextColored(ImVec4(0.78f, 0.78f, 0.92f, 1.0f), "%s", bounds.c_str());
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

void draw_filters(item::SearchPlan& plan) {
    for (size_t i = 0; i < plan.numerics.size(); ++i) {
        item::NumericFilter& f = plan.numerics[i];
        draw_filter_row(static_cast<int>(i), f.enabled, bounds_text(f.min, f.max, f.dp), f.label,
                        f.note);
    }
    for (size_t i = 0; i < plan.stats.size(); ++i) {
        item::StatFilter& f = plan.stats[i];
        std::string bounds = bounds_text(f.min, f.max, f.dp);
        if (f.tiered) bounds += " (tier)";
        std::string note;
        if (f.type != data::ModType::Explicit) note = data::trade_prefix(f.type);
        draw_filter_row(static_cast<int>(1000 + i), f.enabled, bounds, f.text, note);
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
        draw_item_tooltip(*it, app.fonts());

        item::SearchPlan& plan = app.plan();
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Separator();
        draw_derived_numbers(*it, app.derived(), app.fonts());

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::Separator();
        if (!gd) {
            ImGui::TextDisabled("No pricing data yet, so nothing has been matched.");
        } else {
            draw_strategy_picker(app, *it, plan);
            ImGui::Dummy(ImVec2(0, 4));
            draw_filters(plan);
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
