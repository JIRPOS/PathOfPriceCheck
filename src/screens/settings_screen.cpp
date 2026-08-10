#include "screens/settings_screen.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include "app.hpp"
#include "platform/clipboard.hpp"
#include "quickpaste.hpp"
#include "ui/glyphs.hpp"
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
    hotkey_row(app, ui::text(ui::Msg::HotkeyQuickPaste), Action::QuickPaste, c.quick_paste);

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

/// A square icon button, or the word behind it when the glyph subset and `ui/glyphs.hpp` have
/// drifted apart. `tip` is what it does, since an icon cannot say so itself.
bool icon_button(App& app, const char* glyph, const char* word, const char* tip, float w) {
    const bool pressed = ImGui::Button(app.fonts().has_glyphs ? glyph : word, ImVec2(w, 0));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    return pressed;
}

constexpr float kPasteIconW = 30.0f;
constexpr float kPasteGripW = 24.0f;
constexpr float kPasteSlotW = 18.0f;

/// What the popup will do with the list, said in the list: the number key this entry answers
/// to, or an empty column where there is no key to press. A column of its own either way, or
/// the headings of the enabled and the disabled entries would not line up.
void paste_slot_number(const Config& c, size_t index) {
    const float x = ImGui::GetCursorPosX();
    const std::vector<size_t> active = active_pastes(c.pastes);
    for (size_t slot = 0; slot < active.size(); ++slot) {
        if (active[slot] != index) continue;
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::col::kAccent);
        ImGui::Text("%zu", slot + 1);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 0.0f); // back onto the row; the width is set below
        break;
    }
    // Set rather than advanced: `SameLine(offset)` measures from the window's left edge, not
    // from the cursor, which puts the whole row on top of itself.
    ImGui::SetCursorPosX(x + kPasteSlotW + ImGui::GetStyle().ItemSpacing.x);
}

/// What a row asked for, to be done once the loop drawing the list has finished: a delete changes
/// the vector being walked, and a reorder needs the height of a row that has not been drawn yet.
struct PasteAction {
    size_t removed = 0;
    bool removing = false;
    size_t grabbed = 0;
    bool grabbing = false;         // a handle was pressed this frame
    std::vector<float> heights;    // each row's, in list order, as drawn this frame
};

/// A reorder in progress. Tracked by us and not by ImGui's held-item id, because a row's id is
/// its position in the list: the moment a move lands, ImGui is holding the handle of the row that
/// slid into the old place, and the drag would carry on shoving whatever kept arriving there.
/// What has to survive a move is the paste's identity, so that is what is kept.
struct PasteDrag {
    bool active = false;
    size_t index = 0; // where the dragged paste is *now*
    float paid = 0.0f; // pixels of the pointer's travel already spent on moves
};
PasteDrag paste_drag;

/// Turn the pointer's travel into moves. A row is picked up only once the pointer has covered the
/// whole height of the neighbour it is heading for, and that height is then taken off the tally —
/// which is both the hysteresis and the reason the row stays under the hand.
///
/// Asking instead which row the pointer is *over* reverses itself whenever two rows differ in
/// height, and these do: a move drops the pointer back over the row it just came from, that reads
/// as a move the other way, and the list flickers between the two until the button comes up.
void resolve_paste_drag(Config& c, const PasteAction& act) {
    if (act.grabbing) paste_drag = PasteDrag{true, act.grabbed, 0.0f};
    if (!paste_drag.active) return;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || paste_drag.index >= act.heights.size()) {
        paste_drag = PasteDrag{};
        return;
    }
    // Raw: the default threshold would hold the delta at zero for the first few pixels and then
    // hand over all of them at once, which is a jump the tally cannot account for.
    const float travel = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).y - paste_drag.paid;
    const bool down = travel > 0.0f;
    if (down ? paste_drag.index + 1 >= act.heights.size() : paste_drag.index == 0) {
        // Travel off the end of the list is forgotten rather than banked: banked, the hand would
        // owe that distance back before the row it is still holding would move again.
        paste_drag.paid += travel;
        return;
    }
    const size_t to = down ? paste_drag.index + 1 : paste_drag.index - 1;
    // One move per frame: the heights were measured in the order the list had when it was drawn,
    // and a second move would be reading them for an order that no longer exists. A flick that
    // outruns this is not lost — it stays on the tally and is paid off over the next frames.
    const float step = act.heights[to];
    if (std::abs(travel) < step) return;
    if (!move_paste(c.pastes, paste_drag.index, to)) return;
    paste_drag.index = to;
    paste_drag.paid += down ? step : -step;
}

