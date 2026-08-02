#pragma once

struct ImFont;

namespace ppc {

/// The Fontin faces Path of Exile itself uses. Sizes are chosen at use time —
/// ImGui 1.92 fonts are dynamically scalable, so one ImFont* per face covers every
/// size: PushFont(fonts.bold, 22.0f).
struct Fonts {
    ImFont* regular = nullptr;
    ImFont* bold = nullptr;
    ImFont* italic = nullptr;
    ImFont* small_caps = nullptr;
};

/// Loads Fontin into the current ImGui context and makes Regular the default. Uses
/// the faces embedded in the executable unless $PPC_FONT_DIR points at a directory
/// holding Fontin-{Regular,Bold,Italic,SmallCaps}.ttf, which then wins.
/// Must be called after ImGui::CreateContext(). `size_px` becomes style.FontSizeBase.
Fonts load_fonts(float size_px);

} // namespace ppc
