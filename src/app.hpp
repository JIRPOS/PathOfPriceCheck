#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <optional>

#include "config.hpp"
#include "data/game_data.hpp"
#include "data/updater.hpp"
#include "update/updater.hpp"
#include "item/derive.hpp"
#include "item/plan.hpp"
#include "icon_cache.hpp"
#include "league_service.hpp"
#include "exchange_service.hpp"
#include "ninja_service.hpp"
#include "overlay.hpp"
#include "platform/hotkeys.hpp"
#include "trade_service.hpp"

struct SDL_Surface;
struct SDL_Tray;
union SDL_Event;

namespace ppc {

enum class Screen { Hidden, PriceCheck, Settings, QuickPaste };

/// How long a price check waits for the game to publish its copy before dropping it. Past this
/// the user has moved on, and a panel that opens late is a panel about the wrong item.
inline constexpr uint64_t kCopyTimeoutMs = 2000;

/// How often a pending copy asks the clipboard owner to render (see `clipboard_poke`). Asking is
/// what makes Wine publish at all; asking every frame would be hammering it mid-handover.
inline constexpr uint64_t kPokeIntervalMs = 100;

/// How long a check for a newer bundle or release stays fresh. A re-check rides on the next
/// thing the user does rather than on a timer, so this is a floor on the interval and not the
/// interval itself: a session nobody touches makes no request after the one at startup.
inline constexpr uint64_t kRecheckIntervalMs = 30 * 60 * 1000;

/// Which of the game's two item panels the price check came from. Decides which side
/// the overlay docks against so it never covers the item being priced.
enum class Side { Stash, Inventory };

/// How the price-check overlay window is divided. The window is wider than the panel: the
/// rest is a **gutter** on the side away from the item frame, holding the item being priced at
/// its top and a hovered listing's item below that. The overlay only paints where ImGui draws a
/// window, so whatever the two of them leave over stays transparent.
///
/// It exists because a tooltip had nowhere else to go. ImGui clamps every window to the
/// viewport, and the viewport is the SDL window — so before the gutter, a popup wide enough to
/// read landed on top of the very listings it was meant to be compared against. The item in hand
/// moved into it for a different reason: the panel is a column on a screen that is always wider
/// than it is tall, so height, not width, is what it runs out of.
/// All values are ImGui viewport coordinates.
struct PanelLayout {
    float panel_x = 0; ///< 0 when the gutter is to the right, else the gutter's width
    float panel_w = 0;
    float tip_x = 0;
    float tip_w = 0; ///< 0 when the game window left no room for one
};

/// Which filter row has its range editor open, and where on screen it was opened from.
///
/// `index` addresses `SearchPlan::stats` or `SearchPlan::numerics` according to `kind`, so it
/// only means anything against the plan it was opened on — every place that rebuilds the plan
/// clears this. Held on `App` for the same reason `settings_tab_` is: the screen is a free
/// function redrawn from scratch every frame and has nowhere of its own to keep it.
struct FilterEdit {
    enum class Kind : uint8_t { None, Stat, Numeric };
    Kind kind = Kind::None;
    size_t index = 0;
    /// The row's own extent, in ImGui viewport coordinates: `y` is its bottom, which is where
    /// the editor opens, and `top` is what the editor falls back to when there is no room below
    /// and it has to go above instead. Over the row is the one place it may not go — everything
    /// it would otherwise have to repeat is printed there.
    float y = 0, top = 0;
    /// Set on the frame the row was clicked and consumed by the screen, which is the only
    /// place allowed to call `ImGui::OpenPopup` — the id it opens has to be pushed from
    /// outside the filter table, whose own id would otherwise be part of it.
    bool opening = false;
    bool open() const { return kind != Kind::None; }
};

/// The paste being written in Settings, and the draft it is written into.
///
/// A draft rather than the entry itself: the dialog can be cancelled, and an edit typed
/// straight into `Config::pastes` would already have happened by then. Held on `App` for the
/// same reason `settings_tab_` is — the screen is a free function rebuilt every frame.
struct PasteEdit {
    bool open = false;
    bool adding = false; ///< appended on Done, rather than written back over `index`
    size_t index = 0;
    Paste draft;
};

/// A search result's own item, parsed from the clipboard text the API ships with every
/// listing. Parsed lazily — twenty of these up front is work for rows nobody hovers — and
/// resolved against the same pinned bundle snapshot as the item in hand.
struct ListingItem {
    std::optional<item::Item> item; ///< empty when the listing carried no text, or it did not parse
    item::Derived derived;
};

class App {
public:
    /// `relaunched_after_update` is the `--updated` switch: the process we are replacing may
    /// still be exiting, so the single-instance claim waits for it instead of reporting that
    /// the application is already running.
    int run(bool relaunched_after_update = false);

