#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "config.hpp"
#include "data/game_data.hpp"
#include "data/updater.hpp"
#include "league_service.hpp"
#include "overlay.hpp"
#include "platform/hotkeys.hpp"

struct SDL_Surface;
struct SDL_Tray;
union SDL_Event;

namespace ppc {

enum class Screen { Hidden, PriceCheck, Settings };

/// Which of the game's two item panels the price check came from. Decides which side
/// the overlay docks against so it never covers the item being priced.
enum class Side { Stash, Inventory };

class App {
public:
    int run();

    // Accessors used by the screen renderers.
    Config& config() { return config_; }
    const Fonts& fonts() const { return overlay_.fonts(); }
    const std::string& clipboard_text() const { return clipboard_; }
    bool copying() const { return copy_pending_; } ///< price-check is awaiting the clipboard
    bool copy_late() const { return copy_late_; } ///< the copy is overdue; still watching for it
    const LeagueService& leagues() const { return leagues_; }
    void refresh_leagues() { leagues_.refresh(true); }
    /// The loaded bundle, or null while none is installed. Renderers must copy the
    /// shared_ptr once per frame — a mid-frame swap would otherwise dangle.
    std::shared_ptr<data::GameData> game_data() const { return data_; }
    data::DataUpdater::Status data_status() const { return updater_.status(); }
    void check_for_data() { updater_.start_check(); }

    void begin_capture(Action which);        ///< next key press rebinds this action
    bool capturing(Action which) const { return capturing_ && capture_which_ == which; }
    void apply_and_save_config();            ///< persist config + re-register hotkeys
    void close_overlay() { set_screen(Screen::Hidden); }
    void quit() { running_ = false; }

private:
    void on_hotkey(Action a);                ///< fired from the hotkey thread
    void handle_event(const SDL_Event& e);   ///< process one SDL event on the main thread
    void handle_action(Action a);            ///< handled on the main thread
    void end_capture();                      ///< stop capturing and re-grab hotkeys
    void poll_pending_copy();                ///< fill the price-check text once the copy lands
    void poll_click_away();                  ///< dismiss price-check on a click outside it
    void update_overlay_placement();         ///< track the game window; move the overlay over it
    void place_overlay();                    ///< size + position the overlay for the current screen
    Side cursor_side() const;                ///< which half of the game window the mouse is in
    void set_screen(Screen s);
    void rebind_hotkeys();
    bool init_tray(SDL_Surface* icon);

    Config config_ = Config::load();
    Overlay overlay_;
    LeagueService leagues_;
    data::DataUpdater updater_;
    std::shared_ptr<data::GameData> data_;
    std::unique_ptr<HotkeyListener> hotkeys_;
    SDL_Tray* tray_ = nullptr;
    Screen screen_ = Screen::Hidden;
    Side side_ = Side::Inventory; ///< side the current price check docked to
    std::string clipboard_;
    bool running_ = true;

    bool capturing_ = false;
    Action capture_which_ = Action::PriceCheck;

    bool copy_pending_ = false;     ///< a simulated copy is in flight, awaiting the clipboard
    bool copy_late_ = false;        ///< past the deadline with no item text yet
    std::string copy_before_;       ///< clipboard contents before the copy, to detect the change
    uint64_t copy_started_ms_ = 0;  ///< when the simulated copy was injected
    bool clipboard_dirty_ = false;  ///< a clipboard-update event arrived; re-read once
    uint64_t last_clipboard_poll_ms_ = 0; ///< throttles the clipboard re-read while waiting
    bool mouse_was_down_ = false;   ///< prior global mouse button state, for click-away edges

    bool dev_mode_ = false;  ///< PPC_DEV_OVERLAY: keep overlay up regardless of focus
    bool had_focus_ = false; ///< overlay has gained focus since it was shown

    // One contiguous block from SDL_RegisterEvents. Kept as distinct types rather than
    // widening Action: handle_action() gates on the game being foreground, which would
    // silently swallow an async result whenever PoE isn't in front.
    uint32_t hotkey_event_ = 0; ///< carries an Action, pushed from the hotkey thread
    uint32_t league_event_ = 0; ///< carries a LeagueService::Result*
    uint32_t data_event_ = 0;   ///< the data updater changed state

    bool game_present_ = false; ///< the game window was found on the last poll
    int game_x_ = 0, game_y_ = 0, game_w_ = 0, game_h_ = 0; ///< last placed-over geometry
    uint64_t last_detect_ms_ = 0;                           ///< throttles the game-window poll
    bool need_redraw_ = true;   ///< repaint requested (event, state change, or reposition)
};

} // namespace ppc
