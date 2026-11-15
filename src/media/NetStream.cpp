#include "media/NetStream.h"

#include "media/FrameConverter.h"

#include "platform/Socket.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
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
    WsaScope() { ok = bsl::sock::startup(); }
    ~WsaScope()
    {
        if (ok)
            bsl::sock::cleanup();
    }
    bool ok = false;
};

}

std::vector<std::string> localIPv4Addresses()
{
    std::vector<std::string> out;
    for (const bsl::sock::LocalInterface& li : bsl::sock::localInterfaces()) {
        if (li.loopback)
            continue;
        in_addr ia{};
        ia.s_addr = htonl(li.ipHost);
        char text[INET_ADDRSTRLEN] = {};
        if (!inet_ntop(AF_INET, &ia, text, sizeof(text)))
            continue;
        const std::string ip = text;
        if (ip.rfind("169.254.", 0) == 0 || ip.rfind("127.", 0) == 0 || ip == "0.0.0.0")
            continue;
        if (std::find(out.begin(), out.end(), ip) == out.end())
            out.push_back(ip);
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
        if (st.variantCount > 1)
            out += " · variante " + std::to_string(st.variantIndex) + " de " + std::to_string(st.variantCount);
        if (st.bufferMs >= 1000.0) {
            std::snprintf(buf, sizeof(buf), " · buffer %.1f s", st.bufferMs / 1000.0);
            out += buf;
        } else if (st.bufferFrames > 0) {
            std::snprintf(buf, sizeof(buf), " · buffer %.0f ms", st.bufferMs);
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
    bsl::sock::Handle s = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, udp ? IPPROTO_UDP : IPPROTO_TCP);
    if (s == bsl::sock::kInvalid)
        return true;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (inet_pton(AF_INET, bindAddress.c_str(), &addr.sin_addr) != 1)
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bool ok = true;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const int code = bsl::sock::lastError();
        ok = false;
        if (error) {
            if (bsl::sock::isAddrInUse(code))
                *error = "Porta " + std::to_string(port) + " em uso por outro programa (ou por outro clip). Escolha outra porta.";
            else if (bsl::sock::isAccessDenied(code))
                *error = "Sem permissão para usar a porta " + std::to_string(port) + ".";
            else
                *error = "Não foi possível abrir a porta " + std::to_string(port) + " (erro " + std::to_string(code) + ").";
        }
    }
    bsl::sock::close(s);
    return ok;
}

struct QueuedFrame {
    Frame frame;
    Clock::time_point due;
};

struct StreamWorker::Shared {
    Config cfg;
    std::atomic<bool> stop{false};
    std::atomic<bool> finished{false};
    std::atomic<long long> deadline{0};
    mutable std::mutex m;
    std::condition_variable cv;
    Status st;
    long long nextRetryMs = 0;