    // Accessors used by the screen renderers.
    Config& config() { return config_; }
    const Fonts& fonts() const { return overlay_.fonts(); }
    const std::string& clipboard_text() const { return clipboard_; }
    const LeagueService& leagues() const { return leagues_; }
    void refresh_leagues() { leagues_.refresh(true); }
    /// The loaded bundle, or null while none is installed. Renderers must copy the
    /// shared_ptr once per frame — a mid-frame swap would otherwise dangle.
    std::shared_ptr<data::GameData> game_data() const { return data_; }
    data::DataUpdater::Status data_status() const { return updater_.status(); }
    /// Also restarts the freshness clock `refresh_checks()` keeps, so a check asked for by hand
    /// is not followed by one riding on the next hotkey.
    void check_for_data();

    update::Updater::Status update_status() const { return app_updater_.status(); }
    void check_for_update(); ///< as `check_for_data()`, for the application itself
    /// True once the user has waved the notice away. Settings keeps showing the update either
    /// way — this only silences the two surfaces that sit over the game.
    bool update_dismissed() const { return update_dismissed_; }
    void dismiss_update_notice() { update_dismissed_ = true; }
    /// Applies the staged update and starts its replacement, then closes this one. Only ever
    /// from the Settings button: nothing here decides on its own to close over a running game.
    void restart_for_update() {
        if (app_updater_.restart_now()) quit();
    }

    /// The item the current price check is about, or null while there is none. Valid until
    /// the next price check: it points into the bundle snapshot `item_data_` pins.
    /// The vocabulary the item in hand was read with — the pinned bundle's, so it stays the
    /// one the item was parsed against even after the updater swaps a newer bundle in.
    /// English while no bundle is installed, which is what the compiled-in table is for.
    const data::Lexicon& item_lexicon() const {
        return item_data_ ? item_data_->lexicon() : data::Lexicon::english();
    }

    const item::Item* item() const { return item_ ? &*item_ : nullptr; }
    const item::Derived& derived() const { return derived_; }
    item::SearchPlan& plan() { return plan_; }
    /// Re-derive the plan for a different pricing strategy — a rare is sometimes worth more
    /// as a base than as its rolls, and only the user knows which they meant.
    void set_strategy(item::Strategy s);
    /// Say which unique an unidentified one is, from `Item::unique_candidates`. Null puts it
    /// back to undecided, which is what the panel's "change" affordance does. Re-plans and
    /// re-prices exactly as a strategy change does: from here on the item is that unique,
    /// still unidentified.
    void set_unique(const data::BaseType* u);

    // Trade search. The plan on screen is the query: the user ticks filters and presses
    // Search, or opens the same search on the site without spending an API call on it.
    const TradeService& trade() const { return trade_; }
    /// poe.ninja's reference price for the item in hand — the going rate for the kinds of
    /// item a stat query cannot price. Refreshed with the plan, never on a button: it costs
    /// no GGG request and at most one poe.ninja request per category per half hour.
    const NinjaService& ninja() const { return ninja_; }
    /// What the in-game currency exchange did with this item in the last published hour —
    /// which for a stack of currency, a scarab or a fragment is the market it is actually
    /// traded on, and the reason those have no trade search at all.
    const ExchangeService& currency_exchange() const { return currency_exchange_; }
    /// Open the item's poe.ninja page, which is where the variants and the full history are.
    void open_reference_page();
    IconCache& icons() { return icons_; }
    void start_search();
    void open_search_in_browser();
    /// One more page of the search already run. See `TradeService::load_more`.
    void load_more();
    /// The listing's own item, parsed on first ask and cached until the results change.
    /// Never null for an index in range; its `item` is what says whether it parsed.
    const ListingItem* listing_item(size_t i);
    /// Where the panel sits inside the overlay window, and where the gutter beside it is.
    const PanelLayout& layout() const { return layout_; }
    /// How far down the gutter the item card reached this frame — opaque UI of ours over the
    /// game, so `poll_click_away` has to spare it. 0 when it was drawn in the panel instead.
    void set_card_height(float h) { card_h_ = h; }
    /// Which Settings tab is open. Held here rather than in the screen because the screen is a
    /// free function redrawn from scratch every frame.
    int settings_tab() const { return settings_tab_; }
    void set_settings_tab(int i) { settings_tab_ = i; }
    /// Which filter row has its range editor open. Mutable because the screen both reads it
    /// and consumes `opening` in the same frame.
    FilterEdit& filter_edit() { return filter_edit_; }
    /// Open the editor on a row spanning `top`..`bottom` in viewport coordinates.
    void edit_filter(FilterEdit::Kind kind, size_t index, float top, float bottom);
    void close_filter_edit() { filter_edit_ = {}; }
    /// Whether the filters the strategy left out are expanded. **Collapsed for every price
    /// check**, deliberately: they are the modifiers the strategy decided the item is not
    /// bought for, and a list that opens with six map affixes on it buries the two rows that
    /// price the map. Reset with the plan, like the editor above.
    bool hidden_filters_shown() const { return hidden_filters_shown_; }
    void show_hidden_filters(bool on) { hidden_filters_shown_ = on; }
    /// False while there is nothing to search — no bundle, a strategy with no query behind it
    /// (currency), or a gem the bundle could not name.
    bool can_search() const;
    /// Whether this item trades on the in-game currency exchange **at all**, which is what
    /// decides that there is no trade search for it. A property of the item, so it is answered
    /// from the bundle rather than from whichever hour the live feed happens to hold: keying it
    /// off a market in the last hour gave a Weeping Essence of Greed a Search button that could
    /// only ever come back empty, on every hour nobody happened to trade one in.
    bool trades_on_exchange() const;

