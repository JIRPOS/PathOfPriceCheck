#include "screens/settings_screen.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include "app.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
#include "util/debug_log.hpp"

namespace ppc {
namespace {

using ui::col::kWarn;

/// Label column width. Every row's control starts here, so the panel reads as one grid.
constexpr float kLabelW = 160.0f;

/// The dialog's own name, in the size the game gives a screen title.
constexpr float kTitleSize = 21.0f;

/// Draws `label` in the left column and parks the cursor on the control column. For the rows
/// that build their own control; everything else goes through `row()`.
void row_label(const char* label) {
    ImGui::AlignTextToFramePadding();
    // Tinted, where the value beside it is not: it is most of how the game's option screens
    // read as a grid without one being drawn.
    ImGui::PushStyleColor(ImGuiCol_Text, ui::col::kLabel);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(kLabelW); // fixed, not measured: every label here is far under 160px
}

/// `row_label` with the item width already set, returning the hidden id ("##label") to hand the
/// widget — ImGui draws a control's own label to its *right*, which is what made this panel
/// look inconsistent.
///
/// The returned id lives in a static buffer valid only until the next call, so use it inline:
/// `ImGui::SliderInt(row("Width"), ...)`. One row() per expression.
const char* row(const char* label, float width = -FLT_MIN) {
    row_label(label);
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

/// Continues the current line with a `w`-wide control flush against the content region's right
/// edge — measured from where the cursor actually is, so a scrollbar or a child window's inset
/// moves the control with it rather than under it.
void right_align(float w) {
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
}

/// A titled block, in the small caps the game sets every heading in. The rule above it is a
/// divider *between* blocks, so the first block of a tab does without one — the body it sits in
/// already has a border of its own.
void section(App& app, const char* title) {
    if (ImGui::GetCursorPosY() > ImGui::GetStyle().WindowPadding.y) {
        ImGui::Spacing();
        ImGui::Separator();
    }
    ImGui::PushFont(app.fonts().small_caps, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::col::kSection);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void hotkey_row(App& app, const char* label, Action which, Hotkey& hk) {
    const char* id = row(label, 180.0f);
    std::string cur = app.capturing(which) ? ui::text(ui::Msg::PressKeys) : to_string(hk);
    ImGui::PushID(id);
    if (ImGui::Button(cur.c_str(), ImVec2(180, 0))) app.begin_capture(which);
    ImGui::PopID();
}

constexpr float kRefreshW = 84.0f;

void league_row(App& app, Config& c) {
    const LeagueService& svc = app.leagues();

    row_label(ui::text(ui::Msg::League));
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
    if (ImGui::Button(ui::text(ui::Msg::Refresh), ImVec2(kRefreshW, 0))) app.refresh_leagues();
    ImGui::EndDisabled();
    if (cd > 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(ui::text(ui::Msg::JustRefreshed), cd);

    row_gutter();
    switch (svc.state()) {
    case LeagueState::Loading:
        ImGui::TextDisabled("%s", ui::text(ui::Msg::FetchingLeagues));
        break;
    case LeagueState::Ok:
        ImGui::TextDisabled(ui::text(ui::Msg::LeagueCount), svc.list().size());
        break;
    case LeagueState::Error:
        // curl's messages run past the panel edge, so wrap rather than clip.
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, ui::text(ui::Msg::LeagueError), svc.error().c_str());
        ImGui::PopTextWrapPos();
        break;
    case LeagueState::Idle:
        ImGui::TextDisabled("%s", ui::text(ui::Msg::OfflineList));
        break;
    }
}

void data_row(App& app) {
    using State = data::DataUpdater::State;
    const data::DataUpdater::Status st = app.data_status();

    row_label(ui::text(ui::Msg::Bundle));

    const bool busy = st.state == State::Checking || st.state == State::Downloading ||
                      st.state == State::Installing;
    switch (st.state) {
    case State::Downloading:
        if (st.bytes_total)
            ImGui::Text(ui::text(ui::Msg::Downloading), st.bytes_done / 1e6, st.bytes_total / 1e6);
        else
            ImGui::TextUnformatted(ui::text(ui::Msg::DownloadingPlain));
        break;
    case State::Checking:
        ImGui::TextUnformatted(ui::text(ui::Msg::CheckingUpdates));
        break;
    case State::Installing:
        ImGui::TextUnformatted(ui::text(ui::Msg::Installing));
        break;
    case State::Failed:
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "%s (%s)",
                           st.data_version.empty() ? ui::text(ui::Msg::NoDataInstalled)
                                                    : st.data_version.c_str(),
                           st.error.c_str());
        ImGui::PopTextWrapPos();
        break;
    default:
        if (st.data_version.empty())
            ImGui::TextDisabled("%s", ui::text(ui::Msg::NotDownloadedYet));
        else
            ImGui::Text("%s", st.data_version.c_str());
        break;
    }

    // Right-aligned: the status text left of it varies in width every frame while a
    // download runs, and a button that slides around is unclickable.
    constexpr float kCheckW = 110.0f;
    right_align(kCheckW);
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(ui::text(ui::Msg::CheckNow), ImVec2(kCheckW, 0))) app.check_for_data();
    ImGui::EndDisabled();

    row_gutter();
    const std::shared_ptr<data::GameData> gd = app.game_data();
    if (!gd) {
        ImGui::TextDisabled("%s", ui::text(ui::Msg::ParsingWorksWithout));
        return;
    }
    ImGui::TextDisabled(ui::text(ui::Msg::StatWordings), gd->stat_count());
    // A condition of the licence the per-unique modifier data comes under, so it is shown
    // wherever the data itself is: the bundle states the credit, this only renders it.
    if (!gd->unique_mods_attribution().empty()) {
        row_gutter();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled(ui::text(ui::Msg::UniqueModsCredit),
                            std::string(gd->unique_mods_attribution()).c_str());
        ImGui::PopTextWrapPos();
    }
}

/// The application's own version, and whatever the updater has to say about it.
///
/// This is the one place an update can be acted on. The two surfaces over the game only ever
/// mention it — a panel drawn on top of Path of Exile is not where anyone should be given a
/// button that closes the application.
void update_row(App& app, Config& c) {
    using State = update::Updater::State;
    const update::Updater::Status st = app.update_status();

    // An id scope of its own: this row's button reads "Check now" and so does the bundle's, and
    // two identical labels in one window are one widget as far as ImGui is concerned.
    ImGui::PushID("update");

    row_label(ui::text(ui::Msg::Application));

    const bool busy = st.state == State::Checking || st.state == State::Downloading ||
                      st.state == State::Verifying;
    switch (st.state) {
    case State::Downloading:
        if (st.bytes_total)
            ImGui::Text(ui::text(ui::Msg::UpdateDownloading), st.bytes_done / 1e6,
                        st.bytes_total / 1e6);
        else
            ImGui::TextUnformatted(ui::text(ui::Msg::DownloadingPlain));
        break;
    case State::Checking:
    case State::Verifying:
        ImGui::TextUnformatted(ui::text(ui::Msg::UpdateChecking));
        break;
    case State::Ready:
        ImGui::TextColored(kWarn, ui::text(ui::Msg::UpdateReady), st.available.c_str());
        break;
    case State::Offer:
        ImGui::TextColored(kWarn, ui::text(ui::Msg::UpdateAvailable), st.available.c_str());
        break;
    case State::UpToDate:
        ImGui::Text("%s \xe2\x80\x94 %s", APP_VERSION, ui::text(ui::Msg::UpToDate));
        break;
    default:
        // Idle and Failed both. A failed check is not worth a warning colour: the answer is
        // the same either way, and the reason is in the debug log.
        ImGui::Text("%s", APP_VERSION);
        break;
    }

    // Right-aligned, for the same reason the bundle's button is: the text left of it changes
    // width every frame while something is downloading.
    constexpr float kActionW = 110.0f;
    right_align(kActionW);
    if (st.state == State::Ready) {
        if (ImGui::Button(ui::text(ui::Msg::RestartNow), ImVec2(kActionW, 0)))
            app.restart_for_update();
    } else if (st.state == State::Offer) {
        if (ImGui::Button(ui::text(ui::Msg::OpenReleasePage), ImVec2(kActionW, 0)))
            SDL_OpenURL(st.notes_url.empty() ? update::kReleasesUrl : st.notes_url.c_str());
    } else {
        ImGui::BeginDisabled(busy);
        if (ImGui::Button(ui::text(ui::Msg::CheckNow), ImVec2(kActionW, 0)))
            app.check_for_update();
        ImGui::EndDisabled();
    }

    // The gutter belongs to the help line, not to the checkbox below it: row() lays out its own
    // label column, and starting it already on a SameLine drew the box on top of its own label.
    if (st.state == State::Offer) {
        using Offered = update::Updater::Offered;
        row_gutter();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", ui::text(st.reason == Offered::Unmanaged
                                               ? ui::Msg::UpdateOfferUnmanaged
                                           : st.reason == Offered::NoAsset
                                               ? ui::Msg::UpdateOfferNoAsset
                                               : ui::Msg::UpdateOfferHelp));
        ImGui::PopTextWrapPos();
    }
    ImGui::Checkbox(row(ui::text(ui::Msg::AutoUpdate)), &c.auto_update);
    row_gutter();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", ui::text(ui::Msg::AutoUpdateHelp));
    ImGui::PopTextWrapPos();

