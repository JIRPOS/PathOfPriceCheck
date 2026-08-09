#include "update/updater.hpp"

#include <cstdlib>
#include <fstream>

#include <SDL3/SDL.h>

#include "net/http.hpp"
#include "paths.hpp"
#include "platform/process.hpp"
#include "update/install.hpp"
#include "util/debug_log.hpp"
#include "util/sha256.hpp"

namespace fs = std::filesystem;

namespace ppc::update {
namespace {

/// The switch the relaunched copy is started with. It means "the process you are replacing may
/// still be exiting", and all it does is make the single-instance claim patient — see
/// `App::run`. Without it the new copy loses the race against the old one's mutex and reports
/// that the application is already running, which after an update reads as a broken update.
constexpr const char* kRelaunchFlag = "--updated";

} // namespace

std::string_view to_string(Updater::State s) {
    switch (s) {
    case Updater::State::Idle: return "idle";
    case Updater::State::Checking: return "checking";
    case Updater::State::Downloading: return "downloading";
    case Updater::State::Verifying: return "verifying";
    case Updater::State::UpToDate: return "up to date";
    case Updater::State::Ready: return "ready";
    case Updater::State::Offer: return "offered";
    case Updater::State::Failed: return "failed";
    }
    return "?";
}

Updater::~Updater() { shutdown(); }

fs::path Updater::staged_path() const { return root_ / "staged"; }
fs::path Updater::installer_path() const { return root_ / "installer.exe"; }

bool Updater::hand_over_to_installer(bool relaunch) {
    std::error_code ec;
    fs::remove(installer_path(), ec);
    fs::rename(staged_path(), installer_path(), ec);
    if (ec) {
        debug::log("[update] cannot hand the installer over: %s", ec.message().c_str());
        return false;
    }
    std::vector<std::string> args{"/VERYSILENT", "/NORESTART"};
    // Only when the user pressed Restart now. An update applied because the application is
    // closing must not put it back up: that would be the app deciding to run.
    if (relaunch) args.emplace_back("/LAUNCH");
    return spawn_detached(installer_path(), args);
}

void Updater::init(fs::path cache_root, uint32_t sdl_event_type) {
    root_ = std::move(cache_root);
    event_ = sdl_event_type;
    flavour_ = detect_flavour();

    // $APPIMAGE, not /proc/self/exe: inside a mounted AppImage the latter points at the
    // extracted binary in a temporary mount that vanishes on exit. The file to replace is the
    // .AppImage itself, at the path desktop integration knows.
    if (const char* img = std::getenv("APPIMAGE"); flavour_ == Flavour::AppImage && img && *img)
        target_ = fs::path(img);
    else
        target_ = exe_path();

    {
        std::lock_guard lock(mu_);
        status_.flavour = flavour_;
    }

    sweep_old(target_);
    ensure_dir(root_);
    // The installer handed over last time, which has long since finished. Nothing is running
    // from it by now, so this is the moment it can go.
    std::error_code ec;
    fs::remove(installer_path(), ec);
}

void Updater::apply_on_exit() {
    std::error_code ec;
    if (!fs::is_regular_file(staged_path(), ec)) return;

    // On the way out, not on the way in. Applied at startup instead, the swap would land on
    // disk while this process runs the *old* image, so the update would need a second restart
    // to take — which is not what "applied on the next start" promises anyone.
    switch (method_for(flavour_)) {
    case Method::Swap: {
        std::string err;
        if (apply_swap(staged_path(), target_, &err))
            SDL_Log("update: applied on exit; the next start runs it");
        else
            SDL_Log("update: could not apply on exit (%s)", err.c_str());
        break;
    }
    case Method::RunInstaller:
        // No relaunch: the user closed the application, and putting it back up because an update
        // happened to be waiting would be the app deciding to run.
        hand_over_to_installer(false);
        break;
    case Method::None: break;
    }
}

void Updater::set_state(State s) {
    {
        std::lock_guard lock(mu_);
        status_.state = s;
    }
    publish();
}

void Updater::publish() {
    if (!event_) return;
    SDL_Event e{};
    e.type = event_;
    SDL_PushEvent(&e);
}

Updater::Status Updater::status() const {
    std::lock_guard lock(mu_);
    return status_;
}

void Updater::start_check() {
    if (busy_.exchange(true)) return;
    if (worker_.joinable()) worker_.join();
    cancel_ = false;
    {
        std::lock_guard lock(mu_);
        status_.error.clear();
        status_.bytes_done = status_.bytes_total = 0;
    }
    worker_ = std::thread([this] { run(); });
}

void Updater::cancel() { cancel_ = true; }

void Updater::shutdown() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    event_ = 0;
}