    std::deque<QueuedFrame> queue;
    std::vector<Frame> pool;
    std::atomic<long long> packetMs{0};
    std::atomic<int> packetCount{0};
    Clock::time_point lastDue{};
    Clock::time_point wall0{};
    bool anchored = false;
    double pts0 = 0.0;
    double lastPts = 0.0;
    double frameDur = 1.0 / 30.0;

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

using Sec = std::chrono::duration<double>;

Clock::duration dur(double sec)
{
    return std::chrono::duration_cast<Clock::duration>(Sec(sec));
}

constexpr double kBufferedTargetSec = 1.5;
constexpr double kRealtimeJitterSec = 0.15;
constexpr double kStaleSec = 1.0;
constexpr double kMaxBufferBytes = 64.0 * 1024.0 * 1024.0;
constexpr size_t kMaxPacketBytes = 24u * 1024u * 1024u;
constexpr long long kMaxVariantPixels = 1920LL * 1088LL;
constexpr size_t kPoolLimit = 128;

struct Policy {
    bool buffered = false;
    double targetSec = kRealtimeJitterSec;
    double startSec = kRealtimeJitterSec;
    double lateSec = 0.1;
    double maxLeadSec = 0.6;
    int maxFrames = 16;
    int readyFrames = 4;
};

bool bufferedScheme(const std::string& scheme)
{
    return scheme == "http" || scheme == "https";
}

Policy policyFor(const Config& cfg, const std::string& scheme)
{
    Policy p;
    const double custom = cfg.bufferMs > 0 ? cfg.bufferMs / 1000.0 : 0.0;
    p.buffered = cfg.mode == Mode::Pull && bufferedScheme(scheme);
    if (p.buffered) {
        p.targetSec = custom > 0.0 ? custom : kBufferedTargetSec;
        p.startSec = 0.1;
        p.maxLeadSec = p.targetSec * 4.0;
        p.maxFrames = p.readyFrames;
    } else {
        const double jitter = custom > 0.0 ? custom : kRealtimeJitterSec;
        p.targetSec = jitter;
        p.startSec = jitter;
        p.maxLeadSec = std::max(0.6, jitter * 3.0);
    }
    return p;
}

void recycle(Shared& s, Frame& f)
{
    if (s.pool.size() < kPoolLimit)
        s.pool.push_back(std::move(f));
}

void trimQueue(Shared& s, const Policy& p, Clock::time_point now)
{
    const size_t hard = static_cast<size_t>(p.maxFrames) + 8;
    while (s.queue.size() > 1 && (s.queue.front().due < now - dur(kStaleSec) || s.queue.size() > hard)) {
        ++s.st.dropped;
        recycle(s, s.queue.front().frame);
        s.queue.pop_front();
    }
}

bool waitForReady(Shared& s, const Policy& p)
{
    std::unique_lock lk(s.m);
    while (!s.stop.load()) {
        trimQueue(s, p, Clock::now());
        if (static_cast<int>(s.queue.size()) < p.readyFrames)
            return true;
        s.cv.wait_for(lk, std::chrono::milliseconds(8), [&] { return s.stop.load(); });
    }
    return false;
}

class PacketQueue {
public:
    PacketQueue(std::atomic<long long>& ms, std::atomic<int>& count) : ms_(ms), count_(count) {}
    ~PacketQueue()
    {
        std::lock_guard lk(m_);
        for (Item& it : q_)
            av_packet_free(&it.pkt);
        q_.clear();
        publish();
    }

    bool waitRoom(const std::atomic<bool>& stop, double targetSec)
    {
        std::unique_lock lk(m_);
        while (!stop.load()) {
            if (sec_ < targetSec && bytes_ < kMaxPacketBytes)
                return true;
            cv_.wait_for(lk, std::chrono::milliseconds(8));
        }
        return false;
    }

    void push(AVPacket* src, double sec)
    {
        AVPacket* p = av_packet_alloc();
        if (!p)
            return;
        av_packet_move_ref(p, src);
        std::lock_guard lk(m_);
        sec_ += sec;
        bytes_ += static_cast<size_t>(p->size);
        q_.push_back({p, sec});
        publish();
        cv_.notify_all();
    }

    AVPacket* pop(const std::atomic<bool>& stop)
    {
        std::unique_lock lk(m_);
        while (!stop.load()) {
            if (!q_.empty()) {
                Item it = q_.front();
                q_.pop_front();
                sec_ = std::max(0.0, sec_ - it.sec);
                bytes_ -= static_cast<size_t>(it.pkt->size);
                publish();
                cv_.notify_all();
                return it.pkt;
            }
            if (done_)
                return nullptr;
            cv_.wait_for(lk, std::chrono::milliseconds(8));
        }
        return nullptr;
    }

    void close()
    {
        std::lock_guard lk(m_);
        done_ = true;
        cv_.notify_all();
    }

private:
    struct Item {
        AVPacket* pkt;
        double sec;
    };

    void publish()
    {
        ms_.store(static_cast<long long>(sec_ * 1000.0));
        count_.store(static_cast<int>(q_.size()));
    }

