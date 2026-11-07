#include "dmx/DmxProtocol.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace bsl::dmx {

namespace {

constexpr char kArtId[8] = {'A', 'r', 't', '-', 'N', 'e', 't', '\0'};
constexpr std::uint8_t kAcnId[12] = {'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', 0, 0, 0};
constexpr std::uint16_t kArtProtocolVersion = 14;

void putString(std::uint8_t* dst, std::size_t size, const std::string& text)
{
    std::memset(dst, 0, size);
    std::memcpy(dst, text.data(), std::min(text.size(), size - 1));
}

std::string readString(const std::uint8_t* src, std::size_t size)
{
    std::size_t n = 0;
    while (n < size && src[n] != 0)
        ++n;
    return std::string(reinterpret_cast<const char*>(src), n);
}

void put16(std::uint8_t* p, std::uint16_t v)
{
    p[0] = static_cast<std::uint8_t>(v >> 8);
    p[1] = static_cast<std::uint8_t>(v & 0xFF);
}

void put32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<std::uint8_t>(v & 0xFF);
}

}

std::uint16_t artNetOpCode(const std::uint8_t* p, std::size_t n)
{
    if (!p || n < 12 || std::memcmp(p, kArtId, sizeof(kArtId)) != 0)
        return 0;
    return static_cast<std::uint16_t>(p[8] | (p[9] << 8));
}

bool parseArtDmx(const std::uint8_t* p, std::size_t n, DmxFrame& out)
{
    if (artNetOpCode(p, n) != kArtOpDmx || n < 20)
        return false;
    if (be16(p + 10) < kArtProtocolVersion)
        return false;
    const int length = be16(p + 16);
    if (length < 1 || length > kChannels || n < 18 + static_cast<std::size_t>(length))
        return false;
    out = DmxFrame{};
    out.protocol = Protocol::ArtNet;
    out.sequence = p[12];
    out.universe = ((p[15] & 0x7F) << 8) | p[14];
    out.length = length;
    std::memcpy(out.data.data(), p + 18, static_cast<std::size_t>(length));
    return true;
}

bool parseArtPoll(const std::uint8_t* p, std::size_t n, ArtPollInfo& out)
{
    if (artNetOpCode(p, n) != kArtOpPoll)
        return false;
    out = ArtPollInfo{};
    if (n >= 13)
        out.flags = p[12];
    if (n >= 14)
        out.diagPriority = p[13];
    return true;
}

std::vector<std::uint8_t> buildArtDmx(int portAddress, std::uint8_t sequence, const std::uint8_t* data, int length)
{
    length = std::clamp(length, 0, kChannels);
    std::vector<std::uint8_t> out(18 + static_cast<std::size_t>(length), 0);
    std::memcpy(out.data(), kArtId, sizeof(kArtId));
    out[8] = kArtOpDmx & 0xFF;
    out[9] = kArtOpDmx >> 8;
    put16(&out[10], kArtProtocolVersion);
    out[12] = sequence;
    out[13] = 0;
    out[14] = static_cast<std::uint8_t>(portAddress & 0xFF);
    out[15] = static_cast<std::uint8_t>((portAddress >> 8) & 0x7F);
    put16(&out[16], static_cast<std::uint16_t>(length));
    if (length > 0 && data)
        std::memcpy(&out[18], data, static_cast<std::size_t>(length));
    return out;
}

std::vector<std::uint8_t> buildArtPoll(std::uint8_t flags, std::uint8_t diagPriority)
{
    std::vector<std::uint8_t> out(14, 0);
    std::memcpy(out.data(), kArtId, sizeof(kArtId));
    out[8] = kArtOpPoll & 0xFF;
    out[9] = kArtOpPoll >> 8;
    put16(&out[10], kArtProtocolVersion);
    out[12] = flags;
    out[13] = diagPriority;
    return out;
}

