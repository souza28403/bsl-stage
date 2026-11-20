#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bsl::media {

enum class HapTexture { DXT1, DXT5, YCoCgDXT5 };

enum class HapStatus { Ok, Unsupported, Invalid };

std::size_t hapTextureSize(HapTexture format, int width, int height);

HapStatus hapParseFrame(const std::uint8_t* data, std::size_t size, int width, int height, HapTexture& format,
                        std::vector<std::uint8_t>& texture);

}
