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
#include "quickpaste.hpp"
#include "screens/pricecheck_screen.hpp"
#include "screens/quickpaste_screen.hpp"
#include "screens/report_screen.hpp"
#include "screens/settings_screen.hpp"
#include "trade/query.hpp"
#include "ui/strings.hpp"
#include "ui/theme.hpp"
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

/// True when the owner answered with a real format list rather than one of `clipboard_targets`'
/// parenthesised non-answers ("(no owner)", "(no reply)", "(refused)", "(no display)"). The
/// difference is the whole diagnosis below: an owner that replies exists and is responsive.
bool owner_answered(const std::string& targets) {
    return !targets.empty() && targets.front() != '(';
}

/// What a give-up means when the owner answered. Empty when it did not, because then the
/// failure is a different one — nobody owns the selection, or the owner is wedged itself.
///
/// **No guess about who the owner is**, which is the point: two facts already in hand at the
/// give-up line are conclusive together. Nothing asserted ownership during the entire check
/// (that *is* the give-up condition), and the owner answered a format list (so it exists and
/// responds). A live, responsive owner that never re-asserted is not the game's clipboard —
/// the copy was never published and every poke went to somebody else's selection.
///
/// Two earlier attempts at this were built on identity and both were wrong, which is why it is
/// worded this way. Comparing `_NET_WM_PID` cannot work: the owner in the captured failures is
/// KWin's own selection window, which advertises no pid and no `WM_CLASS` at all. Fingerprinting
/// the format list against Wine's would be a third guess — every capture of Wine's formats came
/// through the Wayland bridge rather than from X, so there is nothing to match against.
///
/// The format list is still logged beside this, and it is usually self-describing about who *did*
/// have it: `chromium/x-source-url` is the browser, `application/x-kde-onlyReplaceEmpty` is the
/// clipboard manager. That is a hint for a reader, deliberately not a rule for the code.
std::string clipboard_wedge_note(const std::string& targets) {
    if (!owner_answered(targets)) return {};
    return "the clipboard has an owner that answers, and it never re-asserted during this check —"
           " so the selection belongs to something other than the game and the game's copy was"
           " never published. Under Wayland this is the known wedge: a copy made in a Wayland"
           " application takes the selection from Wine, which re-acquires it only when the window"
           " manager activates the game again. Alt-tab out of the game and back to clear it."
           " The format list above usually names who has it.";
}

/// Which paste-list slot a key press picks, or -1 for a key that picks none.
///
/// **Scancodes and not keycodes.** The digits are printed on the number row only on a US
/// layout; on a Czech one the same physical keys produce `ěščřžýáíé`, and the popup would be
/// unusable by number for everybody whose layout is not en-US. A scancode is the key's position,
/// which is what "the second key along" means and what the digit in the square stands for. The
/// keypad answers the same slots, since somebody whose hand is already there should not have to
/// move it either.
int paste_slot_for(SDL_Scancode sc) {
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return sc - SDL_SCANCODE_1;
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return sc - SDL_SCANCODE_KP_1;
    return -1;
}

void SDLCALL tray_exit_cb(void* userdata, SDL_TrayEntry*) {
    static_cast<App*>(userdata)->quit();
}

// Settings is a free-floating dialog, centered over the game; only price-check docks. One fixed
// size for every tab: the height used to be measured from whichever tab was open, and a dialog
// that resized itself under the pointer on every tab click was worse than the scrollbar it was
// avoiding. 720 fits the tallest tab and still fits inside a 768-tall game window. The cap is the
// game's own height, which is the one thing that can force that scrollbar.
constexpr int kSettingsW = 640, kSettingsH = 720;

// The report dialog is two columns — the payload on the left, the screenshot beside it — so it is
// wider than Settings and no taller. The confirmation that follows it is a sentence and a button.
constexpr int kReportW = 940, kReportH = 660;
constexpr int kNoticeW = 420, kNoticeH = 150;

// The idle status: two short lines over the lower half of the mana globe. Wide enough for a long
// data version at the size below, and no wider — the window is what swallows mouse input, and
// while idle it is only click-through because nothing else is open.
constexpr int kStatusW = 200, kStatusH = 48;
constexpr int kStatusUpdateH = 68; ///< one line taller while an update is waiting
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

