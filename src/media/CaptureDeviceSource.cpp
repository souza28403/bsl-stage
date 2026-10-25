#include "media/CaptureDeviceSource.h"

#include "app/Log.h"
#include "media/DeviceCatalog.h"

#include <algorithm>
#include <cctype>
#include <thread>

namespace bsl::media {

using Clock = std::chrono::steady_clock;

struct CaptureDeviceSource::Pending
{
    std::mutex m;
    bool done = false;
    bool ok = false;
    std::string error;
    std::string info;
    std::unique_ptr<VideoDecoder> decoder;
};

namespace {

constexpr auto kRetryInterval = std::chrono::seconds(2);
constexpr auto kSilenceLimit = std::chrono::seconds(3);

std::string upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string formatLabel(const CaptureFormat& f, double fps)
{
    std::string name = f.codec.empty() ? f.pixelFormat : f.codec;
    if (name == "yuyv422")
        name = "yuyv";
    return std::to_string(f.width) + " x " + std::to_string(f.height) + " | " + std::to_string(static_cast<int>(fps + 0.5)) +
           " fps | " + upper(name);
}

// Descarta o decoder numa thread solta: fechar um dispositivo travado nao pode congelar a UI
void retire(std::unique_ptr<VideoDecoder> dec)
{
    if (!dec)
        return;
    std::thread([d = std::move(dec)]() mutable { d.reset(); }).detach();
}

} // namespace

CaptureDeviceSource::CaptureDeviceSource() = default;

CaptureDeviceSource::~CaptureDeviceSource()
{
    close();
}

std::vector<ParamDesc> CaptureDeviceSource::describe(const ParamValues& values)
{
    using namespace param;
    DeviceCatalog& cat = DeviceCatalog::instance();
    cat.ensureListed();

    std::string device;
    if (const Json* v = values.find("device"); v && v->is_string())
        device = v->get<std::string>();
    if (device.empty()) {
        const auto names = cat.deviceNames();
        if (!names.empty())
            device = names.front();
    }

    std::vector<ParamOption> formats = {{"auto", "Automático (melhor)"}};
    if (!device.empty()) {
        cat.ensureFormats(device);
        struct Entry
        {
            CaptureFormat f;
            double fps;
        };
        std::vector<Entry> entries;
        for (const CaptureFormat& f : cat.formats(device)) {
            entries.push_back({f, f.fpsMax});
            for (double fps : {60.0, 30.0, 25.0, 24.0, 15.0, 10.0, 5.0})
                if (fps >= f.fpsMin && fps < f.fpsMax)
                    entries.push_back({f, fps});
        }
        std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            const long aa = static_cast<long>(a.f.width) * a.f.height, ab = static_cast<long>(b.f.width) * b.f.height;
            if (aa != ab)
                return aa > ab;
            if (a.fps != b.fps)
                return a.fps > b.fps;
            return a.f.codec == "mjpeg" && b.f.codec != "mjpeg";
        });
        for (const Entry& e : entries) {
            const std::string value = DeviceCatalog::formatValue(e.f, e.fps);
            const bool dup = std::any_of(formats.begin(), formats.end(), [&](const ParamOption& o) { return o.value == value; });
            if (!dup)
                formats.push_back({value, formatLabel(e.f, e.fps)});
        }
    }

    std::vector<ParamDesc> d;
    d.push_back(makeCustom("device", "Dispositivo", "capture-device", std::string(), "Entrada"));
    d.push_back(makeEnum("format", "Formato", std::move(formats), "auto"));
    d.back().tooltip = "Resolução e taxa de quadros oferecidas pelo dispositivo.";
    return d;
}

std::string CaptureDeviceSource::label(const ParamValues& values)
{
    std::string device;
    if (const Json* v = values.find("device"); v && v->is_string())
        device = v->get<std::string>();
    if (device.empty())
        return "Placa de captura";
    if (device.size() > 26) {
        size_t cut = 24;
        while (cut > 0 && (static_cast<unsigned char>(device[cut]) & 0xC0) == 0x80)
            --cut;
        device = device.substr(0, cut) + "…";
    }
    return "Webcam: " + device;
}

std::string CaptureDeviceSource::deviceName() const
{
    std::string device;
    if (const Json* v = values_.find("device"); v && v->is_string())
        device = v->get<std::string>();
    if (device.empty()) {
        const auto names = DeviceCatalog::instance().deviceNames();
        if (!names.empty())
            device = names.front();
    }
    return device;
}