    std::atomic<long long>& ms_;
    std::atomic<int>& count_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<Item> q_;
    double sec_ = 0.0;
    size_t bytes_ = 0;
    bool done_ = false;
};

void pushFrame(Shared& s, const Policy& p, Frame& f, double t, bool havePts)
{
    std::lock_guard lk(s.m);
    const auto now = Clock::now();
    if (!havePts)
        t = s.anchored ? s.lastPts + s.frameDur : 0.0;
    const bool empty = s.queue.empty();
    const bool jump = s.anchored && (t < s.lastPts - 0.5 || t > s.lastPts + 2.0);
    if (!s.anchored || jump) {
        Clock::time_point start = now + dur(p.startSec);
        if (s.anchored && !empty)
            start = s.lastDue + dur(s.frameDur);
        s.wall0 = start;
        s.pts0 = t;
    } else if (empty) {
        const double late = Sec(now - (s.wall0 + dur(t - s.pts0))).count();
        if (late > p.lateSec)
            s.wall0 += dur(late + p.startSec);
    }
    Clock::time_point due = s.wall0 + dur(t - s.pts0);
    if (!empty && due < s.lastDue)
        due = s.lastDue;
    if (!p.buffered && Sec(due - now).count() > p.maxLeadSec) {
        s.st.dropped += s.queue.size();
        for (QueuedFrame& q : s.queue)
            recycle(s, q.frame);
        s.queue.clear();
        due = now + dur(p.startSec);
        s.wall0 = due - dur(t - s.pts0);
    }
    if (s.anchored && !jump && t > s.lastPts && t - s.lastPts < 0.5)
        s.frameDur = 0.9 * s.frameDur + 0.1 * (t - s.lastPts);
    s.anchored = true;
    s.lastPts = t;
    s.lastDue = due;
    f.pts = t;
    s.queue.emplace_back();
    s.queue.back().due = due;
    std::swap(s.queue.back().frame, f);
    trimQueue(s, p, now);
}

int readTimeoutMs(const std::string& scheme)
{
    return bufferedScheme(scheme) ? 20000 : 5000;
}

void setOpenOptions(AVDictionary** o, const Config& cfg, const std::string& scheme, bool buffered)
{
    av_dict_set(o, "protocol_whitelist", "rtmp,rtmps,rtmpt,rtmpe,rtmpts,rtmpte,rtsp,rtp,srt,udp,tcp,tls,http,https,crypto,hls,ffrtmphttp,ffrtmpcrypt", 0);
    if (buffered) {
        av_dict_set(o, "http_persistent", "1", 0);
        av_dict_set(o, "reconnect", "1", 0);
        av_dict_set(o, "reconnect_streamed", "1", 0);
        av_dict_set(o, "reconnect_on_network_error", "1", 0);
        av_dict_set(o, "reconnect_delay_max", "5", 0);
        av_dict_set(o, "live_start_index", "-2", 0);
        av_dict_set(o, "seg_max_retry", "3", 0);
        av_dict_set(o, "rw_timeout", "8000000", 0);
        return;
    }
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
    int best = -1, smallest = -1;
    long long bestPix = 0, smallestPix = 0, bestRate = 0;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVCodecParameters* par = fmt->streams[i]->codecpar;
        if (par->codec_type != AVMEDIA_TYPE_VIDEO || (fmt->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC))
            continue;
        if (requireSize && (par->width <= 0 || par->height <= 0))
            continue;
        if (!avcodec_find_decoder(par->codec_id))
            continue;
        const long long pix = static_cast<long long>(par->width) * par->height;
        const int idx = static_cast<int>(i);
        if (pix <= kMaxVariantPixels && (best < 0 || pix > bestPix || (pix == bestPix && par->bit_rate > bestRate))) {
            best = idx;
            bestPix = pix;
            bestRate = par->bit_rate;
        }
        if (smallest < 0 || pix < smallestPix) {
            smallest = idx;
            smallestPix = pix;
        }
    }
    return best >= 0 ? best : smallest;
}

struct HlsVariant {
    std::string uri;
    long long bandwidth = 0;
    long long pixels = 0;
};

struct HlsChoice {
    std::string url;
    int index = 0;
    int count = 0;
};

std::string attributeValue(const std::string& line, const std::string& key)
{
    const std::string needle = key + "=";
    size_t pos = 0;
    while ((pos = line.find(needle, pos)) != std::string::npos) {
        if (pos == 0 || line[pos - 1] == ':' || line[pos - 1] == ',') {
            const size_t v = pos + needle.size();
            if (v < line.size() && line[v] == '"') {
                const size_t e = line.find('"', v + 1);
                return line.substr(v + 1, e == std::string::npos ? std::string::npos : e - v - 1);
            }
            const size_t e = line.find(',', v);
            return line.substr(v, e == std::string::npos ? std::string::npos : e - v);
        }
        pos += needle.size();
    }
    return {};
}

