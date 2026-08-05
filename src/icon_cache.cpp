#include "icon_cache.hpp"

#include <fstream>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "net/http.hpp"
#include "paths.hpp"
#include "util/debug_log.hpp"
#include "util/sha256.hpp"

namespace fs = std::filesystem;

namespace ppc {
namespace {

/// The CDN's image paths are base64 blobs with slashes in them, so the URL is not a file
/// name. Its digest is, and it is stable across runs — which is the whole point of the
/// disk cache: these never change, so a second launch should make no requests at all.
fs::path cache_path(const std::string& url) {
    return cache_dir() / "icons" / (sha256_hex(url) + ".png");
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// RGBA32 whatever the PNG was, so `pump()` has one upload path.
SDL_Surface* decode(const std::string& png) {
    SDL_IOStream* io = SDL_IOFromConstMem(png.data(), png.size());
    if (!io) return nullptr;
    SDL_Surface* s = SDL_LoadPNG_IO(io, true);
    if (!s) return nullptr;
    if (s->format == SDL_PIXELFORMAT_RGBA32) return s;
    SDL_Surface* conv = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    return conv;
}

} // namespace

IconCache::~IconCache() { shutdown(); }

void IconCache::init() {
    if (worker_.joinable()) return;
    worker_ = std::thread([this] { work(); });
}

void IconCache::shutdown() {
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    // Whatever the worker decoded but no frame ever uploaded.
    for (Ready& r : ready_)
        if (r.surface) SDL_DestroySurface(r.surface);
    ready_.clear();
    for (auto& [url, e] : entries_)
        if (e.tex) {
            const auto id = static_cast<GLuint>(e.tex);
            glDeleteTextures(1, &id);
        }
    entries_.clear();
}

uint64_t IconCache::texture(const std::string& url, int* w, int* h) {
    if (url.empty()) return 0;
    Entry& e = entries_[url];
    if (!e.tex && !e.pending && !e.failed) {
        e.pending = true;
        {
            const std::lock_guard lock(mutex_);
            queue_.push_back(url);
        }
        cv_.notify_one();
    }
    if (w) *w = e.w;
    if (h) *h = e.h;
    return e.tex;
}

bool IconCache::pump() {
    std::vector<Ready> batch;
    {
        const std::lock_guard lock(mutex_);
        batch.swap(ready_);
    }
    for (Ready& r : batch) {
        Entry& e = entries_[r.url];
        e.pending = false;
        if (!r.surface) {
            e.failed = true;
            continue;
        }
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // The decoder hands back tightly packed rows; the default 4-byte alignment would
        // shear anything whose width is not a multiple of four.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, r.surface->w, r.surface->h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, r.surface->pixels);
        e.tex = tex;
        e.w = r.surface->w;
        e.h = r.surface->h;
        SDL_DestroySurface(r.surface);
    }
    return !batch.empty();
}

void IconCache::work() {
    for (;;) {
        std::string url;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            url = std::move(queue_.front());
            queue_.pop_front();
        }

        const fs::path path = cache_path(url);
        std::string png = read_file(path);
        if (png.empty()) {
            // The CDN, not the API: no rate limit policy applies and these are immutable.
            net::Request req;
            req.url = url;
            req.timeout_ms = 10'000;
            const net::Response resp = net::get(req);
            if (!resp.ok() || resp.body.empty()) {
                debug::log("[icon]   %s: %s", url.c_str(),
                           resp.error.empty() ? ("HTTP " + std::to_string(resp.status)).c_str()
                                              : resp.error.c_str());
            } else {
                png = resp.body;
                ensure_dir(path.parent_path());
                std::ofstream out(path, std::ios::binary);
                out.write(png.data(), static_cast<std::streamsize>(png.size()));
            }
        }

        SDL_Surface* s = png.empty() ? nullptr : decode(png);
        const std::lock_guard lock(mutex_);
        ready_.push_back(Ready{std::move(url), s});
    }
}

} // namespace ppc
