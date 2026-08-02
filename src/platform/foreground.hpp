#pragma once

#include <string>

namespace ppc {

/// True if the OS foreground/active window's title contains `needle`.
/// Used to gate the overlay so it only reacts while Path of Exile is focused.
bool foreground_title_contains(const std::string& needle);

} // namespace ppc
