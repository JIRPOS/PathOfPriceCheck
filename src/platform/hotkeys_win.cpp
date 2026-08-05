#include "platform/hotkeys.hpp"

#include <cctype>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <windows.h>

#include "util/debug_log.hpp"

namespace ppc {
namespace {

UINT win_mods(Mod m) {
    UINT f = MOD_NOREPEAT;
    if (has(m, Mod::Ctrl)) f |= MOD_CONTROL;
    if (has(m, Mod::Shift)) f |= MOD_SHIFT;
    if (has(m, Mod::Alt)) f |= MOD_ALT;
    if (has(m, Mod::Super)) f |= MOD_WIN;
    return f;
}

UINT win_vk(const std::string& name) {
    if (name.size() == 1) {
        char c = name[0];
        if (std::isalpha(static_cast<unsigned char>(c))) return static_cast<UINT>(std::toupper(c));
        if (std::isdigit(static_cast<unsigned char>(c))) return static_cast<UINT>(c);
    }
    if (name == "Space") return VK_SPACE;
    if (name == "Enter") return VK_RETURN;
    if (name == "Escape") return VK_ESCAPE;
    if (name == "Tab") return VK_TAB;
    if (name == "Backspace") return VK_BACK;
    if (name == "Delete") return VK_DELETE;
    if (name == "Insert") return VK_INSERT;
    if (name == "Home") return VK_HOME;
    if (name == "End") return VK_END;
    if (name == "PageUp") return VK_PRIOR;
    if (name == "PageDown") return VK_NEXT;
    if (name == "Up") return VK_UP;
    if (name == "Down") return VK_DOWN;
    if (name == "Left") return VK_LEFT;
    if (name == "Right") return VK_RIGHT;
    if (name.size() >= 2 && name[0] == 'F') {
        int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + (n - 1);
    }
    return 0;
}

const UINT WM_PPC_REBIND = WM_APP + 1;
const UINT WM_PPC_QUIT = WM_APP + 2;

class WinHotkeys final : public HotkeyListener {
public:
    explicit WinHotkeys(std::function<void(Action)> cb) : cb_(std::move(cb)) {
        thread_ = std::thread([this] { run(); });
        std::unique_lock lk(mu_);
        ready_.wait(lk, [this] { return tid_ != 0; });
    }

    ~WinHotkeys() override {
        if (!tid_) return;
        PostThreadMessage(tid_, WM_PPC_QUIT, 0, 0);
        if (thread_.joinable()) thread_.join();
    }

    bool rebind(const std::vector<std::pair<Hotkey, Action>>& b) override {
        {
            std::lock_guard lk(mu_);
            pending_ = b;
            have_pending_ = true;
        }
        if (tid_) PostThreadMessage(tid_, WM_PPC_REBIND, 0, 0);
        return true;
    }

private:
    void apply_pending() {
        std::vector<std::pair<Hotkey, Action>> b;
        {
            std::lock_guard lk(mu_);
            if (!have_pending_) return;
            b.swap(pending_);
            have_pending_ = false;
        }
        for (int id : ids_) UnregisterHotKey(nullptr, id);
        ids_.clear();
        id_action_.clear();
        int id = 1;
        for (auto& [hk, act] : b) {
            if (!hk.valid()) continue;
            UINT vk = win_vk(hk.key);
            if (!vk) continue;
            const bool ok = RegisterHotKey(nullptr, id, win_mods(hk.mods), vk);
            debug::trace("[hotkey] register %s as vk=0x%x: %s", to_string(hk).c_str(), vk,
                         ok ? "ok" : "refused (another application holds it)");
            if (ok) {
                ids_.push_back(id);
                id_action_[id] = act;
                ++id;
            }
        }
    }

    void run() {
        MSG msg;
        PeekMessage(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // force a message queue
        {
            std::lock_guard lk(mu_);
            tid_ = GetCurrentThreadId();
        }
        ready_.notify_one();
        apply_pending(); // handle a rebind that arrived before we were ready

        while (GetMessage(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_HOTKEY) {
                auto it = id_action_.find(static_cast<int>(msg.wParam));
                if (it != id_action_.end()) {
                    debug::trace("[hotkey] fire action=%d", (int)it->second);
                    cb_(it->second);
                }
            } else if (msg.message == WM_PPC_REBIND) {
                apply_pending();
            } else if (msg.message == WM_PPC_QUIT) {
                break;
            }
        }
        for (int id : ids_) UnregisterHotKey(nullptr, id);
    }

    std::function<void(Action)> cb_;
    std::thread thread_;
    std::mutex mu_;
    std::condition_variable ready_;
    DWORD tid_ = 0;
    std::vector<std::pair<Hotkey, Action>> pending_;
    bool have_pending_ = false;
    std::vector<int> ids_;
    std::unordered_map<int, Action> id_action_;
};

} // namespace

std::unique_ptr<HotkeyListener> HotkeyListener::create(std::function<void(Action)> cb) {
    return std::make_unique<WinHotkeys>(std::move(cb));
}

} // namespace ppc
