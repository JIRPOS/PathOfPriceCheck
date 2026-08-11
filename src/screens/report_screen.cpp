#include "screens/report_screen.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "app.hpp"
#include "report/report.hpp"
#include "ui/theme.hpp"

namespace ppc {
namespace {

/// The dialog's own name, in the size the game gives a screen title. Same as Settings': this is
/// a second dialog of the same kind and it should not read as a different application.
constexpr float kTitleSize = 21.0f;

/// The captured text is data rather than prose, and at the body size a monospace face is both
/// wider and heavier than Fontin — two panels of it would fill the dialog with four fields.
constexpr float kMonoSize = 14.0f;

/// How the middle splits. The left column is the payload and the right is one picture of it, so
/// the picture gets what it needs to still be recognisable and the text gets the rest.
constexpr float kShotColumn = 300.0f;

/// How much of the left column the clipboard capture takes, the parse dump getting the rest. The
/// capture is the shorter of the two on nearly every item, and it is the one a reporter checks
/// before pressing Send.
constexpr float kItemShare = 0.42f;

void label(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ui::col::kLabel);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

/// A read-only view of one of the payload's fields: monospace, scrollable, and selectable so a
/// reporter can copy a line out of it. Read-only rather than disabled — a disabled box cannot be
/// scrolled, and the whole point is that the payload can be read before it is sent.
void preview(App& app, const char* id, std::string& text, float height) {
    ImGui::PushFont(app.fonts().mono, kMonoSize);
    // Whole lines. A box sized to whatever was left over ends on half a line of text, which on a
    // preview whose whole claim is "this is what will be sent" reads as truncation.
    const float line = ImGui::GetTextLineHeight();
    const float pad = ImGui::GetStyle().FramePadding.y * 2;
    const float rows = std::max(3.0f, std::floor((height - pad) / line));
    ImGui::InputTextMultiline(id, &text, ImVec2(-FLT_MIN, rows * line + pad),
                              ImGuiInputTextFlags_ReadOnly);
    ImGui::PopFont();
}

/// Trim to the relay's cap without cutting a character in half.
///
/// The character filter below stops typing at the cap, but a paste arrives whole and there is no
/// keystroke to refuse — so this is the one that actually enforces it, and it steps back over
/// UTF-8 continuation bytes rather than leaving a half a character the relay would then strip.
void clamp_comment(std::string& s) {
    if (s.size() <= report::kCommentMax) return;
    size_t n = report::kCommentMax;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    s.resize(n);
}

int refuse_past_cap(ImGuiInputTextCallbackData* data) {
    const auto* s = static_cast<const std::string*>(data->UserData);
    if (s->size() >= report::kCommentMax) data->EventChar = 0; // dropped
    return 0;
}

void draw_comment(ReportDraft& draft) {
    label("Describe the issue (optional)");
    const float h = ImGui::GetTextLineHeight() * 3 + ImGui::GetStyle().FramePadding.y * 2;
    ImGui::InputTextMultiline("##comment", &draft.comment, ImVec2(-FLT_MIN, h),
                              ImGuiInputTextFlags_CallbackCharFilter, &refuse_past_cap,
                              &draft.comment);
    clamp_comment(draft.comment);

    const bool full = draft.comment.size() >= report::kCommentMax;
    const std::string count =
        std::to_string(draft.comment.size()) + " / " + std::to_string(report::kCommentMax);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                         ImGui::CalcTextSize(count.c_str()).x);
    if (full) ImGui::TextColored(ui::col::kWarn, "%s", count.c_str());
    else ImGui::TextDisabled("%s", count.c_str());
}

/// The picture, fitted into the column without being stretched and centred in what is left.
///
/// It is a **preview and not the attachment** — the file sent is the capture at its own size, and
/// this is however much of the column it fits into. A panel is far taller than it is wide, so on
/// nearly every capture the height is what binds.
void draw_shot(const ReportDraft& draft) {
    label("Screenshot");
    const ImVec2 room = ImGui::GetContentRegionAvail();
    if (!draft.shot_tex || draft.shot.w <= 0 || draft.shot.h <= 0) {
        ImGui::TextDisabled("Nothing was captured, so there is nothing to attach.");
        return;
    }
    const float w = static_cast<float>(draft.shot.w);
    const float h = static_cast<float>(draft.shot.h);
    const float scale = std::min(room.x / w, room.y / h);
    const ImVec2 size(w * scale, h * scale);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (room.x - size.x) * 0.5f));
    ImGui::Image(draft.shot_tex, size);
}

