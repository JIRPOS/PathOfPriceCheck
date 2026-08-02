#include "overlay.hpp"

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

void Overlay::begin_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
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
