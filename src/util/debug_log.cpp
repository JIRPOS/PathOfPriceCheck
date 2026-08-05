#include "util/debug_log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

#include "paths.hpp"
#include "util/base64.hpp"

namespace fs = std::filesystem;

namespace ppc::debug {
namespace {

std::mutex mu;         ///< the hotkey thread logs too; every entry below is under it
std::ofstream file;
std::string file_path;
std::string current_check;
uint64_t check_counter = 0;

/// Keep this many runs' logs. A run is a few hundred KB at most, but they accumulate for a
/// user who leaves the option on.
constexpr size_t kKeepLogs = 10;
constexpr size_t kMaxTextBytes = 64 * 1024;

bool stderr_trace() {
    static bool on = std::getenv("PPC_DEBUG_COPY") != nullptr;
    return on;
}

std::tm local_tm(std::time_t t) {
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

std::string stamp(bool with_date) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::tm tm = local_tm(system_clock::to_time_t(now));
    char buf[64];
    std::strftime(buf, sizeof buf, with_date ? "%Y-%m-%d %H:%M:%S" : "%H:%M:%S", &tm);
    char out[80];
    std::snprintf(out, sizeof out, "%s.%03d", buf, static_cast<int>(ms.count()));
    return out;
}

/// Oldest first, so the tail is what to keep.
void prune_logs(const fs::path& dir) {
    std::error_code ec;
    std::vector<fs::path> logs;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("ppc-", 0) == 0 && e.path().extension() == ".log") logs.push_back(e.path());
    }
    if (logs.size() < kKeepLogs) return;
    // The name is the timestamp, so lexical order is chronological. One extra goes, to leave
    // room for the file about to be created.
    std::sort(logs.begin(), logs.end());
    for (size_t i = 0; i <= logs.size() - kKeepLogs; ++i) fs::remove(logs[i], ec);
}

void write_line(const std::string& line) { // caller holds mu
    if (!file.is_open()) return;
    file << stamp(false) << " [" << (current_check.empty() ? "----" : current_check) << "] " << line
         << '\n';
    file.flush(); // a crash or a hang is exactly the case this log exists for
}

std::string vformat(const char* fmt, va_list ap) {
    va_list copy;
    va_copy(copy, ap);
    const int n = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    std::vsnprintf(s.data(), s.size() + 1, fmt, ap);
    return s;
}

} // namespace

std::string digest(std::string_view s) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 0x100000001B3ull;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

bool enabled() {
    std::lock_guard lk(mu);
    return file.is_open();
}

std::string log_path() {
    std::lock_guard lk(mu);
    return file_path;
}

void set_enabled(bool on) {
    {
        std::lock_guard lk(mu);
        if (on == file.is_open()) return;
        if (!on) {
            write_line("debug log closed");
            file.close();
            file_path.clear();
            return;
        }
        const fs::path dir = cache_dir() / "logs";
        if (!ensure_dir(dir)) return;
        prune_logs(dir);
        const std::tm tm = local_tm(std::time(nullptr));
        char name[64];
        std::strftime(name, sizeof name, "ppc-%Y%m%d-%H%M%S.log", &tm);
        const fs::path p = dir / name;
        file.open(p, std::ios::out | std::ios::trunc);
        if (!file.is_open()) return;
        file_path = p.string();
    }
    log("debug log opened %s \xe2\x80\x94 PathOfPriceCheck %s (%s)", stamp(true).c_str(),
        APP_VERSION,
#ifdef _WIN32
        "windows"
#else
        "linux"
#endif
    );
}

bool tracing() { return stderr_trace() || enabled(); }

std::string begin_check() {
    // 32 unambiguous characters: no 0/O/1/I, because the whole point is that a user can read
    // this off the panel and write it down.
    static constexpr char kAlpha[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    const uint64_t ns = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::string id;
    {
        std::lock_guard lk(mu);
        uint64_t v = ns ^ (++check_counter * 0x9E3779B97F4A7C15ull);
        v ^= v >> 29;
        v *= 0xBF58476D1CE4E5B9ull;
        v ^= v >> 32;
        for (int i = 0; i < 4; ++i, v >>= 5) id += kAlpha[v & 31];
        current_check = id;
    }
    log("---- price check %s ----", id.c_str());
    return id;
}

std::string check_id() {
    std::lock_guard lk(mu);
    return current_check;
}

void trace(const char* fmt, ...) {
    if (!tracing()) return;
    va_list ap;
    va_start(ap, fmt);
    const std::string s = vformat(fmt, ap);
    va_end(ap);
    if (stderr_trace()) std::fprintf(stderr, "%s\n", s.c_str());
    std::lock_guard lk(mu);
    write_line(s);
}

void log(const char* fmt, ...) {
    if (!enabled()) return;
    va_list ap;
    va_start(ap, fmt);
    const std::string s = vformat(fmt, ap);
    va_end(ap);
    std::lock_guard lk(mu);
    write_line(s);
}

void log_text(const char* label, std::string_view text) {
    if (!enabled()) return;
    const std::string_view head = text.substr(0, std::min(text.size(), kMaxTextBytes));
    log("%s: %zu bytes fnv=%s%s", label, text.size(), digest(text).c_str(),
        head.size() < text.size() ? " (base64 truncated)" : "");
    if (!text.empty()) log("%s.b64: %s", label, base64_encode(head).c_str());
}

} // namespace ppc::debug
