#include "app.hpp"

#include <cstdlib>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "platform/foreground.hpp"
#include "platform/input_sim.hpp"
#include "platform/platform.hpp"
#include "screens/pricecheck_screen.hpp"
#include "screens/settings_screen.hpp"

namespace ppc {
namespace {

std::string read_clipboard() {
    std::string s;
    if (char* txt = SDL_GetClipboardText()) {
        s = txt;
        SDL_free(txt);
    }
    return s;
}

void SDLCALL tray_exit_cb(void* userdata, SDL_TrayEntry*) {
    static_cast<App*>(userdata)->quit();
}

} // namespace

int App::run() {
    platform_init();
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    user_event_ = SDL_RegisterEvents(1);
    if (!overlay_.init("PathOfPriceCheck Overlay")) {
        SDL_Log("overlay init failed");
        SDL_Quit();
        return 1;
    }
    init_tray();

    hotkeys_ = HotkeyListener::create([this](Action a) { on_hotkey(a); });
    rebind_hotkeys();

    dev_mode_ = std::getenv("PPC_DEV_OVERLAY") != nullptr;
    if (dev_mode_) set_screen(Screen::Settings); // local UI dev

    SDL_Event e;
    while (running_) {
        if (SDL_WaitEventTimeout(&e, 100)) {
            do {
                if (e.type == SDL_EVENT_QUIT) {
                    running_ = false;
                } else if (e.type == user_event_) {
                    handle_action(static_cast<Action>(e.user.code));
                } else if (e.type == SDL_EVENT_KEY_DOWN && capturing_) {
                    if (e.key.key == SDLK_ESCAPE) {
                        capturing_ = false; // cancel capture, don't bind Escape
                    } else if (std::string name = key_name_from_sdl(e.key.key); !name.empty()) {
                        SDL_Keymod km = SDL_GetModState();
                        Mod m = Mod::None;
                        if (km & SDL_KMOD_CTRL) m = m | Mod::Ctrl;
                        if (km & SDL_KMOD_SHIFT) m = m | Mod::Shift;
                        if (km & SDL_KMOD_ALT) m = m | Mod::Alt;
                        if (km & SDL_KMOD_GUI) m = m | Mod::Super;
                        (capture_which_ == Action::PriceCheck ? config_.price_check : config_.settings) =
                            Hotkey{m, name};
                        capturing_ = false;
                    }
                } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
                    set_screen(Screen::Hidden);
                } else if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                    had_focus_ = true;
                } else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                    // Dismiss when the user clicks away, but only once it has actually
                    // held focus (avoids closing before the WM focuses the new window).
                    if (!dev_mode_ && screen_ != Screen::Hidden && had_focus_) set_screen(Screen::Hidden);
                }
                overlay_.process_event(e);
            } while (SDL_PollEvent(&e));
        }
        SDL_UpdateTrays();

        if (screen_ != Screen::Hidden) {
            overlay_.begin_frame();
            if (screen_ == Screen::Settings)
                draw_settings_screen(*this);
            else
                draw_pricecheck_screen(*this);
            overlay_.end_frame();
        }
    }

    if (tray_) SDL_DestroyTray(tray_);
    hotkeys_.reset();
    overlay_.shutdown();
    SDL_Quit();
    return 0;
}

bool App::init_tray() {
    SDL_Surface* icon = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
    if (icon) {
        SDL_FillSurfaceRect(icon, nullptr, SDL_MapSurfaceRGB(icon, 32, 32, 38));
        SDL_Rect inner{5, 5, 22, 22};
        SDL_FillSurfaceRect(icon, &inner, SDL_MapSurfaceRGB(icon, 201, 158, 74)); // PoE-ish gold
    }
    tray_ = SDL_CreateTray(icon, "PathOfPriceCheck");
    if (icon) SDL_DestroySurface(icon);
    if (!tray_) {
        SDL_Log("tray unavailable (no system tray host?)");
        return false;
    }
    SDL_TrayMenu* menu = SDL_CreateTrayMenu(tray_);
    SDL_TrayEntry* exit = SDL_InsertTrayEntryAt(menu, -1, "Exit", SDL_TRAYENTRY_BUTTON);
    SDL_SetTrayEntryCallback(exit, tray_exit_cb, this);
    return true;
}

void App::on_hotkey(Action a) {
    SDL_Event ev{};
    ev.type = user_event_;
    ev.user.code = static_cast<int32_t>(a);
    SDL_PushEvent(&ev);
}

std::string App::grab_item() {
    // Only auto-copy while the game is focused; otherwise use whatever's already copied.
    if (!foreground_title_contains(config_.poe_window_title)) return read_clipboard();
    std::string before = read_clipboard();
    simulate_copy();
    for (int i = 0; i < 30; ++i) { // wait up to ~450ms for the clipboard to change
        SDL_Delay(15);
        std::string now = read_clipboard();
        if (!now.empty() && now != before) return now;
    }
    return read_clipboard();
}

void App::handle_action(Action a) {
    if (a == Action::PriceCheck) {
        clipboard_ = grab_item();
        set_screen(Screen::PriceCheck);
    } else {
        set_screen(screen_ == Screen::Settings ? Screen::Hidden : Screen::Settings);
    }
}

void App::set_screen(Screen s) {
    screen_ = s;
    overlay_.set_visible(s != Screen::Hidden);
    if (s != Screen::Hidden) {
        had_focus_ = false; // wait for the freshly-shown window to gain focus
        SDL_RaiseWindow(overlay_.window());
    }
}

void App::begin_capture(Action which) {
    capturing_ = true;
    capture_which_ = which;
}

void App::apply_and_save_config() {
    config_.save();
    rebind_hotkeys();
}

void App::rebind_hotkeys() {
    hotkeys_->rebind({{config_.price_check, Action::PriceCheck}, {config_.settings, Action::ToggleSettings}});
}

} // namespace ppc
