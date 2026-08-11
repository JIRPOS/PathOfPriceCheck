#include "overlay.hpp"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

namespace ppc {

bool Overlay::init(const char* title) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8); // transparent framebuffer

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP |
                            SDL_WINDOW_UTILITY | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                            SDL_WINDOW_TRANSPARENT;
    window_ = SDL_CreateWindow(title, 520, 680, flags);
    if (!window_) return false;

    SDL_GLContext ctx = SDL_GL_CreateContext(window_);
    if (!ctx) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }
    gl_ = ctx;
    SDL_GL_MakeCurrent(window_, ctx);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    fonts_ = load_fonts(18.0f);
    ImGui_ImplSDL3_InitForOpenGL(window_, ctx);
    ImGui_ImplOpenGL3_Init("#version 150");
    return true;
}

void Overlay::shutdown() {
    if (!window_) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(static_cast<SDL_GLContext>(gl_));
    SDL_DestroyWindow(window_);
    window_ = nullptr;
}

void Overlay::process_event(const SDL_Event& e) { ImGui_ImplSDL3_ProcessEvent(&e); }

/// While a mouse button is held, the **physical** button and pointer are the authority.
///
/// A drag that leaves this window is ordinary here: the overlay is never wider than it needs to be
/// (there is no click-through yet), and the range slider is deliberately draggable past its own
/// ends. What is not guaranteed is that the release comes back to us. The overlay is an
/// override-redirect window, and SDL's X11 capture — the thing that is supposed to keep the events
/// coming while the pointer is elsewhere — does nothing at all when XInput2 owns the pointer
/// (`X11_CaptureMouse` returns early unless the window also holds a grab). The button-up is then
/// delivered to whatever is under the cursor, ImGui never hears it, and the widget stays grabbed:
/// the reported symptom was a slider knob that kept following the mouse after the button was long
/// since released.
///
/// So: read the global state each frame that a button is down, feed the position in as window
/// coordinates — which is also what keeps a drag tracking while the cursor is outside — and
/// release any button the OS says is up. Costs an `SDL_GetGlobalMouseState` only during drags,
/// and `poll_click_away` already makes that call unconditionally.
void Overlay::sync_held_mouse() {
    ImGuiIO& io = ImGui::GetIO();
    // `io.MouseDown` is last frame's state: this frame's events are queued and applied by
    // NewFrame below, which is exactly the ordering wanted — a press that has only just arrived
    // was made inside the window and needs no reconciling.
    const bool held = io.MouseDown[ImGuiMouseButton_Left] || io.MouseDown[ImGuiMouseButton_Right] ||
                      io.MouseDown[ImGuiMouseButton_Middle];
    if (!held) return;

    float gx = 0, gy = 0;
    const SDL_MouseButtonFlags down = SDL_GetGlobalMouseState(&gx, &gy);
    int wx = 0, wy = 0;
    SDL_GetWindowPosition(window_, &wx, &wy);
    io.AddMousePosEvent(gx - float(wx), gy - float(wy));

    static constexpr SDL_MouseButtonFlags kMask[]{SDL_BUTTON_LMASK, SDL_BUTTON_RMASK,
                                                  SDL_BUTTON_MMASK};
    for (int b = 0; b < int(std::size(kMask)); ++b)
        if (io.MouseDown[b] && !(down & kMask[b])) io.AddMouseButtonEvent(b, false);
}

void Overlay::begin_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    sync_held_mouse(); // after the backend has queued its events, before NewFrame applies them
    ImGui::NewFrame();
}

void Overlay::end_frame() {
    ImGui::Render();
    int w, h;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f); // transparent; ImGui paints the opaque panels
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    // Before the swap, not after: the back buffer is what was just drawn, and what a swap
    // leaves in it is undefined.
    if (want_capture_) {
        want_capture_ = false;
        read_back(w, h);
    }
    SDL_GL_SwapWindow(window_);
}

/// The drawn frame, off the back buffer and turned the right way up.
///
/// **Un-premultiplied on the way out.** ImGui blends `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` onto
/// a framebuffer cleared to transparent black, so what accumulates is colour already multiplied
/// by its own coverage — correct for a compositor, and a shade too dark for anything that reads
/// the file as ordinary straight-alpha RGBA, which is every PNG viewer and Discord. Dividing it
/// back out is what makes the attachment look like the panel did.
void Overlay::read_back(int w, int h) {
    if (w <= 0 || h <= 0) return;
    Capture c;
    c.w = w;
    c.h = h;
    c.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, c.rgba.data());

    // GL's origin is the bottom-left and every consumer of this expects the top-left, so the
    // rows come back in the wrong order and are swapped in place.
    const size_t stride = static_cast<size_t>(w) * 4;
    std::vector<uint8_t> line(stride);
    for (int y = 0; y < h / 2; ++y) {
        uint8_t* top = c.rgba.data() + static_cast<size_t>(y) * stride;
        uint8_t* bottom = c.rgba.data() + static_cast<size_t>(h - 1 - y) * stride;
        std::copy_n(top, stride, line.data());
        std::copy_n(bottom, stride, top);
        std::copy_n(line.data(), stride, bottom);
    }
    for (size_t i = 0; i + 3 < c.rgba.size(); i += 4) {
        const unsigned a = c.rgba[i + 3];
        if (a == 0 || a == 255) continue;
        for (int k = 0; k < 3; ++k)
            c.rgba[i + k] = static_cast<uint8_t>(std::min(255u, c.rgba[i + k] * 255u / a));
    }
    capture_ = std::move(c);
}

uint64_t Overlay::upload_texture(const Capture& c) {
    if (c.empty()) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, c.w, c.h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 c.rgba.data());
    return tex;
}

void Overlay::free_texture(uint64_t tex) {
    if (!tex) return;
    const auto id = static_cast<GLuint>(tex);
    glDeleteTextures(1, &id);
}

void Overlay::set_visible(bool v) {
    if (v)
        SDL_ShowWindow(window_);
    else
        SDL_HideWindow(window_);
    visible_ = v;
}

bool Overlay::has_focus() const {
    return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS);
}

} // namespace ppc
