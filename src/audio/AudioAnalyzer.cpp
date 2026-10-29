#include "audio/AudioAnalyzer.h"

#include "app/Log.h"
#include "audio/AudioDsp.h"

#include <windows.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <initguid.h> // com INITGUID, o propkeydef.h define os PKEY_* deste arquivo (sem precisar de uma lib)
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>

namespace bsl::audio {

namespace {

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

std::string toUtf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(std::max(n, 0)), '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring toWide(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(std::max(n, 0)), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string friendlyName(IMMDevice* dev)
{
    ComPtr<IPropertyStore> store;
    if (FAILED(dev->OpenPropertyStore(STGM_READ, &store)) || !store)
        return "Dispositivo de áudio";
    PROPVARIANT v;
    PropVariantInit(&v);
    std::string name = "Dispositivo de áudio";
    if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal)
        name = toUtf8(v.pwszVal);
    PropVariantClear(&v);
    return name;
}

std::string deviceId(IMMDevice* dev)
{
    LPWSTR raw = nullptr;
    std::string id;
    if (SUCCEEDED(dev->GetId(&raw)) && raw) {
        id = toUtf8(raw);
        CoTaskMemFree(raw);
    }
    return id;
}

struct ComScope
{
    HRESULT hr;
    explicit ComScope(DWORD model) : hr(CoInitializeEx(nullptr, model)) {}
    ~ComScope()
    {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
};

// A saida que esta tocando agora (a de maior pico); sem nada tocando, a padrao. Como o AudioCapture dos plugins.
ComPtr<IMMDevice> findActiveRenderDevice(IMMDeviceEnumerator* enumerator)
{
    ComPtr<IMMDeviceCollection> col;
    ComPtr<IMMDevice> best;
    float bestPeak = -1.0f;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
        UINT count = 0;
        col->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> dev;
            if (FAILED(col->Item(i, &dev)) || !dev)
                continue;
            float peak = 0.0f;
            ComPtr<IAudioMeterInformation> meter;
            if (SUCCEEDED(dev->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr,
                                        reinterpret_cast<void**>(meter.GetAddressOf()))) && meter)
                meter->GetPeakValue(&peak);
            if (peak > bestPeak) {
                bestPeak = peak;
                best = dev;
            }
        }
    }
    if (!best || bestPeak <= 0.0001f) {
        ComPtr<IMMDevice> def;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &def)) && def)
            return def;
    }
    return best;
}

bool isFloatFormat(const WAVEFORMATEX* f)
{
    if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return true;
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22)
        return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f)->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT;
    return false;
}

// Pacote de qualquer formato PCM/float -> mono (media dos canais)
void toMono(const BYTE* data, UINT32 frames, const WAVEFORMATEX* f, std::vector<float>& out)
{
    const int ch = std::max<int>(f->nChannels, 1);
    out.resize(frames);
    const bool isFloat = isFloatFormat(f);
    const int bits = f->wBitsPerSample;
    for (UINT32 i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int c = 0; c < ch; ++c) {
            const size_t idx = static_cast<size_t>(i) * static_cast<size_t>(ch) + static_cast<size_t>(c);
            if (isFloat && bits == 32) {
                sum += reinterpret_cast<const float*>(data)[idx];
            } else if (bits == 16) {
                sum += static_cast<float>(reinterpret_cast<const std::int16_t*>(data)[idx]) / 32768.0f;
            } else if (bits == 24) {
                const BYTE* p = data + idx * 3;
                std::int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
                if (v & 0x800000)
                    v |= static_cast<std::int32_t>(0xFF000000);
                sum += static_cast<float>(v) / 8388608.0f;
            } else if (bits == 32) {
                sum += static_cast<float>(reinterpret_cast<const std::int32_t*>(data)[idx]) / 2147483648.0f;
            }
        }
        out[i] = sum / static_cast<float>(ch);
    }
}

} // namespace

struct AudioAnalyzer::Impl
{
    mutable std::mutex mutex;
    std::condition_variable cv;
    AudioConfig config;
    std::atomic<std::uint64_t> dspGen{1};    // qualquer mudanca de configuracao
    std::atomic<std::uint64_t> deviceGen{1}; // mudou o que exige reconectar (ligado, fonte, dispositivo)
    AudioSnapshot snap;
    std::optional<AudioSnapshot> override_;
    std::string status = "Parado";
    std::thread thread;
    std::atomic<bool> stopFlag{false};
    bool started = false;

    AudioDsp dsp; // so a thread de captura mexe

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
    // Uma conexao: devolve quando precisa reconectar (config mudou, dispositivo caiu) ou parar
    void session(const AudioConfig& cfg, IMMDeviceEnumerator* enumerator);
};

void AudioAnalyzer::Impl::run()
{
    ComScope com(COINIT_MULTITHREADED);
    if (FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE) {
        setStatus("Áudio indisponível (COM)");
        return;
    }
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) || !enumerator) {
        setStatus("Áudio indisponível (sem MMDevice)");
        return;
    }

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
            // espera uma mudanca de configuracao (ou o fim)
            while (!stopFlag.load() && deviceGen.load() == gen)
                waitFor(std::chrono::milliseconds(100));
            continue;
        }
        session(cfg, enumerator.Get());
        if (stopFlag.load())
            break;
        if (deviceGen.load() == gen) // a sessao acabou por falha, nao por mudanca: tenta de novo em 1,5 s
            waitFor(std::chrono::milliseconds(1500));
    }
    publishZero();
}