/// One entry: what the popup would show of it, plus what can be done to it here. The heading and
/// the text are **read-only** — this list is for arranging, and a field that is typed into is a
/// field that has to be finished before anything else can be clicked. Writing is the dialog.
void paste_row(App& app, Config& c, size_t i, PasteAction& act) {
    Paste& p = c.pastes[i];
    ImGui::PushID(static_cast<int>(i));
    const float top = ImGui::GetCursorScreenPos().y;

    // The handle. Pressing it starts a reorder; the drag itself is resolved after the loop, which
    // needs the heights of rows this one has not reached yet. ImGui has no drag-and-drop for a
    // list this small, and the two-button version costs a row of chrome per entry.
    //
    // While a drag runs, the held look is painted on the row being moved rather than on the id
    // ImGui is holding — those part company on the first move, and the pressed handle left behind
    // on a row standing still is the drag appearing to have gone somewhere it has not.
    const bool held = paste_drag.active && paste_drag.index == i;
    const ImVec4 grip = ImGui::GetStyleColorVec4(held ? ImGuiCol_ButtonActive : ImGuiCol_Button);
    ImGui::PushStyleColor(ImGuiCol_Button, grip);
    if (paste_drag.active) {
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, grip);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, grip);
    }
    icon_button(app, ui::kGlyphGrip, "=", ui::text(ui::Msg::PasteReorder), kPasteGripW);
    ImGui::PopStyleColor(paste_drag.active ? 3 : 1);
    if (ImGui::IsItemActivated()) {
        act.grabbed = i;
        act.grabbing = true;
    }
    ImGui::SameLine();

    // Off is the only way to make room, so the box that would take the tenth slot is the one
    // that is disabled — no error to read, and the row above says how many are left.
    const bool full = enabled_pastes(c.pastes) >= kMaxActivePastes;
    ImGui::BeginDisabled(!p.enabled && full);
    ImGui::Checkbox("##on", &p.enabled);
    ImGui::EndDisabled();
    ImGui::SameLine();

    paste_slot_number(c, i);

    const float heading_x = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    if (p.heading.empty()) ImGui::TextDisabled("%s", ui::text(ui::Msg::PasteUntitled));
    else ImGui::TextUnformatted(p.heading.c_str());

    right_align(kPasteIconW * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    if (icon_button(app, ui::kGlyphEdit, "...", ui::text(ui::Msg::PasteEdit), kPasteIconW)) {
        app.paste_edit() = PasteEdit{true, false, i, p};
    }
    ImGui::SameLine();
    if (icon_button(app, ui::kGlyphDelete, "X", ui::text(ui::Msg::PasteDelete), kPasteIconW)) {
        act.removed = i;
        act.removing = true;
    }

    // The text, one line and dim, under the heading and in its column. The same line the popup
    // draws, so what is arranged here is what will be read there.
    ImGui::SetCursorPosX(heading_x);
    const std::string preview = paste_preview(p.body, 200);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", preview.empty() ? ui::text(ui::Msg::PasteEmptyBody)
                                              : preview.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    act.heights.push_back(ImGui::GetCursorScreenPos().y - top);
    ImGui::PopID();
}

