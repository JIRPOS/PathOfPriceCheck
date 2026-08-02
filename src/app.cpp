#include "app.hpp"

#include <cstdlib>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "platform/foreground.hpp"
#include "platform/platform.hpp"
#include "screens/pricecheck_screen.hpp"
#include "screens/settings_screen.hpp"

namespace ppc {

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

    hotkeys_ = HotkeyListener::create([this](Action a) { on_hotkey(a); });
    rebind_hotkeys();

    if (std::getenv("PPC_DEV_OVERLAY")) { // local dev: show settings without PoE running
        dev_active_ = true;
        set_screen(Screen::Settings);
    }

    SDL_Event e;
    while (running_) {
        if (SDL_WaitEventTimeout(&e, 100)) {
            do {
                if (e.type == SDL_EVENT_QUIT) {
                    running_ = false;
                } else if (e.type == user_event_) {
                    handle_action(static_cast<Action>(e.user.code));
                } else if (e.type == SDL_EVENT_KEY_DOWN && capturing_) {
                    std::string name = key_name_from_sdl(e.key.key);
                    if (!name.empty()) {
                        SDL_Keymod km = SDL_GetModState();
                        Mod m = Mod::None;
                        if (km & SDL_KMOD_CTRL) m = m | Mod::Ctrl;
                        if (km & SDL_KMOD_SHIFT) m = m | Mod::Shift;
                        if (km & SDL_KMOD_ALT) m = m | Mod::Alt;
                        if (km & SDL_KMOD_GUI) m = m | Mod::Super;
                        Hotkey hk{m, name};
                        if (capture_which_ == Action::PriceCheck)
                            config_.price_check = hk;
                        else
                            config_.settings = hk;
                        capturing_ = false;
                    }
                }
                overlay_.process_event(e);
            } while (SDL_PollEvent(&e));
        }

        bool active = poe_active();
        if (!active && screen_ != Screen::Hidden) set_screen(Screen::Hidden);

        if (active && screen_ != Screen::Hidden) {
            overlay_.begin_frame();
            if (screen_ == Screen::Settings)
                draw_settings_screen(*this);
            else if (screen_ == Screen::PriceCheck)
                draw_pricecheck_screen(*this);
            overlay_.end_frame();
        }
    }

    hotkeys_.reset();
    overlay_.shutdown();
    SDL_Quit();
    return 0;
}

void App::on_hotkey(Action a) {
    SDL_Event ev{};
    ev.type = user_event_;
    ev.user.code = static_cast<int32_t>(a);
    SDL_PushEvent(&ev);
}

void App::handle_action(Action a) {
    // Only react while PoE is the focused window (or we're already interacting).
    if (!dev_active_ && !foreground_title_contains(config_.poe_window_title) && !overlay_.has_focus())
        return;

    if (a == Action::PriceCheck) {
        if (char* txt = SDL_GetClipboardText()) {
            clipboard_ = txt;
            SDL_free(txt);
        }
        set_screen(Screen::PriceCheck);
    } else {
        set_screen(screen_ == Screen::Settings ? Screen::Hidden : Screen::Settings);
    }
}

bool App::poe_active() const {
    return dev_active_ || foreground_title_contains(config_.poe_window_title) || overlay_.has_focus();
}

void App::set_screen(Screen s) {
    screen_ = s;
    overlay_.set_visible(s != Screen::Hidden);
    if (s != Screen::Hidden) SDL_RaiseWindow(overlay_.window());
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
