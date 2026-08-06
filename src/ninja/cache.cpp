#include "ninja/cache.hpp"

#include <chrono>
#include <fstream>
#include <iterator>
#include <system_error>

#include <nlohmann/json.hpp>

#include "paths.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ppc::ninja::cache {
namespace {

/// Bump to invalidate everything written by an older layout.
constexpr int kCacheVersion = 1;

} // namespace

fs::path file(const Key& k) { return cache_dir() / "ninja" / cache_name(k); }

std::optional<Entry> load(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;

    std::string header;
    if (!std::getline(in, header)) return std::nullopt;
    const json j = json::parse(header, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    if (j.value("version", -1) != kCacheVersion) return std::nullopt;

    Entry e;
    e.etag = j.value("etag", std::string());
    e.fetched_at = j.value("fetched_at", int64_t{0});
    e.body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (e.body.empty()) return std::nullopt;
    return e;
}

bool store(const fs::path& p, const Entry& e) {
    ensure_dir(p.parent_path());
    json j;
    j["version"] = kCacheVersion;
    j["etag"] = e.etag;
    j["fetched_at"] = e.fetched_at;
    std::ofstream out(p, std::ios::binary);
    if (!out) return false;
    // dump() never emits a newline of its own, so the header owns exactly one line and the
    // body starts at the next byte — which is what `load` relies on.
    out << j.dump() << "\n";
    out.write(e.body.data(), static_cast<std::streamsize>(e.body.size()));
    return out.good();
}

bool fresh(int64_t fetched_at, int64_t now_s, int64_t ttl_s) {
    if (fetched_at <= 0 || fetched_at > now_s) return false;
    return now_s - fetched_at < ttl_s;
}

void prune(int64_t keep_s) {
    std::error_code ec;
    const fs::path dir = cache_dir() / "ninja";
    const auto cutoff = fs::file_time_type::clock::now() - std::chrono::seconds(keep_s);
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        // The write time, not the header's `fetched_at`: a 304 rewrites the file, so this is
        // "when anything last wanted it" rather than "how old the prices in it are".
        if (e.last_write_time(ec) < cutoff && !ec) fs::remove(e.path(), ec);
    }
}

} // namespace ppc::ninja::cache
