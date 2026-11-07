#include "update/SemVer.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace bsl::update {

namespace {

bool parseNumber(std::string_view s, int& out)
{
    if (s.empty() || s.size() > 9)
        return false;
    for (char c : s)
        if (c < '0' || c > '9')
            return false;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc();
}

bool isNumeric(const std::string& s)
{
    if (s.empty())
        return false;
    for (char c : s)
        if (c < '0' || c > '9')
            return false;
    return true;
}

}

std::string SemVer::toString() const
{
    std::string out = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    for (size_t i = 0; i < pre.size(); ++i)
        out += (i == 0 ? "-" : ".") + pre[i];
    return out;
}

SemVer parseSemVer(std::string_view text)
{
    SemVer v;
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
        text.remove_prefix(1);

    if (const size_t plus = text.find('+'); plus != std::string_view::npos)
        text = text.substr(0, plus);

    std::string_view core = text;
    std::string_view pre;
    bool hasPre = false;
    if (const size_t dash = text.find('-'); dash != std::string_view::npos) {
        core = text.substr(0, dash);
        pre = text.substr(dash + 1);
        hasPre = true;
    }

    int parts[3] = {0, 0, 0};
    int count = 0;
    size_t start = 0;
    while (true) {
        const size_t dot = core.find('.', start);
        const std::string_view piece = core.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start);
        if (count >= 3 || !parseNumber(piece, parts[count]))
            return v;
        ++count;
        if (dot == std::string_view::npos)
            break;
        start = dot + 1;
    }
    if (hasPre) {
        if (pre.empty())
            return v;
        size_t s = 0;
        while (true) {
            const size_t dot = pre.find('.', s);
            const std::string_view id = pre.substr(s, dot == std::string_view::npos ? std::string_view::npos : dot - s);
            if (id.empty())
                return v;
            for (char c : id)
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-'))
                    return v;
            v.pre.emplace_back(id);
            if (dot == std::string_view::npos)
                break;
            s = dot + 1;
        }
    }
    v.major = parts[0];
    v.minor = parts[1];
    v.patch = parts[2];
    v.valid = true;
    return v;
}

int compare(const SemVer& a, const SemVer& b)
{
    if (a.valid != b.valid)
        return a.valid ? 1 : -1;
    if (a.major != b.major)
        return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor)
        return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch)
        return a.patch < b.patch ? -1 : 1;
    if (a.pre.empty() != b.pre.empty())
        return a.pre.empty() ? 1 : -1;
    const size_t n = std::min(a.pre.size(), b.pre.size());
    for (size_t i = 0; i < n; ++i) {
        const std::string& x = a.pre[i];
        const std::string& y = b.pre[i];
        if (x == y)
            continue;
        const bool xn = isNumeric(x), yn = isNumeric(y);
        if (xn && yn) {
            const unsigned long long xv = std::stoull(x.size() > 18 ? x.substr(0, 18) : x);
            const unsigned long long yv = std::stoull(y.size() > 18 ? y.substr(0, 18) : y);
            if (xv != yv)
                return xv < yv ? -1 : 1;
            if (x.size() != y.size())
                return x.size() < y.size() ? -1 : 1;
            continue;
        }
        if (xn != yn)
            return xn ? -1 : 1;
        return x < y ? -1 : 1;
    }
    if (a.pre.size() != b.pre.size())
        return a.pre.size() < b.pre.size() ? -1 : 1;
    return 0;
}

}