/// The third status line, or empty when there is nothing waiting. Short on purpose: the marker
/// is 200px of text over the mana globe, and the place to read the detail is Settings.
std::string update_status_line(const update::Updater::Status& st) {
    switch (st.state) {
    case update::Updater::State::Ready: return "v" + st.available + " ready";
    case update::Updater::State::Offer: return "v" + st.available + " available";
    default: return {};
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

/// The idle marker: the application's version and the data bundle's, over the lower half of the
/// mana globe (see `Config::status_right`). It says the overlay is alive and which data it is
/// pricing against — the two things there is no other way to see without opening Settings. Only
/// drawn while the game is the window in front; `update_overlay_placement` unmaps it otherwise.
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
    const std::string news =
        app.update_dismissed() ? std::string() : update_status_line(app.update_status());
    const float line_h = ImGui::GetTextLineHeightWithSpacing();
    const int lines = news.empty() ? 2 : 3;
    const float top = (io.DisplaySize.y - line_h * lines) * 0.5f;
    draw_outlined_line(version.c_str(), io.DisplaySize.x * 0.5f, top, kStatusAlpha);
    draw_outlined_line(data.c_str(), io.DisplaySize.x * 0.5f, top + line_h, kStatusAlpha);
    // Fully opaque where the other two are half: this one is the only line here that is asking
    // for something rather than reporting state.
    if (!news.empty())
        draw_outlined_line(news.c_str(), io.DisplaySize.x * 0.5f, top + line_h * 2, 1.0f);
    ImGui::PopFont();
    ImGui::End();
}

} // namespace

