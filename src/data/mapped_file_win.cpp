#include "data/mapped_file.hpp"

#include <utility>

#include <windows.h>

namespace ppc::data {

MappedFile& MappedFile::operator=(MappedFile&& o) noexcept {
    if (this != &o) {
        close();
        data_ = std::exchange(o.data_, nullptr);
        size_ = std::exchange(o.size_, 0);
        handle_ = std::exchange(o.handle_, nullptr);
        fd_ = std::exchange(o.fd_, -1);
    }
    return *this;
}

bool MappedFile::open(const std::filesystem::path& p) {
    close();
    // FILE_SHARE_DELETE matters: without it a mapped bundle cannot be replaced or removed
    // at all. The cache still installs into a fresh versioned directory rather than over a
    // live mapping, but this keeps a stale directory from becoming permanently undeletable.
    HANDLE file = ::CreateFileW(p.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz{};
    if (!::GetFileSizeEx(file, &sz) || sz.QuadPart <= 0) {
        ::CloseHandle(file);
        return false;
    }

    HANDLE mapping = ::CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    ::CloseHandle(file); // the mapping keeps its own reference
    if (!mapping) return false;

    void* view = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        ::CloseHandle(mapping);
        return false;
    }

    data_ = static_cast<const uint8_t*>(view);
    size_ = static_cast<size_t>(sz.QuadPart);
    handle_ = mapping;
    return true;
}

void MappedFile::close() {
    if (data_) ::UnmapViewOfFile(data_);
    if (handle_) ::CloseHandle(static_cast<HANDLE>(handle_));
    data_ = nullptr;
    size_ = 0;
    handle_ = nullptr;
}

} // namespace ppc::data
