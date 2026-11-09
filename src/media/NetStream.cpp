#include "media/NetStream.h"

#include "media/FrameConverter.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/rational.h>
}

namespace bsl::media::net {

namespace {

using Clock = std::chrono::steady_clock;

long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

const char* const kSchemes[] = {"rtmp", "rtmps", "rtsp", "rtsps", "srt", "udp", "rtp", "tcp", "http", "https"};

std::string schemeOf(const std::string& url)
{
    const size_t p = url.find("://");
    if (p == std::string::npos || p == 0)
        return {};
    return lower(url.substr(0, p));
}

bool allowedScheme(const std::string& scheme)
{
    return std::any_of(std::begin(kSchemes), std::end(kSchemes), [&](const char* s) { return scheme == s; });
}

bool passiveScheme(const std::string& scheme)
{
    return scheme == "udp" || scheme == "rtp";
}

std::string errString(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

std::string describeError(int r, const Config& cfg, bool deadlineHit)
{
    const std::string port = std::to_string(cfg.port);
    if (r == AVERROR(EADDRINUSE))
        return "Porta " + port + " em uso por outro programa.";
    if (r == AVERROR(EACCES))
        return "Sem permissão para usar a porta " + port + ".";
    if (r == AVERROR(ECONNREFUSED))
        return "Conexão recusada pelo servidor.";
    if (r == AVERROR(ETIMEDOUT) || (r == AVERROR_EXIT && deadlineHit))
        return "Tempo esgotado: o servidor não respondeu ou recusou a conexão.";
    if (r == AVERROR(ENETUNREACH) || r == AVERROR(EHOSTUNREACH) || r == AVERROR(ENETDOWN))
        return "Rede ou servidor inacessível.";
    if (r == AVERROR_HTTP_UNAUTHORIZED || r == AVERROR_HTTP_FORBIDDEN)
        return "Acesso negado: confira usuário, senha ou chave da transmissão.";
    if (r == AVERROR_HTTP_NOT_FOUND)
        return "Transmissão não encontrada: confira o endereço.";
    if (r == AVERROR_PROTOCOL_NOT_FOUND)
        return "Protocolo não suportado.";
    if (r == AVERROR_INVALIDDATA)
        return "Formato de transmissão não reconhecido.";
    if (r == AVERROR_EOF)
        return "Transmissão encerrada pelo servidor.";
    if (r == AVERROR(EIO) || r == AVERROR(ENOENT) || r == AVERROR(EINVAL))
        return "Falha de rede: endereço não encontrado ou conexão recusada.";
    return "Falha ao conectar (" + errString(r) + ").";
}

struct WsaScope {
    WsaScope()
    {
        WSADATA d{};
        ok = WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }
    ~WsaScope()
    {
        if (ok)
            WSACleanup();
    }
    bool ok = false;
};

}

std::vector<std::string> localIPv4Addresses()
{
    std::vector<std::string> out;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf(size);
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG r = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    if (r == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        r = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (r != NO_ERROR)
        return out;
    for (const IP_ADAPTER_ADDRESSES* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        for (const IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            const auto* sin = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
            char text[INET_ADDRSTRLEN] = {};
            if (!InetNtopA(AF_INET, const_cast<IN_ADDR*>(&sin->sin_addr), text, sizeof(text)))
                continue;
            const std::string ip = text;
            if (ip.rfind("169.254.", 0) == 0 || ip.rfind("127.", 0) == 0 || ip == "0.0.0.0")
                continue;
            if (std::find(out.begin(), out.end(), ip) == out.end())
                out.push_back(ip);
        }
    }
    return out;
}

std::string sanitizeName(const std::string& name)
{
    std::string out;
    for (const unsigned char c : trim(name)) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.')
            out.push_back(static_cast<char>(c));
        else if (c == ' ')
            out.push_back('-');
        if (out.size() >= 64)
            break;
    }
    return out.empty() ? "stream1" : out;
}

std::string normalizePath(const std::string& path)
{
    std::string out = "/";
    for (const unsigned char c : trim(path)) {
        if (c == '\\')
            continue;
        if (c == '/') {
            if (out.back() != '/')
                out.push_back('/');
        } else if (std::isalnum(c) || c == '-' || c == '_' || c == '.') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('-');
        }
        if (out.size() >= 120)
            break;
    }
    while (out.size() > 1 && out.back() == '/')
        out.pop_back();
    if (out == "/")
        return "/live/stream1";
    if (out.find('/', 1) == std::string::npos)
        out = "/live" + out;
    return out;
}

std::string listenUrl(const Config& cfg)
{
    const std::string port = std::to_string(cfg.port);
    if (cfg.protocol == ListenProtocol::Srt)
        return "srt://" + cfg.bindAddress + ":" + port + "?mode=listener";
    return "rtmp://" + cfg.bindAddress + ":" + port + normalizePath(cfg.path);
}

std::string displayAddress(const Config& cfg, const std::string& host)
{
    const std::string port = std::to_string(cfg.port);
    if (cfg.protocol == ListenProtocol::Srt)
        return "srt://" + host + ":" + port;
    return "rtmp://" + host + ":" + port + normalizePath(cfg.path);
}

bool looksLikeStreamUrl(const std::string& text)
{
    const std::string t = trim(text);
    return allowedScheme(schemeOf(t));
}

std::string redactUrl(const std::string& url)
{
    const std::string t = trim(url);
    const std::string scheme = schemeOf(t);
    if (scheme.empty())
        return t.empty() ? std::string() : "(endereço inválido)";
    std::string rest = t.substr(scheme.size() + 3);
    const size_t slash = rest.find_first_of("/?");
    std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    const bool hasPath = slash != std::string::npos && slash + 1 < rest.size();
    if (const size_t at = authority.rfind('@'); at != std::string::npos)
        authority = authority.substr(at + 1);
    return scheme + "://" + authority + (hasPath ? "/…" : "");
}

std::string urlFromShortcut(const std::filesystem::path& file)
{
    std::error_code ec;
    if (lower(file.extension().string()) != ".url" || std::filesystem::file_size(file, ec) > 65536)
        return {};
    std::ifstream in(file, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.size() > 4 && lower(t.substr(0, 4)) == "url=") {
            const std::string url = trim(t.substr(4));
            return looksLikeStreamUrl(url) ? url : std::string();
        }
    }
    return {};
}

