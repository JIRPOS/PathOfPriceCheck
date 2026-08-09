#include "app.hpp"

#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>

#include "icon.hpp"
#include "item/resolve.hpp"
#include "net/http.hpp"
#include "paths.hpp"
#include "platform/clipboard.hpp"
#include "platform/foreground.hpp"
#include "platform/input_sim.hpp"
#include "platform/overlay_native.hpp"
#include "platform/platform.hpp"
#include "platform/single_instance.hpp"
#include "screens/pricecheck_screen.hpp"
#include "screens/settings_screen.hpp"
#include "trade/query.hpp"
#include "ui/strings.hpp"
#include "util/debug_log.hpp"

namespace ppc {
namespace {

/// Cheap sniff for PoE clipboard text. Guards against latching whatever the user last
/// copied elsewhere when the game's own copy is slow or never arrives. Only the head is
/// examined: the marker is in the first two lines or the text is not an item.
/// One line's worth of clipboard text for a trace line. Item text is mostly newlines, and a
/// log where one fact spans twelve lines is not greppable.
std::string preview(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size() && out.size() < 60; ++i)
        out += s[i] == '\n' ? std::string("\\n") : std::string(1, s[i]);
    return out;
}

std::string read_clipboard(const char* label) {
    uint64_t t0 = SDL_GetTicks();
    // A live owner answers in well under a millisecond; the budget only bounds how long a
    // dead one can stall the frame loop, and we poll again shortly anyway.
    std::string s = clipboard_text(60);
    if (debug::tracing())
        debug::trace("[copy]   read clipboard (%s): %zu bytes in %llums | %s", label, s.size(),
                     (unsigned long long)(SDL_GetTicks() - t0), preview(s).c_str());
    if (debug::enabled()) {
        // The whole thing once per distinct value: the watch re-reads every 100ms and the
        // interesting event is the read *changing*, not the twenty that repeat.
        static std::string last;
        std::string d = debug::digest(s);
        if (d == last) {
            debug::log("%s: unchanged (fnv=%s)", label, d.c_str());
        } else {
            debug::log_text(label, s);
            last = std::move(d);
        }
    }
    return s;
}

void SDLCALL tray_exit_cb(void* userdata, SDL_TrayEntry*) {
    static_cast<App*>(userdata)->quit();
}

// Settings is a free-floating dialog, centered over the game; only price-check docks. Sized to
// hold every section without scrolling — the panel is a form, and a form that scrolls hides the
// Save button under whatever the user was just reading. Capped at the game's own height, since
// a dialog taller than the screen scrolls whatever this says.
constexpr int kSettingsW = 640, kSettingsH = 980;

// The idle status: two short lines over the middle of the mana globe. Wide enough for a long
// data version at the size below, and no wider — the window is what swallows mouse input, and
// while idle it is only click-through because nothing else is open.
constexpr int kStatusW = 200, kStatusH = 48;
constexpr float kStatusFontSize = 15.0f;
constexpr float kStatusAlpha = 0.5f; ///< it sits on top of the game's own HUD

/// What the second status line says when there is no bundle version to print — which is either
/// a first run still downloading or a client that has never managed to. A blank line there reads
/// as something broken.
std::string data_status_line(const data::DataUpdater::Status& st) {
    if (!st.data_version.empty()) return st.data_version;
    switch (st.state) {
    case data::DataUpdater::State::Idle:
    case data::DataUpdater::State::Failed: return "no data";
    default: return "updating\xe2\x80\xa6";
    }
}

/// One centred line in yellow with a black outline, drawn straight into the draw list: the
/// overlay has no background of its own here, so the text is over whatever the game is showing
/// and needs to carry its own contrast. The outline is the same string stamped at eight offsets
/// around the glyphs, which is cheap at two lines and needs no shader.
void draw_outlined_line(const char* text, float centre_x, float y, float alpha) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const ImVec2 extent = ImGui::CalcTextSize(text);
    const ImVec2 at(centre_x - extent.x * 0.5f, y);
    const ImU32 outline = IM_COL32(0, 0, 0, static_cast<int>(alpha * 255));
    for (const ImVec2 d : {ImVec2(-1, -1), ImVec2(0, -1), ImVec2(1, -1), ImVec2(-1, 0),
                           ImVec2(1, 0), ImVec2(-1, 1), ImVec2(0, 1), ImVec2(1, 1)})
        dl->AddText(font, size, ImVec2(at.x + d.x, at.y + d.y), outline, text);
    dl->AddText(font, size, at, IM_COL32(255, 215, 0, static_cast<int>(alpha * 255)), text);
}