std::vector<HlsVariant> parseMasterPlaylist(const std::string& text)
{
    std::vector<HlsVariant> out;
    if (text.compare(0, 7, "#EXTM3U") != 0)
        return out;
    bool pending = false;
    HlsVariant cur;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos)
            eol = text.size();
        const std::string line = trim(text.substr(pos, eol - pos));
        pos = eol + 1;
        if (line.empty())
            continue;
        if (line.rfind("#EXT-X-STREAM-INF:", 0) == 0) {
            cur = HlsVariant{};
            cur.bandwidth = std::atoll(attributeValue(line, "BANDWIDTH").c_str());
            const std::string res = attributeValue(line, "RESOLUTION");
            const size_t x = res.find('x');
            if (x != std::string::npos)
                cur.pixels = std::atoll(res.substr(0, x).c_str()) * std::atoll(res.substr(x + 1).c_str());
            pending = true;
        } else if (line[0] != '#' && pending) {
            cur.uri = line;
            out.push_back(cur);
            pending = false;
        }
    }
    return out;
}

std::string resolveUrl(const std::string& base, const std::string& ref)
{
    if (ref.find("://") != std::string::npos)
        return ref;
    const size_t schemeEnd = base.find("://");
    if (schemeEnd == std::string::npos)
        return {};
    if (ref.rfind("//", 0) == 0)
        return base.substr(0, schemeEnd + 1) + ref;
    const size_t authorityEnd = base.find_first_of("/?#", schemeEnd + 3);
    const std::string origin = authorityEnd == std::string::npos ? base : base.substr(0, authorityEnd);
    if (ref[0] == '/')
        return origin + ref;
    const std::string noQuery = base.substr(0, base.find_first_of("?#"));
    const size_t slash = noQuery.rfind('/');
    if (slash == std::string::npos || slash < schemeEnd + 3)
        return origin + "/" + ref;
    return noQuery.substr(0, slash + 1) + ref;
}

bool looksLikePlaylist(const std::string& url)
{
    const std::string path = lower(url.substr(0, url.find_first_of("?#")));
    return path.size() > 5 && (path.compare(path.size() - 5, 5, ".m3u8") == 0 || path.compare(path.size() - 4, 4, ".m3u") == 0);
}

bool fetchText(Shared& s, const std::string& url, std::string& out)
{
    AVDictionary* o = nullptr;
    av_dict_set(&o, "protocol_whitelist", "http,https,tcp,tls,crypto", 0);
    av_dict_set(&o, "rw_timeout", "8000000", 0);
    AVIOInterruptCB cb{&interruptCallback, &s};
    AVIOContext* io = nullptr;
    s.deadline = nowMs() + 10000;
    const int r = avio_open2(&io, url.c_str(), AVIO_FLAG_READ, &cb, &o);
    av_dict_free(&o);
    if (r >= 0) {
        unsigned char buf[8192];
        while (out.size() < 262144) {
            const int n = avio_read(io, buf, sizeof(buf));
            if (n <= 0)
                break;
            out.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
        }
        avio_closep(&io);
    }
    s.deadline = 0;
    return r >= 0 && !out.empty();
}