std::vector<std::uint8_t> buildArtPollReply(const ArtNodeInfo& info)
{
    std::vector<std::uint8_t> out(kArtPollReplySize, 0);
    std::uint8_t* p = out.data();
    std::memcpy(p, kArtId, sizeof(kArtId));
    p[8] = kArtOpPollReply & 0xFF;
    p[9] = kArtOpPollReply >> 8;
    std::memcpy(p + 10, info.ip.data(), 4);
    p[14] = kArtNetPort & 0xFF;
    p[15] = kArtNetPort >> 8;
    put16(p + 16, info.version);
    p[18] = static_cast<std::uint8_t>(info.net & 0x7F);
    p[19] = static_cast<std::uint8_t>(info.sub & 0x0F);
    put16(p + 20, info.oem);
    p[22] = 0;
    p[23] = 0xD0;
    p[24] = static_cast<std::uint8_t>(info.estaMan & 0xFF);
    p[25] = static_cast<std::uint8_t>(info.estaMan >> 8);
    putString(p + 26, 18, info.shortName);
    putString(p + 44, 64, info.longName);
    putString(p + 108, 64, info.report);
    const int ports = static_cast<int>(std::min<std::size_t>(info.universes.size(), 4));
    p[172] = 0;
    p[173] = static_cast<std::uint8_t>(ports);
    for (int i = 0; i < ports; ++i) {
        p[174 + i] = 0x80;
        p[182 + i] = info.receiving ? 0x80 : 0x00;
        p[190 + i] = static_cast<std::uint8_t>(info.universes[static_cast<std::size_t>(i)] & 0x0F);
    }
    p[194] = 100;
    p[200] = 0x00;
    std::memcpy(p + 201, info.mac.data(), 6);
    std::memcpy(p + 207, info.ip.data(), 4);
    p[211] = static_cast<std::uint8_t>(info.bindIndex);
    p[212] = 0x08;
    return out;
}

bool parseArtPollReply(const std::uint8_t* p, std::size_t n, ArtPollReplyView& out)
{
    if (artNetOpCode(p, n) != kArtOpPollReply || n < 213)
        return false;
    out = ArtPollReplyView{};
    std::memcpy(out.ip.data(), p + 10, 4);
    out.net = p[18];
    out.sub = p[19];
    out.shortName = readString(p + 26, 18);
    out.longName = readString(p + 44, 64);
    out.numPorts = be16(p + 172);
    std::memcpy(out.portTypes.data(), p + 174, 4);
    std::memcpy(out.swOut.data(), p + 190, 4);
    out.bindIndex = p[211];
    return true;
}

bool parseSacnData(const std::uint8_t* p, std::size_t n, DmxFrame& out, int* startCode)
{
    if (startCode)
        *startCode = -1;
    if (!p || n < static_cast<std::size_t>(kSacnHeaderSize) + 1)
        return false;
    if (be16(p) != 0x0010 || be16(p + 2) != 0x0000 || std::memcmp(p + 4, kAcnId, sizeof(kAcnId)) != 0)
        return false;
    if ((p[16] & 0xF0) != 0x70 || be32(p + 18) != 0x00000004)
        return false;
    if ((p[38] & 0xF0) != 0x70 || be32(p + 40) != 0x00000002)
        return false;
    if ((p[115] & 0xF0) != 0x70 || p[117] != 0x02 || p[118] != 0xA1 || be16(p + 119) != 0 || be16(p + 121) != 1)
        return false;
    const int count = be16(p + 123);
    if (count < 1 || count > kChannels + 1 || n < static_cast<std::size_t>(kSacnHeaderSize) + static_cast<std::size_t>(count))
        return false;
    const int universe = be16(p + 113);
    if (universe < 1 || universe > kMaxUniverse)
        return false;
    if (startCode)
        *startCode = p[kSacnHeaderSize];
    if (p[kSacnHeaderSize] != 0x00)
        return false;

    out = DmxFrame{};
    out.protocol = Protocol::Sacn;
    out.universe = universe;
    out.priority = std::min<std::uint8_t>(p[108], 200);
    out.sequence = p[111];
    out.terminated = (p[112] & 0x40) != 0;
    std::memcpy(out.cid.data(), p + 22, 16);
    out.sourceName = readString(p + 44, 64);
    out.length = count - 1;
    if (out.length > 0)
        std::memcpy(out.data.data(), p + kSacnHeaderSize + 1, static_cast<std::size_t>(out.length));
    return true;
}