/// The idle marker: the application's version and the data bundle's, over the middle of the mana
/// globe (see `Config::status_right`). It says the overlay is alive and which data it is pricing
/// against — the two things there is no other way to see without opening Settings. Only drawn
/// while the game is the window in front; `update_overlay_placement` unmaps it otherwise.
void draw_status_marker(App& app) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##ppc_status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(app.fonts().small_caps, kStatusFontSize);
    const std::string version = "PoPC v" APP_VERSION;
    const std::string data = data_status_line(app.data_status());
    const float line_h = ImGui::GetTextLineHeightWithSpacing();
    const float top = (io.DisplaySize.y - line_h * 2) * 0.5f;
    draw_outlined_line(version.c_str(), io.DisplaySize.x * 0.5f, top, kStatusAlpha);
    draw_outlined_line(data.c_str(), io.DisplaySize.x * 0.5f, top + line_h, kStatusAlpha);
    ImGui::PopFont();
    ImGui::End();
}

} // namespace

int App::run() {
    // Before the log, not after: a second instance opening its own log file would start a new
    // one and prune the ten kept, so the run being diagnosed can be pushed out of the window by
    // a stray double-click. A rejected launch is not this application's session and writes
    // nothing at all.
    InstanceLock instance("PathOfPriceCheck");
    if (!instance.held()) {
        // Said out loud, unlike a failed price check. That silence is a rule about the overlay,
        // which is noise over a game; this is a launch the user just performed on their desktop
        // and is owed an answer to, and the answer is that they already have what they asked
        // for — the tray icon is right there. Exit 0 for the same reason: the intended state
        // holds, so a desktop launcher has no error to report.
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "PathOfPriceCheck",
                                 "PathOfPriceCheck is already running.\n\n"
                                 "Look for the icon in the system tray.",
                                 nullptr);
        return 0;
    }
    platform_init();
    // Before anything else touches the clipboard or the hotkeys: this session's log has to
    // start at the same instant the process does, since "the first price check after launch"
    // is the failure being chased.
    debug::set_enabled(config_.debug_log);
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
    // SDL disables the screensaver at video init on the assumption it is running a
    // game, and on Linux holds an org.freedesktop.ScreenSaver inhibit — reason
    // "Playing a game" — for the life of the process, so an idle tray app blocks
    // sleep. The game does its own inhibiting; we are a desktop app.
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    // SDL hands back a contiguous range, so the offsets are guaranteed.
    const uint32_t event_base = SDL_RegisterEvents(6);
    if (!event_base) {
        SDL_Log("SDL_RegisterEvents failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    hotkey_event_ = event_base;
    league_event_ = event_base + 1;
    data_event_ = event_base + 2;
    trade_event_ = event_base + 3;
    ninja_event_ = event_base + 4;
    exchange_event_ = event_base + 5;

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
    // Dates are the opposite case: a timestamp is for the reader, not for the game, so it is
    // written the way their machine writes one. Explicit rather than inherited, because
    // nothing calls setlocale on Windows — a GUI-subsystem binary gets neither SDL's XIM nor
    // GTK — and a C-locale date is exactly the invariant-looking format this avoids.
    std::setlocale(LC_TIME, "");

    net::init();
    leagues_.init(league_event_);
    leagues_.load_cache(); // file read only; the network is touched when Settings opens
    trade_.init(trade_event_);
    trade_.load_cache(); // currency symbols; the search itself fetches them if they are stale
    ninja_.init(ninja_event_);
    currency_exchange_.init(exchange_event_);
    icons_.init();

    // Reclaim superseded bundles and map the installed one before anything else can hold a
    // mapping — on Windows a mapped directory cannot be removed.
    updater_.init(cache_dir() / "data", data_event_);
    ui::set_language(config_.ui_language, config_.client_language);
    updater_.set_language(config_.client_language);
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
    log_session_start(); // after dev_mode_, which changes what every gate below does
    if (dev_mode_) { // local UI dev, no game needed
        overlay_.set_visible(true);
        // PPC_DEV_ITEM=<file> opens the price-check panel on a captured clipboard instead,
        // which is the only way to iterate on it without the game running.
        if (std::getenv("PPC_DEV_IDLE")) {
            // The idle status marker, which otherwise only ever appears while the game is the
            // window in front. Laid out against the display, since there is no game to measure.
            place_overlay();
        } else if (const char* path = std::getenv("PPC_DEV_ITEM")) {
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
        // A pending copy counts as active even though nothing is on screen: it is polled from
        // this loop, and at the idle 250ms the poke and the read are both a quarter second late.
        bool active = screen_ != Screen::Hidden || copy_pending_;
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
        // Textures can only be made where the GL context is, and a symbol that arrived
        // while nothing was repainting has to bring its own repaint with it.
        if (icons_.pump()) need_redraw_ = true;
        if (overlay_.visible() && (active || need_redraw_)) {
            overlay_.begin_frame();
            if (screen_ == Screen::Settings)
                draw_settings_screen(*this);
            else if (screen_ == Screen::PriceCheck)
                draw_pricecheck_screen(*this);
            else
                draw_status_marker(*this);
            overlay_.end_frame();
            need_redraw_ = false;
        }
    }

    if (tray_) SDL_DestroyTray(tray_);
    hotkeys_.reset();
    leagues_.shutdown(); // joins + drains its events; must precede SDL_Quit
    trade_.shutdown();
    ninja_.shutdown();
    currency_exchange_.shutdown();
    icons_.shutdown(); // frees GL textures, so before the context goes with the overlay
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
    // Mint the id here, on the hotkey thread, rather than in handle_action: everything the
    // press causes — including the lines the hotkey and clipboard layers write before the SDL
    // event is even drained — then carries the same tag.
    if (a == Action::PriceCheck) debug::begin_check();
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
    } else if (e.type == trade_event_) {
        trade_.on_done(e);
        listing_items_.clear(); // the rows they were parsed for may be gone; re-parse on hover
    } else if (e.type == ninja_event_) {
        ninja_.on_done(e);
        need_redraw_ = true;
    } else if (e.type == exchange_event_) {
        currency_exchange_.on_done(e);
        need_redraw_ = true;
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
    } else if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        had_focus_ = true;
        debug::log("[app]    overlay focus gained");
    } else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        debug::log("[app]    overlay focus lost (screen=%d, had_focus=%d)", (int)screen_,
                   (int)had_focus_);
        // Price-check auto-dismisses when you click back into the game; Settings stays
        // open until closed manually (its hotkey or the X button). had_focus_ avoids
        // closing before the window has actually taken focus.
        if (!dev_mode_ && screen_ == Screen::PriceCheck && had_focus_) set_screen(Screen::Hidden);
    }
    overlay_.process_event(e);
    need_redraw_ = true; // an event may have changed the UI
}

