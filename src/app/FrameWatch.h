#pragma once

#include <atomic>
#include <thread>

namespace bsl {

class FrameWatch
{
public:
    enum Phase
    {
        Events,
        Update,
        Compositor,
        Outputs,
        Ui,
        FloatingWindows,
        Present,
        Limiter,
        Idle,
        PhaseCount
    };

    FrameWatch() = default;
    ~FrameWatch();
    FrameWatch(const FrameWatch&) = delete;
    FrameWatch& operator=(const FrameWatch&) = delete;

    void start();
    void stop();
    void enter(Phase phase);

    static const char* name(int phase);

    class Blocking
    {
    public:
        Blocking();
        ~Blocking();
        Blocking(const Blocking&) = delete;
        Blocking& operator=(const Blocking&) = delete;
    };

private:
    void run();

    std::atomic<int> phase_{Idle};
    std::atomic<long long> phaseStartMs_{0};
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

}
