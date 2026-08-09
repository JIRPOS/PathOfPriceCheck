#include "platform/single_instance.hpp"

#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ppc {

InstanceLock::InstanceLock(const std::string& key) {
    // The key is ASCII by contract, so widening a byte at a time is the whole conversion and
    // needs no code page. "Local\" scopes the mutex to the logon session: one instance per
    // user, rather than one per machine.
    std::wstring name = L"Local\\";
    for (unsigned char ch : key) name += static_cast<wchar_t>(ch);

    HANDLE h = ::CreateMutexW(nullptr, TRUE, name.c_str());
    if (!h) {
        held_ = true; // cannot tell — see held()
        return;
    }
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        // The handle is valid but somebody else owns the mutex; ours is a second reference and
        // holding it would keep the object alive past their exit for no reason.
        ::CloseHandle(h);
        held_ = false;
        return;
    }
    handle_ = reinterpret_cast<std::intptr_t>(h);
    held_ = true;
}

void InstanceLock::release() {
    if (handle_ != -1) {
        HANDLE h = reinterpret_cast<HANDLE>(handle_);
        ::ReleaseMutex(h);
        ::CloseHandle(h);
    }
    handle_ = -1;
    held_ = false;
}

InstanceLock::~InstanceLock() { release(); }

InstanceLock::InstanceLock(InstanceLock&& other) noexcept
    : held_(std::exchange(other.held_, false)), handle_(std::exchange(other.handle_, -1)) {}

InstanceLock& InstanceLock::operator=(InstanceLock&& other) noexcept {
    if (this != &other) {
        release();
        held_ = std::exchange(other.held_, false);
        handle_ = std::exchange(other.handle_, -1);
    }
    return *this;
}

} // namespace ppc