/// Wait for the clipboard to be written, then show what it holds if it is an item.
///
/// The stamp is the entire test, and it is why this is short. It moves only when something
/// writes the clipboard, so there is nothing to reconcile: no snapshot of the previous text to
/// diff against, no rule for text that happens to be byte-identical, no separate accelerators
/// to cross-check. It costs no request to the owner either, so this can run every frame and
/// the one real read happens once, after there is something new to read.
///
/// Failure is silent by design. Past the deadline, or on a clipboard that holds something that
/// is not an item, the check is simply dropped — an overlay that reports its own plumbing is
/// noise over a game, and the log has the detail for when it matters.
void App::poll_pending_copy() {
    if (!copy_pending_) return;
    const uint64_t elapsed = SDL_GetTicks() - copy_started_ms_;
    const uint64_t stamp = clipboard_stamp();

    if (stamp == copy_stamp_) { // nothing has been copied yet
        if (elapsed < kCopyTimeoutMs) {
            // Ask, don't just listen: Wine renders the clipboard only when someone requests it,
            // and publishes by re-asserting ownership — which is the very thing being waited on.
            // Throttled: the loop runs at 60Hz while a copy is pending so the *reply* is seen
            // promptly, but a hundred conversion requests at an owner mid-handover is a poke, not
            // a nudge.
            if (SDL_GetTicks() - copy_poked_ms_ >= kPokeIntervalMs) {
                copy_poked_ms_ = SDL_GetTicks();
                clipboard_poke();
            }
            nudge_clipboard_handover(elapsed);
            return;
        }
        if (debug::enabled())
            debug::log("[copy] gave up after %llums: the clipboard was never written. %s owner=%s"
                       " targets=%s",
                       (unsigned long long)elapsed, focus_info().c_str(),
                       clipboard_owner_info().c_str(), clipboard_targets(100).c_str());
        abandon_copy();
        return;
    }

    copy_pending_ = false;
    if (debug::enabled())
        debug::log("[copy] clipboard written after %llums (stamp %llu -> %llu) owner=%s",
                   (unsigned long long)elapsed, (unsigned long long)copy_stamp_,
                   (unsigned long long)stamp, clipboard_owner_info().c_str());
    accept_clipboard(read_clipboard("clipboard.copy"));
    if (!item_) {
        debug::log("[copy] dropped: what was copied is not an item");
        abandon_copy();
        return;
    }
    set_screen(Screen::PriceCheck);
}

