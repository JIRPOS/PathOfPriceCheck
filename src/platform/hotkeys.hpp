#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "platform/input.hpp"

namespace ppc {

/// System-wide (global) hotkey registration. `on_action` fires from an internal
/// OS thread, so it must be thread-safe (the app forwards it to the main loop).
class HotkeyListener {
public:
    virtual ~HotkeyListener() = default;

    /// Replace all bindings. Invalid or unmappable hotkeys are skipped.
    virtual bool rebind(const std::vector<std::pair<Hotkey, Action>>& bindings) = 0;

    static std::unique_ptr<HotkeyListener> create(std::function<void(Action)> on_action);
};

} // namespace ppc
