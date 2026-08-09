#include "overlay.hpp"

#include <iterator>

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
    SDL_GL_SwapWindow(window_);
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