/// Give up on the copy in flight, leaving nothing on screen. Hands back any keyboard focus the
/// handover nudge took, so a failed check doesn't quietly leave the game unable to receive keys.
void App::abandon_copy() {
    copy_pending_ = false;
    if (overlay_.has_focus() && screen_ != Screen::Settings)
        focus_game_window(config_.poe_window_title);
    need_redraw_ = true;
}

/// Path of Exile under Wine does not hand its copy to the X CLIPBOARD selection when it copies.
/// It hands it over when the game loses focus — measured with a standalone watcher and this
/// application not running at all, on a *manual* Ctrl+C, so nothing in the injection or the read
/// path can shorten it. Until then the previous owner keeps serving the previous text and every
/// signal we have says, correctly, that no copy has happened.
///
/// So take the keyboard onto the panel: the focus-out is the event the game is waiting for, and
/// the panel is a thing the user clicks anyway. Deliberately **not** unconditional — a healthy
/// clipboard (any Windows machine, a native X11 game) answers the first poll, and stealing the
/// keyboard mid-fight for a copy that was never late is a worse bug than the one being fixed.
/// Hence: once per check, only past the grace period, and only while the game is still the
/// window in front — if the user has already alt-tabbed away, the focus-out has happened and
/// grabbing focus would take it off whatever they moved to.
void App::nudge_clipboard_handover(uint64_t elapsed) {
    // Three polls of the 100ms loop. Long enough that a clipboard which works never sees this.
    static constexpr uint64_t kGraceMs = 350;
    if (copy_nudged_ || elapsed < kGraceMs) return;
    copy_nudged_ = true;
    // No screen check: nothing is on screen during a check, by design. Guarding on
    // `screen_ == PriceCheck` is what silently disabled this after the panel stopped being
    // shown while the copy was in flight.
    if (dev_mode_ || !overlay_.visible()) return;
    if (overlay_.has_focus() || !foreground_title_contains(config_.poe_window_title)) {
        debug::log("[copy]   no handover after %llums, but the game no longer holds focus",
                   (unsigned long long)elapsed);
        return;
    }
    debug::log("[copy]   no handover after %llums — taking the keyboard onto the panel so the"
               " game sees a focus-out",
               (unsigned long long)elapsed);
    overlay_take_keyboard_focus(overlay_.window());
}

void App::accept_clipboard(std::string text) {
    clipboard_ = std::move(text);
    strategy_override_.reset(); // a choice belongs to the item it was made for
    unique_choice_.clear();     // as does which unique an unidentified one turned out to be
    trade_.clear();             // and so do the listings: they priced the previous item
    listing_items_.clear();
    rebuild_plan();
    // Off by default. A price check the user meant only to read the item with should not
    // spend a request, which is why this is opt-in rather than the way the panel behaves.
    if (config_.auto_search) start_search();
}

bool App::can_search() const {
    // An unidentified unique nobody has named yet is not a search that would come back wrong,
    // it is a search for a different item: a Prismatic Jewel's fifty uniques share nothing but
    // the base, and the cheapest of them would read as this one's price.
    if (item_ && item_->needs_unique_choice()) return false;
    return item_ && item_data_ && trade::searchable(plan_) && !trades_on_exchange();
}

bool App::trades_on_exchange() const {
    // Two sources, strongest evidence first. A market in the hour we fetched *proves* the item
    // trades there, whatever the bundle says — which is also what keeps this working on a
    // bundle published before the flag existed.
    if (currency_exchange_.listing()) return true;
    // Otherwise the bundle answers, and only where it has an answer to give: on an older bundle
    // `has_exchange_flags()` is false and a missing flag means "unknown", not "no".
    return item_ && item_->base && item_data_ && item_data_->has_exchange_flags() &&
           item_->base->exchange;
}

