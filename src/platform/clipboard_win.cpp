#include "platform/clipboard.hpp"

#include <cstdio>
#include <utility>

#include <windows.h>

#include "util/debug_log.hpp"

namespace ppc {
namespace {

std::string window_desc(HWND h) {
    if (!h) return "none";
    char buf[128];
    wchar_t title[128] = L"";
    GetWindowTextW(h, title, 128);
    char utf8[256] = "";
    WideCharToMultiByte(CP_UTF8, 0, title, -1, utf8, sizeof utf8, nullptr, nullptr);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    std::snprintf(buf, sizeof buf, "0x%p pid=%lu '%s'", (void*)h, (unsigned long)pid, utf8);
    return buf;
}

} // namespace

uint64_t clipboard_stamp() {
    // Bumped by every write, whoever makes it, and readable without opening the clipboard —
    // so it never takes the lock and never blocks the app doing the copying.
    return GetClipboardSequenceNumber();
}

void clipboard_poke() {
    // Nothing to do: a Windows copy renders into the clipboard immediately, and the sequence
    // number moves with it. The X11 half needs this because Wine renders only on request.
}

std::string clipboard_owner_info() {
    // Neither call opens the clipboard, so nothing here can block or disturb a write in
    // flight — the same property the X11 side needs.
    std::string s = window_desc(GetClipboardOwner());
    s += " seq=" + std::to_string(GetClipboardSequenceNumber());
    return s;
}

std::string clipboard_targets(int) {
    static const std::pair<UINT, const char*> kFormats[] = {
        {CF_UNICODETEXT, "CF_UNICODETEXT"}, {CF_TEXT, "CF_TEXT"}, {CF_OEMTEXT, "CF_OEMTEXT"}};
    std::string out;
    for (const auto& [id, name] : kFormats)
        if (IsClipboardFormatAvailable(id)) out += (out.empty() ? "" : " ") + std::string(name);
    return out.empty() ? "(none)" : out;
}

bool clipboard_set_text(const std::string& text) {
    if (text.size() > kMaxClipboardWrite) return false; // the X11 ceiling, kept one rule
    // The same retry the read path does, and for the same reason: the lock belongs to whoever
    // opened it last, and the application we are pasting into may be holding it briefly.
    for (int waited = 0; !OpenClipboard(nullptr); waited += 10) {
        if (waited >= 200) {
            debug::log("[paste]  clipboard locked by %s, gave up",
                       window_desc(GetOpenClipboardWindow()).c_str());
            return false;
        }
        Sleep(10);
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    HGLOBAL mem = n > 0 ? GlobalAlloc(GMEM_MOVEABLE, static_cast<size_t>(n) * sizeof(wchar_t))
                        : nullptr;
    bool ok = false;
    if (mem) {
        if (wchar_t* dst = static_cast<wchar_t*>(GlobalLock(mem))) {
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, dst, n);
            GlobalUnlock(mem);
            EmptyClipboard();
            // The clipboard owns the block once SetClipboardData succeeds, and only then —
            // a failure leaves it ours to free.
            ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        }
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    debug::log("[paste]  put %zu bytes on the clipboard (ok: %d)", text.size(), (int)ok);
    return ok;
}

std::string clipboard_text(int timeout_ms) {
    // No async handshake here — the data is already in the clipboard. The only wait is for
    // the global lock, which the copying app can hold briefly; retry rather than fail.
    for (int waited = 0; !OpenClipboard(nullptr); waited += 10) {
        if (waited >= timeout_ms) {
            debug::trace("[copy]   clipboard locked by %s, gave up after %dms",
                         window_desc(GetOpenClipboardWindow()).c_str(), timeout_ms);
            return {};
        }
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