    // The paste list. `pick_paste` is the whole of what the popup does: the text goes on the
    // clipboard and the popup closes, handing the game back the focus it took for the number
    // keys. **Nothing presses Ctrl+V** — see the rule in docs/quickpaste.md.
    void pick_paste(size_t index);
    /// Open Settings on the tab where pastes are managed, which is the popup's only other
    /// affordance and the answer to an empty list.
    void open_paste_settings();
    /// Which paste Settings has open in its editor. Mutable: the dialog reads and writes it in
    /// the same frame.
    PasteEdit& paste_edit() { return paste_edit_; }

    /// Copy-path diagnostic log (util/debug_log). Toggling it takes effect immediately —
    /// waiting for Save would mean the run that reproduced the bug went unrecorded — but it
    /// still needs a Save to persist.
    void set_debug_log(bool on);
    /// Writes the current check's id to the clipboard, for pasting into a bug report.
    void copy_check_id();

    void begin_capture(Action which);        ///< next key press rebinds this action
    bool capturing(Action which) const { return capturing_ && capture_which_ == which; }
    void apply_and_save_config();            ///< persist config + re-register hotkeys
    void close_overlay() { set_screen(Screen::Hidden); }
    void quit() { running_ = false; }

private:
    void on_hotkey(Action a);                ///< fired from the hotkey thread
    void handle_event(const SDL_Event& e);   ///< process one SDL event on the main thread
    void handle_action(Action a);            ///< handled on the main thread
    void refresh_checks();                   ///< re-check for a bundle and a release, if stale
    void end_capture();                      ///< stop capturing and re-grab hotkeys
    void poll_pending_copy();                ///< show the item once the clipboard is written
    void abandon_copy();                     ///< drop the copy in flight, showing nothing
    void nudge_clipboard_handover(uint64_t elapsed); ///< make the game let go of the copy
    void restore_game_activation();                  ///< undo the nudge, whatever it achieved
    void accept_clipboard(std::string text); ///< take item text: parse, resolve, plan
    void rebuild_plan();                     ///< re-resolve and re-plan the item in hand
    void price_reference();                  ///< ask poe.ninja about the item as planned
    void poll_click_away();                  ///< dismiss price-check on a click outside it
    void update_overlay_placement();         ///< track the game window; move the overlay over it
    void reclaim_keyboard();                 ///< re-take the focus a screen that types needs
    void take_keyboard();                    ///< claim the input focus, and record that we did
    void give_keyboard_back();               ///< return focus we took, if the game still wants it
    void place_overlay();                    ///< size + position the overlay for the current screen
    Side cursor_side() const;                ///< which half of the game window the mouse is in
    void set_screen(Screen s);
    void log_state(const char* when);        ///< dump everything the copy path depends on
    void log_session_start();                ///< the run's configuration, once
    void rebind_hotkeys();
    bool init_tray(SDL_Surface* icon);

