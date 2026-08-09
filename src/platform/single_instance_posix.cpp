#include "platform/single_instance.hpp"

#include <cerrno>
#include <filesystem>
#include <utility>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include "paths.hpp"

namespace ppc {

InstanceLock::InstanceLock(const std::string& key) {
    const std::filesystem::path dir = cache_dir();
    if (!ensure_dir(dir)) {
        held_ = true; // cannot tell — see held()
        return;
    }
    const std::filesystem::path path = dir / (key + ".lock");
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        held_ = true;
        return;
    }
    int rc = 0;
    do {
        rc = ::flock(fd, LOCK_EX | LOCK_NB);
    } while (rc != 0 && errno == EINTR);
    if (rc != 0) {
        // EWOULDBLOCK is the one answer that means another instance: anything else (a
        // filesystem with no locking, most famously) is a lock we could not evaluate.
        held_ = errno != EWOULDBLOCK;
        ::close(fd);
        return;
    }
    // Diagnostics only, and the reason the file has any contents at all: `cat` on it during a
    // "why won't it start" report names the process holding it. Nothing ever reads it back —
    // the lock is the lock.
    if (::ftruncate(fd, 0) == 0) {
        const std::string pid = std::to_string(::getpid()) + "\n";
        [[maybe_unused]] const ssize_t n = ::write(fd, pid.data(), pid.size());
    }
    handle_ = fd;
    held_ = true;
}

void InstanceLock::release() {
    // Closing the descriptor drops the flock. The file itself stays: unlinking it would race
    // with the next instance, which may already have opened this inode and be about to lock a
    // path that no longer exists — two processes would then hold two different files and both
    // believe they are alone.
    if (handle_ >= 0) ::close(static_cast<int>(handle_));
    handle_ = -1;
    held_ = false;
}

InstanceLock::~InstanceLock() { release(); }

InstanceLock::InstanceLock(InstanceLock&& other) noexcept
    : held_(std::exchange(other.held_, false)), handle_(std::exchange(other.handle_, -1)) {}

InstanceLock& InstanceLock::operator=(InstanceLock&& other) noexcept {
    if (this != &other) {
        release();
        held_ = std::exchange(other.held_, false);
        handle_ = std::exchange(other.handle_, -1);
    }
    return *this;
}

} // namespace ppc
