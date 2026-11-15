#pragma once

#include <ctime>

namespace bslplat {

inline bool localTime(std::time_t t, std::tm& out)
{
#ifdef _WIN32
    return localtime_s(&out, &t) == 0;
#else
    return localtime_r(&t, &out) != nullptr;
#endif
}

inline bool utcTime(std::time_t t, std::tm& out)
{
#ifdef _WIN32
    return gmtime_s(&out, &t) == 0;
#else
    return gmtime_r(&t, &out) != nullptr;
#endif
}

}