std::string validateConfig(const Config& cfg)
{
    if (cfg.mode == Mode::Listen) {
        if (cfg.port < 1 || cfg.port > 65535)
            return "Porta inválida: use um número de 1 a 65535.";
        return {};
    }
    const std::string url = trim(cfg.url);
    if (url.empty())
        return "Cole o endereço da transmissão (por exemplo rtmp://servidor/app/chave).";
    if (url.size() > 2048)
        return "Endereço grande demais.";
    for (const unsigned char c : url)
        if (c < 0x20 || c == 0x7f || c == ' ')
            return "Endereço inválido: contém espaços ou caracteres de controle.";
    const std::string scheme = schemeOf(url);
    if (scheme.empty())
        return "Endereço inválido: comece com rtmp://, rtsp://, srt://, udp:// ou http(s)://.";
    if (!allowedScheme(scheme))
        return "Protocolo não suportado: use rtmp, rtmps, rtsp, srt, udp ou http(s) (lista .m3u8).";
    const std::string rest = url.substr(scheme.size() + 3);
    if (rest.empty() || rest[0] == '/')
        return "Endereço inválido: falta o servidor depois de " + scheme + "://.";
    return {};
}

std::string describeStatus(const Config& cfg, const Status& st)
{
    auto stripDot = [](std::string m) {
        while (!m.empty() && (m.back() == '.' || m.back() == ' '))
            m.pop_back();
        return m;
    };
    const bool listen = cfg.mode == Mode::Listen;
    switch (st.state) {
    case State::Stopped: return "Parado";
    case State::Connecting:
        if (!st.message.empty())
            return st.message;
        return listen ? "Conectando…" : "Conectando a " + st.address;
    case State::Waiting:
        if (st.hadSignal)
            return "Sem sinal – aguardando novo envio em " + st.address;
        return "Aguardando envio em " + st.address;
    case State::Live: {
        std::string out = "Ao vivo – " + std::to_string(st.width) + "x" + std::to_string(st.height);
        char buf[48];
        if (st.fps > 0.5) {
            std::snprintf(buf, sizeof(buf), " · %.0f fps", st.fps);
            out += buf;
        }
        if (!st.codec.empty()) {
            std::string c = st.codec;
            std::transform(c.begin(), c.end(), c.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
            out += " · " + c;
        }
        if (st.kbps > 1.0) {
            std::snprintf(buf, sizeof(buf), " · %.1f Mbps", st.kbps / 1000.0);
            out += buf;
        }
        return out;
    }
    case State::NoSignal: {
        std::string out = "Sem sinal – tentando de novo em " + std::to_string(std::max(st.retryInSeconds, 1)) + " s";
        if (!st.message.empty())
            out += " (" + stripDot(st.message) + ")";
        return out;
    }
    case State::Error: {
        std::string out = "Erro: " + st.message;
        if (st.retryInSeconds > 0)
            out += " Nova tentativa em " + std::to_string(st.retryInSeconds) + " s.";
        return out;
    }
    }
    return {};
}

bool portAvailable(const std::string& bindAddress, int port, bool udp, std::string* error)
{
    WsaScope wsa;
    if (!wsa.ok)
        return true;
    SOCKET s = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, udp ? IPPROTO_UDP : IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return true;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    if (InetPtonA(AF_INET, bindAddress.c_str(), &addr.sin_addr) != 1)
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bool ok = true;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const int code = WSAGetLastError();
        ok = false;
        if (error) {
            if (code == WSAEADDRINUSE)
                *error = "Porta " + std::to_string(port) + " em uso por outro programa (ou por outro clip). Escolha outra porta.";
            else if (code == WSAEACCES)
                *error = "Sem permissão para usar a porta " + std::to_string(port) + ".";
            else
                *error = "Não foi possível abrir a porta " + std::to_string(port) + " (erro " + std::to_string(code) + ").";
        }
    }
    closesocket(s);
    return ok;
}

