#pragma once

namespace bsl {

enum class PerformanceProfile { Eco, Balanced, Maximum };

inline constexpr int kProfileCount = 3;

struct ProfileSettings
{
    int uiFps;           // 0 = vsync
    float previewScale;  // fracao da resolucao do canvas usada no preview
    const char* label;   // nome exibido na UI
    const char* key;     // nome salvo no JSON
};

const ProfileSettings& settingsFor(PerformanceProfile profile);
PerformanceProfile profileFromKey(const char* key, PerformanceProfile fallback);

} // namespace bsl
