#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ppc {

/// Standard base64, padded. Used by the debug log to record clipboard bytes verbatim.
std::string base64_encode(std::string_view in);

/// The inverse. Whitespace is skipped, so a payload wrapped across lines decodes; anything
/// else outside the alphabet is a failure rather than something to guess at, and so is a
/// length that cannot be a whole number of quanta.
///
/// A trade listing carries the seller's item as base64 of the exact clipboard text
/// (`item.extended.text`), which is what makes it parseable by the same code path as a real
/// copy — so a decode that quietly returned partial bytes would surface as an item that
/// half-parsed.
std::optional<std::string> base64_decode(std::string_view in);

} // namespace ppc