int App::run(bool relaunched_after_update) {
    // Before the log, not after: a second instance opening its own log file would start a new
    // one and prune the ten kept, so the run being diagnosed can be pushed out of the window by
    // a stray double-click. A rejected launch is not this application's session and writes
    // nothing at all.
    InstanceLock instance("PathOfPriceCheck");
    // Started by the copy it is replacing, which is still on its way out and still holding the
    // lock. Waiting is the whole difference between an update that lands and one that looks
    // like it broke the application: the replacement would otherwise report that the tool is
    // already running, and the tray icon it points at is the one that is about to disappear.
    for (int i = 0; relaunched_after_update && !instance.held() && i < 30; ++i) {
        SDL_Delay(100);
        instance = InstanceLock("PathOfPriceCheck");
    }
    if (!instance.held()) {
        // Said out loud, unlike a failed price check. That silence is a rule about the overlay,
        // which is noise over a game; this is a launch the user just performed on their desktop
        // and is owed an answer to, and the answer is that they already have what they asked
        // for — the tray icon is right there. Exit 0 for the same reason: the intended state
        // holds, so a desktop launcher has no error to report.
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Path of Price Check",
                                 "Path of Price Check is already running.\n\n"
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
    const uint32_t event_base = SDL_RegisterEvents(8);
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
    update_event_ = event_base + 6;
    report_event_ = event_base + 7;

    if (!overlay_.init("Path of Price Check Overlay")) {
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
    report_.init(report_event_);
    icons_.init();

    // Reclaim superseded bundles and map the installed one before anything else can hold a
    // mapping — on Windows a mapped directory cannot be removed.
    updater_.init(cache_dir() / "data", data_event_);
    ui::set_language(config_.ui_language, config_.client_language);
    updater_.set_language(config_.client_language);
    data_ = updater_.load_installed();
    check_for_data(); // background; the panel degrades gracefully until it lands

    app_updater_.init(cache_dir() / "update", update_event_);
    if (config_.auto_update) check_for_update();

    hotkeys_ = HotkeyListener::create([this](Action a) { on_hotkey(a); });
    rebind_hotkeys();

    // Transparent, always-on-top, click-through overlay. It stays hidden until the game
    // is detected, then maps once over it (see update_overlay_placement) — no per-frame
    // redraw while idle. It's override-redirect (unmanaged) so the compositor stacks it
    // over exclusive-fullscreen; the WM then won't focus it, so Settings claims keyboard
    // focus explicitly (see set_screen). PPC_MANAGED opts out for debugging.
    if (!std::getenv("PPC_MANAGED")) overlay_set_unmanaged(overlay_.window(), true);

    dev_mode_ = std::getenv("PPC_DEV_OVERLAY") != nullptr;
    anonymise_ = std::getenv("PPC_DEV_ANON") != nullptr;
    log_session_start(); // after dev_mode_, which changes what every gate below does
    if (dev_mode_) { // local UI dev, no game needed
        overlay_.set_visible(true);
        // PPC_DEV_ITEM=<file> opens the price-check panel on a captured clipboard instead,
        // which is the only way to iterate on it without the game running.
        if (std::getenv("PPC_DEV_IDLE")) {
            // The idle status marker, which otherwise only ever appears while the game is the
            // window in front. Laid out against the display, since there is no game to measure.
            place_overlay();
        } else if (std::getenv("PPC_DEV_PASTE")) {
            // The paste popup, at wherever the pointer happens to be — the only way to see it
            // without the game, since it is placed against a cursor the hotkey sampled.
            float mx = 0, my = 0;
            SDL_GetGlobalMouseState(&mx, &my);
            paste_x_ = static_cast<int>(mx);
            paste_y_ = static_cast<int>(my);
            set_screen(Screen::QuickPaste);
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
            // Per frame, and outside every screen: a screen that pushes its own window colour
            // pops the base value back on the way out, so a one-shot write from a checkbox
            // would not survive the frame it was made in.
            ui::set_opaque_windows(config_.reduce_transparency);
            if (screen_ == Screen::Settings)
                draw_settings_screen(*this);
            else if (screen_ == Screen::PriceCheck)
                draw_pricecheck_screen(*this);
            else if (screen_ == Screen::QuickPaste)
                draw_quickpaste_screen(*this);
            else if (screen_ == Screen::BugReport || screen_ == Screen::ReportSent)
                draw_report_screen(*this);
            else
                draw_status_marker(*this);
            overlay_.end_frame();
            need_redraw_ = false;
            // Between frames, because every part of this needs a frame boundary: the panel has
            // to be redrawn in its masked face before it can be read back, the read-back has to
            // be of a frame that is finished, and the resize the dialog brings must not land
            // inside one. See `open_bug_report`.
            if (report_opening_ == Opening::Masking) {
                overlay_.request_capture(); // of the next frame, which is the masked one
                report_opening_ = Opening::Capturing;
                need_redraw_ = true;
            } else if (report_opening_ == Opening::Capturing) {
                finish_bug_report();
            }
        }
    }

    if (tray_) SDL_DestroyTray(tray_);
    hotkeys_.reset();
    leagues_.shutdown(); // joins + drains its events; must precede SDL_Quit
    trade_.shutdown();
    ninja_.shutdown();
    currency_exchange_.shutdown();
    report_.shutdown();
    icons_.shutdown(); // frees GL textures, so before the context goes with the overlay
    updater_.shutdown();
    app_updater_.shutdown();
    // Last, and after the worker is joined: this is the moment a downloaded release becomes
    // the one that starts next time. On Windows it hands the job to the installer, which needs
    // this process gone — so nothing may be added below that expects to still be running.
    app_updater_.apply_on_exit();
    net::shutdown();
    overlay_.shutdown();
    SDL_Quit();
    return 0;
}

bool App::init_tray(SDL_Surface* icon) {
    tray_ = SDL_CreateTray(icon, "Path of Price Check");
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
    } else if (e.type == update_event_) {
        need_redraw_ = true;
    } else if (e.type == report_event_) {
        report_.on_done(e);
        // Only a send that landed closes the dialog. A refusal leaves it exactly as it was, with
        // the reason on it: the text the user wrote is in that window and nowhere else.
        if (report_.state() == ReportState::Sent && screen_ == Screen::BugReport) {
            drop_report_draft(); // the dialog is done with it, and it holds a GL texture
            set_screen(Screen::ReportSent);
        }
        need_redraw_ = true;
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
            switch (capture_which_) {
            case Action::PriceCheck: config_.price_check = Hotkey{m, name}; break;
            case Action::ToggleSettings: config_.settings = Hotkey{m, name}; break;
            case Action::QuickPaste: config_.quick_paste = Hotkey{m, name}; break;
            }
            end_capture();
        }
    } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
        // An open range editor takes Escape first. The key only reaches a price check because the
        // editor claimed the keyboard, and closing the whole check on it would throw away the row
        // the user was aiming at. ImGui closes its popup on the same press, so both agree.
        if (filter_edit_.open()) close_filter_edit();
        else if (screen_ == Screen::BugReport) close_bug_report();
        else if (screen_ == Screen::ReportSent) dismiss_report_result();
        else set_screen(Screen::Hidden);
    } else if (e.type == SDL_EVENT_KEY_DOWN && screen_ == Screen::QuickPaste) {
        // The whole reason the popup claims the keyboard. A slot nothing is in is not a miss to
        // report — the popup stays up and the mouse still works.
        const int slot = paste_slot_for(e.key.scancode);
        const std::vector<size_t> active = active_pastes(config_.pastes);
        if (slot >= 0 && static_cast<size_t>(slot) < active.size())
            pick_paste(active[static_cast<size_t>(slot)]);
    } else if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        had_focus_ = true;
        debug::log("[app]    overlay focus gained");
    } else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        debug::log("[app]    overlay focus lost (screen=%d, had_focus=%d)", (int)screen_,
                   (int)had_focus_);
        // Price-check and the paste popup auto-dismiss when you click back into the game;
        // Settings stays open until closed manually (its hotkey or the X button). had_focus_
        // avoids closing before the window has actually taken focus.
        if (!dev_mode_ && (screen_ == Screen::PriceCheck || screen_ == Screen::QuickPaste) &&
            had_focus_)
            set_screen(Screen::Hidden);
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
            // Wine exports within ~160ms of losing the active window or not at all, so past
            // that the game is being held out of the foreground for nothing.
            static constexpr uint64_t kHandoverHoldMs = 250;
            if (copy_deactivated_ && SDL_GetTicks() - copy_nudge_ms_ >= kHandoverHoldMs)
                restore_game_activation();
            return;
        }
        if (debug::enabled()) {
            // Asked once and used twice: this is a real conversion request to the owner, so it
            // belongs only here — on the path where there is nothing left to perturb.
            const std::string targets = clipboard_targets(100);
            debug::log("[copy] gave up after %llums: the clipboard was never written. %s owner=%s"
                       " targets=%s",
                       (unsigned long long)elapsed, focus_info().c_str(),
                       clipboard_owner_info().c_str(), targets.c_str());
            // On its own line: the line above says what was observed, this one says what it
            // means, and only the second is worth pasting into a report.
            const std::string why = clipboard_wedge_note(targets);
            if (!why.empty()) debug::log("[copy]   diagnosis: %s", why.c_str());
        }
        abandon_copy();
        return;
    }

    copy_pending_ = false;
    restore_game_activation(); // the copy landed, so the focus-out has done all it can
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

