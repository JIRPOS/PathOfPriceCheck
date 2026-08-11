#pragma once

#include <cstdint>
#include <utility>

#include "capture.hpp"
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

    /// Read this window back at the end of the frame being drawn now.
    ///
    /// Asked for **during** a frame and answered at the end of it, rather than taken on the spot:
    /// the caller is a button, and a button is pressed halfway through the drawing of the very
    /// panel that is worth capturing. Anything that resizes or repoints the window has to wait
    /// for the same moment, or the read-back and the draw data disagree about how big it is.
    void request_capture() { want_capture_ = true; }
    /// The pixels the last request produced, moved out. Empty until then, and again after.
    Capture take_capture() { return std::exchange(capture_, {}); }

    /// An RGBA image as a texture ImGui can draw, and its release. Here rather than on the
    /// caller because the GL context is this class's; `IconCache` owns the same pair for the
    /// pictures it downloads. 0 is the failure, and is safe to pass back to `free_texture`.
    uint64_t upload_texture(const Capture& c);
    void free_texture(uint64_t tex);

private:
    void sync_held_mouse();
    void read_back(int w, int h);

    SDL_Window* window_ = nullptr;
    void* gl_ = nullptr; // SDL_GLContext
    bool visible_ = false;
    bool want_capture_ = false;
    Capture capture_;
    Fonts fonts_;
};

} // namespace ppc
