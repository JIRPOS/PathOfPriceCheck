#include "fonts.hpp"

#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>

#include "data/mapped_file.hpp"

namespace ppc {
namespace {

// Fontin, base85-encoded, bundled into the executable so a build has no asset
// dependency. Regenerate with scripts/gen-font-data.sh; see assets/fonts/README.md
// for the license it ships under.
#include "fontin_data.inc"

constexpr const char* kRegular = "Fontin-Regular.ttf";
constexpr const char* kBold = "Fontin-Bold.ttf";
constexpr const char* kItalic = "Fontin-Italic.ttf";
constexpr const char* kSmallCaps = "Fontin-SmallCaps.ttf";

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

/// AddFontFromFileTTF asserts on a missing file, so probe first and degrade to the
/// face we already have rather than taking down a debug build over a partial dir.
ImFont* add_file_face(const std::string& dir, const char* file, float size_px, ImFont* fallback) {
    std::string path = dir + file;
    if (!exists(path)) {
        SDL_Log("missing %s, substituting Regular", file);
        return fallback;
    }
    ImFont* f = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), size_px);
    return f ? f : fallback;
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

/// One ImFont merging every face `system_faces()` found, or null when it found none.
///
/// The files are **mapped, not read**: ImGui 1.92 rasterizes glyphs on demand and keeps the
/// font bytes for the life of the atlas, so a collection nothing on screen needs costs
/// address space rather than resident memory. That is what makes merging a 19MB CJK face
/// affordable in an overlay whose whole point is being small.
ImFont* load_unicode_face(float size_px) {
    static std::vector<data::MappedFile> maps; // must outlive the atlas; it reads them lazily
    ImFont* font = nullptr;
    for (const std::string& p : system_faces()) {
        data::MappedFile m;
        if (!m.open(p)) continue;
        maps.push_back(std::move(m));
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false; // `maps` is the storage
        cfg.MergeMode = font != nullptr;
        ImFont* got = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
            const_cast<uint8_t*>(maps.back().data()), static_cast<int>(maps.back().size()),
            size_px, &cfg);
        if (!got) {
            maps.pop_back();
            continue;
        }
        SDL_Log("names render in %s", p.c_str());
        if (!font) font = got;
    }
    return font;
}

} // namespace

Fonts load_fonts(float size_px) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::GetStyle().FontSizeBase = size_px;

    Fonts f;
    if (std::string dir = find_font_dir(); !dir.empty()) {
        f.regular = io.Fonts->AddFontFromFileTTF((dir + kRegular).c_str(), size_px);
        f.bold = add_file_face(dir, kBold, size_px, f.regular);
        f.italic = add_file_face(dir, kItalic, size_px, f.regular);
        f.small_caps = add_file_face(dir, kSmallCaps, size_px, f.regular);
    } else {
        // Fontin ships SmallCaps as its own family rather than an OpenType `smcp`
        // feature — load-bearing, since ImGui does no shaping or feature substitution.
        auto add = [&](const char* data) {
            return io.Fonts->AddFontFromMemoryCompressedBase85TTF(data, size_px);
        };
        f.regular = add(fontin_regular_compressed_data_base85);
        f.bold = add(fontin_bold_compressed_data_base85);
        f.italic = add(fontin_italic_compressed_data_base85);
        f.small_caps = add(fontin_small_caps_compressed_data_base85);
    }
    // Last, so a merge cannot latch onto one of the Fontin faces by accident.
    f.unicode = load_unicode_face(size_px);
    if (!f.unicode) {
        SDL_Log("no system face with non-Latin coverage; names may render as boxes");
        f.unicode = f.regular;
    }
    io.FontDefault = f.regular;
    return f;
}

} // namespace ppc