    Config config_ = Config::load();
    Overlay overlay_;
    LeagueService leagues_;
    TradeService trade_;
    NinjaService ninja_;
    ExchangeService currency_exchange_;
    IconCache icons_;
    data::DataUpdater updater_;
    update::Updater app_updater_;
    /// Waved away for this session only. Deliberately not persisted: the staged update is
    /// still there next launch, and so is the notice.
    bool update_dismissed_ = false;
    std::shared_ptr<data::GameData> data_;
    /// The snapshot the item in hand was resolved against. Held separately from `data_`
    /// because the updater can swap that out mid-price-check, and the item points into the
    /// bundle it was resolved with.
    std::shared_ptr<data::GameData> item_data_;
    std::optional<item::Item> item_;
    item::Derived derived_;
    item::SearchPlan plan_;
    /// Parallel to `trade_.results().listings`, filled on hover. Dropped whenever a trade
    /// result lands, which is cheaper than reasoning about whether it invalidated anything.
    std::vector<std::optional<ListingItem>> listing_items_;
    std::optional<item::Strategy> strategy_override_; ///< the user's choice, until the next item
    /// Which unique the user said an unidentified one is, by **name** rather than by the record
    /// — `rebuild_plan` re-resolves against whatever bundle is current, and a pointer into the
    /// one it was chosen from would not survive a data update. Cleared with the item.
    std::string unique_choice_;
    std::unique_ptr<HotkeyListener> hotkeys_;
    SDL_Tray* tray_ = nullptr;
    Screen screen_ = Screen::Hidden;
    Side side_ = Side::Inventory; ///< side the current price check docked to
    PanelLayout layout_;          ///< set by place_overlay, read by the price-check renderer
    float card_h_ = 0;            ///< height the item card drew at, in the gutter (see set_card_height)
    int settings_tab_ = 0;        ///< which Settings tab is open
    FilterEdit filter_edit_;      ///< which filter row has its range editor open
    PasteEdit paste_edit_;        ///< the paste Settings has open in its editor
    /// Where the cursor was when the paste hotkey fired. Sampled there rather than read at
    /// placement time for the same reason `side_` is: by then the hand has moved.
    int paste_x_ = 0, paste_y_ = 0;
    bool hidden_filters_shown_ = false; ///< the filters the strategy left out are expanded
    std::string clipboard_;
    bool running_ = true;

    bool capturing_ = false;
    Action capture_which_ = Action::PriceCheck;

    bool copy_pending_ = false;    ///< a simulated copy is in flight, awaiting the clipboard
    uint64_t copy_stamp_ = 0;      ///< clipboard_stamp() as it was before the copy was injected
    uint64_t copy_started_ms_ = 0; ///< when the simulated copy was injected
    uint64_t copy_poked_ms_ = 0;   ///< last clipboard_poke(), which is throttled
    bool copy_nudged_ = false;     ///< the handover nudge has fired for this check; once is enough
    uint64_t copy_nudge_ms_ = 0;   ///< when it fired, so the game is not left deactivated
    bool copy_deactivated_ = false;///< the game is currently off the WM's active window, by us
    bool mouse_was_down_ = false;  ///< prior global mouse button state, for click-away edges

    /// We called `overlay_take_keyboard_focus` and have not handed it back. Our own record of
    /// it, because SDL's `has_focus()` lags the call by a round trip and the moment it matters
    /// most — a paste popup dismissed the instant it opened — is inside that gap.
    bool took_keyboard_ = false;
    bool dev_mode_ = false;  ///< PPC_DEV_OVERLAY: keep overlay up regardless of focus
    bool had_focus_ = false; ///< overlay has gained focus since it was shown

    // One contiguous block from SDL_RegisterEvents. Kept as distinct types rather than
    // widening Action: handle_action() gates on the game being foreground, which would
    // silently swallow an async result whenever PoE isn't in front.
    uint32_t hotkey_event_ = 0; ///< carries an Action, pushed from the hotkey thread
    uint32_t league_event_ = 0; ///< carries a LeagueService::Result*
    uint32_t data_event_ = 0;   ///< the data updater changed state
    uint32_t update_event_ = 0; ///< the application updater changed state
    uint32_t trade_event_ = 0;  ///< carries a TradeService::Result*
    uint32_t ninja_event_ = 0;  ///< carries a NinjaService::Result*
    uint32_t exchange_event_ = 0; ///< carries an ExchangeService::Result*

    bool game_present_ = false; ///< the game window was found on the last poll
    int game_x_ = 0, game_y_ = 0, game_w_ = 0, game_h_ = 0; ///< last placed-over geometry
    uint64_t last_detect_ms_ = 0;                           ///< throttles the game-window poll
    /// When each updater last started a check, for `refresh_checks()`. Two clocks and not one:
    /// **Check now** on either Settings row must not postpone the other's re-check.
    uint64_t data_checked_ms_ = 0, update_checked_ms_ = 0;
    int game_state_logged_ = -1; ///< last (present, focused) pair written to the debug log
    bool need_redraw_ = true;   ///< repaint requested (event, state change, or reposition)
};

} // namespace ppc
