#pragma once

#include "dmx/DmxEngine.h"
#include "dmx/DmxNet.h"

#include <string>

namespace bsl::dmx {

class Service
{
public:
    enum class State { Off, Waiting, Receiving, Lost, Error };

    Service() = default;

    void update(const NetConfig& cfg, Composition& comp, control::KeyService& keys, const control::SelectionRef& selection);
    void shutdown();

    Receiver& receiver() { return receiver_; }
    Engine& engine() { return engine_; }
    const Engine& engine() const { return engine_; }
    const Snapshot& snapshot() const { return snap_; }
    const std::string& error() const { return error_; }
    State state() const;
    void requestRestart() { restart_ = true; }

private:
    Receiver receiver_;
    Engine engine_;
    Snapshot snap_;
    NetConfig started_;
    std::vector<int> startedUniverses_;
    bool haveStarted_ = false;
    bool restart_ = false;
    double nextRetry_ = 0.0;
    std::string error_;
};

}
