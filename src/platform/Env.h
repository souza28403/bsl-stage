#pragma once

#include "platform/Utf.h"

#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#include <unistd.h>
#endif

namespace bslplat {

inline std::wstring envWide(const wchar_t* name)
{
#ifdef _WIN32
    wchar_t buf[2048] = {};
    const DWORD n = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    if (n > 0 && n < sizeof(buf) / sizeof(buf[0]))
        return std::wstring(buf, n);
    return {};
#else
    const char* v = std::getenv(wideToUtf8(name).c_str());
    return v ? utf8ToWide(v) : std::wstring();
#endif
}

inline unsigned long currentProcessId()
{
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}

}