void Updater::run() {
    // Nothing here reaches the user: an update check that failed is not worth a word over a
    // running game, and the log is where the detail belongs.
    const auto fail = [this](std::string msg) {
        debug::log("[update] %s", msg.c_str());
        {
            std::lock_guard lock(mu_);
            status_.state = State::Failed;
            status_.error = std::move(msg);
        }
        busy_ = false;
        publish();
    };

    set_state(State::Checking);

    net::Request req;
    // Dev-only, like PPC_DEV_ITEM: until a release publishes latest.json there is no way to
    // reach any state past "failed", and the three notice surfaces cannot be looked at.
    if (const char* dev = std::getenv("PPC_DEV_UPDATE_URL"); dev && *dev)
        req.url = dev;
    else
        req.url = kLatestUrl;
    req.timeout_ms = 20000;
    const net::Response resp = net::get(req);
    if (!resp.ok()) {
        // Includes the 404 every release published before this feature existed answers with.
        // Silent, like every other failure here: an overlay narrating its own plumbing is noise
        // over a game, and the debug log has the detail.
        fail(!resp.error.empty() ? resp.error : ("HTTP " + std::to_string(resp.status)));
        return;
    }

    Release rel;
    std::string err;
    if (!parse_release(resp.body, rel, &err)) return fail(err);

    // Strictly newer, never merely different: unlike the data bundle, which is rolled back by
    // publishing an older version, a binary downgrade would fight the release the user chose.
    if (!(running_version() < rel.version)) {
        debug::log("[update] running %s, published %s: nothing to do", APP_VERSION,
                   rel.version.str().c_str());
        {
            std::lock_guard lock(mu_);
            status_.state = State::UpToDate;
            status_.available.clear();
        }
        busy_ = false;
        publish();
        return;
    }

    {
        std::lock_guard lock(mu_);
        status_.available = rel.version.str();
        status_.notes_url = rel.notes_url;
    }

    const Asset* asset = pick_asset(rel, flavour_);
    const Method method = method_for(flavour_);
    // Three ways to have news we cannot act on, and they are one answer to the user: the
    // release page. An unknown flavour is a distribution package or a build tree; an unwritable
    // directory is a .zip unpacked into Program Files; a missing asset is a release that did
    // not publish one for this platform.
    if (method == Method::None || !asset || !install_dir_writable()) {
        debug::log("[update] %s offered, not applied: flavour=%s asset=%s writable=%d",
                   rel.version.str().c_str(), std::string(to_string(flavour_)).c_str(),
                   asset ? asset->name.c_str() : "none", install_dir_writable() ? 1 : 0);
        {
            std::lock_guard lock(mu_);
            status_.state = State::Offer;
        }
        busy_ = false;
        publish();
        return;
    }

    {
        std::lock_guard lock(mu_);
        status_.state = State::Downloading;
        status_.bytes_total = asset->size;
    }
    publish();

    // Straight to disk through the streaming sink rather than into a string: an installer is
    // tens of megabytes and there is no reason for it to sit in the heap of a program whose
    // whole job is to draw a small panel.
    const fs::path tmp = staged_path().string() + ".part";
    std::error_code ec;
    fs::remove(tmp, ec);
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) return fail("cannot write to " + tmp.string());

        Sha256 hash;
        net::Request dr;
        dr.url = asset->url;
        dr.timeout_ms = 600000; // an installer over a slow line; cancel_ is the real bound
        dr.on_progress = [this](uint64_t done, uint64_t) {
            {
                std::lock_guard lock(mu_);
                status_.bytes_done = done;
            }
            return !cancel_.load();
        };
        dr.on_body = [&](const char* data, size_t n) {
            if (cancel_) return false;
            hash.update(data, n);
            out.write(data, static_cast<std::streamsize>(n));
            return out.good();
        };
        const net::Response dres = net::get(dr);
        out.close();

        if (!dres.ok()) {
            fs::remove(tmp, ec);
            return fail(!dres.error.empty() ? dres.error
                                            : ("HTTP " + std::to_string(dres.status)));
        }

        set_state(State::Verifying);
        const std::string got = hash.hex();
        // Case-insensitively, because the digest is ours to publish and a future generator
        // that upper-cases it should not brick the updater.
        std::string want = asset->sha256;
        for (char& c : want)
            if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
        if (got != want) {
            fs::remove(tmp, ec);
            return fail("checksum mismatch on " + asset->name);
        }
    }

    // Rename into place last, so `staged` never names a partial file — the same rule the data
    // bundle's install follows, for the same reason.
    fs::remove(staged_path(), ec);
    fs::rename(tmp, staged_path(), ec);
    if (ec) {
        fs::remove(tmp, ec);
        return fail("cannot stage the download: " + ec.message());
    }

    debug::log("[update] %s staged and verified; waiting for a restart",
               rel.version.str().c_str());
    {
        std::lock_guard lock(mu_);
        status_.state = State::Ready;
    }
    busy_ = false;
    publish();
}

bool Updater::restart_now() {
    if (status().state != State::Ready) return false;
    std::error_code ec;
    if (!fs::is_regular_file(staged_path(), ec)) return false;

    switch (method_for(flavour_)) {
    case Method::RunInstaller:
        // The installer replaces the .exe we are executing, so it has to outlive us: it is
        // started here and closes what is left of this process itself (CloseApplications), then
        // its own [Run] entry brings the application back. Which means the caller *must* quit.
        return hand_over_to_installer(true);
    case Method::Swap: {
        std::string err;
        if (!apply_swap(staged_path(), target_, &err)) {
            std::lock_guard lock(mu_);
            status_.state = State::Failed;
            status_.error = err;
            return false;
        }
        // The replacement claims the single-instance lock this process still holds, so it is
        // told to wait for it rather than to give up.
        if (!spawn_detached(target_, {kRelaunchFlag})) return false;
        return true;
    }
    case Method::None: break;
    }
    return false;
}

} // namespace ppc::update
