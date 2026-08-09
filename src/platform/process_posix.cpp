#include "platform/process.hpp"

#include <cerrno>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace ppc {

bool spawn_detached(const std::filesystem::path& program, const std::vector<std::string>& args) {
    const std::string path = program.string();

    const pid_t first = ::fork();
    if (first < 0) return false;
    if (first == 0) {
        // Intermediate child. Its own child is what actually runs, so that when this one exits
        // immediately the grandchild is reparented to init and nothing has to reap it.
        if (::fork() == 0) {
            ::setsid(); // survive the terminal or session that started us going away

            std::vector<char*> argv;
            argv.reserve(args.size() + 2);
            argv.push_back(const_cast<char*>(path.c_str()));
            for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
            argv.push_back(nullptr);

            ::execv(path.c_str(), argv.data());
            // Only reachable when execv failed; _exit rather than exit so no atexit handler
            // inherited from the parent runs twice.
            ::_exit(127);
        }
        ::_exit(0);
    }

    // Reap the intermediate, which exits at once. The grandchild is not ours to wait for.
    int status = 0;
    while (::waitpid(first, &status, 0) < 0 && errno == EINTR) {
    }
    return true;
}

} // namespace ppc
