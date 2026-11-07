#include "dmx/DmxMerge.h"

#include <algorithm>

namespace bsl::dmx {

void UniverseMerger::configure(const MergeConfig& config)
{
    config_ = config;
    recompute();
}

bool UniverseMerger::sameSource(const Source& s, const DmxFrame& f, std::uint32_t ip) const
{
    if (s.protocol != f.protocol)
        return false;
    return f.protocol == Protocol::Sacn ? s.cid == f.cid : s.ip == ip;
}

bool UniverseMerger::ingest(const DmxFrame& frame, std::uint32_t ip, double now)
{
    Source* src = nullptr;
    for (Source& s : sources_)
        if (sameSource(s, frame, ip)) {
            src = &s;
            break;
        }

    if (src) {
        const bool useSeq = frame.protocol == Protocol::Sacn || frame.sequence != 0;
        if (useSeq && src->seqValid && src->live) {
            const int diff = static_cast<std::int8_t>(static_cast<std::uint8_t>(frame.sequence - src->lastSeq));
            if (diff <= 0 && diff > -20)
                return false;
        }
    } else {
        if (frame.terminated)
            return false;
        if (sources_.size() >= kMaxSources) {
            auto oldest = std::min_element(sources_.begin(), sources_.end(),
                                           [](const Source& a, const Source& b) { return a.lastSeen < b.lastSeen; });
            sources_.erase(oldest);
        }
        sources_.emplace_back();
        src = &sources_.back();
        src->protocol = frame.protocol;
        src->ip = ip;
        src->cid = frame.cid;
        src->data = DmxData{};
    }

    const bool wasLive = src->live;
    src->ip = ip;
    src->name = frame.sourceName;
    src->priority = frame.protocol == Protocol::Sacn ? frame.priority : config_.artNetPriority;
    src->lastSeen = now;
    src->lastSeq = frame.sequence;
    src->seqValid = frame.protocol == Protocol::Sacn || frame.sequence != 0;
    ++src->packets;
    src->terminated = frame.terminated;
    src->live = !frame.terminated;

    const bool fresh = src->packets == 1 || !wasLive;
    for (int i = 0; i < kChannels; ++i) {
        const std::uint8_t v = i < frame.length ? frame.data[static_cast<std::size_t>(i)] : std::uint8_t{0};
        if (fresh || src->data[static_cast<std::size_t>(i)] != v)
            src->stamp[static_cast<std::size_t>(i)] = ++counter_;
        src->data[static_cast<std::size_t>(i)] = v;
    }

    ++packets_;
    lastFrame_ = now;
    everSeen_ = true;
    recompute();
    return true;
}

void UniverseMerger::tick(double now)
{
    bool changed = false;
    for (Source& s : sources_) {
        if (s.live && now - s.lastSeen > config_.timeoutSec) {
            s.live = false;
            changed = true;
        }
    }
    const std::size_t before = sources_.size();
    std::erase_if(sources_, [&](const Source& s) { return !s.live && now - s.lastSeen > config_.timeoutSec * 12.0 + 30.0; });
    if (changed || sources_.size() != before)
        recompute();
}

void UniverseMerger::recompute()
{
    int maxPriority = -1;
    for (const Source& s : sources_)
        if (s.live)
            maxPriority = std::max<int>(maxPriority, s.priority);

    active_ = maxPriority >= 0;
    if (!active_) {
        if (config_.loss == LossPolicy::Release && everSeen_)
            output_.fill(0);
        return;
    }

    DmxData out{};
    std::array<std::uint32_t, kChannels> best{};
    for (const Source& s : sources_) {
        if (!s.live || s.priority != maxPriority)
            continue;
        for (std::size_t i = 0; i < static_cast<std::size_t>(kChannels); ++i) {
            if (config_.mode == MergeMode::Htp) {
                out[i] = std::max(out[i], s.data[i]);
            } else if (s.stamp[i] > best[i]) {
                best[i] = s.stamp[i];
                out[i] = s.data[i];
            }
        }
    }
    output_ = out;
}

std::vector<SourceStat> UniverseMerger::sources() const
{
    int maxPriority = -1;
    for (const Source& s : sources_)
        if (s.live)
            maxPriority = std::max<int>(maxPriority, s.priority);
    std::vector<SourceStat> out;
    for (const Source& s : sources_) {
        SourceStat st;
        st.protocol = s.protocol;
        st.ip = s.ip;
        st.name = s.name;
        st.priority = s.priority;
        st.lastSeen = s.lastSeen;
        st.packets = s.packets;
        st.live = s.live;
        st.inMerge = s.live && s.priority == maxPriority;
        out.push_back(std::move(st));
    }
    return out;
}

std::size_t UniverseMerger::liveSources() const
{
    return static_cast<std::size_t>(std::count_if(sources_.begin(), sources_.end(), [](const Source& s) { return s.live; }));
}

}
