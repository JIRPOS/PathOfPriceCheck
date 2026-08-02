#include "fonts.hpp"

#include <string>

#include <SDL3/SDL.h>
#include <imgui.h>

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
    io.FontDefault = f.regular;
    return f;
}

} // namespace ppc