bool CaptureDeviceSource::open(const std::filesystem::path& /*path*/, std::string* /*error*/)
{
    close();
    clock_ = ClipClock{};
    open_ = true;
    paramsDirty_ = false;
    frames_ = framesThisConnection_ = 0;
    lastError_.clear();
    DeviceCatalog::instance().ensureListed();
    showBlack(); // textura valida desde o primeiro quadro
    state_ = State::Connecting;
    stateSince_ = Clock::now();
    startConnect();
    return true;
}

void CaptureDeviceSource::dropDecoder()
{
    retire(std::move(decoder_));
    decoder_.reset();
    if (pending_) {
        std::lock_guard lk(pending_->m);
        retire(std::move(pending_->decoder));
    }
    pending_.reset();
}

void CaptureDeviceSource::close()
{
    dropDecoder();
    yuv_.reset();
    if (rgbaTex_) {
        glDeleteTextures(1, &rgbaTex_);
        rgbaTex_ = 0;
    }
    rgbaW_ = rgbaH_ = 0;
    usingYuv_ = false;
    open_ = false;
}

GLuint CaptureDeviceSource::texture() const
{
    return (usingYuv_ && yuv_) ? yuv_->texture() : rgbaTex_;
}

void CaptureDeviceSource::showBlack()
{
    Frame black;
    black.allocate(width_, height_, PixelFormat::RGBA8);
    for (size_t i = 3; i < black.planes[0].size(); i += 4)
        black.planes[0][i] = 255;
    uploadRgbaTexture(rgbaTex_, rgbaW_, rgbaH_, black);
    usingYuv_ = false;
}

void CaptureDeviceSource::present(const Frame& frame)
{
    if (frame.isYuv()) {
        if (!yuv_)
            yuv_ = std::make_unique<YuvConverter>();
        yuv_->convert(frame);
        usingYuv_ = true;
    } else {
        uploadRgbaTexture(rgbaTex_, rgbaW_, rgbaH_, frame);
        usingYuv_ = false;
    }
    width_ = frame.width;
    height_ = frame.height;
}

void CaptureDeviceSource::startConnect()
{
    retire(std::move(decoder_));
    decoder_.reset();
    pending_.reset();

    const ParamSet p = currentParams();
    formatValue_ = p.s("format");
    device_ = deviceName();
    state_ = State::Connecting;
    stateSince_ = Clock::now();
    if (device_.empty())
        return; // ainda sem lista de dispositivos (update() tenta de novo)

    auto pending = std::make_shared<Pending>();
    pending_ = pending;
    const std::string device = device_;
    const std::string format = formatValue_;
    std::thread([pending, device, format] {
        DeviceCatalog& cat = DeviceCatalog::instance();
        ChosenFormat chosen = DeviceCatalog::parseValue(format);
        if (chosen.automatic)
            chosen = DeviceCatalog::pickBest(cat.formatsBlocking(device));

        auto tryOpen = [&](const ChosenFormat& c, std::unique_ptr<VideoDecoder>& dec, std::string& error) {
            VideoDecoderOptions opt;
            opt.live = true;
            opt.inputFormat = "dshow";
            opt.queueSize = 2;
            opt.inputOptions.emplace_back("rtbufsize", "32M");
            if (!c.automatic) {
                opt.inputOptions.emplace_back("video_size", std::to_string(c.width) + "x" + std::to_string(c.height));
                opt.inputOptions.emplace_back("framerate", std::to_string(c.fps));
                opt.inputOptions.emplace_back(c.isCodec ? "vcodec" : "pixel_format", c.format);
            }
            dec = std::make_unique<VideoDecoder>();
            if (dec->open("video=" + device, opt))
                return true;
            error = dec->error();
            dec.reset();
            return false;
        };

        std::unique_ptr<VideoDecoder> dec;
        std::string error;
        bool ok = tryOpen(chosen, dec, error);
        if (!ok && !chosen.automatic) {
            ChosenFormat none;
            ok = tryOpen(none, dec, error); // formato recusado: tenta o padrao do dispositivo
            if (ok)
                log::warn("Placa de captura: o formato escolhido (" + std::to_string(chosen.width) + "x" + std::to_string(chosen.height) +
                          "@" + std::to_string(chosen.fps) + " " + chosen.format + ") foi recusado por \"" + device +
                          "\"; usando o padrão do dispositivo");
        }
        std::lock_guard lk(pending->m);
        pending->ok = ok;
        pending->error = error;
        pending->decoder = std::move(dec);
        pending->done = true;
    }).detach();
}