void draw_build_line(App& app, const report::Meta& m) {
    label("Build");
    ImGui::PushFont(app.fonts().mono, kMonoSize);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(("app " + m.version + "   os " + m.os + "   league " +
                            (m.league.empty() ? "-" : m.league) + "   data " +
                            (m.bundle.empty() ? "none" : m.bundle))
                               .c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
}

/// The middle: the payload on the left, the one picture of it on the right.
void draw_body(App& app, ReportDraft& draft, float height) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float shot_w = std::min(kShotColumn, ImGui::GetContentRegionAvail().x * 0.4f);
    const float left_w = ImGui::GetContentRegionAvail().x - shot_w - style.ItemSpacing.x;

    ImGui::BeginChild("##payload", ImVec2(left_w, height));
    // Measured from what is left after the two labels and the build line, so the boxes fill the
    // column exactly rather than being given a line count that is wrong at any other font size.
    const float labels = ImGui::GetTextLineHeightWithSpacing() * 2;
    const float build = ImGui::GetTextLineHeightWithSpacing() * 2 + style.ItemSpacing.y;
    const float boxes = std::max(ImGui::GetTextLineHeight() * 6,
                                 ImGui::GetContentRegionAvail().y - labels - build);
    label("Item, exactly as the game copied it");
    preview(app, "##item", draft.payload.item, boxes * kItemShare);
    label("What this tool made of it");
    preview(app, "##parse", draft.payload.parse, boxes * (1.0f - kItemShare));
    draw_build_line(app, draft.payload.meta);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##shot", ImVec2(0, height));
    draw_shot(draft);
    ImGui::EndChild();
}

/// What is sent and what is not, in front of the button that sends it. Measured as well as
/// drawn, because how many lines it wraps to is what the body's height is whatever is left of.
constexpr const char* kDisclaimer =
    "This report is anonymous: no login, no machine identifier, nothing that says who you are, "
    "and nothing kept on this computer. What is above is the whole of what is sent \xe2\x80\x94 "
    "the item text, what this tool made of it, the build line, whatever you wrote, and the "
    "picture only if you tick the box. The picture is a capture of this tool's own window and "
    "nothing else, so the game behind it is not in it, and sellers' names were replaced by their "
    "position in the results before it was taken.";

float disclaimer_height(float width) {
    return ImGui::CalcTextSize(kDisclaimer, nullptr, false, width).y;
}

void draw_disclaimer() {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", kDisclaimer);
    ImGui::PopTextWrapPos();
}

/// The footer: the button on the right and the consent immediately to its left, so the thing
/// being agreed to and the act of sending are read in one glance.
void draw_footer(App& app, ReportDraft& draft) {
    const ReportService& svc = app.report();
    const bool sending = svc.state() == ReportState::Sending;
    const ImGuiStyle& style = ImGui::GetStyle();
    constexpr float kSendW = 140.0f;

    ImGui::BeginDisabled(sending);
    const char* consent = "Attach the screenshot (see preview)";
    const float consent_w = ImGui::CalcTextSize(consent).x + ImGui::GetFrameHeight() +
                            style.ItemInnerSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - kSendW -
                         consent_w - style.ItemSpacing.x * 2);
    ImGui::BeginDisabled(draft.shot.empty());
    ImGui::Checkbox(consent, &draft.attach);
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    ImGui::SameLine(0.0f, style.ItemSpacing.x * 2);
    // Lit, as Settings lights its Save: it is the one control here that does anything outside
    // this window.
    ImGui::PushStyleColor(ImGuiCol_Button, ui::col::kButtonHovered);
    ImGui::BeginDisabled(sending || draft.payload.item.empty());
    if (ImGui::Button(sending ? "Sending\xe2\x80\xa6" : "Send", ImVec2(kSendW, 0)))
        app.send_bug_report();
    ImGui::EndDisabled();
    ImGui::PopStyleColor();
}

