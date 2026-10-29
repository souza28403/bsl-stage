#include "app/PerformanceProfile.h"

#include <cstring>

namespace bsl {

namespace {

const ProfileSettings kProfiles[kProfileCount] = {
    {30, 0.50f, "Eco", "eco", 0},
    {60, 0.75f, "Balanceado", "balanced", 15},
    {0, 1.00f, "Máximo", "maximum", 30},
};

} // namespace

const ProfileSettings& settingsFor(PerformanceProfile profile)
{
    return kProfiles[static_cast<int>(profile)];
}

PerformanceProfile profileFromKey(const char* key, PerformanceProfile fallback)
{
    for (int i = 0; i < kProfileCount; ++i)
        if (std::strcmp(kProfiles[i].key, key) == 0)
            return static_cast<PerformanceProfile>(i);
    return fallback;
}

} // namespace bsl