void AudioAnalyzer::Impl::session(const AudioConfig& cfg, IMMDeviceEnumerator* enumerator)
{
    const bool loopback = cfg.source == AudioSource::Loopback;
    ComPtr<IMMDevice> device;
    if (!cfg.deviceId.empty()) {
        const std::wstring wid = toWide(cfg.deviceId);
        enumerator->GetDevice(wid.c_str(), &device);
    }
    if (!device) {
        if (loopback)
            device = findActiveRenderDevice(enumerator);
        else
            enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
    }
    if (!device) {
        publishZero();
        setStatus(loopback ? "Sem dispositivo de saída de áudio" : "Sem dispositivo de entrada de áudio");
        return;
    }
    const std::string name = friendlyName(device.Get());

    ComPtr<IAudioClient> client;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()))) || !client) {
        publishZero();
        setStatus("Não foi possível abrir: " + name);
        return;
    }
    WAVEFORMATEX* mix = nullptr;
    if (FAILED(client->GetMixFormat(&mix)) || !mix) {
        publishZero();
        setStatus("Formato de áudio indisponível: " + name);
        return;
    }
    const DWORD flags = loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
    const HRESULT initHr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 10000000, 0, mix, nullptr);
    ComPtr<IAudioCaptureClient> capture;
    if (FAILED(initHr) || FAILED(client->GetService(IID_PPV_ARGS(&capture))) || !capture) {
        CoTaskMemFree(mix);
        publishZero();
        setStatus("Não foi possível capturar: " + name);
        return;
    }
    if (FAILED(client->Start())) {
        CoTaskMemFree(mix);
        publishZero();
        setStatus("Não foi possível iniciar: " + name);
        return;
    }

    // Copia o formato (inclusive a extensao) antes de liberar o original
    std::vector<BYTE> formatBytes(sizeof(WAVEFORMATEX) + mix->cbSize);
    std::memcpy(formatBytes.data(), mix, formatBytes.size());
    CoTaskMemFree(mix);
    const auto* fmt = reinterpret_cast<const WAVEFORMATEX*>(formatBytes.data());

    dsp.reset();
    dsp.setSampleRate(static_cast<int>(fmt->nSamplesPerSec));
    std::uint64_t seenDsp = 0;
    const std::uint64_t myDeviceGen = deviceGen.load();
    setStatus(std::string(loopback ? "Som do PC: " : "Entrada: ") + name);

    std::vector<float> mono;
    auto lastData = Clock::now();
    auto lastTick = Clock::now();
    auto lastCheck = Clock::now();
    while (!stopFlag.load() && deviceGen.load() == myDeviceGen) {
        if (dspGen.load() != seenDsp) {
            seenDsp = dspGen.load();
            std::lock_guard lk(mutex);
            dsp.setConfig(config);
        }
        UINT32 packet = 0;
        if (FAILED(capture->GetNextPacketSize(&packet)))
            break; // dispositivo mudou ou foi removido: reconecta
        bool gotData = false;
        while (packet > 0 && !stopFlag.load()) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD bufFlags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &bufFlags, nullptr, nullptr)))
                break;
            if (frames > 0) {
                if (bufFlags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    mono.assign(frames, 0.0f);
                } else {
                    toMono(data, frames, fmt, mono);
                }
                dsp.feed(mono.data(), mono.size());
                gotData = true;
            }
            capture->ReleaseBuffer(frames);
            if (FAILED(capture->GetNextPacketSize(&packet)))
                break;
        }
        const auto now = Clock::now();
        if (gotData) {
            lastData = now;
            publish(true);
        } else if (now - lastData > std::chrono::milliseconds(40)) {
            // Loopback sem nada tocando nao entrega pacotes: o tempo anda com silencio e as bandas caem
            dsp.feedSilence(std::chrono::duration<double>(now - lastTick).count());
            publish(true);
            // Parado ha 2,5 s no loopback automatico: confere se o som foi para outra saida
            if (loopback && cfg.deviceId.empty() && now - lastCheck > std::chrono::milliseconds(2500)) {
                lastCheck = now;
                ComPtr<IMMDevice> other = findActiveRenderDevice(enumerator);
                if (other && deviceId(other.Get()) != deviceId(device.Get()))
                    break;
            }
        }
        lastTick = now;
        waitFor(std::chrono::milliseconds(5));
    }
    client->Stop();
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
    ComScope com(COINIT_APARTMENTTHREADED);
    if (FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE)
        return out;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) || !enumerator)
        return out;
    ComPtr<IMMDeviceCollection> col;
    const EDataFlow flow = source == AudioSource::Loopback ? eRender : eCapture;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col)) || !col)
        return out;
    UINT count = 0;
    col->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> dev;
        if (FAILED(col->Item(i, &dev)) || !dev)
            continue;
        out.push_back({deviceId(dev.Get()), friendlyName(dev.Get())});
    }
    return out;
}

} // namespace bsl::audio
