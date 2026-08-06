#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ppc::exchange::cache {

/// Where an hour's digest body is kept between runs.
std::filesystem::path file(int64_t hour);

/// The body verbatim, with no header of any kind — unlike the poe.ninja cache there is
/// nothing to record beside it. A digest is immutable and its hour is its file name, so
/// there is no etag to send and no freshness to decide: the file either is the hour being
/// asked for or it is not.
std::string load(int64_t hour);
bool store(int64_t hour, const std::string& body);

/// How many hours are kept. Each is a couple of megabytes covering every league, and only the
/// newest is ever read — the one behind it exists so that stepping back an hour (the feed
/// publishes late often enough to matter) does not cost a second download.
inline constexpr int kKeepHours = 2;

/// Delete every digest older than the newest `kKeepHours`. Best effort.
void prune(int keep = kKeepHours);

} // namespace ppc::exchange::cache
