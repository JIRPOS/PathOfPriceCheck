#include "platform/process.hpp"

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ppc {
namespace {

/// CreateProcess takes one command line and lets the callee split it, so every argument has to
/// be quoted the way the C runtime's parser will unsplit it. The rule that is easy to get wrong:
/// backslashes are literal *except* immediately before a quote, where they double.
void append_quoted(std::wstring& out, const std::wstring& arg) {
    out += L'"';
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"') {
            out.append(slashes * 2 + 1, L'\\');
        } else {
            out.append(slashes, L'\\');
        }
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\'); // the closing quote is next, so these double too
    out += L'"';
}

} // namespace

bool spawn_detached(const std::filesystem::path& program, const std::vector<std::string>& args) {
    std::wstring cmd;
    append_quoted(cmd, program.wstring());
    for (const std::string& a : args) {
        cmd += L' ';
        // Our own arguments are ASCII switches, so widening a byte at a time is the whole
        // conversion — the same shortcut single_instance_win.cpp takes, for the same reason.
        std::wstring w;
        for (unsigned char ch : a) w += static_cast<wchar_t>(ch);
        append_quoted(cmd, w);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    const std::wstring dir = program.parent_path().wstring();
    const BOOL ok = ::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                                     CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT,
                                     nullptr, dir.empty() ? nullptr : dir.c_str(), &si, &pi);
    if (!ok) return false;
    // Closing both handles is what makes it detached: the child keeps running and Windows
    // cleans it up on its own exit.
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return true;
}

} // namespace ppc
