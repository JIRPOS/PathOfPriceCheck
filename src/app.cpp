#include "app.hpp"

#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>

#include "data/install.hpp"
#include "icon.hpp"
#include "item/resolve.hpp"
#include "net/http.hpp"
#include "paths.hpp"
#include "platform/clipboard.hpp"
#include "platform/foreground.hpp"
#include "platform/input_sim.hpp"
#include "platform/overlay_native.hpp"
#include "platform/platform.hpp"
#include "screens/pricecheck_screen.hpp"
#include "screens/settings_screen.hpp"

namespace ppc {
namespace {

/// PPC_DEBUG_COPY=1 traces the whole hotkey → copy → clipboard timeline to stderr.
bool trace_copy() {
    static bool on = std::getenv("PPC_DEBUG_COPY") != nullptr;
    return on;
}

/// Cheap sniff for PoE clipboard text. Guards against latching whatever the user last
/// copied elsewhere when the game's own copy is slow or never arrives. Only the head is
/// examined: the marker is in the first two lines or the text is not an item.
bool looks_like_item(const std::string& s) {
    return item::looks_like_item(std::string_view(s.data(), std::min<size_t>(s.size(), 256)));
}

std::string read_clipboard() {
    uint64_t t0 = SDL_GetTicks();
    // A live owner answers in well under a millisecond; the budget only bounds how long a
    // dead one can stall the frame loop, and we poll again shortly anyway.
    std::string s = clipboard_text(60);
    if (trace_copy())
        SDL_Log("[copy]   read clipboard: %zu bytes in %llums | %.60s", s.size(),
                (unsigned long long)(SDL_GetTicks() - t0), s.c_str());
    return s;
}

void SDLCALL tray_exit_cb(void* userdata, SDL_TrayEntry*) {
    static_cast<App*>(userdata)->quit();
}

// Settings is a free-floating dialog, centered over the game; only price-check docks.
constexpr int kSettingsW = 520, kSettingsH = 680;

// SPIKE: a small always-on marker so we can eyeball whether the transparent,
// click-through overlay actually floats over the (fullscreen) game. Not final UI.
void draw_idle_marker() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 84.0f, 8.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("##ppc_idle", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(ImVec4(0.79f, 0.62f, 0.29f, 1.0f), "\xe2\x97\x8f PPC");
    ImGui::End();
}

} // namespace

int App::run() {
    platform_init();
    SDL_SetMainReady();
#ifndef _WIN32
    // v1 targets Linux/X11; under Wayland this runs via XWayland, which shares a
    // (non focus-gated) X11 clipboard with the game and matches our X11 platform
    // seams. Overridable via the SDL_VIDEO_DRIVER env var. Windows has no x11
    // backend at all, so the hint there makes SDL_Init fail outright.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
#endif
    SDL_SetHint(SDL_HINT_APP_ID, "PathOfPriceCheck"); // stable WM_CLASS
    // Do NOT let showing/raising steal focus from the game: the overlay is override-
    // redirect and stacks above via the compositor, and we only ever want keyboard
    // focus for Settings (claimed explicitly). Stealing focus would break the copy.
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    // SDL hands back a contiguous range, so the offsets are guaranteed.
    const uint32_t event_base = SDL_RegisterEvents(3);
    if (!event_base) {
        SDL_Log("SDL_RegisterEvents failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    hotkey_event_ = event_base;
    league_event_ = event_base + 1;
    data_event_ = event_base + 2;

    if (!overlay_.init("PathOfPriceCheck Overlay")) {
        SDL_Log("overlay init failed");
        SDL_Quit();
        return 1;
    }
    SDL_Surface* icon = load_app_icon();
    if (icon) SDL_SetWindowIcon(overlay_.window(), icon);
    init_tray(icon);
    if (icon) SDL_DestroySurface(icon);
    // Every number this program prints is game data, not prose, and the game writes '.': a
    // cs_CZ LC_NUMERIC renders "1.79 attacks per second" as "1,79". This has to come last —
    // both SDL's X11 backend (XIM) and the tray's GTK call setlocale(LC_ALL, "") during init
    // and undo an earlier attempt. Parsing never consults the locale (from_chars).
    std::setlocale(LC_NUMERIC, "C");

    net::init();
    leagues_.init(league_event_);
    leagues_.load_cache(); // file read only; the network is touched when Settings opens

    // Reclaim superseded bundles and map the installed one before anything else can hold a
    // mapping — on Windows a mapped directory cannot be removed.
    updater_.init(cache_dir() / "data", data_event_);
    data_ = updater_.load_installed();
    updater_.start_check(); // background; the panel degrades gracefully until it lands

    hotkeys_ = HotkeyListener::create([this](Action a) { on_hotkey(a); });
    rebind_hotkeys();

    // Transparent, always-on-top, click-through overlay. It stays hidden until the game
    // is detected, then maps once over it (see update_overlay_placement) — no per-frame
    // redraw while idle. It's override-redirect (unmanaged) so the compositor stacks it
    // over exclusive-fullscreen; the WM then won't focus it, so Settings claims keyboard
    // focus explicitly (see set_screen). PPC_MANAGED opts out for debugging.
    if (!std::getenv("PPC_MANAGED")) overlay_set_unmanaged(overlay_.window(), true);

    dev_mode_ = std::getenv("PPC_DEV_OVERLAY") != nullptr;
    if (dev_mode_) { // local UI dev, no game needed
        overlay_.set_visible(true);
        // PPC_DEV_ITEM=<file> opens the price-check panel on a captured clipboard instead,
        // which is the only way to iterate on it without the game running.
        if (const char* path = std::getenv("PPC_DEV_ITEM")) {
            std::ifstream in(path, std::ios::binary);
            if (in) {
                std::ostringstream ss;
                ss << in.rdbuf();
                accept_clipboard(ss.str());
                set_screen(Screen::PriceCheck);
            } else {
                SDL_Log("PPC_DEV_ITEM: cannot read %s", path);
                set_screen(Screen::Settings);
            }
        } else {
            set_screen(Screen::Settings);
        }
    }

    SDL_Event e;
    while (running_) {
        // Idle: sleep and just poll for the game a few times a second. Active (a panel
        // is open): wake ~60x/s so ImGui stays responsive. Either way we only repaint
        // when something actually changed — no busy redraw behind the running game.
        bool active = screen_ != Screen::Hidden;
        if (SDL_WaitEventTimeout(&e, active ? 16 : 250)) {
            do {
                handle_event(e);
            } while (SDL_PollEvent(&e));
        }
        SDL_UpdateTrays();
        if (!dev_mode_) update_overlay_placement();
        poll_pending_copy();
        poll_click_away();

        // Repaint only when needed: continuously while a panel is open (for hover/caret),
        // otherwise just once after a change. A static idle frame persists in the front
        // buffer, so we don't redraw it behind the game.
        active = screen_ != Screen::Hidden;
        if (overlay_.visible() && (active || need_redraw_)) {
            overlay_.begin_frame();
            if (screen_ == Screen::Settings)
                draw_settings_screen(*this);
            else if (screen_ == Screen::PriceCheck)
                draw_pricecheck_screen(*this);
            else
                draw_idle_marker();
            overlay_.end_frame();
            need_redraw_ = false;
        }
    }

    if (tray_) SDL_DestroyTray(tray_);
    hotkeys_.reset();
    leagues_.shutdown(); // joins + drains its events; must precede SDL_Quit
    updater_.shutdown();
    net::shutdown();
    overlay_.shutdown();
    SDL_Quit();
    return 0;
}

bool App::init_tray(SDL_Surface* icon) {
    tray_ = SDL_CreateTray(icon, "PathOfPriceCheck");
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
    ev.type = hotkey_event_;
    ev.user.code = static_cast<int32_t>(a);
    SDL_PushEvent(&ev);
}

void App::handle_event(const SDL_Event& e) {
    if (e.type == SDL_EVENT_QUIT) {
        running_ = false;
    } else if (e.type == hotkey_event_) {
        handle_action(static_cast<Action>(e.user.code));
    } else if (e.type == league_event_) {
        leagues_.on_done(e);
    } else if (e.type == data_event_) {
        if (auto gd = updater_.take_ready_bundle()) {
            data_ = std::move(gd);
            // The first bundle often lands after the first price check of a session; the item
            // on screen was parsed without one and is not priceable until it is re-resolved.
            if (item_ && item_data_ != data_) rebuild_plan();
        }
    } else if (e.type == SDL_EVENT_KEY_DOWN && capturing_) {
        if (e.key.key == SDLK_ESCAPE) {
            end_capture(); // cancel capture, don't bind Escape
        } else if (std::string name = key_name_from_sdl(e.key.key); !name.empty()) {
            SDL_Keymod km = SDL_GetModState();
            Mod m = Mod::None;
            if (km & SDL_KMOD_CTRL) m = m | Mod::Ctrl;
            if (km & SDL_KMOD_SHIFT) m = m | Mod::Shift;
            if (km & SDL_KMOD_ALT) m = m | Mod::Alt;
            if (km & SDL_KMOD_GUI) m = m | Mod::Super;
            (capture_which_ == Action::PriceCheck ? config_.price_check : config_.settings) =
                Hotkey{m, name};
            end_capture();
        }
    } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
        set_screen(Screen::Hidden);
    } else if (e.type == SDL_EVENT_CLIPBOARD_UPDATE) {
        clipboard_dirty_ = true;
        if (trace_copy()) SDL_Log("[copy]   clipboard-update event");
    } else if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        had_focus_ = true;
    } else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        // Price-check auto-dismisses when you click back into the game; Settings stays
        // open until closed manually (its hotkey or the X button). had_focus_ avoids
        // closing before the window has actually taken focus.
        if (!dev_mode_ && screen_ == Screen::PriceCheck && had_focus_) set_screen(Screen::Hidden);
    }
    overlay_.process_event(e);
    need_redraw_ = true; // an event may have changed the UI
}

