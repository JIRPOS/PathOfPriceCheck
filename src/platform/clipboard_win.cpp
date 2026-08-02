#include "platform/clipboard.hpp"

#include <windows.h>

namespace ppc {

std::string clipboard_text(int timeout_ms) {
    // No async handshake here — the data is already in the clipboard. The only wait is for
    // the global lock, which the copying app can hold briefly; retry rather than fail.
    for (int waited = 0; !OpenClipboard(nullptr); waited += 10) {
        if (waited >= timeout_ms) return {};
        Sleep(10);
    }
    std::string out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* w = static_cast<const wchar_t*>(GlobalLock(h))) {
            int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (n > 1) { // n counts the terminator
                out.resize(static_cast<size_t>(n) - 1);
                WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
            }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

} // namespace ppc
