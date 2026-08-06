#include "exchange/cache.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include "exchange/exchange.hpp"
#include "paths.hpp"

namespace fs = std::filesystem;

namespace ppc::exchange::cache {
namespace {

fs::path dir() { return cache_dir() / "exchange"; }

/// The hour a cached file covers, or 0 for anything else in the directory.
int64_t hour_of(const fs::path& p) {
    const std::string name = p.filename().string();
    int64_t h = 0;
    const char* begin = name.data();
    const auto [end, ec] = std::from_chars(begin, begin + name.size(), h);
    return ec == std::errc() && std::string_view(end) == ".json" ? h : 0;
}

} // namespace

fs::path file(int64_t hour) { return dir() / cache_name(hour); }

std::string load(int64_t hour) {
    std::ifstream in(file(hour), std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool store(int64_t hour, const std::string& body) {
    ensure_dir(dir());
    std::ofstream out(file(hour), std::ios::binary);
    if (!out) return false;
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    return out.good();
}

void prune(int keep) {
    std::error_code ec;
    std::vector<int64_t> hours;
    for (const fs::directory_entry& e : fs::directory_iterator(dir(), ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (const int64_t h = hour_of(e.path()); h > 0) hours.push_back(h);
    }
    if (static_cast<int>(hours.size()) <= keep) return;
    std::sort(hours.begin(), hours.end(), std::greater<>());
    for (size_t i = static_cast<size_t>(keep); i < hours.size(); ++i)
        fs::remove(file(hours[i]), ec);
}

} // namespace ppc::exchange::cache
