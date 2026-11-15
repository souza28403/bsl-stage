#include "audio/AudioAnalyzer.h"

#include "app/Log.h"
#include "audio/AudioDsp.h"

#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>

namespace bsl::audio {

namespace {

using Clock = std::chrono::steady_clock;

struct DevEntry
{
    std::string id;
    std::string name;
    ma_device_id maId{};
    bool monitor = false;
};

bool hasSuffix(const std::string& s, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool initContext(ma_context& ctx)
{
    ma_context_config cfg = ma_context_config_init();
    cfg.pulse.pApplicationName = "BSL Stage";
    const ma_backend backends[] = {ma_backend_pulseaudio, ma_backend_alsa};
    return ma_context_init(backends, 2, &cfg, &ctx) == MA_SUCCESS;
}

std::vector<DevEntry> captureEntries(ma_context& ctx)
{
    std::vector<DevEntry> out;
    ma_device_info* playback = nullptr;
    ma_device_info* capture = nullptr;
    ma_uint32 playbackCount = 0, captureCount = 0;
    if (ma_context_get_devices(&ctx, &playback, &playbackCount, &capture, &captureCount) != MA_SUCCESS || !capture)
        return out;
    for (ma_uint32 i = 0; i < captureCount; ++i) {
        DevEntry e;
        e.maId = capture[i].id;
        e.name = capture[i].name;
        if (ctx.backend == ma_backend_pulseaudio) {
            e.id = capture[i].id.pulse;
            e.monitor = hasSuffix(e.id, ".monitor");
        } else {
            e.id = capture[i].id.alsa;
        }
        if (e.id.empty())
            continue;
        out.push_back(std::move(e));
    }
    return out;
}

struct Capture
{
    std::mutex mutex;
    std::vector<float> queue;
    std::size_t maxSamples = 48000;
};

void dataCallback(ma_device* device, void*, const void* input, ma_uint32 frames)
{
    auto* cap = static_cast<Capture*>(device->pUserData);
    if (!cap || !input || frames == 0)
        return;
    const float* in = static_cast<const float*>(input);
    std::lock_guard lk(cap->mutex);
    if (cap->queue.size() + frames > cap->maxSamples)
        cap->queue.clear();
    cap->queue.insert(cap->queue.end(), in, in + frames);
}

}

struct AudioAnalyzer::Impl
{
    mutable std::mutex mutex;
    std::condition_variable cv;
    AudioConfig config;
    std::atomic<std::uint64_t> dspGen{1};
    std::atomic<std::uint64_t> deviceGen{1};
    AudioSnapshot snap;
    std::optional<AudioSnapshot> override_;
    std::string status = "Parado";
    std::thread thread;
    std::atomic<bool> stopFlag{false};
    bool started = false;
    unsigned rotate = 0;

    AudioDsp dsp;

    void setStatus(const std::string& s)
    {
        std::lock_guard lk(mutex);
        status = s;
    }

    void publish(bool active)
    {
        const AudioSnapshot s = dsp.snapshot(active);
        std::lock_guard lk(mutex);
        snap = s;
    }

    void publishZero()
    {
        std::lock_guard lk(mutex);
        snap = AudioSnapshot{};
    }

    bool waitFor(std::chrono::milliseconds ms)
    {
        std::unique_lock lk(mutex);
        cv.wait_for(lk, ms, [&] { return stopFlag.load(); });
        return !stopFlag.load();
    }