// Reading is a synchronous selection round trip to whoever owns the clipboard — under
// XWayland that is the game's Wine process — so poll on a timer rather than every frame.
void App::poll_pending_copy() {
    if (!copy_pending_) return;
    const uint64_t tick = SDL_GetTicks();
    // SDL_EVENT_CLIPBOARD_UPDATE only fires once the *new* owner answers SDL's TARGETS
    // conversion, so a handover nobody answers is silent. Take it as an accelerator and
    // poll alongside it — a read is a couple of milliseconds when the owner is live.
    if (!clipboard_dirty_ && tick - last_clipboard_poll_ms_ < 100) return;
    const bool by_event = clipboard_dirty_;
    clipboard_dirty_ = false;
    last_clipboard_poll_ms_ = tick;
    const uint64_t elapsed = tick - copy_started_ms_;

    std::string now = read_clipboard();
    // Same text as before the copy is only trustworthy if ownership actually changed —
    // otherwise it's the previous owner still serving, mid-handover.
    if (looks_like_item(now) && (now != copy_before_ || by_event)) {
        if (trace_copy()) SDL_Log("[copy] done after %llums", (unsigned long long)elapsed);
        accept_clipboard(std::move(now));
        copy_pending_ = false;
        copy_late_ = false;
    } else if (elapsed >= 2500 && !copy_late_) {
        // Say so, but keep watching: the game's handover to the X11 selection can land
        // seconds later, and latching a failure here is what made a late item look lost.
        // Never fall back to copy_before_ — that is whatever was copied last, from any
        // application, and showing it reads as a successful but wrong price check.
        SDL_Log("no item text on the clipboard yet %llums after the copy", (unsigned long long)elapsed);
        copy_late_ = true;
    }
    need_redraw_ = true;
}