void App::start_search() {
    if (!can_search()) return;
    trade_.search(config_.league, trade::build_query(plan_, config_.listing_status),
                  config_.result_count);
    need_redraw_ = true;
}

/// The plan as it stands, handed to the trade site itself. Deliberately built from the
/// filters rather than from a search we already ran: the id of a finished search would open
/// whatever was ticked at the time, which is not what the panel is showing after the user
/// has changed their mind about a mod.
void App::open_search_in_browser() {
    if (!can_search()) return;
    const std::string url = trade::web_url_for_query(
        config_.league, trade::build_query(plan_, config_.listing_status));
    debug::log("[trade]  opening %s", url.c_str());
    if (!SDL_OpenURL(url.c_str())) debug::log("[trade]  SDL_OpenURL failed: %s", SDL_GetError());
}

void App::load_more() {
    trade_.load_more();
    need_redraw_ = true;
}

const ListingItem* App::listing_item(size_t i) {
    const std::vector<trade::Listing>& ls = trade_.results().listings;
    if (i >= ls.size()) return nullptr;
    if (listing_items_.size() != ls.size()) listing_items_.resize(ls.size());
    std::optional<ListingItem>& slot = listing_items_[i];
    if (slot) return &*slot;

    ListingItem li;
    li.item = item::parse_item(ls[i].item_text, item_lexicon());
    if (li.item) {
        // The same snapshot the item in hand was pinned to, and for the same reason: the
        // updater swaps `data_` from its own thread, and a resolved item points into the
        // bundle it was matched in.
        if (item_data_) item::resolve_item(*item_data_, *li.item);
        li.derived = item::derive(item_data_.get(), *li.item);
    } else if (!ls[i].item_text.empty()) {
        debug::log("[trade]  listing %zu: %zu bytes of item text did not parse", i,
                   ls[i].item_text.size());
    }
    slot = std::move(li);
    return &*slot;
}

void App::rebuild_plan() {
    // Pin the snapshot the item is resolved against *before* reading it: the updater swaps
    // `data_` from its own thread, every stat the item points at lives in the bundle it was
    // matched in, and the vocabulary the text is read with has to come from that same bundle.
    item_data_ = data_;
    item_ = item::parse_item(clipboard_, item_lexicon());
    plan_ = {};
    derived_ = {};
    if (!item_) {
        if (!clipboard_.empty())
            debug::log("[item]   %zu bytes on the clipboard did not parse as an item",
                       clipboard_.size());
        return;
    }
    debug::log("[item]   parsed: rarity=%d class='%s' name='%s' base='%s' %zu modifiers",
               (int)item_->rarity, item_->item_class.c_str(), item_->name.c_str(),
               item_->base_type.c_str(), item_->mods.size());
    if (item_data_) {
        item::resolve_item(*item_data_, *item_);
        // A choice the user already made survives a re-resolve — a bundle update mid-check is
        // no reason to ask them again — but it is re-matched by name against the candidates the
        // *new* bundle offers, since the records it was made from are gone.
        if (!unique_choice_.empty())
            for (const data::BaseType* u : item_->unique_candidates)
                if (u->name == unique_choice_) item::choose_unique(*item_, u);
        derived_ = item::derive(item_data_.get(), *item_);
        plan_ = item::build_plan(*item_data_, *item_, derived_, strategy_override_,
                                 config_.range_match);
        price_reference();
    } else {
        // No bundle yet: the item still parses and renders, it just cannot be priced.
        derived_ = item::derive(nullptr, *item_);
    }
    need_redraw_ = true;
}

void App::set_strategy(item::Strategy s) {
    strategy_override_ = s;
    if (item_ && item_data_) {
        plan_ = item::build_plan(*item_data_, *item_, derived_, strategy_override_,
                                 config_.range_match);
        // The strategy is what decides whether poe.ninja prices this at all — a rare read as
        // a base item has no reference price, and switching back has to bring it back.
        price_reference();
    }
    need_redraw_ = true;
}