/// Give up on the copy in flight, leaving nothing on screen. Hands back whatever the handover
/// nudge took, so a failed check doesn't quietly leave the game out of the foreground or unable
/// to receive keys.
void App::abandon_copy() {
    copy_pending_ = false;
    restore_game_activation();
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
/// So give the game a focus-out. **The focus that counts is the window manager's, not the X
/// server's**: this used to call `overlay_take_keyboard_focus`, which moves the input focus onto
/// our override-redirect panel, and the log says plainly that it achieved nothing — `input=` moved
/// to us while `active=` stayed on the game, and Wine never re-exported. An override-redirect
/// window is one the WM does not manage and therefore can never make active, so that call could
/// not have worked whatever it did to the server. `deactivate_game_window` asks the WM instead,
/// with a throwaway pixel of a window as the thing to activate.
///
/// Deliberately **not** unconditional — a healthy clipboard (any Windows machine, a native X11
/// game) answers the first poll, and taking the game out of the foreground mid-fight for a copy
/// that was never late is a worse bug than the one being fixed. Hence: once per check, only past
/// the grace period, and only while the game is still the window in front — if the user has
/// already alt-tabbed away, the focus-out has happened and there is nothing to ask for.
void App::nudge_clipboard_handover(uint64_t elapsed) {
    // Three polls of the 100ms loop. Long enough that a clipboard which works never sees this.
    static constexpr uint64_t kGraceMs = 350;
    if (copy_nudged_ || elapsed < kGraceMs) return;
    copy_nudged_ = true;
    copy_nudge_ms_ = SDL_GetTicks();
    // No screen check: nothing is on screen during a check, by design. Guarding on
    // `screen_ == PriceCheck` is what silently disabled this after the panel stopped being
    // shown while the copy was in flight.
    if (dev_mode_ || !overlay_.visible()) return;
    if (overlay_.has_focus() || !foreground_title_contains(config_.poe_window_title)) {
        debug::log("[copy]   no handover after %llums, but the game no longer holds focus",
                   (unsigned long long)elapsed);
        return;
    }
    copy_deactivated_ = deactivate_game_window();
    debug::log("[copy]   no handover after %llums — asked the window manager to take the game"
               " off the active window (%s). %s",
               (unsigned long long)elapsed, copy_deactivated_ ? "asked" : "nothing to ask",
               focus_info().c_str());
}

/// Put the game back, and never leave it to the timeout: the whole point is a focus-out the
/// *user* did not ask for, so it has to be the shortest one that works. Called the moment the
/// clipboard moves, when the check is dropped, and on a hold timer for the case where neither
/// happens quickly — the measured export lands within 160ms of the game losing focus, so a copy
/// that has not arrived by then is not waiting on this.
void App::restore_game_activation() {
    if (!copy_deactivated_) return;
    copy_deactivated_ = false;
    activate_game_window(config_.poe_window_title);
    debug::log("[copy]   handed the game back after %llums. %s",
               (unsigned long long)(SDL_GetTicks() - copy_nudge_ms_), focus_info().c_str());
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
    // The editor addresses a row of the plan that is about to stop existing. Bounds belong to
    // the item in hand and nothing about them survives to the next one, and neither does
    // having opened the section the strategy's leftovers are behind.
    close_filter_edit();
    hidden_filters_shown_ = false;
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

/// Open the range editor on a row — and **claim the keyboard**, or its two boxes cannot be typed
/// into at all.
///
/// A price check is drawn on an override-redirect window, which the window manager will not focus,
/// so the X input focus stays on the game and every keystroke goes there. The boxes still *look*
/// live, because the caret follows the mouse and the pointer works regardless: clicking one
/// activates it and then nothing arrives. Settings has always claimed the focus for the same
/// reason, one line down.
///
/// This does not violate the focus rule. What it takes is the **server's** input focus and not the
/// window manager's activation — `active=` stays on the game, which is the whole point of the note
/// on `nudge_clipboard_handover` — and it is taken on a deliberate click on our own text field
/// rather than to make the game do something. It is not handed back when the editor closes: the
/// game regaining focus is what dismisses a price check, so returning it would close the panel out
/// from under the edit. `set_screen(Hidden)` gives it back when the check itself ends.
void App::edit_filter(FilterEdit::Kind kind, size_t index, float top, float bottom) {
    filter_edit_ = FilterEdit{kind, index, bottom, top, /*opening=*/true};
    if (!overlay_.has_focus()) take_keyboard();
    need_redraw_ = true;
}

void App::set_strategy(item::Strategy s) {
    strategy_override_ = s;
    close_filter_edit(); // a different strategy is a different set of rows
    hidden_filters_shown_ = false;
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
    close_filter_edit(); // and so are the rows: a different unique is a different mod pool
    hidden_filters_shown_ = false;
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
    // The paste popup dismisses the same way and by the same measurement — it is all panel and
    // no gutter, so the rectangle below is simply its whole window.
    if (screen_ == Screen::Hidden) {
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
    // Settings does not dismiss on a click away — it closes on its own X, its hotkey or
    // Escape. What a click *into* it means is that the user is coming back to it, possibly
    // from another application that took the keyboard with it, and this is the earliest
    // moment we can tell: the poll in `update_overlay_placement` is up to 400ms behind, and
    // in the meantime the dialog would swallow their first sentence.
    // Neither the report dialog nor its confirmation dismisses on a click away, for the reason
    // Settings does not and one of its own: the text in that window exists nowhere else, and
    // losing it to a stray click over the game would be losing the report.
    if (screen_ == Screen::Settings || screen_ == Screen::BugReport ||
        screen_ == Screen::ReportSent) {
        if (on_panel) reclaim_keyboard();
        return;
    }
    if (!on_panel && !on_card) set_screen(Screen::Hidden);
}

void App::check_for_data() {
    data_checked_ms_ = SDL_GetTicks();
    debug::log("[data]   check requested");
    updater_.start_check();
}

void App::check_for_update() {
    update_checked_ms_ = SDL_GetTicks();
    debug::log("[update] check requested");
    app_updater_.start_check();
}

void App::refresh_checks() {
    // Riding on the user's own hotkey rather than on a timer, so a copy left running overnight
    // asks for nothing and the requests only ever land beside something they were going to
    // notice anyway. Both workers are off the main thread, so this returns immediately.
    const uint64_t now = SDL_GetTicks();
    if (now - data_checked_ms_ >= kRecheckIntervalMs) check_for_data();
    // Nothing to gain from re-checking while a release is already staged or offered: the answer
    // would be the same one, and the check would take the notice down for the length of it.
    if (config_.auto_update && now - update_checked_ms_ >= kRecheckIntervalMs &&
        !app_updater_.status().has_news())
        check_for_update();
}

void App::handle_action(Action a) {
    // The hotkeys are grabbed system-wide, so they fire while the user is in a browser or a
    // terminal too. Gate every action on the game actually being in front rather than firing
    // into whatever they're really doing. Settings is the one exception: it holds the
    // keyboard focus itself, so the game can't be foreground while it's open, and its hotkey
    // still has to close it.
    const bool game_focused = foreground_title_contains(config_.poe_window_title);
    if (a == Action::PriceCheck) log_state("hotkey");
    // The exception is a hotkey closing the screen it opened. Both of those screens hold the
    // keyboard focus themselves, so the game *cannot* be foreground while one is up, and the
    // hotkey that opened it has to be able to take it away again.
    const bool closes_own_screen = (a == Action::ToggleSettings && screen_ == Screen::Settings) ||
                                   (a == Action::QuickPaste && screen_ == Screen::QuickPaste);
    if (!game_focused && !dev_mode_ && !closes_own_screen) {
        debug::trace("[copy] hotkey ignored: game not focused");
        return;
    }
    // Past the gate, so this is the user reaching for *this* application and not a hotkey that
    // fired into a browser. Whatever it finds is news for the next press, never for this one.
    refresh_checks();

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
        // A previous check's handover is always undone by now, but never inject into a game
        // this one left deactivated — and a second `deactivate_game_window` would leak the
        // window the first allocated.
        restore_game_activation();
        const uint64_t t0 = SDL_GetTicks();
        simulate_copy();
        copy_pending_ = true;
        copy_nudged_ = false;
        copy_poked_ms_ = 0; // poke on the first poll, not one interval in
        copy_started_ms_ = SDL_GetTicks();
        // The owner window id is worth recording here even though nothing can be *concluded*
        // from it yet: across the captures it is the one field that predicts the outcome, and
        // reading it costs no round trip. Diagnosing has to wait for the give-up line, because
        // the evidence that settles it — whether the owner answers at all — is a real
        // conversion request, and issuing one at injection would perturb the handover being
        // measured.
        if (debug::enabled())
            debug::log("[copy] injected in %llums, stamp=%llu owner=%s",
                       (unsigned long long)(copy_started_ms_ - t0),
                       (unsigned long long)copy_stamp_, clipboard_owner_info().c_str());
    } else if (a == Action::QuickPaste) {
        if (screen_ == Screen::QuickPaste) {
            set_screen(Screen::Hidden);
            return;
        }
        // A price check still waiting on the clipboard would read our own write as the item it
        // asked about — the stamp moves, the text is a paste, and it is dropped as "not an
        // item" several hundred milliseconds after the user has moved on to something else.
        if (copy_pending_) {
            debug::log("[paste]  dropping the copy in flight: the paste list writes the"
                       " clipboard itself");
            abandon_copy();
        }
        // Where the hand is now, not where it will be when the window is placed.
        float mx = 0, my = 0;
        SDL_GetGlobalMouseState(&mx, &my);
        paste_x_ = static_cast<int>(mx);
        paste_y_ = static_cast<int>(my);
        set_screen(Screen::QuickPaste);
    } else {
        set_screen(screen_ == Screen::Settings ? Screen::Hidden : Screen::Settings);
    }
}

/// Put a paste on the clipboard and close, which is the whole of what the popup does.
///
/// **Nothing presses Ctrl+V.** The paste happens where the user means it to, in their own field
/// and at their own time — an injected keystroke into a game window is a different promise from
/// the one this application makes about the copy path, and a mistimed one types into the chat
/// box of whatever had focus.
void App::pick_paste(size_t index) {
    if (index >= config_.pastes.size()) return;
    const Paste& p = config_.pastes[index];
    const bool ok = clipboard_set_text(p.body);
    debug::log("[paste]  picked '%s' (%zu bytes)%s", p.heading.c_str(), p.body.size(),
               ok ? "" : " \xe2\x80\x94 the clipboard would not take it");
    set_screen(Screen::Hidden); // which hands the focus back to the game
}

void App::open_paste_settings() {
    settings_tab_ = kQuickPasteTab;
    set_screen(Screen::Settings);
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
    // Somebody else is in front: a browser, a terminal, anything that is not the game and not
    // us. **Nothing of ours floats over it — Settings included.** That exemption used to be
    // justified by Settings holding the keyboard focus, so the game could never be foreground
    // while it was up; but it only holds while Settings still *has* the focus, and alt-tabbing
    // to look something up takes it away. What was left was a dialog painted over the browser
    // that could not be typed into, because the window manager will not focus an
    // override-redirect window and nothing asked it to again.
    //
    // **Our own window counts as the game being in front**, because from the user's side it
    // is: closing a panel that had taken the focus (a click on it, or the clipboard handover
    // nudge) leaves the focus on our now-empty overlay for as long as the compositor takes to
    // hand it back, and the marker used to blink out for exactly that gap.
    const bool elsewhere = g.present && !g.focused && !overlay_.has_focus();
    if (!g.present || elsewhere) {
        // With the game *gone* only the idle marker goes with it: a panel left open would have
        // no way back, since every hotkey that could reopen it is gated on the game being in
        // front. With the game merely behind something else, everything hides and comes back.
        if (overlay_.visible() && (elsewhere || screen_ == Screen::Hidden))
            overlay_.set_visible(false);
        if (!g.present) { // forget geometry so it re-places when the game comes back
            game_present_ = false;
            game_w_ = game_h_ = 0;
        }
        return;
    }

    bool moved = g.x != game_x_ || g.y != game_y_ || g.w != game_w_ || g.h != game_h_;
    if (game_present_ && overlay_.visible() && !moved) {
        // Back from another application without the window having moved: still owed the
        // keyboard, since it was lost to whatever was in front and no window manager will hand
        // it to a window it does not manage.
        reclaim_keyboard();
        return; // already placed, nothing changed
    }
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
    if (screen_ != Screen::Hidden) {
        SDL_RaiseWindow(overlay_.window());
        reclaim_keyboard();
    }
    need_redraw_ = true; // first placement / a move: repaint once
}

/// Take the keyboard back for a screen that cannot work without it.
///
/// Claiming it once, when the screen opens, is not enough: alt-tab to a browser and the focus
/// goes with it, and **nothing ever gives it back** — the window manager will not focus an
/// override-redirect window, so returning to the game leaves Settings on screen, apparently
/// live, swallowing every keystroke. Reported from a session where looking a regex up in a
/// browser cost the whole dialog. So it is re-claimed whenever the game is in front again,
/// which is the same condition that puts the window back on screen.
///
/// Only the two screens that are *about* the keyboard, and only when we do not already hold it:
/// a price check takes it on demand (`edit_filter`) and must not take it otherwise, or the
/// focus-loss that dismisses it could never happen.
void App::reclaim_keyboard() {
    if (overlay_.has_focus()) return;
    if (screen_ != Screen::Settings && screen_ != Screen::QuickPaste &&
        screen_ != Screen::BugReport)
        return;
    debug::log("[app]    reclaiming the keyboard for screen %d", (int)screen_);
    take_keyboard();
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

    if (screen_ == Screen::BugReport || screen_ == Screen::ReportSent) {
        const bool notice = screen_ == Screen::ReportSent;
        const int w = std::min(notice ? kNoticeW : kReportW, gw);
        const int h = std::min(notice ? kNoticeH : kReportH, gh);
        SDL_SetWindowSize(overlay_.window(), w, h);
        SDL_SetWindowPosition(overlay_.window(), gx + (gw - w) / 2, gy + (gh - h) / 2);
        layout_ = PanelLayout{0, float(w), 0, 0};
        return;
    }

    if (screen_ == Screen::Settings) {
        const int sh = std::min(kSettingsH, gh);
        SDL_SetWindowSize(overlay_.window(), kSettingsW, sh);
        SDL_SetWindowPosition(overlay_.window(), gx + (gw - kSettingsW) / 2, gy + (gh - sh) / 2);
        layout_ = PanelLayout{0, kSettingsW, 0, 0};
        return;
    }

    if (screen_ == Screen::QuickPaste) {
        int w = 0, h = 0;
        quickpaste_size(active_pastes(config_.pastes).size(), &w, &h);
        // Right of the cursor and starting at it, which is where a menu opens — then clamped
        // into the game window, which is what turns "downwards" into "upwards" near the bottom
        // edge and into somewhere between the two in the middle. No decision of its own: a rule
        // that picks a direction and a clamp that has to override it would disagree in the
        // cases that matter.
        constexpr int kCursorGap = 14;
        int x = paste_x_ + kCursorGap;
        if (x + w > gx + gw) x = paste_x_ - kCursorGap - w; // no room on the right: open left
        // max(min()), not clamp: a game window narrower or shorter than the popup puts the low
        // bound above the high one, which clamp is not defined for.
        x = std::max(gx, std::min(x, gx + gw - w));
        const int y = std::max(gy, std::min(paste_y_ - kCursorGap, gy + gh - h));
        SDL_SetWindowSize(overlay_.window(), w, h);
        SDL_SetWindowPosition(overlay_.window(), x, y);
        layout_ = PanelLayout{0, float(w), 0, 0};
        debug::log("[paste]  placed %dx%d+%d+%d for a cursor at %d,%d", w, h, x, y, paste_x_,
                   paste_y_);
        return;
    }

    if (screen_ == Screen::Hidden) {
        // Over the lower half of the mana globe, which hangs off the bottom-right corner and
        // scales with the game's height — so both offsets are fractions of that height, exactly
        // like the two frame edges. The window is no bigger than the text: it is click-through
        // while idle, but it is also what the compositor has to composite every time the game
        // redraws.
        const int cx = gx + gw - static_cast<int>(gh * config_.status_right);
        const int cy = gy + gh - static_cast<int>(gh * config_.status_bottom);
        // Room for the update line when there is one, and not a pixel of it otherwise: this
        // window is what the compositor redraws every time the game does.
        const int mh = (app_updater_.status().has_news() && !update_dismissed_) ? kStatusUpdateH
                                                                               : kStatusH;
        SDL_SetWindowSize(overlay_.window(), kStatusW, mh);
        // max(min()), not clamp: a game window narrower than the marker puts the low bound above
        // the high one, and clamp is not defined for that.
        SDL_SetWindowPosition(overlay_.window(),
                              std::max(gx, std::min(cx - kStatusW / 2, gx + gw - kStatusW)),
                              std::max(gy, std::min(cy - mh / 2, gy + gh - mh)));
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
    debug::log("[state]  hotkeys: price check %s, settings %s, paste list %s (%zu of %zu"
               " enabled); league '%s'; window title '%s'",
               to_string(config_.price_check).c_str(), to_string(config_.settings).c_str(),
               to_string(config_.quick_paste).c_str(), enabled_pastes(config_.pastes),
               config_.pastes.size(), config_.league.c_str(), config_.poe_window_title.c_str());
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

// The panel is mid-draw when this is called — the button that calls it is on it — so nothing here
// may resize the window, change the screen, or read anything back. All of it waits, and what waits
// is two frames rather than one:
//
//   press → [this frame finishes as it was] → **masked frame, read back** → dialog
//
// The middle frame is the whole reason for the state machine. It is the panel drawn as it will be
// photographed — sellers' account names replaced, tooltips silent — and a picture can only be of a
// frame that was actually rendered. Drawing it takes about sixteen milliseconds and it is on
// screen for one frame before the dialog covers it, which is not a flicker anyone has reported
// seeing; masking on the press frame instead would work only for as long as the action bar keeps
// being drawn before the results table, which is not a thing this file can promise.
void App::open_bug_report() {
    if (report_opening_ != Opening::No || screen_ != Screen::PriceCheck) return;
    report_opening_ = Opening::Masking;
    need_redraw_ = true;
}

// Everything the report will say, decided here and not touched again. What the dialog shows is
// what it sends, which is a promise it cannot keep if the payload goes on being rebuilt behind it
// — the updater can swap a bundle in and a search can land while the user is still typing.
void App::finish_bug_report() {
    report_opening_ = Opening::No;
    drop_report_draft();
    report_draft_.shot = overlay_.take_capture();
    report_draft_.shot_tex = overlay_.upload_texture(report_draft_.shot);
    // The clipboard capture verbatim, which is the one input every parse bug is against.
    report_draft_.payload.item = clipboard_;
    if (item_) report_draft_.payload.parse = report::describe(*item_, derived_, plan_);
    report_draft_.payload.meta.version = APP_VERSION;
    report_draft_.payload.meta.os = SDL_GetPlatform();
    report_draft_.payload.meta.league = config_.league;
    // The bundle the item was *resolved against*, not whichever is current: a mispricing is as
    // often the data's as the code's, and the two can already differ by this point.
    if (item_data_) report_draft_.payload.meta.bundle = std::string(item_data_->data_version());
    report_.reset();
    debug::log("[report] opening the dialog: %dx%d capture, %zu byte parse dump",
               report_draft_.shot.w, report_draft_.shot.h, report_draft_.payload.parse.size());
    set_screen(Screen::BugReport);
}

// The draft holds a GL texture, so it is dropped through here rather than assigned over.
void App::drop_report_draft() {
    overlay_.free_texture(report_draft_.shot_tex);
    report_draft_ = ReportDraft{};
}

void App::send_bug_report() {
    if (report_.state() == ReportState::Sending) return;
    report::Report r = report_draft_.payload;
    r.comment = report_draft_.comment;
    report_.send(std::move(r), report_draft_.attach ? report_draft_.shot : Capture{});
    need_redraw_ = true;
}

void App::close_bug_report() {
    drop_report_draft();
    report_.reset();
    // Back to the check it was about rather than away entirely: the report was opened from a
    // panel that is still the answer to the question the user asked.
    set_screen(Screen::PriceCheck);
}

// Both answers the send can leave behind, because clearing it is the same act either way: a
// refusal is dismissed back into the dialog it never left, and a success is dismissed off screen
// because the dialog it belonged to is already gone.
void App::dismiss_report_result() {
    report_.reset();
    if (screen_ == Screen::ReportSent) set_screen(Screen::Hidden);
    need_redraw_ = true;
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
    // Settings needs keyboard focus immediately (text fields); a price check takes it only when
    // something on it has to be typed into (edit_filter) or the copy stalls
    // (nudge_clipboard_handover). Closing hands focus back to the game.
    if (s == Screen::QuickPaste) {
        // The number keys are the feature; without the keyboard the popup is a menu you have to
        // aim at. This is the server's input focus and not the window manager's activation —
        // the same thing the range editor takes, and the same reason it is not a violation of
        // the rule about the game's foreground.
        take_keyboard();
    } else if (s == Screen::BugReport) {
        // The dialog is mostly a box to type in, and without the keyboard it is a preview with
        // an unusable comment field.
        take_keyboard();
    } else if (s == Screen::Settings) {
        take_keyboard();
        // TTL-gated, so a warm cache makes this a no-op. A user who never opens Settings
        // never makes a network request at all.
        leagues_.refresh(false);
    } else if (s == Screen::Hidden) {
        give_keyboard_back();
    }
    need_redraw_ = true;
}

/// Claim the X input focus for our own window, and **remember that we did**.
void App::take_keyboard() {
    took_keyboard_ = true;
    overlay_take_keyboard_focus(overlay_.window());
}

/// Hand the keyboard back to the game — but only focus we took, and only if the game is still
/// the window the *user* is in.
///
/// Two conditions rather than one, and the second is not redundant. `overlay_.has_focus()` is
/// SDL's view, which lags the `XSetInputFocus` we just made by however long the round trip takes
/// — so a popup dismissed briskly (pick a paste the moment it opens) could reach here before SDL
/// had registered the focus we ourselves claimed, and the game would be left without it. That is
/// not cosmetic on this path: it is the focus change that makes Wine re-read the X selection, so
/// skipping it is a paste of the previous clipboard.
///
/// `took_keyboard_` is our own record of an action we performed, and the foreground check is
/// what keeps it honest — if the user has alt-tabbed to a browser meanwhile, the focus is theirs
/// to place and pulling it onto the game would be exactly the theft the focus rule forbids.
void App::give_keyboard_back() {
    const bool ours = overlay_.has_focus() ||
                      (took_keyboard_ && foreground_title_contains(config_.poe_window_title));
    took_keyboard_ = false;
    if (ours) focus_game_window(config_.poe_window_title);
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
    hotkeys_->rebind({{config_.price_check, Action::PriceCheck},
                      {config_.settings, Action::ToggleSettings},
                      {config_.quick_paste, Action::QuickPaste}});
}

} // namespace ppc
