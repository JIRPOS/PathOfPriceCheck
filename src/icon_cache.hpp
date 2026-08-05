#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct SDL_Surface;

namespace ppc {

/// Small remote images, drawn in the UI: the currency symbols on a price.
///
/// Three stages on two threads, because each stage can only run in one place. Downloading
/// and decoding are slow and must not touch the frame loop; uploading a texture needs the
/// GL context, which belongs to the frame loop. So the worker fetches (through a disk cache
/// under `<cache>/icons/`) and decodes to an `SDL_Surface`, and `pump()` turns the surfaces
/// it left behind into textures at the top of a frame.
///
/// `texture()` is therefore allowed to answer "not yet" and is asked again next frame; a
/// price with no symbol yet still prints its amount, so nothing waits on this.
class IconCache {
public:
    ~IconCache();

    void init();
    void shutdown(); ///< before the GL context goes; frees the textures

    /// The texture for `url` (an `ImTextureID`), or 0 while it is not ready. The first ask
    /// queues the download. A URL that failed is never retried this run — an icon that is
    /// 404 would otherwise be requested every frame forever.
    uint64_t texture(const std::string& url, int* w = nullptr, int* h = nullptr);

    /// Main thread, at the top of a frame, with the GL context current. True when it
    /// uploaded something, i.e. when the panel now looks different from the last frame.
    bool pump();

private:
    struct Entry {
        uint64_t tex = 0;
        int w = 0, h = 0;
        bool pending = false;
        bool failed = false;
    };
    struct Ready {
        std::string url;
        SDL_Surface* surface = nullptr; ///< null marks a failure
    };

    void work();

    std::unordered_map<std::string, Entry> entries_; ///< main thread only
    std::thread worker_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    std::vector<Ready> ready_;
    bool stopping_ = false;
};

} // namespace ppc
