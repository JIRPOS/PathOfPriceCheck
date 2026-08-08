#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "data/game_data.hpp"

union SDL_Event;

namespace ppc::data {

/// Where the client looks for the current bundle. A fixed release-asset URL, deliberately
/// not the GitHub API: unauthenticated api.github.com allows 60 requests an hour per IP,
/// which a shared address or a restart loop can exhaust. This path is plain CDN.
inline constexpr const char* kManifestUrl =
    "https://github.com/JIRPOS/PathOfPriceCheck-Data/releases/latest/download/manifest.json";

/// Downloads, verifies and installs the game-data bundle off the main thread.
///
/// Nothing is baked into the binary, so a first run with no network has no pricing data at
/// all until one appears — the price-check panel says so rather than pretending.
class DataUpdater {
public:
    enum class State { Idle, Checking, Downloading, Verifying, Installing, UpToDate, Failed };

    struct Status {
        State state = State::Idle;
        int files_done = 0, files_total = 0;
        uint64_t bytes_done = 0, bytes_total = 0;
        std::string data_version;
        std::string error; ///< shown to the user verbatim
        uint64_t last_check_ms = 0;
    };

    DataUpdater() = default;
    ~DataUpdater();

    /// Call once after SDL_RegisterEvents, before anything else here.
    void init(std::filesystem::path cache_root, uint32_t sdl_event_type);

    /// Removes staging leftovers and superseded bundles, then maps the installed one.
    /// Which language's assets a bundle is opened with — `Config::client_language`. Set
    /// before `load_installed()`; a bundle carrying no such language fails to open and is
    /// reported exactly as a corrupt one is, which is the honest answer.
    void set_language(std::string lang) { language_ = std::move(lang); }

    /// Call once at startup, before anything holds a mapping. Null if nothing is installed.
    std::shared_ptr<GameData> load_installed();

    void start_check();  ///< no-op while a worker is running
    void cancel();       ///< asks the worker to stop at the next chunk boundary
    void shutdown();     ///< cancel + join; before SDL_Quit

    Status status() const;
    /// Non-null exactly once per successful install; the caller takes ownership of the swap.
    std::shared_ptr<GameData> take_ready_bundle();

private:
    std::string language_ = "en";
    void run();
    void set_state(State s);
    void publish(); ///< wakes the main loop so the UI repaints

    std::filesystem::path root_;
    uint32_t event_ = 0;
    std::thread worker_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancel_{false};

    mutable std::mutex mu_;
    Status status_;
    std::shared_ptr<GameData> ready_;
};

std::string_view to_string(DataUpdater::State s);

} // namespace ppc::data