HlsChoice chooseHlsVariant(Shared& s, const std::string& url)
{
    HlsChoice choice;
    std::string text;
    if (!fetchText(s, url, text))
        return choice;
    const std::vector<HlsVariant> variants = parseMasterPlaylist(text);
    if (variants.empty())
        return choice;
    int best = -1;
    for (size_t i = 0; i < variants.size(); ++i) {
        const HlsVariant& v = variants[i];
        if (best < 0) {
            best = static_cast<int>(i);
            continue;
        }
        const HlsVariant& b = variants[static_cast<size_t>(best)];
        const bool vFits = v.pixels > 0 ? v.pixels <= kMaxVariantPixels : v.bandwidth <= 8000000;
        const bool bFits = b.pixels > 0 ? b.pixels <= kMaxVariantPixels : b.bandwidth <= 8000000;
        bool better = false;
        if (vFits != bFits)
            better = vFits;
        else if (vFits)
            better = v.pixels != b.pixels ? v.pixels > b.pixels : v.bandwidth > b.bandwidth;
        else
            better = v.pixels != b.pixels ? v.pixels < b.pixels : v.bandwidth < b.bandwidth;
        if (better)
            best = static_cast<int>(i);
    }
    const std::string resolved = resolveUrl(url, variants[static_cast<size_t>(best)].uri);
    if (resolved.empty() || !bufferedScheme(schemeOf(resolved)))
        return choice;
    choice.url = resolved;
    const size_t masterQuery = url.find('?');
    if (resolved.find('?') == std::string::npos && masterQuery != std::string::npos)
        choice.url += url.substr(masterQuery, url.find('#', masterQuery) == std::string::npos ? std::string::npos : url.find('#', masterQuery) - masterQuery);
    choice.index = best + 1;
    choice.count = static_cast<int>(variants.size());
    return choice;
}

