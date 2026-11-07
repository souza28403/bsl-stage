#pragma once

#include "dmx/DmxProtocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bsl::dmx {

enum class MergeMode : std::uint8_t { Htp, Ltp };

enum class LossPolicy : std::uint8_t {
    Hold,
    Release
};

struct MergeConfig
{
    MergeMode mode = MergeMode::Htp;
    double timeoutSec = 2.5;
    LossPolicy loss = LossPolicy::Hold;
    std::uint8_t artNetPriority = 100;

    bool operator==(const MergeConfig&) const = default;
};

struct SourceStat
{
    Protocol protocol = Protocol::ArtNet;
    std::uint32_t ip = 0;
    std::string name;
    std::uint8_t priority = 100;
    double lastSeen = 0.0;
    std::uint64_t packets = 0;
    bool live = true;
    bool inMerge = true;
};

class UniverseMerger
{
public:
    void configure(const MergeConfig& config);
    const MergeConfig& config() const { return config_; }

    bool ingest(const DmxFrame& frame, std::uint32_t ip, double now);
    void tick(double now);

    const DmxData& output() const { return output_; }
    bool active() const { return active_; }
    bool everSeen() const { return everSeen_; }
    bool lost() const { return everSeen_ && !active_; }
    double lastFrame() const { return lastFrame_; }
    std::uint64_t packets() const { return packets_; }
    std::vector<SourceStat> sources() const;
    std::size_t liveSources() const;

private:
    struct Source
    {
        Protocol protocol = Protocol::ArtNet;
        std::uint32_t ip = 0;
        std::array<std::uint8_t, 16> cid{};
        std::string name;
        std::uint8_t priority = 100;
        double lastSeen = 0.0;
        std::uint64_t packets = 0;
        std::uint8_t lastSeq = 0;
        bool seqValid = false;
        bool terminated = false;
        bool live = true;
        DmxData data{};
        std::array<std::uint32_t, kChannels> stamp{};
    };
    static constexpr std::size_t kMaxSources = 16;

    void recompute();
    bool sameSource(const Source& s, const DmxFrame& f, std::uint32_t ip) const;

    MergeConfig config_;
    std::vector<Source> sources_;
    DmxData output_{};
    bool active_ = false;
    bool everSeen_ = false;
    double lastFrame_ = 0.0;
    std::uint64_t packets_ = 0;
    std::uint32_t counter_ = 0;
};

}
