#include "media/Snappy.h"

#include <cstring>

namespace bsl::media {

namespace {

constexpr std::size_t kMaxLength = 0x7FFFFFFFu;

bool readVarint(const std::uint8_t* in, std::size_t n, std::size_t& pos, std::size_t& value)
{
    std::uint64_t v = 0;
    for (int shift = 0; shift <= 28; shift += 7) {
        if (pos >= n)
            return false;
        const std::uint8_t b = in[pos++];
        v |= static_cast<std::uint64_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) {
            if (v > kMaxLength)
                return false;
            value = static_cast<std::size_t>(v);
            return true;
        }
    }
    return false;
}

}

bool snappyUncompressedLength(const std::uint8_t* in, std::size_t n, std::size_t& length)
{
    if (!in)
        return false;
    std::size_t pos = 0;
    return readVarint(in, n, pos, length);
}

bool snappyDecompressInto(const std::uint8_t* in, std::size_t n, std::uint8_t* out, std::size_t outSize)
{
    if (!in || (!out && outSize > 0))
        return false;
    std::size_t pos = 0;
    std::size_t total = 0;
    if (!readVarint(in, n, pos, total) || total != outSize)
        return false;

    std::size_t op = 0;
    while (pos < n) {
        const std::uint8_t tag = in[pos++];
        const unsigned kind = tag & 3u;
        if (kind == 0) {
            std::size_t len = tag >> 2;
            if (len < 60) {
                len += 1;
                if (len <= 16 && pos + 16 <= n && op + 16 <= outSize) {
                    std::memcpy(out + op, in + pos, 16);
                    pos += len;
                    op += len;
                    continue;
                }
            } else {
                const std::size_t extra = len - 59;
                if (pos + extra > n)
                    return false;
                len = 0;
                for (std::size_t i = 0; i < extra; ++i)
                    len |= static_cast<std::size_t>(in[pos + i]) << (8 * i);
                pos += extra;
                len += 1;
            }
            if (len > n - pos || len > outSize - op)
                return false;
            std::memcpy(out + op, in + pos, len);
            pos += len;
            op += len;
            continue;
        }

        std::size_t len;
        std::size_t offset;
        if (kind == 1) {
            if (pos + 1 > n)
                return false;
            len = 4 + ((tag >> 2) & 7u);
            offset = (static_cast<std::size_t>(tag >> 5) << 8) | in[pos];
            pos += 1;
        } else if (kind == 2) {
            if (pos + 2 > n)
                return false;
            len = 1 + (tag >> 2);
            offset = static_cast<std::size_t>(in[pos]) | (static_cast<std::size_t>(in[pos + 1]) << 8);
            pos += 2;
        } else {
            if (pos + 4 > n)
                return false;
            len = 1 + (tag >> 2);
            offset = static_cast<std::size_t>(in[pos]) | (static_cast<std::size_t>(in[pos + 1]) << 8) |
                     (static_cast<std::size_t>(in[pos + 2]) << 16) | (static_cast<std::size_t>(in[pos + 3]) << 24);
            pos += 4;
        }
        if (offset == 0 || offset > op || len > outSize - op)
            return false;
        std::uint8_t* dst = out + op;
        const std::uint8_t* src = dst - offset;
        if (offset >= 16 && len + 16 <= outSize - op) {
            std::size_t i = 0;
            do {
                std::memcpy(dst + i, src + i, 16);
                i += 16;
            } while (i < len);
        } else if (offset >= len) {
            std::memcpy(dst, src, len);
        } else {
            for (std::size_t i = 0; i < len; ++i)
                dst[i] = src[i];
        }
        op += len;
    }
    return op == outSize;
}

bool snappyDecompress(const std::uint8_t* in, std::size_t n, std::vector<std::uint8_t>& out)
{
    out.clear();
    std::size_t total = 0;
    if (!snappyUncompressedLength(in, n, total) || total > n * 22 + 64)
        return false;
    out.resize(total);
    if (!snappyDecompressInto(in, n, out.data(), total)) {
        out.clear();
        return false;
    }
    return true;
}

}