void App::accept_clipboard(std::string text) {
    clipboard_ = std::move(text);
    strategy_override_.reset(); // a choice belongs to the item it was made for
    rebuild_plan();
}

void App::rebuild_plan() {
    item_ = item::parse_item(clipboard_);
    // Pin the snapshot the item is resolved against: the updater swaps `data_` from its own
    // thread, and every stat the item points at lives in the bundle it was matched in.
    item_data_ = data_;
    plan_ = {};
    derived_ = {};
    if (!item_) return;
    if (item_data_) {
        item::resolve_item(*item_data_, *item_);
        derived_ = item::derive(item_data_.get(), *item_);
        plan_ = item::build_plan(*item_data_, *item_, derived_, strategy_override_);
    } else {
        // No bundle yet: the item still parses and renders, it just cannot be priced.
        derived_ = item::derive(nullptr, *item_);
    }
    need_redraw_ = true;
}

void App::set_strategy(item::Strategy s) {
    strategy_override_ = s;
    if (item_ && item_data_)
        plan_ = item::build_plan(*item_data_, *item_, derived_, strategy_override_);
    need_redraw_ = true;
}

void App::poll_click_away() {
    if (screen_ != Screen::PriceCheck) {
        mouse_was_down_ = false;
        return;
    }
    // KWin doesn't reliably send focus-out to an override-redirect window, so watch the
    // global mouse directly: a press outside the panel dismisses it (X button handles
    // presses inside). Read-only price-check never holds focus, so this is the only path.
    float gx = 0, gy = 0;
    bool down = (SDL_GetGlobalMouseState(&gx, &gy) & SDL_BUTTON_LMASK) != 0;
    bool pressed = down && !mouse_was_down_;
    mouse_was_down_ = down;
    if (!pressed) return;

    int wx = 0, wy = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(overlay_.window(), &wx, &wy);
    SDL_GetWindowSize(overlay_.window(), &ww, &wh);
    if (gx < wx || gy < wy || gx >= wx + ww || gy >= wy + wh) set_screen(Screen::Hidden);
}

