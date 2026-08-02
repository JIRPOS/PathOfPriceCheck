#include "screens/settings_screen.hpp"

#include <cstdio>
#include <string>

#include <imgui.h>

#include "app.hpp"

namespace ppc {

static void hotkey_row(App& app, const char* label, Action which, Hotkey& hk) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(160);
    std::string cur = app.capturing(which) ? "press keys..." : to_string(hk);
    ImGui::PushID(label);
    if (ImGui::Button(cur.c_str(), ImVec2(180, 0))) app.begin_capture(which);
    ImGui::PopID();
}

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
    ImGui::Separator();

    char league[64];
    std::snprintf(league, sizeof league, "%s", c.league.c_str());
    if (ImGui::InputText("League", league, sizeof league)) c.league = league;

    char acct[64];
    std::snprintf(acct, sizeof acct, "%s", c.account_name.c_str());
    if (ImGui::InputText("Account (optional)", acct, sizeof acct)) c.account_name = acct;

    char title[64];
    std::snprintf(title, sizeof title, "%s", c.poe_window_title.c_str());
    if (ImGui::InputText("PoE window title", title, sizeof title)) c.poe_window_title = title;

    ImGui::Separator();
    ImGui::PushFont(app.fonts().bold, 0.0f);
    ImGui::TextUnformatted("Hotkeys");
    ImGui::PopFont();
    hotkey_row(app, "Price check", Action::PriceCheck, c.price_check);
    hotkey_row(app, "Settings", Action::ToggleSettings, c.settings);

    ImGui::Separator();
    ImGui::PushFont(app.fonts().bold, 0.0f);
    ImGui::TextUnformatted("Price-check panel");
    ImGui::PopFont();
    ImGui::TextDisabled("Docks beside whichever game panel the cursor was over.");
    ImGui::SliderInt("Width", &c.panel_width, 280, 900, "%d px");
    // Fractions of the game's height, not its width — see Config. Raise one if the panel
    // overlaps that side's frame; the next price check picks up the change.
    ImGui::SliderFloat("Stash edge", &c.stash_edge, 0.40f, 0.90f, "%.3f");
    ImGui::SliderFloat("Inventory edge", &c.inventory_edge, 0.40f, 0.90f, "%.3f");

    ImGui::Separator();
    if (ImGui::Button("Save", ImVec2(120, 0))) app.apply_and_save_config();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", Config::path().c_str());

    ImGui::End();
}

} // namespace ppc
