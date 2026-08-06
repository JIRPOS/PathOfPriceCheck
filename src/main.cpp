#include "app.hpp"

#ifdef _WIN32
// The Windows build is a GUI-subsystem binary (WIN32_EXECUTABLE), or launching it would pop a
// console window beside an application that lives in the tray. That subsystem enters at WinMain,
// so this is the entry point there; SDL_MAIN_HANDLED means SDL provides no shim of its own.
// Nothing is written to stdout/stderr that matters — PPC_DEBUG_COPY's traces have nowhere to go
// here, and the debug log is a file.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return ppc::App{}.run();
}
#else
int main() {
    return ppc::App{}.run();
}
#endif
