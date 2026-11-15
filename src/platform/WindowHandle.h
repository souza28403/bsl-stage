#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

namespace bslplat {

inline void* activeWindowHandle()
{
#ifdef _WIN32
    return GetActiveWindow();
#else
    return nullptr;
#endif
}

}
