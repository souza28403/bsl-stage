#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace bsl::update {

struct SemVer
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::vector<std::string> pre;
    bool valid = false;

    bool isPrerelease() const { return !pre.empty(); }
    std::string toString() const;
};

SemVer parseSemVer(std::string_view text);

int compare(const SemVer& a, const SemVer& b);

inline bool isNewer(const SemVer& candidate, const SemVer& current)
{
    return candidate.valid && compare(candidate, current) > 0;
}

}