struct StreamWorker::Shared {
    Config cfg;
    std::atomic<bool> stop{false};
    std::atomic<bool> finished{false};
    std::atomic<long long> deadline{0};
    mutable std::mutex m;
    std::condition_variable cv;
    Status st;
    Frame front;
    bool fresh = false;
    long long nextRetryMs = 0;

    void setState(State state, const std::string& message = {}, long long retryAtMs = 0)
    {
        std::lock_guard lk(m);
        st.state = state;
        st.message = message;
        nextRetryMs = retryAtMs;
    }

    bool sleepFor(long long ms)
    {
        std::unique_lock lk(m);
        cv.wait_for(lk, std::chrono::milliseconds(ms), [&] { return stop.load(); });
        return !stop.load();
    }
};

namespace {

using Shared = StreamWorker::Shared;

int interruptCallback(void* opaque)
{
    auto* s = static_cast<Shared*>(opaque);
    if (s->stop.load())
        return 1;
    const long long d = s->deadline.load();
    return (d != 0 && nowMs() > d) ? 1 : 0;
}

struct Attempt {
    enum class Kind { Ended, OpenFailed, PortBusy, Fatal };
    Kind kind = Kind::Ended;
    std::string message;
    bool gotFrames = false;
};

int readTimeoutMs(const std::string& scheme)
{
    return (scheme == "http" || scheme == "https") ? 20000 : 5000;
}

void setLowLatencyOptions(AVDictionary** o, const Config& cfg, const std::string& scheme)
{
    av_dict_set(o, "protocol_whitelist", "rtmp,rtmps,rtmpt,rtmpe,rtmpts,rtmpte,rtsp,rtp,srt,udp,tcp,tls,http,https,crypto,hls,ffrtmphttp,ffrtmpcrypt", 0);
    av_dict_set(o, "fflags", "nobuffer", 0);
    av_dict_set(o, "analyzeduration", "500000", 0);
    av_dict_set(o, "probesize", "500000", 0);
    if (cfg.mode == Mode::Listen) {
        if (cfg.protocol == ListenProtocol::Rtmp)
            av_dict_set(o, "listen", "1", 0);
        return;
    }
    av_dict_set(o, "rw_timeout", "8000000", 0);
    if (scheme == "rtmp" || scheme == "rtmps") {
        av_dict_set(o, "rtmp_live", "live", 0);
        av_dict_set(o, "rtmp_buffer", "1000", 0);
    } else if (scheme == "rtsp" || scheme == "rtsps") {
        av_dict_set(o, "rtsp_transport", "tcp", 0);
        av_dict_set(o, "max_delay", "300000", 0);
    } else if (scheme == "udp") {
        av_dict_set(o, "fifo_size", "50000", 0);
        av_dict_set(o, "overrun_nonfatal", "1", 0);
    }
}

int pickVideo(AVFormatContext* fmt, bool requireSize)
{
    const int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0)
        return -1;
    const AVCodecParameters* par = fmt->streams[idx]->codecpar;
    if (requireSize && (par->width <= 0 || par->height <= 0))
        return -1;
    return idx;
}

