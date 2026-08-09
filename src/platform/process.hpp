#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ppc {

/// Starts a program and stops caring about it: no handle is kept, no exit status is collected,
/// and it outlives this process. Used for exactly two things — relaunching ourselves after an
/// update, and handing the staged Windows installer the job of replacing us.
///
/// POSIX double-forks so the child is reparented to init rather than left a zombie nobody
/// reaps; we are about to exit anyway, and a caller that exits between the fork and the wait
/// is how zombies happen.
///
/// False if the program could not be started at all. A program that starts and then fails is
/// indistinguishable from one that works, which is why the caller must not treat true as
/// "the update was applied".
bool spawn_detached(const std::filesystem::path& program,
                    const std::vector<std::string>& args);

} // namespace ppc
