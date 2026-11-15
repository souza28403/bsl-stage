#pragma once

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

namespace bslplat::scan {

inline void skipSpace(const char*& p)
{
    while (*p && std::isspace(static_cast<unsigned char>(*p)))
        ++p;
}

inline bool text(const char*& p, const char* t)
{
    while (*t == ' ') {
        skipSpace(p);
        ++t;
    }
    const std::size_t n = std::strlen(t);
    if (std::strncmp(p, t, n) != 0)
        return false;
    p += n;
    return true;
}

inline bool integer(const char*& p, int& v)
{
    char* e = nullptr;
    const long x = std::strtol(p, &e, 10);
    if (e == p)
        return false;
    v = static_cast<int>(x);
    p = e;
    return true;
}

inline bool real(const char*& p, double& v)
{
    char* e = nullptr;
    const double x = std::strtod(p, &e);
    if (e == p)
        return false;
    v = x;
    p = e;
    return true;
}

inline bool word(const char*& p, std::string& out, std::size_t maxLen)
{
    skipSpace(p);
    const char* b = p;
    while (*p && !std::isspace(static_cast<unsigned char>(*p)) && static_cast<std::size_t>(p - b) < maxLen)
        ++p;
    out.assign(b, p);
    return p != b;
}

}
