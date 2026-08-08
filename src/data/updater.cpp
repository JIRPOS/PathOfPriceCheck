#include "data/updater.hpp"

#include <SDL3/SDL.h>

#include "data/install.hpp"
#include "data/manifest.hpp"
#include "net/http.hpp"

namespace fs = std::filesystem;

namespace ppc::data {

std::string_view to_string(DataUpdater::State s) {
    switch (s) {
    case DataUpdater::State::Idle: return "idle";
    case DataUpdater::State::Checking: return "checking";
    case DataUpdater::State::Downloading: return "downloading";
    case DataUpdater::State::Verifying: return "verifying";
    case DataUpdater::State::Installing: return "installing";
    case DataUpdater::State::UpToDate: return "up to date";
    case DataUpdater::State::Failed: return "failed";
    }
    return "?";
}

DataUpdater::~DataUpdater() { shutdown(); }

void DataUpdater::init(fs::path cache_root, uint32_t sdl_event_type) {
    root_ = std::move(cache_root);
    event_ = sdl_event_type;
}

std::shared_ptr<GameData> DataUpdater::load_installed() {
    BundleStore store(root_);
    std::string removed;
    // Safe here and only here: nothing has mapped a bundle yet, which is what lets the
    // superseded directory be deleted on Windows.
    store.prune(&removed);
    if (!removed.empty()) SDL_Log("data: reclaimed old bundle(s): %s", removed.c_str());

    const fs::path dir = store.current_dir();
    if (dir.empty()) return nullptr;

    std::string err;
    auto gd = GameData::open(dir, language_, &err);
    if (!gd) {
        SDL_Log("data: installed bundle is unusable (%s); will re-download", err.c_str());
        return nullptr;
    }
    std::lock_guard lock(mu_);
    status_.data_version = std::string(gd->data_version());
    return gd;
}

void DataUpdater::set_state(State s) {
    {
        std::lock_guard lock(mu_);
        status_.state = s;
    }
    publish();
}

void DataUpdater::publish() {
    if (!event_) return;
    // SDL_PushEvent is thread-safe and wakes SDL_WaitEventTimeout, so the main loop keeps
    // its 16/250ms cadence and idle still means idle.
    SDL_Event e{};
    e.type = event_;
    SDL_PushEvent(&e);
}

DataUpdater::Status DataUpdater::status() const {
    std::lock_guard lock(mu_);
    return status_;
}

std::shared_ptr<GameData> DataUpdater::take_ready_bundle() {
    std::lock_guard lock(mu_);
    return std::move(ready_);
}

void DataUpdater::start_check() {
    if (busy_.exchange(true)) return;
    if (worker_.joinable()) worker_.join(); // reap the previous, finished worker
    cancel_ = false;
    {
        std::lock_guard lock(mu_);
        status_.error.clear();
        status_.files_done = status_.files_total = 0;
        status_.bytes_done = status_.bytes_total = 0;
    }
    worker_ = std::thread([this] { run(); });
}

void DataUpdater::cancel() { cancel_ = true; }

void DataUpdater::shutdown() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    event_ = 0; // the destructor must not push into a torn-down SDL
}

void DataUpdater::run() {
    const auto fail = [this](std::string msg) {
        std::lock_guard lock(mu_);
        status_.state = State::Failed;
        status_.error = std::move(msg);
    };

    set_state(State::Checking);

    net::Request req;
    req.url = kManifestUrl;
    req.timeout_ms = 20000;
    net::Response resp = net::get(req);
    if (!resp.ok()) {
        fail(!resp.error.empty() ? resp.error : ("HTTP " + std::to_string(resp.status)));
        busy_ = false;
        publish();
        return;
    }

    Manifest m;
    std::string err;
    if (!parse_manifest(resp.body, m, &err)) {
        fail(err);
        busy_ = false;
        publish();
        return;
    }

    BundleStore store(root_);
    // Compared for inequality, never ordering: that is how a bad release is rolled back by
    // publishing an older-looking version.
    if (store.current_version() == m.data_version) {
        {
            std::lock_guard lock(mu_);
            status_.state = State::UpToDate;
            status_.data_version = m.data_version;
            status_.last_check_ms = SDL_GetTicks();
        }
        busy_ = false;
        publish();
        return;
    }

    {
        std::lock_guard lock(mu_);
        status_.state = State::Downloading;
        status_.files_total = static_cast<int>(m.files.size());
        status_.bytes_total = m.total_bytes();
    }
    publish();

    for (const ManifestFile& f : m.files) {
        if (cancel_) {
            fail("cancelled");
            busy_ = false;
            publish();
            return;
        }
        net::Request fr;
        fr.url = f.url;
        fr.timeout_ms = 120000;
        const uint64_t before = status().bytes_done;
        fr.on_progress = [this, before](uint64_t done, uint64_t) {
            {
                std::lock_guard lock(mu_);
                status_.bytes_done = before + done;
            }
            return !cancel_.load();
        };
        net::Response fres = net::get(fr);
        if (!fres.ok()) {
            fail(f.name + ": " + (!fres.error.empty() ? fres.error
                                                      : "HTTP " + std::to_string(fres.status)));
            busy_ = false;
            publish();
            return;
        }
        if (!store.stage(m.data_version, f, fres.body, &err)) {
            fail(err);
            busy_ = false;
            publish();
            return;
        }
        {
            std::lock_guard lock(mu_);
            ++status_.files_done;
            status_.bytes_done = before + fres.body.size();
        }
        publish();
    }

    set_state(State::Installing);
    if (!store.commit(m, &err)) {
        fail(err);
        busy_ = false;
        publish();
        return;
    }

    // Map on this thread, so a corrupt bundle fails here rather than inside a frame.
    auto gd = GameData::open(store.version_dir(m.data_version), language_, &err);
    if (!gd) {
        fail("installed but unusable: " + err);
        busy_ = false;
        publish();
        return;
    }

    {
        std::lock_guard lock(mu_);
        ready_ = std::move(gd);
        status_.state = State::UpToDate;
        status_.data_version = m.data_version;
        status_.last_check_ms = SDL_GetTicks();
    }
    busy_ = false;
    publish();
}

} // namespace ppc::data
