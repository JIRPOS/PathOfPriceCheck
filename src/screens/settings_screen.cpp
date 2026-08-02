#include "screens/settings_screen.hpp"

#include <cfloat>
#include <cstdio>
#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app.hpp"

namespace ppc {
namespace {

/// Label column width. Every row's control starts here, so the panel reads as one grid.
constexpr float kLabelW = 160.0f;

const ImVec4 kWarn(0.90f, 0.55f, 0.25f, 1.0f);

/// Draws `label` in the left column, parks the cursor on the control column with the item
/// width already set, and returns the hidden id ("##label") to hand the widget — ImGui draws
/// a control's own label to its *right*, which is what made this panel look inconsistent.
///
/// The returned id lives in a static buffer valid only until the next call, so use it inline:
/// `ImGui::SliderInt(row("Width"), ...)`. One row() per expression.
const char* row(const char* label, float width = -FLT_MIN) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelW); // fixed, not measured: every label here is far under 160px
    ImGui::SetNextItemWidth(width);
    static char id[96];
    std::snprintf(id, sizeof id, "##%s", label);
    return id;
}

/// Blank label column, to hang a status or help line under the row above.
void row_gutter() {
    ImGui::Dummy(ImVec2(kLabelW - ImGui::GetStyle().ItemSpacing.x, 0.0f));
    ImGui::SameLine();
}

void section(App& app, const char* title) {
    ImGui::Separator();
    ImGui::PushFont(app.fonts().bold, 0.0f);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
}

void hotkey_row(App& app, const char* label, Action which, Hotkey& hk) {
    const char* id = row(label, 180.0f);
    std::string cur = app.capturing(which) ? "press keys\xe2\x80\xa6" : to_string(hk);
    ImGui::PushID(id);
    if (ImGui::Button(cur.c_str(), ImVec2(180, 0))) app.begin_capture(which);
    ImGui::PopID();
}

/// Discards characters that can never appear in "Name#1234". Rejecting a *keystroke* that
/// could never be part of a valid value is safe; rejecting a whole-string state is not —
/// you cannot reach "Foo#1" without passing through the invalid "Foo" and "Foo#".
int account_char_filter(ImGuiInputTextCallbackData* d) {
    const ImWchar c = d->EventChar;
    const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    c == '#';
    return ok ? 0 : 1; // non-zero discards
}

} // namespace

void draw_settings_screen(App& app) {
    Config& c = app.config();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Settings", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

    ImGui::PushFont(app.fonts().bold, 0.0f);
    ImGui::TextUnformatted("PathOfPriceCheck \xe2\x80\x94 Settings");
    ImGui::PopFont();
    ImGui::SameLine(ImGui::GetWindowWidth() - 34);
    if (ImGui::Button("X", ImVec2(24, 0))) app.close_overlay();

    section(app, "General");
    ImGui::InputText(row("League"), &c.league);

    const NameCheck nc = check_account_name(c.account_name);
    if (nc == NameCheck::Malformed) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.42f, 0.13f, 0.13f, 1.0f));
    ImGui::InputTextWithHint(row("Account"), "Name#1234", &c.account_name,
                             ImGuiInputTextFlags_CallbackCharFilter, account_char_filter);
    if (nc == NameCheck::Malformed) {
        ImGui::PopStyleColor();
        row_gutter();
        ImGui::TextColored(kWarn, "Expected Name#1234");
    }

    section(app, "Hotkeys");
    hotkey_row(app, "Price check", Action::PriceCheck, c.price_check);
    hotkey_row(app, "Settings", Action::ToggleSettings, c.settings);

    section(app, "Price-check panel");
    ImGui::TextDisabled("Docks beside whichever game panel the cursor was over.");
    ImGui::SliderInt(row("Width"), &c.panel_width, 280, 900, "%d px");
    // Fractions of the game's height, not its width — see Config. Raise one if the panel
    // overlaps that side's frame; the next price check picks up the change.
    ImGui::SliderFloat(row("Stash edge"), &c.stash_edge, 0.40f, 0.90f, "%.3f");
    ImGui::SliderFloat(row("Inventory edge"), &c.inventory_edge, 0.40f, 0.90f, "%.3f");

    ImGui::Separator();
    if (ImGui::Button("Save", ImVec2(120, 0))) app.apply_and_save_config();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", Config::path().c_str());

    ImGui::End();
}

} // namespace ppc