Attempt runAttempt(Shared& s)
{
    Attempt res;
    const Config& cfg = s.cfg;
    const bool listen = cfg.mode == Mode::Listen;
    const std::string url = listen ? listenUrl(cfg) : trim(cfg.url);
    const std::string scheme = schemeOf(url);
    const bool passive = listen || passiveScheme(scheme);

    if (listen) {
        std::string why;
        if (!portAvailable(cfg.bindAddress, cfg.port, cfg.protocol == ListenProtocol::Srt, &why)) {
            res.kind = Attempt::Kind::PortBusy;
            res.message = why;
            return res;
        }
    }

    AVFormatContext* fmt = avformat_alloc_context();
    if (!fmt) {
        res.kind = Attempt::Kind::OpenFailed;
        res.message = "Sem memória.";
        return res;
    }
    fmt->interrupt_callback.callback = &interruptCallback;
    fmt->interrupt_callback.opaque = &s;

    AVDictionary* opts = nullptr;
    setLowLatencyOptions(&opts, cfg, scheme);

    s.deadline = passive ? 0 : nowMs() + 10000;
    int r = avformat_open_input(&fmt, url.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    const bool deadlineHit = !s.stop.load() && s.deadline.load() != 0 && nowMs() > s.deadline.load();
    s.deadline = 0;
    if (r < 0) {
        res.kind = (r == AVERROR(EADDRINUSE)) ? Attempt::Kind::PortBusy : Attempt::Kind::OpenFailed;
        res.message = describeError(r, cfg, deadlineHit);
        return res;
    }

    struct FmtGuard {
        AVFormatContext** f;
        ~FmtGuard() { avformat_close_input(f); }
    } fmtGuard{&fmt};

    if (listen || !passive) {
        std::lock_guard lk(s.m);
        s.st.state = State::Connecting;
        s.st.message = listen ? "Conexão recebida, lendo o vídeo…" : "Lendo o vídeo…";
    }

    const long long probeBudget = passive && !listen ? 30000 : 8000;
    s.deadline = nowMs() + probeBudget;
    r = avformat_find_stream_info(fmt, nullptr);
    int vidx = r >= 0 ? pickVideo(fmt, true) : -1;
    if (r >= 0 && vidx < 0 && !s.stop.load()) {
        fmt->probesize = 5000000;
        fmt->max_analyze_duration = 3 * static_cast<int64_t>(AV_TIME_BASE);
        s.deadline = nowMs() + probeBudget;
        r = avformat_find_stream_info(fmt, nullptr);
        vidx = r >= 0 ? pickVideo(fmt, false) : -1;
    }
    const bool probeDeadline = !s.stop.load() && s.deadline.load() != 0 && nowMs() > s.deadline.load();
    s.deadline = 0;
    if (r < 0 || vidx < 0) {
        res.kind = Attempt::Kind::OpenFailed;
        res.message = vidx < 0 && r >= 0 ? "A transmissão não tem vídeo." : describeError(r, cfg, probeDeadline);
        return res;
    }

    AVStream* st = fmt->streams[vidx];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        res.kind = Attempt::Kind::OpenFailed;
        res.message = "Este formato de vídeo (" + std::string(avcodec_get_name(st->codecpar->codec_id)) + ") não é suportado.";
        return res;
    }
    AVCodecContext* cc = avcodec_alloc_context3(codec);
    if (!cc || avcodec_parameters_to_context(cc, st->codecpar) < 0) {
        avcodec_free_context(&cc);
        res.kind = Attempt::Kind::OpenFailed;
        res.message = "Falha ao preparar o decodificador.";
        return res;
    }
    cc->pkt_timebase = st->time_base;
    cc->thread_count = 0;
    cc->thread_type = FF_THREAD_SLICE;
    cc->flags |= AV_CODEC_FLAG_LOW_DELAY;
    if (avcodec_open2(cc, codec, nullptr) < 0) {
        avcodec_free_context(&cc);
        res.kind = Attempt::Kind::OpenFailed;
        res.message = "Falha ao abrir o decodificador de vídeo.";
        return res;
    }

    const std::string codecName = avcodec_get_name(st->codecpar->codec_id);
    const double timeBase = av_q2d(st->time_base);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    FrameConverter converter;
    Frame back;

    bool needKey = true;
    const auto keyWaitStart = Clock::now();
    bool anchored = false;
    Clock::time_point wall0;
    double pts0 = 0.0, lastPts = 0.0;
    unsigned long long windowFrames = 0, windowBytes = 0;
    auto windowStart = Clock::now();
    auto lastFrameAt = Clock::now();
    const int readTimeout = readTimeoutMs(scheme);
    std::string endMessage = "Transmissão encerrada.";

    while (!s.stop.load()) {
        s.deadline = nowMs() + readTimeout;
        const int rr = av_read_frame(fmt, pkt);
        if (rr < 0) {
            if (rr == AVERROR(EAGAIN))
                continue;
            const bool timedOut = !s.stop.load() && rr == AVERROR_EXIT;
            endMessage = timedOut ? "Sem dados da transmissão." : (rr == AVERROR_EOF ? "Transmissão encerrada." : describeError(rr, cfg, false));
            break;
        }
        if (pkt->stream_index != vidx) {
            av_packet_unref(pkt);
            continue;
        }
        windowBytes += static_cast<unsigned long long>(pkt->size);
        if (needKey) {
            if (pkt->flags & AV_PKT_FLAG_KEY)
                needKey = false;
            else if (Clock::now() - keyWaitStart > std::chrono::seconds(4))
                needKey = false;
            else {
                av_packet_unref(pkt);
                continue;
            }
        }
        const int sr = avcodec_send_packet(cc, pkt);
        av_packet_unref(pkt);
        if (sr < 0 && sr != AVERROR(EAGAIN) && sr != AVERROR_INVALIDDATA)
            continue;

        while (!s.stop.load()) {
            const int fr_r = avcodec_receive_frame(cc, fr);
            if (fr_r < 0)
                break;
            const auto now = Clock::now();
            lastFrameAt = now;

            if (fr->best_effort_timestamp != AV_NOPTS_VALUE) {
                const double t = static_cast<double>(fr->best_effort_timestamp) * timeBase;
                if (!anchored || t < lastPts - 1.0 || t > lastPts + 5.0) {
                    anchored = true;
                    wall0 = now;
                    pts0 = t;
                }
                lastPts = t;
                const double elapsed = std::chrono::duration<double>(now - wall0).count();
                double ahead = (t - pts0) - elapsed;
                while (ahead > 0.25 && !s.stop.load()) {
                    s.deadline = 0;
                    s.sleepFor(static_cast<long long>(std::min(ahead - 0.1, 0.2) * 1000.0));
                    ahead = (t - pts0) - std::chrono::duration<double>(Clock::now() - wall0).count();
                }
                s.deadline = nowMs() + readTimeout;
            }

            if (!converter.convert(fr, 0, 0, false, back)) {
                av_frame_unref(fr);
                continue;
            }
            back.pts = lastPts;
            const int w = fr->width, h = fr->height;
            av_frame_unref(fr);

            ++windowFrames;
            const double span = std::chrono::duration<double>(now - windowStart).count();
            std::lock_guard lk(s.m);
            std::swap(s.front, back);
            s.fresh = true;
            ++s.st.frames;
            s.st.width = w;
            s.st.height = h;
            s.st.codec = codecName;
            if (!res.gotFrames || s.st.state != State::Live) {
                res.gotFrames = true;
                s.st.state = State::Live;
                s.st.message.clear();
                s.st.hadSignal = true;
                s.nextRetryMs = 0;
            }
            if (span >= 1.0) {
                s.st.fps = static_cast<double>(windowFrames) / span;
                s.st.kbps = static_cast<double>(windowBytes) * 8.0 / 1000.0 / span;
                windowFrames = 0;
                windowBytes = 0;
                windowStart = now;
            }
        }

        if (Clock::now() - lastFrameAt > std::chrono::seconds(res.gotFrames ? 8 : 15)) {
            endMessage = "Sem imagem na transmissão.";
            break;
        }
    }
    s.deadline = 0;

    av_frame_free(&fr);
    av_packet_free(&pkt);
    avcodec_free_context(&cc);
    res.kind = Attempt::Kind::Ended;
    res.message = endMessage;
    return res;
}

