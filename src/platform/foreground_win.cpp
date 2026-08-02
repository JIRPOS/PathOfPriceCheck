#include "platform/foreground.hpp"
#include "platform/platform.hpp"

#include <windows.h>

namespace ppc {

void platform_init() {}

bool foreground_title_contains(const std::string& needle) {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    wchar_t buf[512];
    int len = GetWindowTextW(hwnd, buf, 512);
    if (len <= 0) return false;
    int need = WideCharToMultiByte(CP_UTF8, 0, buf, len, nullptr, 0, nullptr, nullptr);
    std::string title(need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, len, title.data(), need, nullptr, nullptr);
    return title.find(needle) != std::string::npos;
}

} // namespace ppc