/// Why the send did not happen, over the dialog it did not close.
///
/// A modal rather than a line in the footer: the report is still there and still sendable, and
/// the one thing the user must not conclude is that it went. `report_.reset()` on the way out is
/// what keeps this from reopening on the next frame — the failure is the state, and dismissing it
/// is clearing it.
void draw_failure(App& app) {
    constexpr const char* kId = "Report not sent##fail";
    if (app.report().state() == ReportState::Failed && !ImGui::IsPopupOpen(kId))
        ImGui::OpenPopup(kId);
    ImGui::SetNextWindowSize(ImVec2(380, 0));
    if (!ImGui::BeginPopupModal(kId, nullptr,
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoSavedSettings))
        return;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(ui::col::kWarn, "%s", app.report().error().c_str());
    ImGui::TextDisabled("Nothing was sent. The report is still here — try again, or close the "
                        "dialog to drop it.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(120, 0))) {
        app.dismiss_report_result();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_header(App& app, const char* title, void (App::*close)()) {
    const float h = ImGui::GetFrameHeight();
    ImGui::PushFont(app.fonts().small_caps, kTitleSize);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::col::kTitle);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - h);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, h * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Button, ui::col::kClose);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::col::kCloseHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::col::kCloseHovered);
    const bool hit = ImGui::Button("X", ImVec2(h, h));
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close");
    if (hit) (app.*close)();
}

/// The confirmation, which is a window of its own because the dialog it confirms is already
/// closed. The id is the whole of the message worth keeping: it names the post the report became,
/// so quoting it is how a user can be answered about one.
void draw_sent(App& app) {
    ImGuiIO& io = ImGui::GetIO();
    const ui::Theme theme(app.config().reduce_transparency);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Report sent", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
    draw_header(app, "Bug report sent", &App::dismiss_report_result);
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("Thank you \xe2\x80\x94 the report went through.");
    ImGui::PopTextWrapPos();
    ImGui::AlignTextToFramePadding();
    label("Report id");
    ImGui::SameLine();
    ImGui::PushFont(app.fonts().mono, kMonoSize);
    ImGui::TextUnformatted(app.report().id().c_str());
    ImGui::PopFont();
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeightWithSpacing() -
                         ImGui::GetStyle().WindowPadding.y);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 120);
    if (ImGui::Button("Close", ImVec2(120, 0))) app.dismiss_report_result();
    ImGui::End();
}

} // namespace

void draw_report_screen(App& app) {
    if (app.screen() == Screen::ReportSent) {
        draw_sent(app);
        return;
    }

    ReportDraft& draft = app.report_draft();
    ImGuiIO& io = ImGui::GetIO();
    // Opened before Begin, so the window's own background and border are drawn with it. Same
    // theme as Settings: this is a dialog of ours, not a panel read over the game at a glance.
    const ui::Theme theme(app.config().reduce_transparency);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Bug reporter", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);

    draw_header(app, "Path of Price Check bug reporter", &App::close_bug_report);
    ImGui::Separator();
    draw_comment(draft);
    ImGui::Spacing();

    // Everything below the body is fixed height, so the body is what is left. Measured rather
    // than declared, because the disclaimer wraps and how many lines it takes depends on the
    // width the dialog ended up with.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float below = 1.0f + style.ItemSpacing.y * 4 +
                        disclaimer_height(ImGui::GetContentRegionAvail().x) +
                        ImGui::GetFrameHeightWithSpacing();
    draw_body(app, draft, std::max(ImGui::GetTextLineHeight() * 8,
                                   ImGui::GetContentRegionAvail().y - below));

    ImGui::Separator();
    draw_disclaimer();
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeightWithSpacing() -
                         style.WindowPadding.y);
    draw_footer(app, draft);
    draw_failure(app);
    ImGui::End();
}

} // namespace ppc
