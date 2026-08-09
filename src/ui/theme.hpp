#pragma once

#include <imgui.h>

/// The look: Path of Exile's own palette and control shapes, as far as an ImGui widget can
/// carry them.
///
/// Unlike `ui/strings`, this half of `ui/` is ImGui's and therefore **app-side, never
/// `ppc_core`** — nothing here may be included from a file the tests link.
///
/// Deliberately not the global style. `Theme` is scoped to the window that opens it, because
/// only Settings is styled so far: the price-check panel is a different problem (it is read at
/// a glance, over a game, next to the game's own tooltip) and restyling it is not this change.
namespace ppc::ui {

/// Sampled off the game's Options dialog, and lifted where a value that reads well on PoE's
/// ornate frame would not on a flat panel. Names say what the colour is *for*, not what it is.
namespace col {

inline constexpr ImVec4 kWindow{0.086f, 0.071f, 0.055f, 1.0f};
inline constexpr ImVec4 kFrame{0.043f, 0.035f, 0.027f, 1.0f};
inline constexpr ImVec4 kFrameHovered{0.098f, 0.078f, 0.055f, 1.0f};
inline constexpr ImVec4 kFrameActive{0.133f, 0.106f, 0.071f, 1.0f};
inline constexpr ImVec4 kBorder{0.361f, 0.286f, 0.176f, 1.0f};
inline constexpr ImVec4 kButton{0.149f, 0.122f, 0.086f, 1.0f};
inline constexpr ImVec4 kButtonHovered{0.259f, 0.196f, 0.118f, 1.0f};
inline constexpr ImVec4 kButtonActive{0.353f, 0.259f, 0.141f, 1.0f};

inline constexpr ImVec4 kText{0.839f, 0.804f, 0.729f, 1.0f};
inline constexpr ImVec4 kTextDim{0.510f, 0.475f, 0.416f, 1.0f};
/// The left column. The game tints every setting's name and leaves the value plain, which is
/// most of why its option screens read as a grid without drawing one.
inline constexpr ImVec4 kLabel{0.749f, 0.561f, 0.337f, 1.0f};
inline constexpr ImVec4 kSection{0.937f, 0.898f, 0.792f, 1.0f};
inline constexpr ImVec4 kTitle{0.871f, 0.741f, 0.478f, 1.0f};
/// Selection, check marks, slider grabs, the open tab's name — the game says "this one" in
/// orange.
inline constexpr ImVec4 kAccent{0.878f, 0.541f, 0.200f, 1.0f};
/// A tab that is not the open one: recessed rather than raised.
inline constexpr ImVec4 kTabIdle{0.055f, 0.047f, 0.039f, 1.0f};
inline constexpr ImVec4 kClose{0.545f, 0.180f, 0.118f, 1.0f};
inline constexpr ImVec4 kCloseHovered{0.694f, 0.243f, 0.157f, 1.0f};
inline constexpr ImVec4 kWarn{0.90f, 0.55f, 0.25f, 1.0f};

} // namespace col

/// Pushes the style above for as long as it is in scope. Open one immediately before the
/// window's `Begin`, so that the window's own background and border are drawn with it.
///
/// `opaque` is `Config::reduce_transparency`: it makes the panel's own backgrounds solid.
/// A blur is the other way to answer that setting and is not available to us — there is no
/// backdrop to sample. The overlay composites over another process's window, and what is
/// behind our pixels never reaches our framebuffer.
class Theme {
public:
    explicit Theme(bool opaque);
    ~Theme();
    Theme(const Theme&) = delete;
    Theme& operator=(const Theme&) = delete;

private:
    int colours_ = 0;
    int vars_ = 0;
};

/// Window backgrounds *outside* a `Theme` scope — the price-check panel, the item card, the
/// combo popups they open. Solid when `on`, and ImGui's own near-opaque default otherwise.
/// Mutates the global style, so it is called when the setting changes, not per frame; the
/// Settings dialog previews its own checkbox through `Theme` instead, before any Save.
void set_opaque_windows(bool on);

} // namespace ppc::ui