void App::handle_action(Action a) {
    // The hotkeys are grabbed system-wide, so they fire while the user is in a browser or a
    // terminal too. Gate every action on the game actually being in front rather than firing
    // into whatever they're really doing. Settings is the one exception: it holds the
    // keyboard focus itself, so the game can't be foreground while it's open, and its hotkey
    // still has to close it.
    const bool game_focused = foreground_title_contains(config_.poe_window_title);
    if (!game_focused && !dev_mode_ &&
        !(a == Action::ToggleSettings && screen_ == Screen::Settings)) {
        if (trace_copy()) SDL_Log("[copy] hotkey ignored: game not focused");
        return;
    }

    if (a == Action::PriceCheck) {
        // Sample the cursor now, while it's still on the item — the user will have moved
        // on by the time the clipboard lands.
        side_ = cursor_side();
        if (trace_copy()) SDL_Log("[copy] price-check hotkey, game focused=%d", game_focused);
        if (game_focused) {
            // Copy FIRST, while the game still holds focus — the synthetic Ctrl+C must reach
            // it. Don't touch the focus on the way: we just confirmed the game is foreground,
            // and XSetInputFocus on its toplevel can land somewhere Wine didn't put it. Then
            // show the panel; the text fills in asynchronously (poll_pending_copy).
            copy_before_ = read_clipboard();
            uint64_t t0 = SDL_GetTicks();
            simulate_copy();
            if (trace_copy())
                SDL_Log("[copy]   simulate_copy took %llums",
                        (unsigned long long)(SDL_GetTicks() - t0));
            accept_clipboard({}); // show "copying…" until the item lands
            copy_pending_ = true;
            copy_late_ = false;
            clipboard_dirty_ = false;
            copy_started_ms_ = last_clipboard_poll_ms_ = SDL_GetTicks();
        } else { // dev mode: no game to copy from, just show what's already there
            accept_clipboard(read_clipboard());
            copy_pending_ = false;
            copy_late_ = false;
        }
        set_screen(Screen::PriceCheck);
    } else {
        set_screen(screen_ == Screen::Settings ? Screen::Hidden : Screen::Settings);
    }
}

void App::update_overlay_placement() {
    uint64_t now = SDL_GetTicks();
    if (now - last_detect_ms_ < 400) return; // poll a few times a second, not every frame
    last_detect_ms_ = now;

    GameWindow g = find_game_window(config_.poe_window_title);
    // Go dormant when the game is gone *or* merely not in front — the idle marker has no
    // business floating over other applications. Keep polling either way. An open panel is
    // exempt: Settings holds the focus itself, so the game is never foreground while it's up,
    // and price-check dismisses on its own terms.
    if (!g.present || (!g.focused && screen_ == Screen::Hidden)) {
        if (overlay_.visible() && screen_ == Screen::Hidden) overlay_.set_visible(false);
        if (!g.present) { // forget geometry so it re-places when the game comes back
            game_present_ = false;
            game_w_ = game_h_ = 0;
        }
        return;
    }

    bool moved = g.x != game_x_ || g.y != game_y_ || g.w != game_w_ || g.h != game_h_;
    if (game_present_ && overlay_.visible() && !moved) return; // already placed, nothing changed
    game_present_ = true;
    game_x_ = g.x;
    game_y_ = g.y;
    game_w_ = g.w;
    game_h_ = g.h;

    place_overlay();
    if (!overlay_.visible()) {
        overlay_.set_visible(true);
        overlay_set_click_through(overlay_.window(), screen_ == Screen::Hidden);
    }
    if (screen_ != Screen::Hidden) SDL_RaiseWindow(overlay_.window());
    need_redraw_ = true; // first placement / a move: repaint once
}

