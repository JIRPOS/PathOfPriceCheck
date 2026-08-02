#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "config.hpp"
#include "overlay.hpp"
#include "platform/hotkeys.hpp"

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

private:
    void on_hotkey(Action a);                ///< fired from the hotkey thread
    void handle_action(Action a);            ///< handled on the main thread
    bool poe_active() const;                 ///< PoE focused, or our overlay focused
    void set_screen(Screen s);
    void rebind_hotkeys();

    Config config_ = Config::load();
    Overlay overlay_;
    std::unique_ptr<HotkeyListener> hotkeys_;
    Screen screen_ = Screen::Hidden;
    std::string clipboard_;
    bool running_ = true;

    bool capturing_ = false;
    Action capture_which_ = Action::PriceCheck;

    bool dev_active_ = false;                ///< PPC_DEV_OVERLAY: bypass focus gating for local dev
    uint32_t user_event_ = 0;                ///< SDL user-event type carrying an Action
};

} // namespace ppc
