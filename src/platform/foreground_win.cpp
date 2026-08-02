#include "platform/foreground.hpp"
#include "platform/platform.hpp"

#include <string>

#include <windows.h>

namespace ppc {
namespace {

std::string window_title(HWND hwnd) {
    wchar_t buf[512];
    int len = GetWindowTextW(hwnd, buf, 512);
    if (len <= 0) return {};
    int need = WideCharToMultiByte(CP_UTF8, 0, buf, len, nullptr, 0, nullptr, nullptr);
    std::string title(need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, len, title.data(), need, nullptr, nullptr);
    return title;
}

struct FindCtx {
    const std::string* needle;
    GameWindow result;
};

BOOL CALLBACK enum_cb(HWND hwnd, LPARAM lp) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    auto* ctx = reinterpret_cast<FindCtx*>(lp);
    std::string t = window_title(hwnd);
    if (t.empty() || t.find(*ctx->needle) == std::string::npos) return TRUE;
    RECT r;
    if (!GetWindowRect(hwnd, &r)) return TRUE;
    ctx->result.present = true;
    ctx->result.focused = (hwnd == GetForegroundWindow());
    ctx->result.x = r.left;
    ctx->result.y = r.top;
    ctx->result.w = r.right - r.left;
    ctx->result.h = r.bottom - r.top;
    return FALSE; // stop at the first match
}

} // namespace

void platform_init() {}

bool foreground_title_contains(const std::string& needle) {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    std::string title = window_title(hwnd);
    return title.find(needle) != std::string::npos;
}

GameWindow find_game_window(const std::string& needle) {
    FindCtx ctx{&needle, {}};
    EnumWindows(enum_cb, reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

namespace {
struct HwndCtx {
    const std::string* needle;
    HWND hwnd;
};

BOOL CALLBACK find_hwnd_cb(HWND hwnd, LPARAM lp) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    auto* ctx = reinterpret_cast<HwndCtx*>(lp);
    std::string t = window_title(hwnd);
    if (!t.empty() && t.find(*ctx->needle) != std::string::npos) {
        ctx->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}
} // namespace

void focus_game_window(const std::string& needle) {
    HwndCtx ctx{&needle, nullptr};
    EnumWindows(find_hwnd_cb, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.hwnd) SetForegroundWindow(ctx.hwnd);
}

} // namespace ppc
