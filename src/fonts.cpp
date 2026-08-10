#include "fonts.hpp"

#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>

#include "data/mapped_file.hpp"
#include "ui/glyphs.hpp"

namespace ppc {
namespace {

// Fontin, bundled into the executable so a build has no asset dependency. Regenerate with
// scripts/gen-font-data.sh; see assets/fonts/README.md for the license it ships under.
#include "fontin_data.inc"

// The UI glyphs, on the same terms. Regenerate with scripts/gen-glyph-data.sh.
#include "glyph_data.inc"

constexpr const char* kRegular = "Fontin-Regular.ttf";
constexpr const char* kBold = "Fontin-Bold.ttf";
constexpr const char* kItalic = "Fontin-Italic.ttf";
constexpr const char* kSmallCaps = "Fontin-SmallCaps.ttf";

/// `≤` and `≥`, which Fontin cannot draw and does not admit to.
///
/// Its cmap maps both codepoints to a real glyph id and its `glyf` holds **zero bytes** for
/// either, so the text lays out at the right advance width and paints nothing — not even the
/// missing-glyph box that would have made it obvious. **A cmap entry is not evidence that a font
/// can draw something**; the glyph's own length is, and checking only the cmap is how these
/// shipped as if they worked.
///
/// So they are excluded from every Fontin source and borrowed from a system face, which is what
/// keeps the digits beside them in Fontin. `GlyphExcludeRanges` is what makes that possible at
/// all: a merged source only ever serves a codepoint no earlier source claims, and Fontin claims
/// these. `×` (U+00D7) is the same argument and is not here only because nothing draws one — it
/// is absent from Fontin's cmap outright, so adding it to this range is all it would take.
constexpr ImWchar kBorrowedGlyphs[]{0x2264, 0x2265, 0};

/// The complement, for the borrowed source: everything *except* the two. ImGui 1.92 loads
/// glyphs on demand and ignores `GlyphRanges` while doing it, so an exclude list is the only way
/// to say "this source is here for two characters" — and without it the borrowed face would also
/// serve every script Fontin lacks, quietly replacing the boxes `fonts.unicode` exists to draw
/// properly.
constexpr ImWchar kOnlyBorrowed[]{1, 0x2263, 0x2266, IM_UNICODE_CODEPOINT_MAX, 0};

/// The same complement for the glyph subset, which lives in the Private Use Area between
/// U+F00C and U+F0E2. The subset carries no outline outside those two, but it does carry a
/// `.notdef` — and a merged source answering for *that* would serve every codepoint nothing
/// else has, replacing the boxes `fonts.unicode` exists to draw honestly.
constexpr ImWchar kOnlyGlyphs[]{1, 0xF00B, 0xF0E3, IM_UNICODE_CODEPOINT_MAX, 0};

/// Font Awesome draws on a square em and Fontin on a face with descenders, so a glyph baked at
/// the text size stands a touch tall and sits a touch high against the words beside it. Both
/// numbers are fractions of the size so they hold at every scale: the glyphs are only ever
/// used on buttons, where being a hair smaller than the label height is what keeps the button
/// the same height as one with a word on it.
constexpr float kGlyphScale = 0.86f;
constexpr float kGlyphNudgeY = 0.10f;

bool exists(const std::string& path) { return SDL_GetPathInfo(path.c_str(), nullptr); }

std::string with_slash(std::string d) {
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    return d;
}

/// Optional on-disk override, so the typeface can be swapped without a rebuild.
/// Empty unless $PPC_FONT_DIR actually holds Fontin-Regular.ttf.
std::string find_font_dir() {
    const char* env = SDL_getenv("PPC_FONT_DIR");
    if (!env) return {};
    std::string d = with_slash(env);
    if (exists(d + kRegular)) return d;
    SDL_Log("PPC_FONT_DIR=%s has no %s; using the built-in Fontin", env, kRegular);
    return {};
}

/// AddFontFromFileTTF asserts on a missing file, so probe first and return nothing rather than
/// taking down a debug build over a partial dir; the caller substitutes Regular.
ImFont* add_file_face(const std::string& dir, const char* file, float size_px,
                      const ImFontConfig& cfg) {
    const std::string path = dir + file;
    if (!exists(path)) {
        SDL_Log("missing %s, substituting Regular", file);
        return nullptr;
    }
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), size_px, &cfg);
}

