#pragma once

#include <cstdint>
#include <vector>

namespace ppc {

/// Encode straight-alpha RGBA, top row first, as an 8-bit RGBA PNG.
///
/// Only ever used for one thing: the read-back of our own overlay window that a bug report may
/// carry. So it is deliberately the smallest encoder that produces a file every viewer accepts —
/// one `IDAT`, no interlacing, no ancillary chunks, and **no metadata of any kind**, which on a
/// picture leaving a user's machine is a property worth having rather than a shortcut.
///
/// Empty on a zero-sized or short input, which the caller treats as "no screenshot" rather than
/// as an error worth a message: there is nothing a user could do about it.
std::vector<uint8_t> encode_png(const uint8_t* rgba, int w, int h);

} // namespace ppc