/// Writing a paste: the one place a heading or a body is typed. A dialog rather than fields in
/// the list, because the body is multi-line and a list whose rows are text boxes is a list you
/// cannot scan.
///
/// It edits a **draft**, which Done copies back — Cancel has to be able to leave nothing behind,
/// and Settings as a whole is still not saved until its own Save.
void paste_dialog(App& app, Config& c) {
    PasteEdit& pe = app.paste_edit();
    if (pe.open && !ImGui::IsPopupOpen("##paste_edit")) ImGui::OpenPopup("##paste_edit");
    // The full width of the dialog it opens over, centred on it. The text being written is a
    // chat line or a search string, so width is what it wants and height is what it does not:
    // three lines by default, and the box scrolls for the rare paste that runs longer.
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(display.x, 0.0f));
    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("##paste_edit", nullptr, ImGuiWindowFlags_NoTitleBar |
                                                             ImGuiWindowFlags_NoResize |
                                                             ImGuiWindowFlags_NoMove))
        return;

    section(app, ui::text(pe.adding ? ui::Msg::PasteNew : ui::Msg::PasteEdit));
    ImGui::InputTextWithHint(row(ui::text(ui::Msg::PasteHeading)),
                             ui::text(ui::Msg::PasteHeadingHint), &pe.draft.heading);
    row_label(ui::text(ui::Msg::PasteBody));
    ImGui::InputTextMultiline("##body", &pe.draft.body,
                              ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 3.0f +
                                                   ImGui::GetStyle().FramePadding.y * 2.0f));
    row_gutter();
    if (pe.draft.body.size() > kMaxClipboardWrite) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, ui::text(ui::Msg::PasteTooLong), kMaxClipboardWrite);
        ImGui::PopTextWrapPos();
    } else {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", ui::text(ui::Msg::PasteBodyHint));
        ImGui::PopTextWrapPos();
    }

    ImGui::Separator();
    // Nothing to paste is not an error worth wording: the button that would store it is simply
    // not available, which is the same answer the ninth-slot checkbox gives.
    const bool storable = !pe.draft.body.empty() && pe.draft.body.size() <= kMaxClipboardWrite;
    ImGui::BeginDisabled(!storable);
    ImGui::PushStyleColor(ImGuiCol_Button, ui::col::kButtonHovered);
    const std::string done = app.fonts().has_glyphs
                                 ? std::string(ui::kGlyphConfirm) + "  " +
                                       ui::text(ui::Msg::PasteDone)
                                 : std::string(ui::text(ui::Msg::PasteDone));
    if (ImGui::Button(done.c_str(), ImVec2(120, 0))) {
        if (pe.adding) {
            // Enabled if there is a slot for it, and off when the nine are taken — a new paste
            // that silently displaced one of them would be worse than one with no number yet.
            pe.draft.enabled = enabled_pastes(c.pastes) < kMaxActivePastes;
            c.pastes.push_back(pe.draft);
        } else if (pe.index < c.pastes.size()) {
            c.pastes[pe.index].heading = pe.draft.heading;
            c.pastes[pe.index].body = pe.draft.body;
        }
        pe = PasteEdit{};
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(ui::text(ui::Msg::PasteCancel), ImVec2(120, 0))) {
        pe = PasteEdit{};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void quickpaste_tab(App& app, Config& c) {
    section(app, ui::text(ui::Msg::SectionPastes));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", ui::text(ui::Msg::PasteListHelp));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    PasteAction act;
    for (size_t i = 0; i < c.pastes.size(); ++i) paste_row(app, c, i, act);
    if (c.pastes.empty()) ImGui::TextDisabled("%s", ui::text(ui::Msg::PasteNone));
    // After the loop, never inside it: both of these change the list the loop is walking.
    resolve_paste_drag(c, act);
    if (act.removing && act.removed < c.pastes.size())
        c.pastes.erase(c.pastes.begin() + static_cast<ptrdiff_t>(act.removed));

    ImGui::Separator();
    const std::string add = app.fonts().has_glyphs
                                ? std::string(ui::kGlyphAdd) + "  " + ui::text(ui::Msg::PasteNew)
                                : std::string(ui::text(ui::Msg::PasteNew));
    if (ImGui::Button(add.c_str())) app.paste_edit() = PasteEdit{true, true, 0, Paste{}};
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    const size_t on = enabled_pastes(c.pastes);
    if (on >= kMaxActivePastes)
        ImGui::TextDisabled(ui::text(ui::Msg::PasteSlotsFull), kMaxActivePastes);
    else
        ImGui::TextDisabled(ui::text(ui::Msg::PasteSlotsLeft), on, kMaxActivePastes);

    paste_dialog(app, c);
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
    {ui::Msg::TabQuickPaste, &quickpaste_tab},
    {ui::Msg::TabApplication, &application_tab},
};
// The paste popup's "add one" opens Settings on this tab by number, and a tab inserted above
// it would quietly send that button somewhere else.
static_assert(kTabs[kQuickPasteTab].draw == &quickpaste_tab,
              "kQuickPasteTab must name the tab the paste list is on");

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
