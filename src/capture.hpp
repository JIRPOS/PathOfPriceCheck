#pragma once

#include <cstdint>
#include <vector>

namespace ppc {

/// A read-back of our own framebuffer: straight-alpha RGBA, top row first.
///
/// **Our own window and nothing else.** This is not a screen capture — it is the pixels this
/// process drew, so the game behind the transparent parts is not in it and cannot be. That is the
/// whole reason a bug report may carry one at all, and why the dialog can promise that what is
/// previewed is what is sent.
///
/// A header of its own rather than a member of `Overlay`, because the overlay produces one and
/// the report worker consumes one, and neither should have to know about the other.
struct Capture {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;

    bool empty() const { return rgba.empty(); }
};

} // namespace ppc
