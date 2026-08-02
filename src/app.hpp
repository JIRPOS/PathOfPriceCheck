#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "config.hpp"
#include "overlay.hpp"
#include "platform/hotkeys.hpp"

struct SDL_Tray;

namespace ppc {

enum class Screen { Hidden, PriceCheck, Settings };

class App {
public:
    int run();

    // Accessors used by the screen renderers.
    Config& config() { return config_; }
    const std::string& clipboard_text() const { return clipboard_; }

    void begin_capture(Action which);        ///< next key press rebinds this action
    bool capturing(Action which) const { return capturing_ && capture_which_ == which; }
    void apply_and_save_config();            ///< persist config + re-register hotkeys
    void close_overlay() { set_screen(Screen::Hidden); }
    void quit() { running_ = false; }

private:
    void on_hotkey(Action a);                ///< fired from the hotkey thread
    void handle_action(Action a);            ///< handled on the main thread
    std::string grab_item();                 ///< auto-copy from the game, return clipboard
    void set_screen(Screen s);
    void rebind_hotkeys();
    bool init_tray();

    Config config_ = Config::load();
    Overlay overlay_;
    std::unique_ptr<HotkeyListener> hotkeys_;
    SDL_Tray* tray_ = nullptr;
    Screen screen_ = Screen::Hidden;
    std::string clipboard_;
    bool running_ = true;

    bool capturing_ = false;
    Action capture_which_ = Action::PriceCheck;

    bool dev_mode_ = false;   ///< PPC_DEV_OVERLAY: keep overlay up regardless of focus
    bool had_focus_ = false;  ///< overlay has gained focus since it was shown
    uint32_t user_event_ = 0; ///< SDL user-event type carrying an Action
};

} // namespace ppc