Attempt runAttempt(Shared& s)
{
    Attempt res;
    const Config& cfg = s.cfg;
    const bool listen = cfg.mode == Mode::Listen;
    const std::string url = listen ? listenUrl(cfg) : trim(cfg.url);
    const std::string scheme = schemeOf(url);
    const bool passive = listen || passiveScheme(scheme);
    Policy policy = policyFor(cfg, scheme);

    if (listen) {
        std::string why;
        if (!portAvailable(cfg.bindAddress, cfg.port, cfg.protocol == ListenProtocol::Srt, &why)) {
            res.kind = Attempt::Kind::PortBusy;
            res.message = why;
            return res;
        }
    }

    HlsChoice choice;
    if (policy.buffered && looksLikePlaylist(url))
        choice = chooseHlsVariant(s, url);
    if (s.stop.load())
        return res;

    AVFormatContext* fmt = nullptr;
    int r = 0;
    bool deadlineHit = false;
    std::string openUrl = choice.url.empty() ? url : choice.url;
    for (int pass = 0; pass < 2; ++pass) {
        fmt = avformat_alloc_context();
        if (!fmt) {
            res.kind = Attempt::Kind::OpenFailed;
            res.message = "Sem memória.";
            return res;
        }
        fmt->interrupt_callback.callback = &interruptCallback;
        fmt->interrupt_callback.opaque = &s;

        AVDictionary* opts = nullptr;
        setOpenOptions(&opts, cfg, scheme, policy.buffered);

        s.deadline = passive ? 0 : nowMs() + 10000;
        r = avformat_open_input(&fmt, openUrl.c_str(), nullptr, &opts);
        av_dict_free(&opts);
        deadlineHit = !s.stop.load() && s.deadline.load() != 0 && nowMs() > s.deadline.load();
        s.deadline = 0;
        if (r >= 0 || openUrl == url || s.stop.load())
            break;
        openUrl = url;
        choice = HlsChoice{};
    }
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

    const int streamsOpened = static_cast<int>(fmt->nb_streams);
    int programIndex = 0;
    for (unsigned i = 0; i < fmt->nb_streams; ++i)
        if (static_cast<int>(i) != vidx)
            fmt->streams[i]->discard = AVDISCARD_ALL;
    for (unsigned p = 0; p < fmt->nb_programs; ++p) {
        bool has = false;
        for (unsigned k = 0; k < fmt->programs[p]->nb_stream_indexes; ++k)
            has = has || static_cast<int>(fmt->programs[p]->stream_index[k]) == vidx;
        if (has)
            programIndex = static_cast<int>(p) + 1;
        else
            fmt->programs[p]->discard = AVDISCARD_ALL;
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
    if (policy.buffered) {
        cc->thread_count = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 4);
        cc->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    } else {
        cc->thread_type = FF_THREAD_SLICE;
        cc->flags |= AV_CODEC_FLAG_LOW_DELAY;
    }
    if (avcodec_open2(cc, codec, nullptr) < 0) {
        avcodec_free_context(&cc);
        res.kind = Attempt::Kind::OpenFailed;
        res.message = "Falha ao abrir o decodificador de vídeo.";
        return res;
    }

    const std::string codecName = avcodec_get_name(st->codecpar->codec_id);
    const double timeBase = av_q2d(st->time_base);
    double nominalFps = av_q2d(st->avg_frame_rate);
    if (!(nominalFps > 1.0 && nominalFps < 240.0))
        nominalFps = av_q2d(st->r_frame_rate);
    if (!(nominalFps > 1.0 && nominalFps < 240.0))
        nominalFps = 0.0;
    {
        std::lock_guard lk(s.m);
        s.anchored = false;
        if (nominalFps > 0.0)
            s.frameDur = 1.0 / nominalFps;
        s.st.streamsOpened = streamsOpened;
        s.st.streamsUsed = 1;
        s.st.variantCount = choice.count > 0 ? choice.count : std::max(static_cast<int>(fmt->nb_programs), 1);
        s.st.variantIndex = choice.count > 0 ? choice.index : std::max(programIndex, 1);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%dx%d", st->codecpar->width, st->codecpar->height);
        s.st.variant = buf;
        if (nominalFps > 0.0) {
            std::snprintf(buf, sizeof(buf), " %.2f fps", nominalFps);
            s.st.variant += buf;
        }
        s.st.variant += " " + codecName;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    FrameConverter converter;
    PacketQueue pq(s.packetMs, s.packetCount);

    std::atomic<unsigned long long> totalBytes{0}, videoBytes{0}, windowBytes{0};
    std::atomic<bool> noImage{false};
    bool needKey = true;
    const auto keyWaitStart = Clock::now();
    unsigned long long windowFrames = 0, decodedCount = 0;
    double decodeSum = 0.0, decodeMax = 0.0, decodeAcc = 0.0;
    const double windowSec = policy.buffered ? 4.0 : 1.0;
    auto windowStart = Clock::now();
    auto lastFrameAt = Clock::now();
    const int readTimeout = readTimeoutMs(scheme);
    std::string endMessage = "Transmissão encerrada.";

    auto msSinceT = [](Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); };

    auto handlePacket = [&](AVPacket* pk) {
        if (needKey) {
            if (pk->flags & AV_PKT_FLAG_KEY)
                needKey = false;
            else if (Clock::now() - keyWaitStart > std::chrono::seconds(4))
                needKey = false;
            else
                return;
        }
        const auto sendStart = Clock::now();
        const int sr = avcodec_send_packet(cc, pk);
        decodeAcc += msSinceT(sendStart);
        if (sr < 0 && sr != AVERROR(EAGAIN) && sr != AVERROR_INVALIDDATA)
            return;

        while (!s.stop.load()) {
            const auto recvStart = Clock::now();
            const int fr_r = avcodec_receive_frame(cc, fr);
            decodeAcc += msSinceT(recvStart);
            if (fr_r < 0)
                break;
            decodedCount++;
            decodeSum += decodeAcc;
            decodeMax = std::max(decodeMax, decodeAcc);
            decodeAcc = 0.0;
            if (policy.buffered && !waitForReady(s, policy)) {
                av_frame_unref(fr);
                break;
            }
            const auto now = Clock::now();
            lastFrameAt = now;

            const bool havePts = fr->best_effort_timestamp != AV_NOPTS_VALUE;
            const double t = havePts ? static_cast<double>(fr->best_effort_timestamp) * timeBase : 0.0;
            const int w = fr->width, h = fr->height;

            Frame back;
            {
                std::lock_guard lk(s.m);
                if (!s.pool.empty()) {
                    back = std::move(s.pool.back());
                    s.pool.pop_back();
                }
            }
            if (!converter.convert(fr, 0, 0, false, back)) {
                av_frame_unref(fr);
                continue;
            }
            av_frame_unref(fr);
            if (!policy.buffered && w > 0 && h > 0)
                policy.maxFrames = std::clamp(static_cast<int>(kMaxBufferBytes / (static_cast<double>(w) * h * 1.5)), 8, 600);
            pushFrame(s, policy, back, t, havePts);

            ++windowFrames;
            const double span = std::chrono::duration<double>(now - windowStart).count();
            std::lock_guard lk(s.m);
            ++s.st.frames;
            s.st.width = w;
            s.st.height = h;
            s.st.codec = codecName;
            s.st.decoded = decodedCount;
            s.st.bytesRead = totalBytes.load();
            s.st.videoBytes = videoBytes.load();
            s.st.decodeMsAvg = decodeSum / static_cast<double>(decodedCount);
            s.st.decodeMsMax = decodeMax;
            if (nominalFps > 0.0)
                s.st.fps = nominalFps;
            if (!res.gotFrames || s.st.state != State::Live) {
                res.gotFrames = true;
                s.st.state = State::Live;
                s.st.message.clear();
                s.st.hadSignal = true;
                s.nextRetryMs = 0;
            }
            if (span >= windowSec) {
                s.st.inFps = static_cast<double>(windowFrames) / span;
                if (nominalFps <= 0.0)
                    s.st.fps = s.st.inFps;
                s.st.kbps = static_cast<double>(windowBytes.exchange(0)) * 8.0 / 1000.0 / span;
                windowFrames = 0;
                windowStart = now;
            }
        }

        if (Clock::now() - lastFrameAt > std::chrono::seconds(res.gotFrames ? 8 : 15))
            noImage = true;
    };

    std::thread decoder;
    if (policy.buffered)
        decoder = std::thread([&] {
            while (AVPacket* pk = pq.pop(s.stop)) {
                handlePacket(pk);
                av_packet_free(&pk);
            }
        });

    const double fallbackDur = nominalFps > 0.0 ? 1.0 / nominalFps : 1.0 / 30.0;
    while (!s.stop.load()) {
        if (policy.buffered) {
            s.deadline = 0;
            if (!pq.waitRoom(s.stop, policy.targetSec))
                break;
        }
        s.deadline = nowMs() + readTimeout;
        const int rr = av_read_frame(fmt, pkt);
        if (rr < 0) {
            if (rr == AVERROR(EAGAIN))
                continue;
            const bool timedOut = !s.stop.load() && rr == AVERROR_EXIT;
            endMessage = timedOut ? "Sem dados da transmissão." : (rr == AVERROR_EOF ? "Transmissão encerrada." : describeError(rr, cfg, false));
            break;
        }
        totalBytes += static_cast<unsigned long long>(pkt->size);
        if (pkt->stream_index != vidx) {
            av_packet_unref(pkt);
            continue;
        }
        windowBytes += static_cast<unsigned long long>(pkt->size);
        videoBytes += static_cast<unsigned long long>(pkt->size);
        if (policy.buffered) {
            pq.push(pkt, pkt->duration > 0 ? static_cast<double>(pkt->duration) * timeBase : fallbackDur);
        } else {
            handlePacket(pkt);
            av_packet_unref(pkt);
        }
        if (noImage.load()) {
            endMessage = "Sem imagem na transmissão.";
            break;
        }
    }
    s.deadline = 0;
    pq.close();
    if (decoder.joinable())
        decoder.join();

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
    Shared& s = *shared_;
    std::lock_guard lk(s.m);
    if (s.queue.empty())
        return false;
    const auto now = Clock::now();
    bool got = false;
    Frame cur;
    while (!s.queue.empty() && s.queue.front().due <= now) {
        if (got) {
            ++s.st.dropped;
            recycle(s, cur);
        }
        cur = std::move(s.queue.front().frame);
        s.queue.pop_front();
        got = true;
    }
    if (!got)
        return false;
    std::swap(out, cur);
    recycle(s, cur);
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
        out.bufferFrames = static_cast<int>(shared_->queue.size()) + shared_->packetCount.load();
        out.bufferMs = static_cast<double>(shared_->packetMs.load());
        if (!shared_->queue.empty())
            out.bufferMs += std::max(0.0, std::chrono::duration<double, std::milli>(shared_->lastDue - Clock::now()).count());
    }
    if (retryAt > 0) {
        const long long left = retryAt - nowMs();
        out.retryInSeconds = left > 0 ? static_cast<int>((left + 999) / 1000) : 0;
    }
    return out;
}

}
