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

/// The codepoints behind the above, for the one place that has to ask the atlas whether they
/// actually baked rather than trusting that they did.
inline constexpr unsigned int kGlyphCodepoints[]{0xF00C, 0xF0E2};

} // namespace ppc::ui
