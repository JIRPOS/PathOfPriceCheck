#include "data/mapped_file.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <utility>

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
    const int fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;

    struct stat st{};
    if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
        ::close(fd);
        return false;
    }

    void* m = ::mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (m == MAP_FAILED) {
        ::close(fd);
        return false;
    }

    data_ = static_cast<const uint8_t*>(m);
    size_ = static_cast<size_t>(st.st_size);
    fd_ = fd;
    return true;
}

void MappedFile::close() {
    if (data_) ::munmap(const_cast<uint8_t*>(data_), size_);
    if (fd_ >= 0) ::close(fd_);
    data_ = nullptr;
    size_ = 0;
    fd_ = -1;
}

} // namespace ppc::data