    void run();
    void session(const AudioConfig& cfg);
};

void AudioAnalyzer::Impl::run()
{
    while (!stopFlag.load()) {
        AudioConfig cfg;
        std::uint64_t gen = 0;
        {
            std::lock_guard lk(mutex);
            cfg = config;
            gen = deviceGen.load();
        }
        if (!cfg.enabled) {
            publishZero();
            setStatus("Desligado");
            while (!stopFlag.load() && deviceGen.load() == gen)
                waitFor(std::chrono::milliseconds(100));
            continue;
        }
        session(cfg);
        if (stopFlag.load())
            break;
        if (deviceGen.load() == gen)
            waitFor(std::chrono::milliseconds(1500));
    }
    publishZero();
}

void AudioAnalyzer::Impl::session(const AudioConfig& cfg)
{
    const bool loopback = cfg.source == AudioSource::Loopback;
    ma_context ctx;
    if (!initContext(ctx)) {
        publishZero();
        setStatus("Áudio indisponível (sem servidor de áudio)");
        return;
    }

    const std::vector<DevEntry> all = captureEntries(ctx);
    std::vector<DevEntry> monitors;
    for (const DevEntry& e : all)
        if (e.monitor)
            monitors.push_back(e);

    const DevEntry* chosen = nullptr;
    if (!cfg.deviceId.empty()) {
        for (const DevEntry& e : all)
            if (e.id == cfg.deviceId) {
                chosen = &e;
                break;
            }
    } else if (loopback && !monitors.empty()) {
        chosen = &monitors[rotate % monitors.size()];
    }
    if (!chosen && (loopback || !cfg.deviceId.empty())) {
        publishZero();
        if (!cfg.deviceId.empty())
            setStatus("Dispositivo de áudio não encontrado");
        else
            setStatus("Sem dispositivo de saída de áudio para capturar");
        ma_context_uninit(&ctx);
        return;
    }
    if (!chosen && all.empty()) {
        publishZero();
        setStatus("Sem dispositivo de entrada de áudio");
        ma_context_uninit(&ctx);
        return;
    }

    Capture cap;
    ma_device_config dcfg = ma_device_config_init(ma_device_type_capture);
    dcfg.capture.format = ma_format_f32;
    dcfg.capture.channels = 1;
    dcfg.capture.pDeviceID = chosen ? &chosen->maId : nullptr;
    dcfg.sampleRate = 0;
    dcfg.periodSizeInMilliseconds = 10;
    dcfg.dataCallback = dataCallback;
    dcfg.pUserData = &cap;

    ma_device device;
    const std::string name = chosen ? chosen->name : "Entrada padrão";
    if (ma_device_init(&ctx, &dcfg, &device) != MA_SUCCESS) {
        publishZero();
        setStatus("Não foi possível abrir: " + name);
        ma_context_uninit(&ctx);
        return;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        publishZero();
        setStatus("Não foi possível iniciar: " + name);
        ma_device_uninit(&device);
        ma_context_uninit(&ctx);
        return;
    }

    const int sampleRate = static_cast<int>(device.sampleRate);
    cap.maxSamples = static_cast<std::size_t>(std::max(sampleRate, 8000));
    dsp.reset();
    dsp.setSampleRate(sampleRate);
    std::uint64_t seenDsp = 0;
    const std::uint64_t myDeviceGen = deviceGen.load();
    setStatus(std::string(loopback ? "Som do PC: " : "Entrada: ") + name);

    std::vector<float> block;
    auto lastData = Clock::now();
    auto lastSound = Clock::now();
    auto lastTick = Clock::now();
    while (!stopFlag.load() && deviceGen.load() == myDeviceGen) {
        if (dspGen.load() != seenDsp) {
            seenDsp = dspGen.load();
            std::lock_guard lk(mutex);
            dsp.setConfig(config);
        }
        block.clear();
        {
            std::lock_guard lk(cap.mutex);
            block.swap(cap.queue);
        }
        const auto now = Clock::now();
        if (!block.empty()) {
            float peak = 0.0f;
            for (const float v : block)
                peak = std::max(peak, std::fabs(v));
            if (peak > 0.0005f)
                lastSound = now;
            dsp.feed(block.data(), block.size());
            lastData = now;
            publish(true);
        } else if (now - lastData > std::chrono::milliseconds(40)) {
            dsp.feedSilence(std::chrono::duration<double>(now - lastTick).count());
            publish(true);
        }
        if (loopback && cfg.deviceId.empty() && monitors.size() > 1 && now - lastSound > std::chrono::milliseconds(3000)) {
            ++rotate;
            break;
        }
        lastTick = now;
        waitFor(std::chrono::milliseconds(5));
    }
    ma_device_uninit(&device);
    ma_context_uninit(&ctx);
    publishZero();
}

AudioAnalyzer::AudioAnalyzer() : impl_(std::make_unique<Impl>()) {}

AudioAnalyzer::~AudioAnalyzer()
{
    stop();
}

AudioAnalyzer& AudioAnalyzer::instance()
{
    static AudioAnalyzer a;
    return a;
}

void AudioAnalyzer::start()
{
    std::lock_guard lk(impl_->mutex);
    if (impl_->started)
        return;
    impl_->started = true;
    impl_->stopFlag = false;
    impl_->thread = std::thread([this] { impl_->run(); });
}

void AudioAnalyzer::stop()
{
    {
        std::lock_guard lk(impl_->mutex);
        if (!impl_->started)
            return;
        impl_->stopFlag = true;
    }
    impl_->cv.notify_all();
    if (impl_->thread.joinable())
        impl_->thread.join();
    std::lock_guard lk(impl_->mutex);
    impl_->started = false;
    impl_->snap = AudioSnapshot{};
    impl_->status = "Parado";
}

bool AudioAnalyzer::running() const
{
    std::lock_guard lk(impl_->mutex);
    return impl_->started;
}

void AudioAnalyzer::setConfig(const AudioConfig& config)
{
    AudioConfig c = config;
    c.gain = std::clamp(c.gain, 0.1f, 4.0f);
    c.gate = std::clamp(c.gate, 0.0f, 0.15f);
    c.attackMs = std::clamp(c.attackMs, 0.0f, 200.0f);
    c.releaseScale = std::clamp(c.releaseScale, 0.25f, 4.0f);
    {
        std::lock_guard lk(impl_->mutex);
        const bool deviceChange = c.enabled != impl_->config.enabled || c.source != impl_->config.source ||
                                  c.deviceId != impl_->config.deviceId;
        const bool any = c != impl_->config;
        impl_->config = c;
        if (any)
            ++impl_->dspGen;
        if (deviceChange)
            ++impl_->deviceGen;
    }
    impl_->cv.notify_all();
}

AudioConfig AudioAnalyzer::config() const
{
    std::lock_guard lk(impl_->mutex);
    return impl_->config;
}

AudioSnapshot AudioAnalyzer::snapshot() const
{
    std::lock_guard lk(impl_->mutex);
    return impl_->override_ ? *impl_->override_ : impl_->snap;
}

std::string AudioAnalyzer::statusText() const
{
    std::lock_guard lk(impl_->mutex);
    return impl_->status;
}

void AudioAnalyzer::setOverride(const std::optional<AudioSnapshot>& snapshot)
{
    std::lock_guard lk(impl_->mutex);
    impl_->override_ = snapshot;
}

std::vector<AudioDeviceInfo> AudioAnalyzer::listDevices(AudioSource source)
{
    std::vector<AudioDeviceInfo> out;
    ma_context ctx;
    if (!initContext(ctx))
        return out;
    const bool wantMonitor = source == AudioSource::Loopback;
    for (const DevEntry& e : captureEntries(ctx))
        if (e.monitor == wantMonitor)
            out.push_back({e.id, e.name});
    ma_context_uninit(&ctx);
    return out;
}

}