    ImGui::PopID();
}

/// The two languages, which answer to different things and are therefore two rows.
///
/// **Client language** picks the vocabulary item text is read with and the assets the bundle
/// is opened from, so its options are whatever the *installed bundle* declares — asking for
/// one it does not carry simply fails to open it, which is a worse way to find out. It is
/// read once at startup, so the change lands on the next run and the row says so.
///
/// **Interface** is our own text and takes effect immediately, since nothing is cached on it.
/// "Follow the client" is the default and the honest one: a player who set the client to
/// Russian most likely reads Russian.
void language_rows(App& app, Config& c) {
    const std::shared_ptr<data::GameData> gd = app.game_data();
    const std::vector<std::string> fallback{"en"};
    const std::vector<std::string>& client_langs = gd ? gd->languages() : fallback;

    if (ImGui::BeginCombo(row(ui::text(ui::Msg::ClientLanguage)), c.client_language.c_str())) {
        bool seen = false;
        for (const std::string& id : client_langs) {
            const bool sel = id == c.client_language;
            seen = seen || sel;
            if (ImGui::Selectable(id.c_str(), sel)) c.client_language = id;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        // The same rule as the league combo: a configured value the list does not have must
        // still be selectable, or opening Settings silently changes it.
        if (!seen && !c.client_language.empty()) {
            ImGui::Separator();
            ImGui::Selectable(c.client_language.c_str(), true);
        }
        ImGui::EndCombo();
    }
    row_gutter();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", ui::text(ui::Msg::ClientLanguageHelp));
    ImGui::PopTextWrapPos();
    if (gd && !gd->has_lexicon() && c.client_language != "en") {
        row_gutter();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "%s", ui::text(ui::Msg::NoLexicon));
        ImGui::PopTextWrapPos();
    }
    if (gd && c.client_language != std::string(gd->lexicon().language())) {
        row_gutter();
        ImGui::TextDisabled("%s", ui::text(ui::Msg::LanguageNeedsRestart));
    }

    const char* preview = c.ui_language == "auto" || c.ui_language.empty()
                              ? ui::text(ui::Msg::FollowClient)
                              : c.ui_language.c_str();
    if (ImGui::BeginCombo(row(ui::text(ui::Msg::InterfaceLanguage)), preview)) {
        const bool automatic = c.ui_language == "auto" || c.ui_language.empty();
        if (ImGui::Selectable(ui::text(ui::Msg::FollowClient), automatic)) c.ui_language = "auto";
        if (automatic) ImGui::SetItemDefaultFocus();
        for (const std::string_view id : ui::languages()) {
            const std::string s(id);
            const bool sel = s == c.ui_language;
            if (ImGui::Selectable(s.c_str(), sel)) c.ui_language = s;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

/// One side of the range-matching setting: the mode, and the percentage the two `Within` modes
/// read. The percentage box is disabled rather than hidden — the panel is sized to hold every
/// section without scrolling, and a row that changes height with its own value would break that
/// for whichever mode happened to be picked.
void bound_row(const char* label, item::BoundMode& mode, double& pct) {
    constexpr float kPctW = 96.0f;
    const float avail = ImGui::GetContentRegionAvail().x - kLabelW;
    ImGui::PushID(label);
    if (ImGui::BeginCombo(row(label, avail - kPctW - ImGui::GetStyle().ItemSpacing.x),
                          std::string(item::bound_mode_label(mode)).c_str())) {
        for (const item::BoundModeOption& o : item::kBoundModes) {
            const bool sel = o.mode == mode;
            if (ImGui::Selectable(std::string(o.label).c_str(), sel)) mode = o.mode;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!item::uses_pct(mode));
    ImGui::SetNextItemWidth(kPctW);
    float v = static_cast<float>(pct);
    if (ImGui::DragFloat("##pct", &v, 0.25f, 0.0f, 100.0f, "%.4g%%")) pct = v;
    ImGui::EndDisabled();
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

void general_tab(App& app, Config& c) {
    section(app, ui::text(ui::Msg::SectionAccount));
    league_row(app, c);

    const NameCheck nc = check_account_name(c.account_name);
    if (nc == NameCheck::Malformed)
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.32f, 0.09f, 0.07f, 1.0f));
    ImGui::InputTextWithHint(row(ui::text(ui::Msg::Account)), ui::text(ui::Msg::AccountHint),
                             &c.account_name,
                             ImGuiInputTextFlags_CallbackCharFilter, account_char_filter);
    if (nc == NameCheck::Malformed) {
        ImGui::PopStyleColor();
        row_gutter();
        ImGui::TextColored(kWarn, "%s", ui::text(ui::Msg::AccountExpected));
    }

    section(app, ui::text(ui::Msg::SectionLanguage));
    language_rows(app, c);

    section(app, ui::text(ui::Msg::SectionHotkeys));
    hotkey_row(app, ui::text(ui::Msg::HotkeyPriceCheck), Action::PriceCheck, c.price_check);
    hotkey_row(app, ui::text(ui::Msg::HotkeySettings), Action::ToggleSettings, c.settings);

    section(app, ui::text(ui::Msg::SectionAppearance));
    // Nothing to apply: the dialog's own theme reads this every frame, and App hands it to the
    // other panels' backgrounds before each one draws. Save only persists it.
    ImGui::Checkbox(row(ui::text(ui::Msg::ReduceTransparency)), &c.reduce_transparency);
    row_gutter();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", ui::text(ui::Msg::ReduceTransparencyHelp));
    ImGui::PopTextWrapPos();
}

void price_check_tab(App& app, Config& c) {
    section(app, ui::text(ui::Msg::SectionTradeSearch));
    // GGG's own labels, in the site's own order, so what is picked here reads the same as
    // what the trade page shows.
    if (ImGui::BeginCombo(row(ui::text(ui::Msg::Listings)),
                          std::string(trade::status_label(c.listing_status)).c_str())) {
        for (const trade::StatusOption& o : trade::status_options()) {
            const bool sel = o.id == c.listing_status;
            if (ImGui::Selectable(std::string(o.label).c_str(), sel)) c.listing_status = o.id;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    const auto top_n = [](int n) {
        char buf[64];
        std::snprintf(buf, sizeof buf, ui::text(ui::Msg::TopN), n);
        return std::string(buf);
    };
    if (ImGui::BeginCombo(row(ui::text(ui::Msg::FetchTop)), top_n(c.result_count).c_str())) {
        for (const int n : trade::result_counts()) {
            const bool sel = n == c.result_count;
            if (ImGui::Selectable(top_n(n).c_str(), sel)) c.result_count = n;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    row_gutter();
    // The cost, where the choice is made. It is not latency that this trades away — it is how
    // many price checks fit in GGG's five-minute window before the limiter starts making the
    // next one wait. See trade.hpp.
    {
        const int reqs = trade::fetch_requests(c.result_count);
        ImGui::TextDisabled(ui::text(ui::Msg::RequestCost), reqs, reqs == 1 ? "" : "s",
                            std::min(30, 50 / reqs));
    }

    ImGui::Checkbox(row(ui::text(ui::Msg::AutoSearch)), &c.auto_search);
    row_gutter();
    if (c.auto_search)
        ImGui::TextDisabled("%s", ui::text(ui::Msg::AutoSearchOn));
    else
        ImGui::TextDisabled("%s", ui::text(ui::Msg::AutoSearchOff));

    section(app, ui::text(ui::Msg::SectionFilterRanges));
    ImGui::TextDisabled("%s", ui::text(ui::Msg::FilterRangesHelp));
    bound_row(ui::text(ui::Msg::Minimum), c.range_match.min_mode, c.range_match.min_pct);
    bound_row(ui::text(ui::Msg::Maximum), c.range_match.max_mode, c.range_match.max_pct);
    row_gutter();
    // Said once, under both rows: the two options that need explaining are the ones whose names
    // cannot carry it, and "tiered" is the whole reason the default asks for a window at all.
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", ui::text(ui::Msg::BoundHelp));
    ImGui::PopTextWrapPos();

    section(app, ui::text(ui::Msg::SectionPricePanel));
    ImGui::TextDisabled("%s", ui::text(ui::Msg::PanelHelp));
    ImGui::SliderInt(row(ui::text(ui::Msg::PanelWidth)), &c.panel_width, 280, 900, "%d px");
    // Fractions of the game's height, not its width — see Config. Raise one if the panel
    // overlaps that side's frame; the next price check picks up the change.
    ImGui::SliderFloat(row(ui::text(ui::Msg::StashEdge)), &c.stash_edge, 0.40f, 0.90f, "%.3f");
    ImGui::SliderFloat(row(ui::text(ui::Msg::InventoryEdge)), &c.inventory_edge, 0.40f, 0.90f,
                       "%.3f");
}

void application_tab(App& app, Config& c) {
    section(app, ui::text(ui::Msg::SectionGameData));
    data_row(app);

    section(app, ui::text(ui::Msg::SectionUpdates));
    update_row(app, c);

    section(app, ui::text(ui::Msg::SectionDiagnostics));
    if (ImGui::Checkbox(row(ui::text(ui::Msg::DebugLogging)), &c.debug_log))
        app.set_debug_log(c.debug_log);
    row_gutter();
    if (c.debug_log) {
        // The path, not just "on": the user is going to attach this file to a report, and
        // every price check shows the id that indexes into it.
        ImGui::PushTextWrapPos(0.0f);
        const std::string p = debug::log_path();
        ImGui::TextDisabled("%s", p.empty() ? ui::text(ui::Msg::LogOpenFailed) : p.c_str());
        ImGui::PopTextWrapPos();
    } else {
        ImGui::TextDisabled("%s", ui::text(ui::Msg::DebugLogHelp));
    }
}

/// The fixed header: the screen's name in small caps, and the disc in the corner that leaves
/// it. The game puts both in the frame's top edge and neither ever scrolls.
void draw_header(App& app) {
    const float h = ImGui::GetFrameHeight();

    ImGui::PushFont(app.fonts().small_caps, kTitleSize);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::col::kTitle);
    ImGui::TextUnformatted(ui::text(ui::Msg::SettingsTitle));
    ImGui::PopStyleColor();
    ImGui::PopFont();

    right_align(h);
    // Square, so the full rounding makes it a disc — the one red control on the panel, which is
    // where the game puts its only one too.
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, h * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Button, ui::col::kClose);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::col::kCloseHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::col::kCloseHovered);
    if (ImGui::Button("X", ImVec2(h, h))) app.close_overlay();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ui::text(ui::Msg::Close));
}

struct Tab {
    ui::Msg name;
    void (*draw)(App&, Config&);
};

constexpr Tab kTabs[]{
    {ui::Msg::TabGeneral, &general_tab},
    {ui::Msg::TabPriceCheck, &price_check_tab},
    {ui::Msg::TabApplication, &application_tab},
};

/// The fixed tab strip. Buttons rather than `ImGui::BeginTabBar`, because the game marks the
/// open tab by lighting its *name* and ImGui has no colour for a selected tab's label.
void draw_tabs(App& app) {
    for (int i = 0; i < static_cast<int>(std::size(kTabs)); ++i) {
        if (i) ImGui::SameLine(0.0f, 4.0f);
        const bool open = app.settings_tab() == i;
        ImGui::PushStyleColor(ImGuiCol_Text, open ? ui::col::kAccent : ui::col::kText);
        ImGui::PushStyleColor(ImGuiCol_Button, open ? ui::col::kButton : ui::col::kTabIdle);
        if (ImGui::Button(ui::text(kTabs[i].name))) app.set_settings_tab(i);
        ImGui::PopStyleColor(2);
    }
}

/// The fixed footer: Save, and where it is saved to.
void draw_footer(App& app) {
    ImGui::Separator();
    // The one lit button on the panel, as the game lights whichever of its footer buttons
    // commits: everything else here is undone by closing.
    ImGui::PushStyleColor(ImGuiCol_Button, ui::col::kButtonHovered);
    if (ImGui::Button(ui::text(ui::Msg::Save), ImVec2(140, 0))) app.apply_and_save_config();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", Config::path().c_str());
}

} // namespace

void draw_settings_screen(App& app) {
    Config& c = app.config();
    ImGuiIO& io = ImGui::GetIO();
    // Opened before Begin, so the window's own background and border are drawn with it, and
    // reading the live setting rather than the saved one, so the checkbox previews itself.
    const ui::Theme theme(c.reduce_transparency);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Settings", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

    draw_header(app);
    draw_tabs(app);

    // Header and tab strip above, Save below, and only the rows between them scroll. The dialog
    // is one fixed size (App::kSettingsH) whichever tab is open, so the body is whatever the two
    // of them leave — and a tab too tall for it scrolls without taking the header or the Save
    // button with it.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float footer_h = style.ItemSpacing.y * 2.0f + 1.0f + ImGui::GetFrameHeight();
    ImGui::BeginChild("##body", ImVec2(0.0f, ImGui::GetContentRegionAvail().y - footer_h));
    const int open = std::clamp(app.settings_tab(), 0, static_cast<int>(std::size(kTabs)) - 1);
    kTabs[open].draw(app, c);
    ImGui::EndChild();

    draw_footer(app);
    ImGui::End();
}

} // namespace ppc
