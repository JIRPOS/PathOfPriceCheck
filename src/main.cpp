#include "app.hpp"

/// The application takes exactly one switch, `--updated`, and it is not a user-facing one: the
/// updater passes it to the copy it starts in place of itself. Anything else is ignored rather
/// than rejected — this is a tray application launched by a shortcut, not a CLI.
#include <string_view>

#ifdef _WIN32
// The Windows build is a GUI-subsystem binary (WIN32_EXECUTABLE), or launching it would pop a
// console window beside an application that lives in the tray. That subsystem enters at WinMain,
// so this is the entry point there; SDL_MAIN_HANDLED means SDL provides no shim of its own.
// Nothing is written to stdout/stderr that matters — PPC_DEBUG_COPY's traces have nowhere to go
// here, and the debug log is a file.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int) {
    // Searched rather than tokenised: with one switch and no values, splitting the command line
    // would be more code than the thing it decides.
    const std::string_view cmd = lpCmdLine ? lpCmdLine : "";
    const bool updated = cmd.find("--updated") != std::string_view::npos;
    return ppc::App{}.run(updated);
}
#else
int main(int argc, char** argv) {
    bool updated = false;
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--updated") updated = true;
    return ppc::App{}.run(updated);
}
#endif
