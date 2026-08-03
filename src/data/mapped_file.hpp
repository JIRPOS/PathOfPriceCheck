#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace ppc::data {

/// A read-only memory mapping of a whole file.
///
/// The bundle is ~3.8MB of ndjson that a price check touches a couple of dozen lines of.
/// Mapping it and parsing lines on demand costs about a megabyte of resident memory and no
/// startup time, where parsing it all up front costs tens of megabytes and hundreds of
/// milliseconds — which would undo the point of a lightweight overlay.
class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile() { close(); }
    MappedFile(MappedFile&& o) noexcept { *this = std::move(o); }
    MappedFile& operator=(MappedFile&& o) noexcept;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    bool open(const std::filesystem::path& p);
    void close();

    bool valid() const { return data_ != nullptr; }
    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }
    std::string_view view() const {
        return {reinterpret_cast<const char*>(data_), size_};
    }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    void* handle_ = nullptr; ///< POSIX: unused. Windows: the file-mapping HANDLE.
    int fd_ = -1;            ///< POSIX only
};

} // namespace ppc::data