/// The system faces that together cover the scripts PoE account names show up in, most
/// specific first — a Latin/Cyrillic/Greek base, then whatever carries CJK and Hangul.
/// Merged sources are consulted in order for a glyph the ones before them lack, so the base
/// keeps every Latin shape and the big collections only ever serve what nothing else has.
///
/// Deliberately not embedded like Fontin: a CJK collection is ~19MB, several times the whole
/// executable, and dead weight for everyone whose trade results are Latin.
std::vector<std::string> system_faces() {
    std::vector<std::string> out;
#ifdef _WIN32
    const char* root = SDL_getenv("SystemRoot");
    const std::string dir = std::string(root ? root : "C:\\Windows") + "\\Fonts\\";
    for (const char* f : {"segoeui.ttf", "arial.ttf"})
        if (exists(dir + f)) {
            out.push_back(dir + f);
            break;
        }
    // Three of them because no single Windows face carries Hangul, both Chinese scripts and
    // kana at once. All ship with the OS; any that don't are simply skipped.
    for (const char* f : {"malgun.ttf", "msyh.ttc", "YuGothR.ttc", "meiryo.ttc"})
        if (exists(dir + f) && out.size() < 4) out.push_back(dir + f);
#else
    for (const char* f : {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                          "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                          "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
                          "/usr/share/fonts/TTF/DejaVuSans.ttf",
                          "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                          "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                          "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
                          "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"})
        if (exists(f)) {
            out.emplace_back(f);
            break;
        }
    // Noto Sans CJK covers Japanese, Korean and both Chinese scripts in one collection, so
    // unlike Windows the first hit is the whole answer.
    for (const char* f : {"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                          "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                          "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
                          "/usr/share/fonts/opentype/noto-cjk/NotoSansCJK-Regular.ttc",
                          "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
                          "/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc"})
        if (exists(f)) {
            out.emplace_back(f);
            break;
        }
#endif
    return out;
}

/// Faces to borrow `≤` and `≥` from, most likely first — every one that exists is merged, and
/// one that turns out not to carry them after all is simply passed over when the glyph is asked
/// for. Deliberately **not** `system_faces()`: covering the Latin scripts says nothing about the
/// mathematical operators, and Noto Sans — the usual first hit there — does not have either of
/// these, having split them into a Noto Sans Math nobody installs by default.
std::vector<std::string> math_faces() {
    std::vector<std::string> out;
#ifdef _WIN32
    const char* root = SDL_getenv("SystemRoot");
    const std::string dir = std::string(root ? root : "C:\\Windows") + "\\Fonts\\";
    for (const char* f : {"segoeui.ttf", "arial.ttf", "seguisym.ttf"})
#else
    for (const char* f : {"/usr/share/fonts/TTF/DejaVuSans.ttf",
                          "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                          "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                          "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
                          "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                          "/usr/share/fonts/noto/NotoSansMath-Regular.ttf",
                          "/usr/share/fonts/truetype/noto/NotoSansMath-Regular.ttf",
                          "/usr/share/fonts/gnu-free/FreeSans.ttf"})
#endif
    {
#ifdef _WIN32
        const std::string p = dir + f;
#else
        const std::string p = f;
#endif
        if (exists(p)) out.push_back(p);
        if (out.size() == 2) break; // one spare, in case the first has the codepoint but no glyph
    }
    return out;
}

/// Mapped font files, kept for the life of the atlas.
///
/// The files are **mapped, not read**: ImGui 1.92 rasterizes glyphs on demand and keeps the
/// font bytes for the life of the atlas, so a collection nothing on screen needs costs
/// address space rather than resident memory. That is what makes merging a 19MB CJK face
/// affordable in an overlay whose whole point is being small.
std::vector<data::MappedFile>& font_maps() {
    static std::vector<data::MappedFile> maps; // must outlive the atlas; it reads them lazily
    return maps;
}

/// Where a font's bytes are — a file this process mapped, or one of the arrays the build embedded.
/// Taken by value on purpose: `font_maps()` is a vector and grows, so a `MappedFile*` into it dies
/// at the next map — while the *address it mapped* does not, because moving the object moves a
/// pointer and not the pages.
struct FontBytes {
    const uint8_t* data = nullptr;
    size_t size = 0;
    explicit operator bool() const { return data != nullptr; }
};

/// Map a font file and keep it for the life of the atlas, or nothing if it cannot be read.
FontBytes map_font(const std::string& path) {
    data::MappedFile m;
    if (!m.open(path)) return {};
    font_maps().push_back(std::move(m));
    return {font_maps().back().data(), font_maps().back().size()};
}

/// Add a font as a source: its own face, or — with `merge` — folded into whichever face was added
/// last. `exclude` is the codepoints this source must not answer for.
ImFont* add_face(FontBytes b, float size_px, bool merge, const ImWchar* exclude = nullptr) {
    ImFontConfig cfg;
    // Never the atlas's to free: the bytes are a mapping in `font_maps()` or a static array in the
    // binary, and both outlive it. **Not settable on a config that also goes to
    // `AddFontFromFileTTF`** — that one allocates the buffer itself and does leak it if told the
    // atlas does not own it, which is why this config is built here rather than passed in.
    cfg.FontDataOwnedByAtlas = false;
    cfg.MergeMode = merge;
    cfg.GlyphExcludeRanges = exclude;
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<uint8_t*>(b.data),
                                                      static_cast<int>(b.size), size_px, &cfg);
}

