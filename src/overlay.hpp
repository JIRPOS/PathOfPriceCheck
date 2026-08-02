#pragma once

#include "fonts.hpp"

struct SDL_Window;
union SDL_Event;

namespace ppc {

/// SDL3 + OpenGL + Dear ImGui overlay window. Created hidden; shown only while a
/// screen is active and Path of Exile is focused. Transparency / click-through
/// is a later step — for now it's a borderless always-on-top panel.
class Overlay {
public:
    bool init(const char* title);
    void shutdown();

    void process_event(const SDL_Event& e);
    void begin_frame();
    void end_frame();

    void set_visible(bool v);
    bool visible() const { return visible_; }
    bool has_focus() const;
    SDL_Window* window() const { return window_; }
    const Fonts& fonts() const { return fonts_; }

private:
    SDL_Window* window_ = nullptr;
    void* gl_ = nullptr; // SDL_GLContext
    bool visible_ = false;
    Fonts fonts_;
};

} // namespace ppc