void run(std::shared_ptr<Shared> sh)
{
    Shared& s = *sh;
    initFfmpegLogging();
    const Config& cfg = s.cfg;
    const bool listen = cfg.mode == Mode::Listen;

    {
        std::lock_guard lk(s.m);
        s.st = Status{};
        s.st.state = State::Connecting;
        if (listen) {
            const auto ips = localIPv4Addresses();
            s.st.address = displayAddress(cfg, ips.empty() ? "127.0.0.1" : ips.front());
        } else {
            s.st.address = redactUrl(cfg.url);
        }
    }

    const std::string invalid = validateConfig(cfg);
    if (!invalid.empty()) {
        s.setState(State::Error, invalid);
        std::unique_lock lk(s.m);
        s.cv.wait(lk, [&] { return s.stop.load(); });
        s.finished = true;
        return;
    }

    int backoff = 1;
    while (!s.stop.load()) {
        {
            std::lock_guard lk(s.m);
            const std::string sch = schemeOf(trim(cfg.url));
            s.st.state = (listen || passiveScheme(sch)) ? State::Waiting : State::Connecting;
            if (!s.st.hadSignal)
                s.st.message.clear();
            s.nextRetryMs = 0;
        }
        const Attempt a = runAttempt(s);
        if (s.stop.load())
            break;

        if (a.gotFrames)
            backoff = 1;

        if (a.kind == Attempt::Kind::PortBusy) {
            s.setState(State::Error, a.message, nowMs() + backoff * 1000LL);
        } else if (listen) {
            std::lock_guard lk(s.m);
            s.st.state = State::Waiting;
            s.st.message = a.gotFrames ? "O envio terminou." : a.message;
            s.nextRetryMs = 0;
        } else {
            s.setState(State::NoSignal, a.message, nowMs() + backoff * 1000LL);
        }

        long long waitMs = backoff * 1000LL;
        if (listen && a.kind != Attempt::Kind::PortBusy)
            waitMs = a.gotFrames ? 300 : std::min<long long>(waitMs, 1000);
        if (!s.sleepFor(waitMs))
            break;
        backoff = std::min(backoff * 2, 10);
        if (listen && a.kind != Attempt::Kind::PortBusy && a.gotFrames)
            backoff = 1;
    }
    s.finished = true;
}

}