ImFont* load_unicode_face(const std::vector<std::string>& faces, float size_px) {
    ImFont* font = nullptr;
    for (const std::string& p : faces) {
        const FontBytes b = map_font(p);
        if (!b) continue;
        if (ImFont* got = add_face(b, size_px, font != nullptr)) {
            SDL_Log("names render in %s", p.c_str());
            if (!font) font = got;
        }
    }
    return font;
}

} // namespace

Fonts load_fonts(float size_px) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::GetStyle().FontSizeBase = size_px;

    // Mapped up front: the same bytes are merged over every Fontin face, and ImGui keeps them
    // rather than copying, so one mapping serves all four.
    std::vector<FontBytes> borrow;
    for (const std::string& p : math_faces())
        if (const FontBytes b = map_font(p)) borrow.push_back(b);

    ImFontConfig cfg;
    cfg.GlyphExcludeRanges = kBorrowedGlyphs;
    // Merged straight after the face it belongs to: MergeMode folds a source into whichever face
    // was added last, so this cannot be hoisted out of the middle of the list.
    const auto borrowed = [&](ImFont* face) {
        if (!face) return face;
        for (const FontBytes b : borrow) add_face(b, size_px, true, kOnlyBorrowed);
        ImFontConfig gcfg;
        gcfg.FontDataOwnedByAtlas = false; // a static array in the binary
        gcfg.MergeMode = true;
        gcfg.GlyphExcludeRanges = kOnlyGlyphs;
        gcfg.GlyphOffset = ImVec2(0.0f, size_px * kGlyphNudgeY);
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(ppc_glyphs_ttf),
                                       static_cast<int>(sizeof ppc_glyphs_ttf),
                                       size_px * kGlyphScale, &gcfg);
        return face;
    };

    Fonts f;
    if (std::string dir = find_font_dir(); !dir.empty()) {
        f.regular = borrowed(io.Fonts->AddFontFromFileTTF((dir + kRegular).c_str(), size_px, &cfg));
        f.bold = borrowed(add_file_face(dir, kBold, size_px, cfg));
        f.italic = borrowed(add_file_face(dir, kItalic, size_px, cfg));
        f.small_caps = borrowed(add_file_face(dir, kSmallCaps, size_px, cfg));
        if (!f.bold) f.bold = f.regular;
        if (!f.italic) f.italic = f.regular;
        if (!f.small_caps) f.small_caps = f.regular;
    } else {
        // Fontin ships SmallCaps as its own family rather than an OpenType `smcp`
        // feature — load-bearing, since ImGui does no shaping or feature substitution.
        auto add = [&](const unsigned char* data, size_t size) {
            return borrowed(add_face({data, size}, size_px, false, kBorrowedGlyphs));
        };
        f.regular = add(fontin_regular_ttf, sizeof fontin_regular_ttf);
        f.bold = add(fontin_bold_ttf, sizeof fontin_bold_ttf);
        f.italic = add(fontin_italic_ttf, sizeof fontin_italic_ttf);
        f.small_caps = add(fontin_small_caps_ttf, sizeof fontin_small_caps_ttf);
    }
    // Asked rather than assumed: a face existing is not a face carrying these two, and the one
    // Noto ships as its Latin base carries neither. `FindGlyphNoFallback` bakes on demand and
    // answers null only when no source at all served the codepoint, which is exactly the
    // question — the excluded Fontin glyph cannot answer it and a `?` would not be visible here.
    ImFontBaked* baked = f.regular ? f.regular->GetFontBaked(size_px) : nullptr;
    f.has_comparison_glyphs = baked && baked->FindGlyphNoFallback(0x2264) != nullptr &&
                              baked->FindGlyphNoFallback(0x2265) != nullptr;
    if (!f.has_comparison_glyphs)
        SDL_Log("no system face carries \xe2\x89\xa4 or \xe2\x89\xa5; spelling them out instead");
    // Asked of the atlas for the same reason, though this one is embedded and so can only fail
    // by the subset and `ui/glyphs.hpp` having drifted apart. That is worth catching loudly: the
    // symptom downstream is a button with nothing drawn on it.
    f.has_glyphs = baked != nullptr;
    for (const unsigned int cp : ui::kGlyphCodepoints)
        if (!baked || !baked->FindGlyphNoFallback(static_cast<ImWchar>(cp))) f.has_glyphs = false;
    if (!f.has_glyphs)
        SDL_Log("the bundled glyph subset is missing what ui/glyphs.hpp names; "
                "regenerate with scripts/gen-glyph-data.sh");

    // Last, so a merge cannot latch onto one of the Fontin faces by accident.
    f.unicode = load_unicode_face(system_faces(), size_px);
    if (!f.unicode) {
        SDL_Log("no system face with non-Latin coverage; names may render as boxes");
        f.unicode = f.regular;
    }
    io.FontDefault = f.regular;
    return f;
}

} // namespace ppc
