#include "app/FrameWatch.h"

#include "app/Log.h"

#include <atomic>
#include <chrono>
#include <string>

namespace bsl {

namespace {

constexpr long long kSlowPhaseMs = 500;
constexpr long long kStallMs = 3000;
constexpr long long kPollMs = 200;

std::atomic<int> g_blocking{0};
std::atomic<long long> g_blockingEndMs{0};

long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool countsAsWork(int phase)
{
    return phase != FrameWatch::Limiter && phase != FrameWatch::Idle;
}

}

FrameWatch::~FrameWatch()
{
    stop();
}

FrameWatch::Blocking::Blocking()
{
    ++g_blocking;
}

FrameWatch::Blocking::~Blocking()
{
    g_blockingEndMs = nowMs();
    --g_blocking;
}

const char* FrameWatch::name(int phase)
{
    switch (phase) {
    case Events: return "eventos do sistema";
    case Update: return "atualização";
    case Compositor: return "compositor";
    case Outputs: return "janelas de saída";
    case Ui: return "interface";
    case FloatingWindows: return "janelas soltas";
    case Present: return "troca de buffers";
    case Limiter: return "limitador de FPS";
    default: return "parado";
    }
}

void FrameWatch::start()
{
    if (thread_.joinable())
        return;
    stop_ = false;
    phaseStartMs_ = nowMs();
    thread_ = std::thread([this] { run(); });
}

void FrameWatch::stop()
{
    stop_ = true;
    if (thread_.joinable())
        thread_.join();
}

void FrameWatch::enter(Phase phase)
{
    const long long now = nowMs();
    const int previous = phase_.load();
    const long long took = now - phaseStartMs_.load();
    const bool hadBlocking = g_blocking.load() > 0 || g_blockingEndMs.load() >= phaseStartMs_.load();
    if (countsAsWork(previous) && took >= kSlowPhaseMs && !hadBlocking)
        log::warn(std::string("Etapa do quadro demorou ") + std::to_string(took) + " ms: " + name(previous));
    phase_ = phase;
    phaseStartMs_ = now;
}

void FrameWatch::run()
{
    bool reported = false;
    long long reportedPhaseStart = 0;
    while (!stop_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
        const int phase = phase_.load();
        const long long start = phaseStartMs_.load();
        const long long age = nowMs() - start;
        if (phase == Idle || phase == Limiter) {
            reported = false;
            continue;
        }
        const bool hadBlocking = g_blocking.load() > 0 || g_blockingEndMs.load() >= start;
        if (!reported && age >= kStallMs && !hadBlocking) {
            reported = true;
            reportedPhaseStart = start;
            log::warn(std::string("Interface sem responder há ") + std::to_string(age / 1000) + " s na etapa: " + name(phase));
        } else if (reported && start != reportedPhaseStart) {
            reported = false;
        }
    }
}

}