void App::set_unique(const data::BaseType* u) {
    if (!item_ || !item_data_) return;
    item::choose_unique(*item_, u);
    unique_choice_ = item_->unique_entry ? item_->unique_entry->name : std::string();
    debug::log("[item]   unidentified unique read as '%s'", unique_choice_.c_str());
    // The listings priced whatever was searched before the choice, which was either nothing or
    // a different unique.
    trade_.clear();
    listing_items_.clear();
    plan_ = item::build_plan(*item_data_, *item_, derived_, strategy_override_,
                             config_.range_match);
    // The name is what poe.ninja prices a unique by, so this is the first ask that can find one.
    price_reference();
    need_redraw_ = true;
}

/// Ask poe.ninja about the item as the plan now reads it. Unlike a trade search this is not
/// on a button: it spends no GGG request, and the overviews behind it are shared by every
/// check and refreshed at most twice an hour.
void App::price_reference() {
    if (!item_) return;
    ninja_.price(ninja::query_for(*item_, plan_, config_.league));
    // The exchange is asked about every item, not only the ones planned as currency: whether
    // a thing trades there is a fact about the market rather than about our strategy, and the
    // feed is one download for all of them. An item whose base carries no metadata id — an
    // older bundle — asks nothing.
    currency_exchange_.lookup(config_.league,
                              item_->base ? item_->base->metadata_id : std::string());
}

void App::open_reference_page() {
    const std::string& url = ninja_.reference().url;
    if (url.empty()) return;
    debug::log("[ninja]  opening %s", url.c_str());
    if (!SDL_OpenURL(url.c_str())) debug::log("[ninja]  SDL_OpenURL failed: %s", SDL_GetError());
}

void App::poll_click_away() {
    if (screen_ != Screen::PriceCheck) {
        mouse_was_down_ = false;
        return;
    }
    // KWin doesn't reliably send focus-out to an override-redirect window, so watch the
    // global mouse directly: a press outside the panel dismisses it (X button handles
    // presses inside). Price-check only holds focus when the handover nudge had to take it,
    // so the focus-lost path can't be relied on to fire at all.
    float gx = 0, gy = 0;
    bool down = (SDL_GetGlobalMouseState(&gx, &gy) & SDL_BUTTON_LMASK) != 0;
    bool pressed = down && !mouse_was_down_;
    mouse_was_down_ = down;
    if (!pressed) return;

    int wx = 0, wy = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(overlay_.window(), &wx, &wy);
    SDL_GetWindowSize(overlay_.window(), &ww, &wh);
    // Against the *panel*, not the window: the window carries a gutter beside it, and a click on
    // the transparent part of that is a click on the game, which has to dismiss like any other.
    // The item card at the top of the gutter is the exception — that is our own UI.
    const float px = wx + layout_.panel_x;
    const float pw = layout_.panel_w > 0 ? layout_.panel_w : float(ww);
    const bool on_panel = gx >= px && gx < px + pw && gy >= wy && gy < wy + wh;
    const bool on_card = card_h_ > 0 && gx >= wx + layout_.tip_x &&
                         gx < wx + layout_.tip_x + layout_.tip_w && gy >= wy &&
                         gy < wy + card_h_;
    if (!on_panel && !on_card) set_screen(Screen::Hidden);
}

void App::handle_action(Action a) {
    // The hotkeys are grabbed system-wide, so they fire while the user is in a browser or a
    // terminal too. Gate every action on the game actually being in front rather than firing
    // into whatever they're really doing. Settings is the one exception: it holds the
    // keyboard focus itself, so the game can't be foreground while it's open, and its hotkey
    // still has to close it.
    const bool game_focused = foreground_title_contains(config_.poe_window_title);
    if (a == Action::PriceCheck) log_state("hotkey");
    if (!game_focused && !dev_mode_ &&
        !(a == Action::ToggleSettings && screen_ == Screen::Settings)) {
        debug::trace("[copy] hotkey ignored: game not focused");
        return;
    }

    if (a == Action::PriceCheck) {
        // Sample the cursor now, while it's still on the item — the user will have moved
        // on by the time the clipboard lands.
        side_ = cursor_side();
        debug::trace("[copy] price-check hotkey, game focused=%d", game_focused);
        if (!game_focused) { // dev mode: no game to copy from, just show what's already there
            accept_clipboard(read_clipboard("clipboard.dev"));
            if (item_) set_screen(Screen::PriceCheck);
            return;
        }
        // A check already on screen is about the *previous* item. Drop it before starting:
        // if this copy then fails silently, leaving the old panel up would read as a price
        // check of the item now under the cursor.
        if (screen_ == Screen::PriceCheck) set_screen(Screen::Hidden);
        // Take the stamp before injecting, not after — simulate_copy blocks for the length of
        // a human keypress and the copy can land inside it.
        copy_stamp_ = clipboard_stamp();
        // Don't touch the focus on the way in: we just confirmed the game is foreground, and
        // XSetInputFocus on its toplevel can land somewhere Wine didn't put it.
        const uint64_t t0 = SDL_GetTicks();
        simulate_copy();
        copy_pending_ = true;
        copy_nudged_ = false;
        copy_poked_ms_ = 0; // poke on the first poll, not one interval in
        copy_started_ms_ = SDL_GetTicks();
        if (debug::enabled())
            debug::log("[copy] injected in %llums, stamp=%llu owner=%s",
                       (unsigned long long)(copy_started_ms_ - t0),
                       (unsigned long long)copy_stamp_, clipboard_owner_info().c_str());
    } else {
        set_screen(screen_ == Screen::Settings ? Screen::Hidden : Screen::Settings);
    }
}

