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

    /// For text we did not write — account and character names off the trade site, which are
    /// routinely Cyrillic, Hangul or CJK. Fontin has not one of those glyphs, so a name that
    /// is not Latin renders as a row of boxes in every other face here. Assembled from
    /// whatever the OS ships; falls back to `regular`, boxes and all, when it ships nothing.
    ImFont* unicode = nullptr;

    /// For text that is **data rather than prose** — the clipboard capture and the parse dump a
    /// bug report is a preview of. Those are read column-wise and compared line against line, and
    /// Fontin is a proportional face with no figure alignment at all. Whatever monospace the OS
    /// ships; falls back to `regular`, which still reads, just not in columns.
    ImFont* mono = nullptr;

    /// `≤` and `≥` will actually draw — see `kBorrowedGlyphs`. False when the OS shipped no
    /// face to borrow them from, and then whatever wanted them has to spell them "<=" and ">=":
    /// Fontin's own are blank, and a floor of 46 losing its `≥` reads as an exact match.
    bool has_comparison_glyphs = false;

    /// `ui::kGlyph*` will actually draw. These are embedded rather than borrowed, so this can
    /// only be false if the bundled subset and `ui/glyphs.hpp` have drifted apart — which is
    /// exactly the case worth catching, since the symptom is a button with nothing on it.
    bool has_glyphs = false;
};

/// Loads Fontin into the current ImGui context and makes Regular the default. Uses
/// the faces embedded in the executable unless $PPC_FONT_DIR points at a directory
/// holding Fontin-{Regular,Bold,Italic,SmallCaps}.ttf, which then wins.
/// Must be called after ImGui::CreateContext(). `size_px` becomes style.FontSizeBase.
Fonts load_fonts(float size_px);

} // namespace ppc
