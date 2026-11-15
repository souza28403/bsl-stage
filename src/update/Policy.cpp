#include "update/Policy.h"
#include "platform/TimeUtil.h"

#include <cstdio>
#include <ctime>

namespace bsl::update {

bool showBlocksAutoCheck(const ShowState& s)
{
    return s.outputLive || s.windowFullscreen || s.layoutLocked || s.ecoProfile;
}

bool dueForAutoCheck(const Prefs& prefs, int64_t nowUnix)
{
    if (!prefs.autoCheck)
        return false;
    if (prefs.lastCheckUnix <= 0)
        return true;
    const int64_t elapsed = nowUnix - prefs.lastCheckUnix;
    return elapsed >= kCheckIntervalSeconds || elapsed < -kCheckIntervalSeconds;
}

bool isSkipped(const Prefs& prefs, const Release& release)
{
    if (prefs.skippedVersion.empty())
        return false;
    const SemVer skipped = parseSemVer(prefs.skippedVersion);
    if (skipped.valid && release.version.valid)
        return compare(skipped, release.version) == 0;
    return prefs.skippedVersion == release.tag;
}

bool isUpdateFor(const Prefs& prefs, const Release& release, const SemVer& current)
{
    if (release.draft || !release.version.valid)
        return false;
    if (release.isBeta() && !prefs.beta)
        return false;
    return isNewer(release.version, current);
}

bool shouldOffer(const Prefs& prefs, const Release& release, const SemVer& current)
{
    return isUpdateFor(prefs, release, current) && !isSkipped(prefs, release);
}

std::string formatPublishedDate(const std::string& iso)
{
    auto num = [&](size_t pos, size_t len) {
        int v = 0;
        for (size_t i = pos; i < pos + len; ++i) {
            if (iso[i] < '0' || iso[i] > '9')
                return -1;
            v = v * 10 + (iso[i] - '0');
        }
        return v;
    };
    if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-')
        return {};
    const int y = num(0, 4), m = num(5, 2), d = num(8, 2);
    if (y > 1970 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d/%02d/%04d", d, m, y);
        return buf;
    }
    return {};
}

std::string formatLocalDateTime(int64_t unixSeconds)
{
    if (unixSeconds <= 0)
        return "nunca";
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm local{};
    if (!bslplat::localTime(t, local))
        return "nunca";
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", &local);
    return buf;
}

}
