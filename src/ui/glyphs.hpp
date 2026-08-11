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

/// The codepoints behind the above, for the one place that has to ask the atlas whether they
/// actually baked rather than trusting that they did.
inline constexpr unsigned int kGlyphCodepoints[]{0xF00C, 0xF0E2, 0xF0FE, 0xF304, 0xF2ED,
                                                 0xF7A4, 0xF002, 0xF08E, 0xF188};

} // namespace ppc::ui