void CaptureDeviceSource::update(double /*dt*/)
{
    if (!open_)
        return;
    const auto now = Clock::now();

    if (paramsDirty_) {
        paramsDirty_ = false;
        // So reconecta se o dispositivo ou o formato mudaram de verdade
        const ParamSet p = currentParams();
        if (deviceName() != device_ || p.s("format") != formatValue_) {
            lastError_.clear();
            startConnect();
        }
    }

    switch (state_) {
    case State::Connecting: {
        if (!pending_) { // esperando a lista de dispositivos
            const DeviceCatalog& cat = DeviceCatalog::instance();
            if (cat.listed()) {
                if (!cat.deviceNames().empty()) {
                    startConnect();
                } else {
                    state_ = State::NoSignal;
                    lastError_ = "nenhum dispositivo de captura encontrado";
                    nextRetry_ = now + kRetryInterval;
                }
            }
            break;
        }
        std::unique_ptr<VideoDecoder> dec;
        std::string error;
        bool done = false, ok = false;
        {
            std::lock_guard lk(pending_->m);
            done = pending_->done;
            if (done) {
                ok = pending_->ok;
                error = pending_->error;
                dec = std::move(pending_->decoder);
            }
        }
        if (!done)
            break;
        pending_.reset();
        if (ok && dec) {
            decoder_ = std::move(dec);
            const VideoInfo& vi = decoder_->info();
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s | %d x %d @ %.0f fps | %s", device_.c_str(), vi.width, vi.height, vi.fps,
                          vi.codec.c_str());
            openedInfo_ = buf;
            state_ = State::Live;
            lastFrame_ = now;
            framesThisConnection_ = 0;
            lastError_.clear();
            log::info("Placa de captura aberta: " + openedInfo_);
        } else {
            state_ = State::NoSignal;
            lastError_ = error.empty() ? "não foi possível abrir o dispositivo" : error;
            nextRetry_ = now + kRetryInterval;
            if (framesThisConnection_ == 0) {
                log::warn("Placa de captura \"" + device_ + "\": sem sinal (" + lastError_ + "); tentando de novo a cada 2 s");
                framesThisConnection_ = 1; // um aviso so
            }
            showBlack();
        }
        break;
    }
    case State::Live: {
        if (!decoder_) {
            state_ = State::NoSignal;
            nextRetry_ = now + kRetryInterval;
            break;
        }
        if (auto f = decoder_->tryGetLatest()) {
            present(*f);
            ++frames_;
            ++framesThisConnection_;
            lastFrame_ = now;
        } else if (decoder_->atEnd() || now - lastFrame_ > kSilenceLimit) {
            log::warn("Placa de captura \"" + device_ + "\": dispositivo desconectado ou sem imagem; sem sinal");
            retire(std::move(decoder_));
            decoder_.reset();
            lastError_ = "dispositivo desconectado";
            framesThisConnection_ = 1; // evita repetir o aviso a cada tentativa
            state_ = State::NoSignal;
            nextRetry_ = now + kRetryInterval;
            showBlack();
        }
        break;
    }
    case State::NoSignal:
        if (now >= nextRetry_) {
            if (DeviceCatalog::instance().deviceNames().empty())
                DeviceCatalog::instance().refresh();
            startConnect();
        }
        break;
    }
}

SourceStatus CaptureDeviceSource::status() const
{
    using Level = SourceStatus::Level;
    switch (state_) {
    case State::Live: return {Level::Info, openedInfo_};
    case State::Connecting:
        if (framesThisConnection_ > 0 || !lastError_.empty())
            return {Level::Warning, "Sem sinal (reconectando…)"};
        return {Level::Info, device_.empty() ? "Procurando dispositivos…" : "Conectando a " + device_ + "…"};
    case State::NoSignal: return {Level::Warning, "Sem sinal" + (lastError_.empty() ? std::string() : " - " + lastError_)};
    }
    return {};
}

} // namespace bsl::media
