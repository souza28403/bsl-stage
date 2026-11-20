#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bsl::media {

bool snappyUncompressedLength(const std::uint8_t* in, std::size_t n, std::size_t& length);

bool snappyDecompressInto(const std::uint8_t* in, std::size_t n, std::uint8_t* out, std::size_t outSize);

bool snappyDecompress(const std::uint8_t* in, std::size_t n, std::vector<std::uint8_t>& out);

}
