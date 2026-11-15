#include "platform/ExePath.h"

#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace bslplat {

std::filesystem::path exePath()
{
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0)
            return {};
        if (n < buf.size()) {
            buf.resize(n);
            return std::filesystem::path(buf);
        }
        buf.resize(buf.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0)
        return {};
    std::error_code ec;
    auto canon = std::filesystem::weakly_canonical(std::filesystem::path(buf.data()), ec);
    return ec ? std::filesystem::path(buf.data()) : canon;
#else
    std::vector<char> buf(4096, '\0');
    const ssize_t n = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (n <= 0)
        return {};
    return std::filesystem::path(std::string(buf.data(), static_cast<std::size_t>(n)));
#endif
}

std::filesystem::path exeDir()
{
    return exePath().parent_path();
}

}