std::vector<std::uint8_t> buildSacnData(int universe, const std::array<std::uint8_t, 16>& cid, const std::string& sourceName,
                                        std::uint8_t priority, std::uint8_t sequence, const std::uint8_t* data, int length,
                                        bool terminated)
{
    length = std::clamp(length, 0, kChannels);
    const std::size_t size = static_cast<std::size_t>(kSacnHeaderSize) + 1 + static_cast<std::size_t>(length);
    std::vector<std::uint8_t> out(size, 0);
    std::uint8_t* p = out.data();
    put16(p, 0x0010);
    put16(p + 2, 0x0000);
    std::memcpy(p + 4, kAcnId, sizeof(kAcnId));
    put16(p + 16, static_cast<std::uint16_t>(0x7000 | (size - 16)));
    put32(p + 18, 0x00000004);
    std::memcpy(p + 22, cid.data(), 16);
    put16(p + 38, static_cast<std::uint16_t>(0x7000 | (size - 38)));
    put32(p + 40, 0x00000002);
    putString(p + 44, 64, sourceName);
    p[108] = priority;
    put16(p + 109, 0);
    p[111] = sequence;
    p[112] = terminated ? 0x40 : 0x00;
    put16(p + 113, static_cast<std::uint16_t>(universe));
    put16(p + 115, static_cast<std::uint16_t>(0x7000 | (size - 115)));
    p[117] = 0x02;
    p[118] = 0xA1;
    put16(p + 119, 0x0000);
    put16(p + 121, 0x0001);
    put16(p + 123, static_cast<std::uint16_t>(length + 1));
    p[kSacnHeaderSize] = 0x00;
    if (length > 0 && data)
        std::memcpy(p + kSacnHeaderSize + 1, data, static_cast<std::size_t>(length));
    return out;
}

std::uint32_t sacnMulticastAddress(int universe)
{
    return 0xEFFF0000u | static_cast<std::uint32_t>(universe & 0xFFFF);
}

std::string ipToString(std::uint32_t ip)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    return buf;
}

std::vector<int> parseUniverseList(const std::string& text, int maxCount)
{
    std::vector<int> out;
    std::size_t i = 0;
    auto skipSeparators = [&] {
        while (i < text.size() && (text[i] == ',' || text[i] == ';' || text[i] == ' ' || text[i] == '\t'))
            ++i;
    };
    auto readNumber = [&](long& value) {
        if (i >= text.size() || text[i] < '0' || text[i] > '9')
            return false;
        value = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            value = std::min(value * 10 + (text[i] - '0'), 1000000L);
            ++i;
        }
        return true;
    };
    while (i < text.size()) {
        skipSeparators();
        long a = 0;
        if (!readNumber(a)) {
            if (i < text.size())
                ++i;
            continue;
        }
        long b = a;
        if (i < text.size() && text[i] == '-') {
            ++i;
            if (!readNumber(b))
                b = a;
        }
        if (b < a)
            std::swap(a, b);
        for (long u = std::max(1L, a); u <= std::min<long>(b, kMaxUniverse); ++u) {
            if (static_cast<int>(out.size()) >= maxCount)
                break;
            out.push_back(static_cast<int>(u));
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string formatUniverseList(const std::vector<int>& universes)
{
    std::vector<int> sorted = universes;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    std::string out;
    for (std::size_t i = 0; i < sorted.size();) {
        std::size_t j = i;
        while (j + 1 < sorted.size() && sorted[j + 1] == sorted[j] + 1)
            ++j;
        if (!out.empty())
            out += ", ";
        out += std::to_string(sorted[i]);
        if (j > i + 0)
            out += (j == i + 1 ? ", " : "-") + std::to_string(sorted[j]);
        i = j + 1;
    }
    return out;
}

}
