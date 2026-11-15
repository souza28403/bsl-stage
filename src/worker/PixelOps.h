#pragma once

#include <algorithm>
#include <cstdint>

namespace bsl::worker {

inline void unpremultiplyRow(const std::uint8_t* src, std::uint8_t* dst, int pixels)
{
    for (int x = 0; x < pixels; ++x) {
        const std::uint8_t* s = src + x * 4;
        std::uint8_t* d = dst + x * 4;
        const unsigned a = s[3];
        if (a == 0 || a == 255) {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        } else {
            d[0] = static_cast<std::uint8_t>(std::min(255u, (s[0] * 255u + a / 2) / a));
            d[1] = static_cast<std::uint8_t>(std::min(255u, (s[1] * 255u + a / 2) / a));
            d[2] = static_cast<std::uint8_t>(std::min(255u, (s[2] * 255u + a / 2) / a));
        }
        d[3] = static_cast<std::uint8_t>(a);
    }
}

}
