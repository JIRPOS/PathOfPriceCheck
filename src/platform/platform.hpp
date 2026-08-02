#pragma once

namespace ppc {

/// Process-wide platform init. Must run before any other platform call
/// (on X11 it enables Xlib threading).
void platform_init();

} // namespace ppc
