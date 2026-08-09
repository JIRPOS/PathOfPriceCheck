#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "update/release.hpp"

namespace ppc::update {

/// Checks for a newer release, downloads and verifies it off the main thread, and applies it
/// only when the user asks.
///
/// Nothing here ever restarts the application on its own. The whole point of the design is that
/// a price-check overlay must not close over a running game: the download happens quietly, the
/// notice is passive, and the swap waits for **Restart now** or for the next launch.
///
/// The GitHub host this talks to is not GGG's, so it goes nowhere near `trade::request` — that
/// limiter exists for the trade API and sharing it here would only slow both down.
class Updater {
public:
    enum class State {
        Idle,
        Checking,
        Downloading,
        Verifying,
        UpToDate,    ///< nothing newer is published
        Ready,       ///< verified and staged; waiting for a restart
        Offer,       ///< newer exists but cannot be applied here — see `Status::notes_url`
        Failed,
    };

    struct Status {
        State state = State::Idle;
        std::string available;  ///< the newer version, when there is one
        std::string notes_url;  ///< where to send someone the app cannot update for them
        uint64_t bytes_done = 0, bytes_total = 0;
        std::string error;      ///< diagnostic; never shown over the game
        Flavour flavour = Flavour::Unknown;

        /// True when there is something for the user to act on. The three notice surfaces all
        /// gate on this rather than on the state, so they cannot disagree about it.
        bool has_news() const { return state == State::Ready || state == State::Offer; }
    };

    Updater() = default;
    ~Updater();

    /// Call once after SDL_RegisterEvents. Clears the `.old` executable a previous swap left
    /// behind; applies nothing.
    void init(std::filesystem::path cache_root, uint32_t sdl_event_type);

    /// Applies a staged update, without starting anything. Call on the way out, after the
    /// worker has been joined — an update applied at startup would only take effect on the
    /// start after that one.
    void apply_on_exit();

    void start_check(); ///< no-op while a worker is running
    void cancel();
    void shutdown();    ///< cancel + join; before SDL_Quit

    Status status() const;

    /// Applies the staged update and starts the replacement, for the user to call from the
    /// Settings row. Returns true when the caller should now quit — and it must actually quit,
    /// because on Windows the installer is already running and waiting to replace this .exe.
    ///
    /// False means nothing was started and nothing was changed.
    bool restart_now();

private:
    void run();
    void set_state(State s);
    void publish();

    /// Where a verified download waits between the check and the restart. One file, replaced
    /// each time, so a check that supersedes an earlier one cannot leave two behind.
    std::filesystem::path staged_path() const;

    /// The staged installer, renamed out of `staged` at the moment it is handed over. The
    /// rename is what marks it consumed — a swap consumes the file by definition, but an
    /// installer runs *from* it, so without this every later exit would install it again.
    /// Cleared at the next `init()`, by which point nothing is running from it.
    std::filesystem::path installer_path() const;

    /// Renames `staged` aside and starts it. Windows only; see `installer_path()`.
    bool hand_over_to_installer(bool relaunch);

    std::filesystem::path root_;
    std::filesystem::path target_; ///< the executable to replace: our own path, or $APPIMAGE
    Flavour flavour_ = Flavour::Unknown;
    uint32_t event_ = 0;
    std::thread worker_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancel_{false};

    mutable std::mutex mu_;
    Status status_;
};

std::string_view to_string(Updater::State s);

} // namespace ppc::update
