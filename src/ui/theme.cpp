#include "ui/theme.hpp"

namespace ppc::ui {
namespace {

/// What ImGui's dark theme asks for, and what the overlay is worth keeping when transparency
/// is not being reduced: enough of the game shows through to place the panel against it,
/// without the text picking up whatever is behind it.
constexpr float kSeeThrough = 0.93f;

ImVec4 with_alpha(ImVec4 c, float a) { return ImVec4(c.x, c.y, c.z, a); }

/// A colour at a fraction of its own brightness, alpha untouched. The game's dimmer greys are
/// its brighter ones darkened rather than separate hues, so its panels stay one temperature.
ImVec4 dim(ImVec4 c, float f) { return ImVec4(c.x * f, c.y * f, c.z * f, c.w); }

} // namespace

Theme::Theme(bool opaque) {
    const auto colour = [this](ImGuiCol idx, const ImVec4& v) {
        ImGui::PushStyleColor(idx, v);
        ++colours_;
    };
    const auto var = [this](ImGuiStyleVar idx, auto v) {
        ImGui::PushStyleVar(idx, v);
        ++vars_;
    };

    const float bg_a = opaque ? 1.0f : kSeeThrough;

    colour(ImGuiCol_WindowBg, with_alpha(col::kWindow, bg_a));
    // The form sits in a well, the way the game insets a scrolling list. A fraction of black
    // rather than a colour of its own, so it darkens whatever the window already is.
    colour(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.18f));
    colour(ImGuiCol_PopupBg, with_alpha(col::kWindow, opaque ? 1.0f : 0.97f));
    colour(ImGuiCol_Border, col::kBorder);
    colour(ImGuiCol_BorderShadow, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    colour(ImGuiCol_Text, col::kText);
    colour(ImGuiCol_TextDisabled, col::kTextDim);
    colour(ImGuiCol_TextSelectedBg, with_alpha(col::kAccent, 0.35f));

    colour(ImGuiCol_FrameBg, col::kFrame);
    colour(ImGuiCol_FrameBgHovered, col::kFrameHovered);
    colour(ImGuiCol_FrameBgActive, col::kFrameActive);
    // A ticked checkbox takes this instead of FrameBg, and its default is ImGui's blue.
    colour(ImGuiCol_CheckboxSelectedBg, col::kFrameActive);
    colour(ImGuiCol_InputTextCursor, col::kAccent);

    colour(ImGuiCol_Button, col::kButton);
    colour(ImGuiCol_ButtonHovered, col::kButtonHovered);
    colour(ImGuiCol_ButtonActive, col::kButtonActive);

    // Combo entries and any other Selectable.
    colour(ImGuiCol_Header, col::kButtonHovered);
    colour(ImGuiCol_HeaderHovered, col::kButtonActive);
    colour(ImGuiCol_HeaderActive, with_alpha(col::kAccent, 0.45f));

    colour(ImGuiCol_CheckMark, col::kAccent);
    colour(ImGuiCol_SliderGrab, dim(col::kAccent, 0.80f));
    colour(ImGuiCol_SliderGrabActive, col::kAccent);

    colour(ImGuiCol_Separator, dim(col::kBorder, 0.75f));
    colour(ImGuiCol_SeparatorHovered, col::kBorder);
    colour(ImGuiCol_SeparatorActive, col::kAccent);

    colour(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.24f));
    colour(ImGuiCol_ScrollbarGrab, col::kButton);
    colour(ImGuiCol_ScrollbarGrabHovered, col::kButtonHovered);
    colour(ImGuiCol_ScrollbarGrabActive, col::kButtonActive);

    colour(ImGuiCol_NavCursor, col::kAccent);

    // Square corners and a hairline on everything that holds a value: the game draws a bevel
    // around every control, and a border is the nearest thing a flat renderer has to one.
    // Padding is only a little above ImGui's default — the game's own spacing is far too
    // generous to copy onto a panel that has to fit beside a stash.
    var(ImGuiStyleVar_WindowRounding, 0.0f);
    var(ImGuiStyleVar_WindowBorderSize, 1.0f);
    var(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    var(ImGuiStyleVar_ChildRounding, 2.0f);
    var(ImGuiStyleVar_ChildBorderSize, 1.0f);
    var(ImGuiStyleVar_PopupRounding, 2.0f);
    var(ImGuiStyleVar_PopupBorderSize, 1.0f);
    var(ImGuiStyleVar_FrameRounding, 2.0f);
    var(ImGuiStyleVar_FrameBorderSize, 1.0f);
    var(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 4.0f));
    var(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 5.0f));
    var(ImGuiStyleVar_GrabRounding, 2.0f);
    var(ImGuiStyleVar_GrabMinSize, 14.0f);
    var(ImGuiStyleVar_ScrollbarRounding, 2.0f);
    var(ImGuiStyleVar_ScrollbarSize, 12.0f);
    var(ImGuiStyleVar_TabRounding, 2.0f);
    var(ImGuiStyleVar_TabBorderSize, 1.0f);
}

Theme::~Theme() {
    ImGui::PopStyleVar(vars_);
    ImGui::PopStyleColor(colours_);
}

void set_opaque_windows(bool on) {
    ImGuiStyle& s = ImGui::GetStyle();
    const float a = on ? 1.0f : kSeeThrough;
    s.Colors[ImGuiCol_WindowBg].w = a;
    s.Colors[ImGuiCol_PopupBg].w = a;
}

} // namespace ppc::ui
