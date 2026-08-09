#pragma once

#include <cstdint>
#include <string>

namespace ppc {

/// The claim on being *the* running instance for this user.
///
/// Held for as long as the object lives, and released by the operating system when the process
/// dies — including a crash, a `kill -9` or a machine losing power. That is the whole reason it
/// is a kernel lock rather than a pid file: a pid file has to be removed by the process that
/// wrote it, which is exactly what a process that died does not do, and the recovery ("is 4711
/// still us?") is a race against pid reuse that goes wrong in the one direction that matters —
/// refusing to start.
///
/// Two instances are worth stopping because almost everything this app owns is a singleton the
/// operating system will happily hand out twice: the global hotkeys (both copies would grab
/// them, and X11 hands a passive grab to whoever asked first, so the *older* process wins and
/// the newly launched one silently does nothing), the rate-limiter state file (two writers,
/// each unaware of the other's requests, is how a client walks into a lockout), the debug log's
/// prune-to-ten, and the overlay window itself.
///
/// Linux: `flock` on `<cache>/<key>.lock`. flock's locks belong to the open file description
/// rather than to the process, so two claims collide even inside one process — which is what
/// makes this testable without spawning anything. Windows: a session-local named mutex, so it
/// is one instance per logged-in user; `Global\` would stop a second user on the same machine
/// from running the app at all.
///
/// `key` must be a plain name — no path separators, and on Windows no backslash, which would
/// name a different kernel object namespace.
class InstanceLock {
  public:
    InstanceLock() = default;
    explicit InstanceLock(const std::string& key);
    ~InstanceLock();
    InstanceLock(InstanceLock&& other) noexcept;
    InstanceLock& operator=(InstanceLock&& other) noexcept;
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

    /// True when this process may run: either it took the lock, or the lock could not be
    /// evaluated at all (no writable cache directory, a filesystem that does not lock).
    /// **Those two are deliberately the same answer.** Only a lock we positively observed
    /// somebody else holding is grounds for refusing to start; treating "could not tell" as
    /// "already running" would trade a rare annoyance for a total one — an app that will not
    /// launch, with nothing on screen saying why.
    bool held() const { return held_; }

  private:
    void release();

    bool held_ = false;
    /// fd on POSIX, HANDLE on Windows; -1 either way when there is nothing to release.
    std::intptr_t handle_ = -1;
};

} // namespace ppc
