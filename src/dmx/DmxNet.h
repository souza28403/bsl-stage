#pragma once

#include "dmx/DmxMerge.h"
#include "dmx/DmxProtocol.h"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bsl::dmx {

struct NetInterface
{
    std::string name;
    std::string ip;
    std::string mask;
    std::array<std::uint8_t, 6> mac{};
    bool loopback = false;
    std::uint32_t ipHost = 0;
    std::uint32_t maskHost = 0;
};
std::vector<NetInterface> listInterfaces(bool includeLoopback = true);

struct NetConfig
{
    bool enabled = false;
    std::string interfaceIp;
    std::string universesText = "1-4";
    bool artNet = true;
    bool sacn = true;
    MergeMode merge = MergeMode::Htp;
    double timeoutSec = 2.5;
    LossPolicy loss = LossPolicy::Hold;
    int artNetFirstUniverse = 0;
    std::uint8_t artNetPriority = 100;
    std::string nodeName = "BSL Stage";

    bool operator==(const NetConfig&) const = default;
};
nlohmann::json netConfigToJson(const NetConfig& c);
NetConfig netConfigFromJson(const nlohmann::json& j);

struct UniverseView
{
    int universe = 0;
    DmxData data{};
    bool everSeen = false;
    bool active = false;
    bool lost = false;
    bool released = false;
    double lastFrame = 0.0;
    float packetsPerSec = 0.0f;
    std::uint64_t packets = 0;
    std::vector<SourceStat> sources;
};

struct Snapshot
{
    bool running = false;
    double now = 0.0;
    std::string status;
    bool artNetOk = false;
    bool sacnOk = false;
    int multicastJoined = 0;
    int multicastFailed = 0;
    std::uint64_t totalPackets = 0;
    std::uint64_t artPolls = 0;
    std::uint64_t artReplies = 0;
    std::vector<UniverseView> universes;

    const UniverseView* find(int universe) const;
    UniverseView* find(int universe);
    bool receiving(double seconds = 1.0) const;
};

class Receiver
{
public:
    Receiver();
    ~Receiver();
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    static double clock();

    bool start(const NetConfig& config, const std::vector<int>& universes, std::string* error);
    void stop();
    bool running() const { return running_.load(); }

    bool poll(Snapshot& out);

    void setPorts(std::uint16_t artNetPort, std::uint16_t sacnPort);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
    std::uint16_t artPort_ = kArtNetPort;
    std::uint16_t sacnPort_ = kSacnPort;
};

}
