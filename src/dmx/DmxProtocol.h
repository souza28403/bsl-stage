#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bsl::dmx {

inline constexpr int kChannels = 512;
inline constexpr std::uint16_t kArtNetPort = 6454;
inline constexpr std::uint16_t kSacnPort = 5568;
inline constexpr int kMaxUniverse = 63999;

enum class Protocol : std::uint8_t { ArtNet, Sacn };

using DmxData = std::array<std::uint8_t, kChannels>;

struct DmxFrame
{
    Protocol protocol = Protocol::ArtNet;
    int universe = 0;
    std::uint8_t sequence = 0;
    std::uint8_t priority = 100;
    bool terminated = false;
    std::array<std::uint8_t, 16> cid{};
    std::string sourceName;
    int length = 0;
    DmxData data{};
};

inline constexpr std::uint16_t kArtOpPoll = 0x2000;
inline constexpr std::uint16_t kArtOpPollReply = 0x2100;
inline constexpr std::uint16_t kArtOpDmx = 0x5000;
inline constexpr int kArtPollReplySize = 239;

std::uint16_t artNetOpCode(const std::uint8_t* p, std::size_t n);

bool parseArtDmx(const std::uint8_t* p, std::size_t n, DmxFrame& out);

struct ArtPollInfo
{
    std::uint8_t flags = 0;
    std::uint8_t diagPriority = 0;
};
bool parseArtPoll(const std::uint8_t* p, std::size_t n, ArtPollInfo& out);

std::vector<std::uint8_t> buildArtDmx(int portAddress, std::uint8_t sequence, const std::uint8_t* data, int length);
std::vector<std::uint8_t> buildArtPoll(std::uint8_t flags = 0x02, std::uint8_t diagPriority = 0x10);

struct ArtNodeInfo
{
    std::array<std::uint8_t, 4> ip{};
    std::array<std::uint8_t, 6> mac{};
    std::string shortName = "BSL Stage";
    std::string longName = "BSL Stage - VJ / media server";
    std::string report;
    int net = 0;
    int sub = 0;
    std::vector<int> universes;
    int bindIndex = 1;
    std::uint8_t numPortsTotal = 0;
    std::uint16_t oem = 0x00FF;
    std::uint16_t estaMan = 0x7FF0;
    std::uint16_t version = 1;
    bool receiving = false;
};
std::vector<std::uint8_t> buildArtPollReply(const ArtNodeInfo& info);

struct ArtPollReplyView
{
    std::array<std::uint8_t, 4> ip{};
    std::string shortName;
    std::string longName;
    int net = 0, sub = 0;
    int numPorts = 0;
    std::array<std::uint8_t, 4> portTypes{};
    std::array<std::uint8_t, 4> swOut{};
    int bindIndex = 0;
};
bool parseArtPollReply(const std::uint8_t* p, std::size_t n, ArtPollReplyView& out);

inline constexpr int kSacnHeaderSize = 125;
inline constexpr int kSacnOtherVectorDiscovery = 0x00000008;

bool parseSacnData(const std::uint8_t* p, std::size_t n, DmxFrame& out, int* startCode = nullptr);
std::vector<std::uint8_t> buildSacnData(int universe, const std::array<std::uint8_t, 16>& cid, const std::string& sourceName,
                                        std::uint8_t priority, std::uint8_t sequence, const std::uint8_t* data, int length,
                                        bool terminated = false);
std::uint32_t sacnMulticastAddress(int universe);
std::string ipToString(std::uint32_t ipHostOrder);

std::vector<int> parseUniverseList(const std::string& text, int maxCount = 256);
std::string formatUniverseList(const std::vector<int>& universes);

inline std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }
inline std::uint32_t be32(const std::uint8_t* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

}