void App::update_overlay_placement() {
    uint64_t now = SDL_GetTicks();
    if (now - last_detect_ms_ < 400) return; // poll a few times a second, not every frame
    last_detect_ms_ = now;

    GameWindow g = find_game_window(config_.poe_window_title);
    if (const int gs = (g.present ? 2 : 0) | (g.focused ? 1 : 0); gs != game_state_logged_) {
        debug::log("[app]    game window: present=%d focused=%d %dx%d+%d+%d", (int)g.present,
                   (int)g.focused, g.w, g.h, g.x, g.y);
        game_state_logged_ = gs;
    }
    // Go dormant when the game is gone *or* merely not in front — the idle marker has no
    // business floating over other applications. Keep polling either way. An open panel is
    // exempt: Settings holds the focus itself, so the game is never foreground while it's up,
    // and price-check dismisses on its own terms.
    //
    // **Our own window counts as the game being in front**, because from the user's side it
    // is: closing a panel that had taken the focus (a click on it, or the clipboard handover
    // nudge) leaves the focus on our now-empty overlay for as long as the compositor takes to
    // hand it back, and the marker used to blink out for exactly that gap. It is not a hole in
    // the rule above — focusing anything else takes the focus off us too, and the marker goes.
    if (!g.present || (!g.focused && !overlay_.has_focus() && screen_ == Screen::Hidden)) {
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

    if (screen_ == Screen::Settings) {
        const int sh = std::min(kSettingsH, gh);
        SDL_SetWindowSize(overlay_.window(), kSettingsW, sh);
        SDL_SetWindowPosition(overlay_.window(), gx + (gw - kSettingsW) / 2, gy + (gh - sh) / 2);
        layout_ = PanelLayout{0, kSettingsW, 0, 0};
        return;
    }

    if (screen_ == Screen::Hidden) {
        // Over the middle of the mana globe, which hangs off the bottom-right corner and scales
        // with the game's height — so both offsets are fractions of that height, exactly like the
        // two frame edges. The window is no bigger than the text: it is click-through while idle,
        // but it is also what the compositor has to composite every time the game redraws.
        const int cx = gx + gw - static_cast<int>(gh * config_.status_right);
        const int cy = gy + gh - static_cast<int>(gh * config_.status_bottom);
        SDL_SetWindowSize(overlay_.window(), kStatusW, kStatusH);
        // max(min()), not clamp: a game window narrower than the marker puts the low bound above
        // the high one, and clamp is not defined for that.
        SDL_SetWindowPosition(overlay_.window(),
                              std::max(gx, std::min(cx - kStatusW / 2, gx + gw - kStatusW)),
                              std::max(gy, std::min(cy - kStatusH / 2, gy + gh - kStatusH)));
        layout_ = PanelLayout{0, kStatusW, 0, 0};
        return;
    }

    const int pw = std::clamp(config_.panel_width, 200, gw);
    const int px = std::clamp(side_ == Side::Stash
                                  ? gx + static_cast<int>(gh * config_.stash_edge)
                                  : gx + gw - static_cast<int>(gh * config_.inventory_edge) - pw,
                              gx, gx + gw - pw);

    // The window is widened by a transparent gutter for the listing tooltip, taken from the
    // side the panel is *not* docked against — over the game, never over the results. Docked
    // left (right of the stash) it goes right; docked right (left of the inventory) it goes
    // left. Only what is actually free is claimed, so the overlay never spills off the game.
    const int free = side_ == Side::Stash ? (gx + gw) - (px + pw) : px - gx;
    const int gutter = std::clamp(free, 0, pw);
    const int wx = side_ == Side::Stash ? px : px - gutter;

    SDL_SetWindowSize(overlay_.window(), pw + gutter, gh);
    SDL_SetWindowPosition(overlay_.window(), wx, gy);
    layout_ = side_ == Side::Stash
                  ? PanelLayout{0, float(pw), float(pw), float(gutter)}
                  : PanelLayout{float(gutter), float(pw), 0, float(gutter)};
    debug::log("[app]    placed %s: window %dx%d+%d+%d, panel x=%.0f w=%.0f, tip x=%.0f w=%.0f",
               side_ == Side::Stash ? "stash" : "inventory", pw + gutter, gh, wx, gy,
               layout_.panel_x, layout_.panel_w, layout_.tip_x, layout_.tip_w);
}

// The state of everything the copy path depends on, in one place, written at each hotkey press
// and whenever the log is switched on. A price check that goes wrong is nearly always a
// disagreement between two of these — which window is foreground, who owns the selection,
// which screen we already think is open.
void App::log_state(const char* when) {
    if (!debug::enabled()) return;
    const std::string title = foreground_title();
    debug::log("[state]  %s: screen=%d overlay=%s focus=%d dev=%d", when, (int)screen_,
               overlay_.visible() ? "visible" : "hidden", (int)overlay_.has_focus(),
               (int)dev_mode_);
    debug::log("[state]  foreground='%s' matches '%s': %d", title.c_str(),
               config_.poe_window_title.c_str(),
               (int)(title.find(config_.poe_window_title) != std::string::npos));
    debug::log("[state]  game present=%d at %dx%d+%d+%d, side=%s", (int)game_present_, game_w_,
               game_h_, game_x_, game_y_, side_ == Side::Stash ? "stash" : "inventory");
    debug::log("[state]  copy pending=%d, clipboard stamp=%llu owner %s", (int)copy_pending_,
               (unsigned long long)clipboard_stamp(), clipboard_owner_info().c_str());
}

void App::log_session_start() {
    if (!debug::enabled()) return;
    debug::log("[state]  config %s", Config::path().c_str());
    debug::log("[state]  hotkeys: price check %s, settings %s; league '%s'; window title '%s'",
               to_string(config_.price_check).c_str(), to_string(config_.settings).c_str(),
               config_.league.c_str(), config_.poe_window_title.c_str());
    debug::log("[state]  video driver %s", SDL_GetCurrentVideoDriver());
    log_state("startup");
}

void App::set_debug_log(bool on) {
    config_.debug_log = on;
    debug::set_enabled(on);
    log_session_start(); // no-op when it was just turned off
    need_redraw_ = true;
}

// Writing the id back to the clipboard is the point — the user is reporting a check that went
// wrong and needs its id in a message, not transcribed by hand. It does take the selection off
// the game, which the next price check re-takes, and the log says it happened.
void App::copy_check_id() {
    const std::string id = debug::check_id();
    if (id.empty()) return;
    SDL_SetClipboardText(id.c_str());
    debug::log("[app]    wrote the check id to the clipboard on the user's request");
}

void App::set_screen(Screen s) {
    debug::log("[app]    screen %d -> %d", (int)screen_, (int)s);
    screen_ = s;
    // The card is re-measured on the first frame of the new screen, and poll_click_away runs
    // before that frame: without this it would spend one iteration on the last check's card.
    card_h_ = 0;
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
    // Settings needs keyboard focus immediately (text fields); price-check takes it only if
    // the copy stalls (nudge_clipboard_handover). Closing hands focus back to the game.
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
    // Our own text redraws in the new language at once; the client language does not, because
    // the bundle was opened with the old one and every parsed item points into it. Settings
    // says so on the row rather than pretending the change has landed.
    ui::set_language(config_.ui_language, config_.client_language);
}

void App::rebind_hotkeys() {
    hotkeys_->rebind({{config_.price_check, Action::PriceCheck}, {config_.settings, Action::ToggleSettings}});
}

} // namespace ppc
