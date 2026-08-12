#pragma once

/// The UI glyphs the overlay draws, as the UTF-8 that selects them.
///
/// A subset of **Font Awesome Free Solid** is merged over every Fontin face (see `fonts.cpp`),
/// so these are ordinary text: they take the text colour, scale with the font size and sit
/// inside a `Button` label like any other string. They live in the Private Use Area, which
/// Fontin does not claim, so no exclusion is needed to keep the two from fighting.
///
/// **Which codepoints are bundled is `scripts/fetch-glyphs.sh`**, and a name here that is not in
/// that list bakes as nothing at all — the same silent blank Fontin's own `≤` and `≥` produce.
/// `Fonts::has_glyphs` is the check that catches it; every caller has a word to fall back on.
namespace ppc::ui {

inline constexpr const char* kGlyphConfirm = "\xef\x80\x8c"; ///< U+F00C, check
inline constexpr const char* kGlyphReset = "\xef\x83\xa2";   ///< U+F0E2, arrow-rotate-left
inline constexpr const char* kGlyphAdd = "\xef\x83\xbe";     ///< U+F0FE, square-plus
inline constexpr const char* kGlyphEdit = "\xef\x8c\x84";    ///< U+F304, pen
inline constexpr const char* kGlyphDelete = "\xef\x8b\xad";  ///< U+F2ED, trash-can
inline constexpr const char* kGlyphGrip = "\xef\x9e\xa4";    ///< U+F7A4, grip-lines
inline constexpr const char* kGlyphSearch = "\xef\x80\x82"; ///< U+F002, magnifying-glass
inline constexpr const char* kGlyphExternal = "\xef\x82\x8e"; ///< U+F08E, arrow-up-right-from-square
inline constexpr const char* kGlyphBug = "\xef\x86\x88";      ///< U+F188, bug
inline constexpr const char* kGlyphApply = "\xef\x83\x90";   ///< U+F0D0, wand-magic

/// The four map-check verdicts, in the order they cycle. `kGlyphUnrated` is drawn dim rather
/// than left out: every row keeps the same first column, so a list of them reads as a column of
/// answers with the blanks visible instead of as text that starts in three different places.
inline constexpr const char* kGlyphUnrated = "\xef\x84\xa8";   ///< U+F128, question
inline constexpr const char* kGlyphSafe = kGlyphConfirm;       ///< U+F00C, check
inline constexpr const char* kGlyphDangerous = "\xef\x81\xb1"; ///< U+F071, triangle-exclamation
inline constexpr const char* kGlyphDeadly = "\xef\x9c\x94";    ///< U+F714, skull-crossbones

/// The codepoints behind the above, for the one place that has to ask the atlas whether they
/// actually baked rather than trusting that they did.
inline constexpr unsigned int kGlyphCodepoints[]{0xF00C, 0xF0E2, 0xF0FE, 0xF304, 0xF2ED,
                                                 0xF7A4, 0xF002, 0xF08E, 0xF188, 0xF0D0,
                                                 0xF128, 0xF071, 0xF714};

} // namespace ppc::ui
