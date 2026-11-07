#pragma once

#include "ai/AiRuntime.h"

#include <glad/gl.h>

#include <cstdint>
#include <string>
#include <vector>

namespace bsl::ai {

enum AiNeed : unsigned { kNeedDepth = 1u, kNeedPerson = 2u };

enum class AiState
{
    Off,
    Unavailable,
    Warming,
    Ready
};

struct AiFrame
{
    AiState state = AiState::Off;
    GLuint depth = 0;
    GLuint mask = 0;
    int depthW = 0, depthH = 0;
    int maskW = 0, maskH = 0;
    std::string message;
};

std::string aiStatusText(const AiFrame& frame);

struct ModelStats
{
    bool present = false;
    bool loaded = false;
    bool onGpu = false;
    std::string device;
    std::string error;
    double lastMs = 0.0;
    double avgMs = 0.0;
    std::uint64_t runs = 0;
};

struct AiStats
{
    bool enabled = false;
    int rate = 0;
    bool runtimeLoaded = false;
    bool runtimeTried = false;
    std::string runtimeVersion;
    std::string runtimeError;
    std::string gpuName;
    float inferencesPerSec = 0.0f;
    int activeSources = 0;
    ModelStats models[kModelKindCount];
    bool testRunning = false;
    bool testDone = false;
    std::string testReport;
};

class AiEngine
{
public:
    static AiEngine& instance();
    ~AiEngine();
    AiEngine(const AiEngine&) = delete;
    AiEngine& operator=(const AiEngine&) = delete;

    void setProfileRate(int fps);
    void setRateOverride(int fps);
    void setDevicePref(DevicePref pref);
    bool enabled() const;
    int rate() const;
    void refreshModelPresence();

    AiFrame acquire(std::uint64_t sourceId, std::uint64_t frameIndex, GLuint inputTex, int w, int h, bool topOrigin,
                    unsigned need, bool eco = false);

    void setOverride(std::uint64_t sourceId, GLuint depthTex, GLuint maskTex);

    AiStats stats() const;
    void requestSelfTest();

    void shutdown();

private:
    AiEngine();
    struct Impl;
    Impl* impl_;
};

}
