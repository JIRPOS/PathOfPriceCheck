#include "screens/quickpaste_screen.hpp"

#include <string>
#include <vector>

#include <imgui.h>

#include "app.hpp"
#include "quickpaste.hpp"
#include "ui/glyphs.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"

namespace ppc {
namespace {

/// The popup's shape. Every one of these is read twice — by `quickpaste_size`, which places the
/// window before there is a frame to measure in, and by the drawing below. They are the same
/// numbers or the list does not fill its own window.
constexpr float kWindowW = 380.0f;
constexpr float kRowH = 46.0f;    ///< tall enough for the heading and the line under it
constexpr float kSquareGap = 10.0f;
constexpr float kHeadingSize = 17.0f;
constexpr float kPreviewSize = 14.0f;
constexpr float kNumberSize = 20.0f;
constexpr float kFooterH = 38.0f; ///< the separator and the Add button under it
constexpr float kEmptyH = 26.0f;  ///< the one line drawn when nothing is enabled

/// `s` cut to `max_w` pixels with an ellipsis, in whatever font is current. Binary search over
/// character boundaries rather than a walk: this runs for every row of every frame the popup is
/// up, and `CalcTextSize` is itself a walk.
std::string ellipsize(const std::string& s, float max_w) {
    if (s.empty() || ImGui::CalcTextSize(s.c_str()).x <= max_w) return s;
    static constexpr const char* kEllipsis = "\xe2\x80\xa6";
    const float ell = ImGui::CalcTextSize(kEllipsis).x;
    std::vector<size_t> at{0}; // byte offset of each character
    for (size_t i = 1; i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) at.push_back(i);
    size_t lo = 0, hi = at.size(); // characters that fit
    while (lo < hi) {
        const size_t mid = (lo + hi + 1) / 2;
        const size_t bytes = mid == at.size() ? s.size() : at[mid];
        if (ImGui::CalcTextSize(s.c_str(), s.c_str() + bytes).x + ell <= max_w) lo = mid;
        else hi = mid - 1;
    }
    return s.substr(0, lo == at.size() ? s.size() : at[lo]) + kEllipsis;
}

void draw_text_at(const ImVec2& pos, const char* s) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::TextUnformatted(s);
}

/// One paste: the number key on the left, its heading and the first line of its text on the
/// right. The whole strip is the click target — a `Selectable` with everything drawn on top of
/// it, the same shape the unique picker uses — because the thing being aimed at is the entry,
/// not the words in it.
void draw_row(App& app, size_t slot, size_t index) {
    const Paste& p = app.config().pastes[index];
    ImGui::PushID(static_cast<int>(index));
    const ImVec2 at = ImGui::GetCursorPos();
    const bool picked = ImGui::Selectable("##pick", false, ImGuiSelectableFlags_AllowOverlap,
                                          ImVec2(0, kRowH));
    const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();

    // The square carries the key you press instead of aiming. Its own background, darker than
    // the row, so it reads as a key cap rather than as the first word of the heading.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 sq1(p0.x + kRowH, p1.y);
    dl->AddRectFilled(p0, sq1, ImGui::GetColorU32(ui::col::kTabIdle), 2.0f);
    dl->AddRect(p0, sq1, ImGui::GetColorU32(ui::col::kBorder), 2.0f);

    ImGui::PushFont(app.fonts().small_caps, kNumberSize);
    const std::string key = std::to_string(slot + 1);
    const ImVec2 ks = ImGui::CalcTextSize(key.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ui::col::kAccent);
    draw_text_at(ImVec2(p0.x + (kRowH - ks.x) * 0.5f, p0.y + (kRowH - ks.y) * 0.5f), key.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    const float text_x = p0.x + kRowH + kSquareGap;
    const float text_w = p1.x - text_x;
    ImGui::PushFont(app.fonts().small_caps, kHeadingSize);
    const float head_h = ImGui::GetTextLineHeight();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::col::kTitle);
    const std::string heading =
        p.heading.empty() ? std::string(ui::text(ui::Msg::PasteUntitled)) : p.heading;
    draw_text_at(ImVec2(text_x, p0.y + 6.0f), ellipsize(heading, text_w).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::PushFont(app.fonts().regular, kPreviewSize);
    ImGui::PushStyleColor(ImGuiCol_Text, ui::col::kTextDim);
    const std::string body = paste_preview(p.body);
    draw_text_at(ImVec2(text_x, p0.y + 8.0f + head_h),
                 ellipsize(body.empty() ? std::string(ui::text(ui::Msg::PasteEmptyBody)) : body,
                           text_w)
                     .c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    // The cursor was moved back over the row to draw on it, so the row has to close itself or
    // the next one starts inside this one.
    ImGui::SetCursorPos(at);
    ImGui::Dummy(ImVec2(0, kRowH));
    ImGui::PopID();
    if (picked) app.pick_paste(index);
}

} // namespace

void quickpaste_size(size_t entries, int* w, int* h) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rows = entries ? static_cast<float>(entries) * kRowH +
                                     static_cast<float>(entries - 1) * style.ItemSpacing.y
                               : kEmptyH;
    *w = static_cast<int>(kWindowW);
    *h = static_cast<int>(style.WindowPadding.y * 2.0f + rows + kFooterH);
}

void draw_quickpaste_screen(App& app) {
    const ui::Theme theme(app.config().reduce_transparency);
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("QuickPaste", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);

    // A quieter hover than the theme's Selectable: the row is the size of a button and a
    // full-strength highlight on something the mouse only passes over reads as a selection.
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ui::col::kFrameHovered);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ui::col::kFrameActive);
    const std::vector<size_t> active = active_pastes(app.config().pastes);
    for (size_t slot = 0; slot < active.size(); ++slot) draw_row(app, slot, active[slot]);
    if (active.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", ui::text(ui::Msg::QuickPasteNone));
    }
    ImGui::PopStyleColor(2);

    // The way in to where these are managed, since a popup with nothing in it has to say what
    // to do about that — and Settings is where it is said.
    ImGui::Separator();
    const std::string add = app.fonts().has_glyphs
                                ? std::string(ui::kGlyphAdd) + "  " + ui::text(ui::Msg::QuickPasteAdd)
                                : std::string(ui::text(ui::Msg::QuickPasteAdd));
    if (ImGui::Button(add.c_str())) app.open_paste_settings();

    ImGui::End();
}

} // namespace ppc
