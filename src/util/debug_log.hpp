#pragma once

#include <string>
#include <string_view>

/// Opt-in diagnostic log for the hotkey → copy → clipboard path, which is where every
/// remaining bug lives and where nothing is reproducible on demand. Off by default; turned on
/// in Settings (`debug_log` in config.json). Each run writes a fresh file under
/// `<cache>/logs/`, and every press of the price-check hotkey mints a short id that tags each
/// line and is shown in the panel — so "check K7F2 hung" names a span of the log exactly.
namespace ppc::debug {

/// Open (or close) the log. Opening starts a new file for this run and prunes old ones.
void set_enabled(bool on);
bool enabled();
/// The file being written, or empty when the log is off or could not be opened.
std::string log_path();

/// Mint the id for one price check. Every line logged afterwards carries it until the next
/// call; returned so the panel can show it.
std::string begin_check();
std::string check_id();

/// True when anything is listening — the log file or `PPC_DEBUG_COPY`. Guards the X round
/// trips that exist only to be logged.
bool tracing();

/// printf-style. Goes to the log file when it is on, and to stderr under `PPC_DEBUG_COPY`.
void trace(const char* fmt, ...);

/// Log-file only: the loud lines (whole clipboard contents, state dumps) that would drown the
/// stderr trace.
void log(const char* fmt, ...);

/// The exact bytes, as a digest line plus a base64 line — whitespace and encoding both
/// survive being pasted into a report, and the log stays one-line-per-fact. Truncated past
/// 64 KiB, which no item text comes near. No-op when the log is off.
void log_text(const char* label, std::string_view text);

/// 16 hex chars of FNV-1a-64. Cheap identity for "the same bytes as the last poll", which is
/// most of what the copy path needs to say about a clipboard read.
std::string digest(std::string_view text);

} // namespace ppc::debug
