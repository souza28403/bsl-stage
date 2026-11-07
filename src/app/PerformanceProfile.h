#pragma once

namespace bsl {

enum class PerformanceProfile { Eco, Balanced, Maximum };

inline constexpr int kProfileCount = 3;

struct ProfileSettings
{
    int uiFps;
    float previewScale;
    const char* label;
    const char* key;
    int aiFps;
    int maxBackground;
};

const ProfileSettings& settingsFor(PerformanceProfile profile);
PerformanceProfile profileFromKey(const char* key, PerformanceProfile fallback);

}