Side App::cursor_side() const {
    if (game_w_ <= 0) return Side::Inventory;
    float mx = 0, my = 0;
    SDL_GetGlobalMouseState(&mx, &my);
    return mx < game_x_ + game_w_ * 0.5f ? Side::Stash : Side::Inventory;
}

// The stash occupies the left of the screen and the inventory the right, so the half the
// cursor is in tells us which one the item came from — dock full-height against that
// frame's inner edge and the panel never covers what's being priced. Panels that straddle
// the middle (vendor, quest rewards) have no correct answer; the cursor's half wins.
void App::place_overlay() {
    int gx = game_x_, gy = game_y_, gw = game_w_, gh = game_h_;
    if (gw <= 0 || gh <= 0) { // no game yet (PPC_DEV_OVERLAY); lay out against the display
        SDL_Rect r{};
        if (!SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &r)) return;
        gx = r.x, gy = r.y, gw = r.w, gh = r.h;
    }

    if (screen_ != Screen::PriceCheck) {
        SDL_SetWindowSize(overlay_.window(), kSettingsW, kSettingsH);
        SDL_SetWindowPosition(overlay_.window(), gx + (gw - kSettingsW) / 2,
                              gy + (gh - kSettingsH) / 2);
        return;
    }

    const int pw = std::clamp(config_.panel_width, 200, gw);
    const int x = side_ == Side::Stash
                      ? gx + static_cast<int>(gh * config_.stash_edge)
                      : gx + gw - static_cast<int>(gh * config_.inventory_edge) - pw;
    SDL_SetWindowSize(overlay_.window(), pw, gh);
    SDL_SetWindowPosition(overlay_.window(), std::clamp(x, gx, gx + gw - pw), gy);
}

void App::set_screen(Screen s) {
    screen_ = s;
    bool active = s != Screen::Hidden;
    if (!active) copy_pending_ = false; // nothing left to fill in; stop watching the clipboard
    place_overlay();                    // each screen has its own geometry; apply before showing
    if (active && !overlay_.visible()) overlay_.set_visible(true);
    // The window stays mapped; interactivity is what changes. Idle == click-through
    // so input passes to the game; active == catch input and raise to the front.
    overlay_set_click_through(overlay_.window(), !active);
    if (active) {
        had_focus_ = false; // wait for the (re)focused window to report focus
        SDL_RaiseWindow(overlay_.window());
    }
    // Settings needs keyboard focus immediately (text fields); price-check grabs it only
    // after the copy (see handle_action). Closing hands focus back to the game.
    if (s == Screen::Settings) {
        overlay_take_keyboard_focus(overlay_.window());
        // TTL-gated, so a warm cache makes this a no-op. A user who never opens Settings
        // never makes a network request at all.
        leagues_.refresh(false);
    } else if (s == Screen::Hidden && overlay_.has_focus()) {
        focus_game_window(config_.poe_window_title); // only hand back focus we actually took
    }
    need_redraw_ = true;
}

void App::begin_capture(Action which) {
    capturing_ = true;
    capture_which_ = which;
    hotkeys_->rebind({}); // suspend grabs so the combo reaches us instead of firing
}

void App::end_capture() {
    capturing_ = false;
    rebind_hotkeys(); // re-grab (picks up the freshly-captured binding)
}

void App::apply_and_save_config() {
    config_.save();
    rebind_hotkeys();
}

void App::rebind_hotkeys() {
    hotkeys_->rebind({{config_.price_check, Action::PriceCheck}, {config_.settings, Action::ToggleSettings}});
}

} // namespace ppc