StreamWorker::StreamWorker() = default;

StreamWorker::~StreamWorker()
{
    stop();
}

void StreamWorker::start(const Config& cfg)
{
    stop();
    cfg_ = cfg;
    cfg_.path = normalizePath(cfg_.path);
    shared_ = std::make_shared<Shared>();
    shared_->cfg = cfg_;
    shared_->st.state = State::Connecting;
    thread_ = std::thread([sh = shared_] { run(sh); });
}

void StreamWorker::stop(int graceMs)
{
    if (!shared_)
        return;
    {
        std::lock_guard lk(shared_->m);
        shared_->stop = true;
    }
    shared_->cv.notify_all();
    const auto limit = Clock::now() + std::chrono::milliseconds(graceMs);
    while (!shared_->finished.load() && Clock::now() < limit)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (thread_.joinable()) {
        if (shared_->finished.load())
            thread_.join();
        else
            thread_.detach();
    }
    shared_.reset();
}

bool StreamWorker::running() const
{
    return shared_ && !shared_->finished.load();
}

bool StreamWorker::takeFrame(Frame& out)
{
    if (!shared_)
        return false;
    std::lock_guard lk(shared_->m);
    if (!shared_->fresh)
        return false;
    std::swap(out, shared_->front);
    shared_->fresh = false;
    return true;
}

Status StreamWorker::status() const
{
    Status out;
    if (!shared_)
        return out;
    long long retryAt = 0;
    {
        std::lock_guard lk(shared_->m);
        out = shared_->st;
        retryAt = shared_->nextRetryMs;
    }
    if (retryAt > 0) {
        const long long left = retryAt - nowMs();
        out.retryInSeconds = left > 0 ? static_cast<int>((left + 999) / 1000) : 0;
    }
    return out;
}

}
