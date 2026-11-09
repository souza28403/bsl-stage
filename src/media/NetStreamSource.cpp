#include "media/NetStreamSource.h"

#include "app/Log.h"

namespace bsl::media {

using Clock = std::chrono::steady_clock;

namespace {

constexpr auto kApplyDelay = std::chrono::milliseconds(600);

std::string shortText(std::string s, size_t maxBytes)
{
    if (s.size() <= maxBytes)
        return s;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
        --cut;
    return s.substr(0, cut) + "…";
}

bool sameConfig(const net::Config& a, const net::Config& b)
{
    return a.mode == b.mode && a.url == b.url && a.protocol == b.protocol && a.port == b.port && a.path == b.path &&
           a.bindAddress == b.bindAddress;
}

}

NetStreamSource::NetStreamSource() = default;

NetStreamSource::~NetStreamSource()
{
    close();
}

std::vector<ParamDesc> NetStreamSource::describe(const ParamValues& )
{
    using namespace param;
    std::vector<ParamDesc> d;
    d.push_back(makeEnum("mode", "Modo", {{"pull", "Abrir link"}, {"listen", "Receber transmissão"}}, "pull", "Entrada"));
    d.back().tooltip = "Abrir link: o BSL Stage busca a transmissão num endereço.\n"
                       "Receber: o BSL Stage espera um programa de transmissão enviar para esta máquina.";
    d.push_back(makeCustom("url", "Endereço", "net-url", std::string(), "Entrada"));
    d.back().enabledIf = "mode=pull";
    d.push_back(makeEnum("protocol", "Protocolo", {{"rtmp", "RTMP"}, {"srt", "SRT"}}, "rtmp"));
    d.back().enabledIf = "mode=listen";
    d.push_back(makeInt("port", "Porta", net::kDefaultRtmpPort, 1, 65535, "%d"));
    d.back().enabledIf = "mode=listen";
    d.back().tooltip = "Porta que o BSL Stage escuta. Dois clips não podem usar a mesma porta.";
    d.push_back(makeText("path", "Caminho", "/live/stream1"));
    d.back().enabledIf = "mode=listen";
    d.back().tooltip = "Só vale para RTMP: parte final do endereço mostrado ao programa de transmissão (servidor e chave).";
    d.back().maxLength = 120;
    d.push_back(makeCustom("address", "Enviar para", "net-address", std::string()));
    d.back().enabledIf = "mode=listen";
    d.push_back(makeEnum("loss", "Ao perder o sinal", {{"hold", "Manter última imagem"}, {"black", "Preto"}}, "hold", "Sinal"));
    return d;
}

std::string NetStreamSource::label(const ParamValues& values)
{
    const net::Config cfg = configFrom(values);
    if (cfg.mode == net::Mode::Listen)
        return "Receber: porta " + std::to_string(cfg.port);
    std::string host = net::redactUrl(cfg.url);
    if (host.empty())
        return "Stream de rede";
    if (host.rfind("(", 0) == 0)
        return "Stream de rede";
    const size_t scheme = host.find("://");
    if (scheme != std::string::npos)
        host = host.substr(scheme + 3);
    if (host.size() >= 4 && host.compare(host.size() - 4, 4, "/…") == 0)
        host.resize(host.size() - 4);
    return "Stream: " + shortText(host, 28);
}

net::Config NetStreamSource::configFrom(const ParamValues& values)
{
    const ParamSet p(describe(values), values);
    net::Config cfg;
    cfg.mode = p.s("mode") == "listen" ? net::Mode::Listen : net::Mode::Pull;
    cfg.url = p.s("url");
    cfg.protocol = p.s("protocol") == "srt" ? net::ListenProtocol::Srt : net::ListenProtocol::Rtmp;
    cfg.port = p.i("port");
    cfg.path = net::normalizePath(p.s("path"));
    return cfg;
}

GLuint NetStreamSource::texture() const
{
    return (usingYuv_ && yuv_) ? yuv_->texture() : rgbaTex_;
}

void NetStreamSource::onParamsChanged()
{
    paramsDirty_ = true;
    dirtyAt_ = Clock::now();
}

void NetStreamSource::showBlack()
{
    Frame black;
    black.allocate(width_, height_, PixelFormat::RGBA8);
    for (size_t i = 3; i < black.planes[0].size(); i += 4)
        black.planes[0][i] = 255;
    uploadRgbaTexture(rgbaTex_, rgbaW_, rgbaH_, black);
    usingYuv_ = false;
    showingBlack_ = true;
}

void NetStreamSource::present(const Frame& frame)
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
    showingBlack_ = false;
}

void NetStreamSource::startWorker()
{
    appliedConfig_ = configFrom(values_);
    holdLast_ = ParamSet(describe(values_), values_).s("loss") != "black";
    paramsDirty_ = false;
    worker_.start(appliedConfig_);
}

bool NetStreamSource::open(const std::filesystem::path& , std::string* )
{
    close();
    clock_ = ClipClock{};
    open_ = true;
    frames_ = 0;
    showBlack();
    startWorker();
    const net::Config cfg = appliedConfig_;
    log::info(cfg.mode == net::Mode::Listen ? "Stream de rede: recebendo na porta " + std::to_string(cfg.port)
                                            : "Stream de rede: abrindo " + net::redactUrl(cfg.url));
    return true;
}

void NetStreamSource::close()
{
    worker_.stop();
    yuv_.reset();
    if (rgbaTex_) {
        glDeleteTextures(1, &rgbaTex_);
        rgbaTex_ = 0;
    }
    rgbaW_ = rgbaH_ = 0;
    usingYuv_ = false;
    open_ = false;
    paramsDirty_ = false;
}

void NetStreamSource::update(double )
{
    if (!open_)
        return;

    if (paramsDirty_ && Clock::now() - dirtyAt_ >= kApplyDelay) {
        const net::Config next = configFrom(values_);
        const bool restart = !sameConfig(next, appliedConfig_);
        holdLast_ = ParamSet(describe(values_), values_).s("loss") != "black";
        paramsDirty_ = false;
        if (restart) {
            startWorker();
            if (!holdLast_)
                showBlack();
        }
    }

    if (worker_.takeFrame(incoming_) && !incoming_.empty()) {
        present(incoming_);
        ++frames_;
        return;
    }
    if (!holdLast_ && !showingBlack_ && worker_.status().state != net::State::Live)
        showBlack();
}

SourceStatus NetStreamSource::status() const
{
    using Level = SourceStatus::Level;
    const net::Status st = worker_.status();
    const std::string text = net::describeStatus(appliedConfig_, st);
    switch (st.state) {
    case net::State::Live: return {Level::Info, text};
    case net::State::Error: return {Level::Error, text};
    case net::State::NoSignal: return {Level::Warning, text};
    case net::State::Waiting: return {st.hadSignal ? Level::Warning : Level::Info, text};
    default: return {Level::Info, text};
    }
}

}
