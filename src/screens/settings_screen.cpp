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

constexpr float kRefreshW = 84.0f;

void league_row(App& app, Config& c) {
    const LeagueService& svc = app.leagues();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("League");
    ImGui::SameLine(kLabelW);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - kRefreshW -
                            ImGui::GetStyle().ItemSpacing.x);
    // The preview is the configured value, not a list index. A league the list doesn't have
    // — an ended challenge league, or a hand-edited config — must still display, and must
    // survive a Save. Same reason it's appended as a selectable below.
    if (ImGui::BeginCombo("##league", c.league.c_str())) {
        bool seen = false;
        for (const std::string& id : svc.list()) {
            const bool sel = id == c.league;
            seen = seen || sel;
            if (ImGui::Selectable(id.c_str(), sel)) c.league = id;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        if (!seen && !c.league.empty()) {
            ImGui::Separator();
            ImGui::Selectable(c.league.c_str(), true);
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    const int cd = svc.cooldown_s();
    const bool busy = svc.state() == LeagueState::Loading;
    ImGui::BeginDisabled(busy || cd > 0);
    if (ImGui::Button("Refresh", ImVec2(kRefreshW, 0))) app.refresh_leagues();
    ImGui::EndDisabled();
    if (cd > 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Just refreshed \xe2\x80\x94 wait %ds", cd);

    row_gutter();
    switch (svc.state()) {
    case LeagueState::Loading:
        ImGui::TextDisabled("Fetching league list\xe2\x80\xa6");
        break;
    case LeagueState::Ok:
        ImGui::TextDisabled("%zu leagues", svc.list().size());
        break;
    case LeagueState::Error:
        // curl's messages run past the panel edge, so wrap rather than clip.
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "Couldn't reach the trade API (%s)", svc.error().c_str());
        ImGui::PopTextWrapPos();
        break;
    case LeagueState::Idle:
        ImGui::TextDisabled("Offline list");
        break;
    }
}

void data_row(App& app) {
    using State = data::DataUpdater::State;
    const data::DataUpdater::Status st = app.data_status();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Bundle");
    ImGui::SameLine(kLabelW);

    const bool busy = st.state == State::Checking || st.state == State::Downloading ||
                      st.state == State::Installing;
    switch (st.state) {
    case State::Downloading:
        if (st.bytes_total)
            ImGui::Text("Downloading %.1f / %.1f MB", st.bytes_done / 1e6, st.bytes_total / 1e6);
        else
            ImGui::TextUnformatted("Downloading\xe2\x80\xa6");
        break;
    case State::Checking:
        ImGui::TextUnformatted("Checking for updates\xe2\x80\xa6");
        break;
    case State::Installing:
        ImGui::TextUnformatted("Installing\xe2\x80\xa6");
        break;
    case State::Failed:
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "%s (%s)",
                           st.data_version.empty() ? "No data installed" : st.data_version.c_str(),
                           st.error.c_str());
        ImGui::PopTextWrapPos();
        break;
    default:
        if (st.data_version.empty())
            ImGui::TextDisabled("Not downloaded yet");
        else
            ImGui::Text("%s", st.data_version.c_str());
        break;
    }

    // Right-aligned: the status text left of it varies in width every frame while a
    // download runs, and a button that slides around is unclickable.
    constexpr float kCheckW = 110.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - kCheckW - 18.0f);
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Check now", ImVec2(kCheckW, 0))) app.check_for_data();
    ImGui::EndDisabled();

    row_gutter();
    if (auto gd = app.game_data())
        ImGui::TextDisabled("%zu stat wordings indexed", gd->stat_count());
    else
        ImGui::TextDisabled("Item parsing works without this; pricing needs it.");
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
    league_row(app, c);

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

    section(app, "Game data");
    data_row(app);

    ImGui::Separator();
    if (ImGui::Button("Save", ImVec2(120, 0))) app.apply_and_save_config();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", Config::path().c_str());

    ImGui::End();
}

} // namespace ppc
