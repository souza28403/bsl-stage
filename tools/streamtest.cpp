#include "core/Clip.h"
#include "core/Composition.h"
#include "io/ProjectFile.h"
#include "media/NetStream.h"
#include "media/NetStreamSource.h"
#include "output/OutputSetup.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/rational.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace bsl;
using namespace bsl::media;
using Clock = std::chrono::steady_clock;

namespace {

int g_checks = 0;
int g_failures = 0;
int g_skipped = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::printf("  FALHOU (linha %d): %s\n", __LINE__, #cond);       \
        }                                                                    \
    } while (0)

void section(const char* name)
{
    std::printf("[%s]\n", name);
}

double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

bool waitUntil(int timeoutMs, const std::function<bool()>& pred)
{
    const auto t0 = Clock::now();
    while (msSince(t0) < timeoutMs) {
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return pred();
}

int freePort(bool udp)
{
    WSADATA d{};
    WSAStartup(MAKEWORD(2, 2), &d);
    SOCKET s = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, udp ? IPPROTO_UDP : IPPROTO_TCP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = 0;
    InetPtonA(AF_INET, "127.0.0.1", &a.sin_addr);
    bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    int len = sizeof(a);
    getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    const int port = ntohs(a.sin_port);
    closesocket(s);
    return port;
}

struct Holder
{
    SOCKET s = INVALID_SOCKET;
    bool ok = false;
    explicit Holder(int port)
    {
        WSADATA d{};
        WSAStartup(MAKEWORD(2, 2), &d);
        s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<u_short>(port));
        InetPtonA(AF_INET, "127.0.0.1", &a.sin_addr);
        ok = bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 && listen(s, 8) == 0;
    }
    ~Holder()
    {
        if (s != INVALID_SOCKET)
            closesocket(s);
    }
};

struct PubOptions
{
    std::string url;
    std::string format;
    std::string codec;
    bool listen = false;
    int w = 320;
    int h = 240;
    int fps = 25;
    std::vector<std::pair<std::string, std::string>> ioOptions;
};

int pubInterrupt(void* opaque)
{
    return static_cast<std::atomic<bool>*>(opaque)->load() ? 1 : 0;
}

class Publisher
{
public:
    explicit Publisher(PubOptions o) : opt_(std::move(o)) {}
    ~Publisher() { stop(); }

    void start() { thread_ = std::thread([this] { run(); }); }
    void stop()
    {
        stop_ = true;
        if (thread_.joinable())
            thread_.join();
    }
    void setPaused(bool p) { paused_ = p; }
    bool connected() const { return connected_; }
    long frames() const { return frames_; }
    bool failed() const { return failed_; }
    bool done() const { return done_; }
    const std::string& error() const { return error_; }

private:
    void fail(const std::string& why)
    {
        error_ = why;
        failed_ = true;
    }

    void run()
    {
        const AVCodec* codec = avcodec_find_encoder_by_name(opt_.codec.c_str());
        if (!codec) {
            fail("encoder ausente: " + opt_.codec);
            done_ = true;
            return;
        }
        AVFormatContext* oc = nullptr;
        avformat_alloc_output_context2(&oc, nullptr, opt_.format.c_str(), opt_.url.c_str());
        if (!oc) {
            fail("muxer ausente");
            done_ = true;
            return;
        }
        oc->interrupt_callback.callback = &pubInterrupt;
        oc->interrupt_callback.opaque = &stop_;
        AVStream* st = avformat_new_stream(oc, nullptr);
        AVCodecContext* cc = avcodec_alloc_context3(codec);
        cc->width = opt_.w;
        cc->height = opt_.h;
        cc->time_base = AVRational{1, opt_.fps};
        cc->framerate = AVRational{opt_.fps, 1};
        cc->pix_fmt = AV_PIX_FMT_YUV420P;
        cc->gop_size = 10;
        cc->max_b_frames = 0;
        cc->bit_rate = 1500000;
        if (oc->oformat->flags & AVFMT_GLOBALHEADER)
            cc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(cc, codec, nullptr) < 0) {
            fail("avcodec_open2");
            avcodec_free_context(&cc);
            avformat_free_context(oc);
            done_ = true;
            return;
        }
        avcodec_parameters_from_context(st->codecpar, cc);
        st->time_base = cc->time_base;

        AVDictionary* io = nullptr;
        for (const auto& [k, v] : opt_.ioOptions)
            av_dict_set(&io, k.c_str(), v.c_str(), 0);
        if (opt_.listen)
            av_dict_set(&io, "listen", "1", 0);
        int r = avio_open2(&oc->pb, opt_.url.c_str(), AVIO_FLAG_WRITE, &oc->interrupt_callback, &io);
        av_dict_free(&io);
        if (r < 0) {
            char buf[128];
            av_strerror(r, buf, sizeof(buf));
            fail(std::string("avio_open2: ") + buf);
            avcodec_free_context(&cc);
            avformat_free_context(oc);
            done_ = true;
            return;
        }
        AVDictionary* ho = nullptr;
        av_dict_set(&ho, "flush_packets", "1", 0);
        r = avformat_write_header(oc, &ho);
        av_dict_free(&ho);
        if (r < 0) {
            char buf[128];
            av_strerror(r, buf, sizeof(buf));
            fail(std::string("write_header: ") + buf);
        } else {
            connected_ = true;
            pump(oc, cc, st);
            av_write_trailer(oc);
        }
        avio_closep(&oc->pb);
        avcodec_free_context(&cc);
        avformat_free_context(oc);
        done_ = true;
    }

    void pump(AVFormatContext* oc, AVCodecContext* cc, AVStream* st)
    {
        AVFrame* fr = av_frame_alloc();
        fr->format = AV_PIX_FMT_YUV420P;
        fr->width = opt_.w;
        fr->height = opt_.h;
        av_frame_get_buffer(fr, 0);
        AVPacket* pkt = av_packet_alloc();
        const auto t0 = Clock::now();
        int64_t n = 0;
        while (!stop_.load()) {
            if (paused_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            av_frame_make_writable(fr);
            for (int y = 0; y < opt_.h; ++y) {
                for (int x = 0; x < opt_.w; ++x) {
                    const bool left = x < opt_.w / 2;
                    fr->data[0][y * fr->linesize[0] + x] = left ? 90 : 170;
                    if (y >= opt_.h - 24)
                        fr->data[0][y * fr->linesize[0] + x] = static_cast<uint8_t>(40 + (n * 7) % 200);
                }
            }
            for (int y = 0; y < opt_.h / 2; ++y) {
                for (int x = 0; x < opt_.w / 2; ++x) {
                    const bool left = x < opt_.w / 4;
                    fr->data[1][y * fr->linesize[1] + x] = left ? 100 : 140;
                    fr->data[2][y * fr->linesize[2] + x] = left ? 190 : 70;
                }
            }
            fr->pts = n;
            if (avcodec_send_frame(cc, fr) < 0)
                break;
            while (avcodec_receive_packet(cc, pkt) == 0) {
                av_packet_rescale_ts(pkt, cc->time_base, st->time_base);
                pkt->stream_index = st->index;
                if (av_interleaved_write_frame(oc, pkt) < 0) {
                    stop_ = true;
                    break;
                }
            }
            ++n;
            frames_ = static_cast<long>(n);
            const auto due = t0 + std::chrono::microseconds(n * 1000000LL / opt_.fps);
            while (!stop_.load() && Clock::now() < due)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        av_packet_free(&pkt);
        av_frame_free(&fr);
    }

    PubOptions opt_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> done_{false};
    std::atomic<long> frames_{0};
    std::string error_;
};

bool planeAvg(const Frame& f, int plane, int x0, int y0, int x1, int y1, double& out)
{
    if (f.empty() || plane >= Frame::planeCount(f.format))
        return false;
    const size_t p = static_cast<size_t>(plane);
    long sum = 0, n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            sum += f.planes[p][static_cast<size_t>(y) * static_cast<size_t>(f.strides[p]) + static_cast<size_t>(x)];
            ++n;
        }
    out = n ? static_cast<double>(sum) / static_cast<double>(n) : 0.0;
    return n > 0;
}

bool frameMatches(const Frame& f, std::string& why)
{
    if (f.format != PixelFormat::YUV420P || f.width != 320 || f.height != 240) {
        why = "formato/tamanho inesperado " + std::to_string(f.width) + "x" + std::to_string(f.height);
        return false;
    }
    double yl, yr, ul, ur, vl, vr;
    planeAvg(f, 0, 40, 40, 120, 160, yl);
    planeAvg(f, 0, 200, 40, 280, 160, yr);
    planeAvg(f, 1, 20, 20, 60, 80, ul);
    planeAvg(f, 1, 100, 20, 140, 80, ur);
    planeAvg(f, 2, 20, 20, 60, 80, vl);
    planeAvg(f, 2, 100, 20, 140, 80, vr);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Y %.0f/%.0f U %.0f/%.0f V %.0f/%.0f", yl, yr, ul, ur, vl, vr);
    why = buf;
    return std::abs(yl - 90) < 14 && std::abs(yr - 170) < 14 && std::abs(ul - 100) < 12 && std::abs(ur - 140) < 12 &&
           std::abs(vl - 190) < 12 && std::abs(vr - 70) < 12;
}

net::Config listenConfig(int port, net::ListenProtocol proto = net::ListenProtocol::Rtmp)
{
    net::Config c;
    c.mode = net::Mode::Listen;
    c.protocol = proto;
    c.port = port;
    c.path = "/live/teste";
    c.bindAddress = "127.0.0.1";
    return c;
}

net::Config pullConfig(const std::string& url)
{
    net::Config c;
    c.mode = net::Mode::Pull;
    c.url = url;
    return c;
}

bool encoderAvailable(const char* name)
{
    return avcodec_find_encoder_by_name(name) != nullptr;
}

bool waitLiveAndMatch(net::StreamWorker& w, int timeoutMs, std::string& detail)
{
    Frame latest;
    bool matched = false;
    const auto t0 = Clock::now();
    while (msSince(t0) < timeoutMs) {
        Frame f;
        if (w.takeFrame(f)) {
            latest = std::move(f);
            std::string why;
            if (frameMatches(latest, why) && w.status().state == net::State::Live) {
                matched = true;
                detail = why;
                break;
            }
            detail = why;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return matched;
}

void testHelpers()
{
    section("auxiliares: endereços, validação, mascaramento");
    CHECK(net::normalizePath("") == "/live/stream1");
    CHECK(net::normalizePath("palco") == "/live/palco");
    CHECK(net::normalizePath("/ao vivo/Cam 1") == "/ao-vivo/Cam-1");
    CHECK(net::normalizePath("//live//x/") == "/live/x");
    CHECK(net::sanitizeName("  Meu Palco! ") == "Meu-Palco");
    CHECK(net::sanitizeName("###") == "stream1");

    net::Config c = listenConfig(1935);
    c.bindAddress = "0.0.0.0";
    CHECK(net::listenUrl(c) == "rtmp://0.0.0.0:1935/live/teste");
    CHECK(net::displayAddress(c, "192.168.0.5") == "rtmp://192.168.0.5:1935/live/teste");
    c.protocol = net::ListenProtocol::Srt;
    c.port = 9000;
    CHECK(net::listenUrl(c) == "srt://0.0.0.0:9000?mode=listener");
    CHECK(net::displayAddress(c, "10.0.0.2") == "srt://10.0.0.2:9000");

    CHECK(net::validateConfig(pullConfig("")).size() > 0);
    CHECK(net::validateConfig(pullConfig("foo")).find("inválido") != std::string::npos);
    CHECK(net::validateConfig(pullConfig("ftp://servidor/x")).find("Protocolo") != std::string::npos);
    CHECK(net::validateConfig(pullConfig("file:///c:/segredo.mp4")).find("Protocolo") != std::string::npos);
    CHECK(net::validateConfig(pullConfig("rtmp://")).find("inválido") != std::string::npos);
    CHECK(net::validateConfig(pullConfig("rtmp://a b/c")).find("inválido") != std::string::npos);
    CHECK(net::validateConfig(pullConfig("rtmp://servidor/app/chave")).empty());
    CHECK(net::validateConfig(pullConfig("https://servidor/lista.m3u8")).empty());
    CHECK(net::validateConfig(pullConfig("udp://0.0.0.0:5000")).empty());
    net::Config bad = listenConfig(0);
    CHECK(net::validateConfig(bad).find("Porta") != std::string::npos);
    bad.port = 70000;
    CHECK(!net::validateConfig(bad).empty());

    const std::string red = net::redactUrl("rtmp://usuario:senha@servidor.exemplo:1935/live/CHAVE-SECRETA?token=abc");
    CHECK(red == "rtmp://servidor.exemplo:1935/…");
    CHECK(red.find("SECRETA") == std::string::npos && red.find("senha") == std::string::npos);
    CHECK(net::redactUrl("srt://servidor:9000") == "srt://servidor:9000");

    const fs::path tmp = fs::temp_directory_path() / "bslstage_streamtest";
    fs::create_directories(tmp);
    {
        std::ofstream(tmp / "camera.url") << "[InternetShortcut]\r\nURL=rtsp://192.168.0.20/stream1\r\n";
        std::ofstream(tmp / "site.url") << "[InternetShortcut]\r\nURL=https://exemplo.com/\r\n";
    }
    CHECK(net::urlFromShortcut(tmp / "camera.url") == "rtsp://192.168.0.20/stream1");
    CHECK(net::urlFromShortcut(tmp / "site.url").empty() == false);
    CHECK(net::urlFromShortcut(tmp / "naoexiste.url").empty());
    fs::remove_all(tmp);

    const auto ips = net::localIPv4Addresses();
    std::printf("  IPv4 locais: %zu\n", ips.size());
    for (const std::string& ip : ips)
        CHECK(ip.find('.') != std::string::npos && ip.rfind("127.", 0) != 0 && ip.rfind("169.254.", 0) != 0);
}

void testReceiveRtmp()
{
    section("1) receber RTMP: a fonte escuta, o teste publica");
    if (!encoderAvailable("flv")) {
        std::printf("  PULADO: encoder flv1 ausente\n");
        ++g_skipped;
        return;
    }
    const int port = freePort(false);
    net::StreamWorker w;
    w.start(listenConfig(port));
    CHECK(waitUntil(3000, [&] { return w.status().state == net::State::Waiting; }));
    const net::Status waiting = w.status();
    std::printf("  estado: %s\n", net::describeStatus(listenConfig(port), waiting).c_str());
    CHECK(waiting.address == "rtmp://127.0.0.1:" + std::to_string(port) + "/live/teste" ||
          waiting.address.find(":" + std::to_string(port) + "/live/teste") != std::string::npos);

    PubOptions o;
    o.url = "rtmp://127.0.0.1:" + std::to_string(port) + "/live/teste";
    o.format = "flv";
    o.codec = "flv";
    Publisher pub(o);
    pub.start();
    std::string detail;
    const bool ok = waitLiveAndMatch(w, 12000, detail);
    std::printf("  conteúdo: %s\n", detail.c_str());
    CHECK(ok);
    const net::Status live = w.status();
    std::printf("  %s\n", net::describeStatus(listenConfig(port), live).c_str());
    CHECK(live.state == net::State::Live);
    CHECK(live.width == 320 && live.height == 240 && live.codec == "flv1");
    CHECK(live.frames >= 1);
    CHECK(pub.error().empty());

    const auto t0 = Clock::now();
    pub.stop();
    CHECK(waitUntil(8000, [&] { return w.status().state != net::State::Live; }));
    std::printf("  envio terminado, a fonte saiu de Ao vivo em %.0f ms\n", msSince(t0));
    CHECK(waitUntil(4000, [&] { return w.status().state == net::State::Waiting && w.status().hadSignal; }));
    std::printf("  %s\n", net::describeStatus(listenConfig(port), w.status()).c_str());

    Publisher pub2(o);
    pub2.start();
    detail.clear();
    CHECK(waitLiveAndMatch(w, 12000, detail));
    std::printf("  voltou a escutar e recebeu de novo: %s\n", detail.c_str());
    CHECK(w.status().state == net::State::Live);

    pub2.setPaused(true);
    const auto t2 = Clock::now();
    CHECK(waitUntil(15000, [&] { return w.status().state != net::State::Live; }));
    std::printf("  envio travado sem fechar a conexão: a fonte saiu de Ao vivo em %.0f ms\n", msSince(t2));
    pub2.stop();
    w.stop();
}

void testPullRtmp()
{
    section("2) puxar RTMP: o teste escuta, a fonte puxa; 3) queda e reconexão");
    if (!encoderAvailable("flv")) {
        std::printf("  PULADO: encoder flv1 ausente\n");
        ++g_skipped;
        return;
    }
    const int port = freePort(false);
    const std::string url = "rtmp://127.0.0.1:" + std::to_string(port) + "/live/saida";
    PubOptions o;
    o.url = url;
    o.format = "flv";
    o.codec = "flv";
    o.listen = true;

    auto pub = std::make_unique<Publisher>(o);
    pub->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    net::StreamWorker w;
    w.start(pullConfig(url));
    std::string detail;
    const bool ok = waitLiveAndMatch(w, 12000, detail);
    std::printf("  conteúdo: %s\n", detail.c_str());
    CHECK(ok);
    CHECK(w.status().state == net::State::Live);
    std::printf("  %s\n", net::describeStatus(pullConfig(url), w.status()).c_str());

    pub->stop();
    pub.reset();
    const auto t0 = Clock::now();
    CHECK(waitUntil(10000, [&] { return w.status().state == net::State::NoSignal; }));
    std::printf("  queda detectada em %.0f ms\n", msSince(t0));
    const net::Status lost = w.status();
    std::printf("  %s\n", net::describeStatus(pullConfig(url), lost).c_str());
    CHECK(lost.state == net::State::NoSignal && lost.hadSignal);

    pub = std::make_unique<Publisher>(o);
    pub->start();
    detail.clear();
    const auto t1 = Clock::now();
    const bool back = waitLiveAndMatch(w, 20000, detail);
    std::printf("  reconectou em %.0f ms: %s\n", msSince(t1), detail.c_str());
    CHECK(back);
    CHECK(w.status().state == net::State::Live);
    pub->stop();
    w.stop();
}

void testSrtAndUdp()
{
    section("SRT e UDP em MPEG-TS");
    if (!encoderAvailable("mpeg4")) {
        std::printf("  PULADO: encoder mpeg4 ausente\n");
        ++g_skipped;
        return;
    }
    {
        const int port = freePort(true);
        net::StreamWorker w;
        w.start(listenConfig(port, net::ListenProtocol::Srt));
        const bool waiting = waitUntil(3000, [&] { return w.status().state == net::State::Waiting; });
        CHECK(waiting);
        PubOptions o;
        o.url = "srt://127.0.0.1:" + std::to_string(port) + "?pkt_size=1316";
        o.format = "mpegts";
        o.codec = "mpeg4";
        Publisher pub(o);
        pub.start();
        std::string detail;
        const bool ok = waitLiveAndMatch(w, 15000, detail);
        std::printf("  SRT receber: %s (%s)\n", ok ? "ok" : "FALHOU", ok ? detail.c_str() : pub.error().c_str());
        if (!ok && !waiting) {
            std::printf("  SRT indisponível neste build/máquina\n");
            ++g_skipped;
        } else {
            CHECK(ok);
        }
        pub.stop();
        w.stop();
    }
    {
        const int port = freePort(true);
        const std::string url = "srt://127.0.0.1:" + std::to_string(port) + "?mode=listener&pkt_size=1316";
        PubOptions o;
        o.url = url;
        o.format = "mpegts";
        o.codec = "mpeg4";
        o.listen = false;
        Publisher pub(o);
        pub.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        net::StreamWorker w;
        w.start(pullConfig("srt://127.0.0.1:" + std::to_string(port)));
        std::string detail;
        const bool ok = waitLiveAndMatch(w, 15000, detail);
        std::printf("  SRT puxar: %s (%s)\n", ok ? "ok" : "FALHOU", ok ? detail.c_str() : pub.error().c_str());
        CHECK(ok);
        pub.stop();
        w.stop();
    }
    {
        const int port = freePort(true);
        net::StreamWorker w;
        w.start(pullConfig("udp://127.0.0.1:" + std::to_string(port)));
        CHECK(waitUntil(3000, [&] { return w.status().state == net::State::Waiting; }));
        PubOptions o;
        o.url = "udp://127.0.0.1:" + std::to_string(port) + "?pkt_size=1316";
        o.format = "mpegts";
        o.codec = "mpeg4";
        Publisher pub(o);
        pub.start();
        std::string detail;
        const bool ok = waitLiveAndMatch(w, 15000, detail);
        std::printf("  UDP puxar: %s (%s)\n", ok ? "ok" : "FALHOU", ok ? detail.c_str() : pub.error().c_str());
        CHECK(ok);
        pub.stop();
        w.stop();
    }
}

void testStopSpeed()
{
    section("4) parada: destruir enquanto bloqueada termina em < 500 ms");
    {
        const int port = freePort(false);
        auto w = std::make_unique<net::StreamWorker>();
        w->start(listenConfig(port));
        CHECK(waitUntil(3000, [&] { return w->status().state == net::State::Waiting; }));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const auto t0 = Clock::now();
        w.reset();
        const double ms = msSince(t0);
        std::printf("  escutando RTMP sem ninguém: %.0f ms\n", ms);
        CHECK(ms < 500.0);
    }
    {
        const int port = freePort(true);
        auto w = std::make_unique<net::StreamWorker>();
        w->start(listenConfig(port, net::ListenProtocol::Srt));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const auto t0 = Clock::now();
        w.reset();
        const double ms = msSince(t0);
        std::printf("  escutando SRT sem ninguém: %.0f ms\n", ms);
        CHECK(ms < 500.0);
    }
    {
        Holder h(freePort(false));
        CHECK(h.ok);
        sockaddr_in a{};
        int len = sizeof(a);
        getsockname(h.s, reinterpret_cast<sockaddr*>(&a), &len);
        const int port = ntohs(a.sin_port);
        auto w = std::make_unique<net::StreamWorker>();
        w->start(pullConfig("rtmp://127.0.0.1:" + std::to_string(port) + "/live/x"));
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        const net::State s = w->status().state;
        const auto t0 = Clock::now();
        w.reset();
        const double ms = msSince(t0);
        std::printf("  puxando de um servidor mudo (estado antes: %d): %.0f ms\n", static_cast<int>(s), ms);
        CHECK(ms < 500.0);
    }
    {
        auto w = std::make_unique<net::StreamWorker>();
        w->start(pullConfig("rtmp://10.255.255.1/live/x"));
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        const auto t0 = Clock::now();
        w.reset();
        const double ms = msSince(t0);
        std::printf("  puxando de endereço que não responde: %.0f ms\n", ms);
        CHECK(ms < 500.0);
    }
    {
        auto w = std::make_unique<net::StreamWorker>();
        w->start(pullConfig("rtmp://127.0.0.1:1/live/x"));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto t0 = Clock::now();
        w.reset();
        CHECK(msSince(t0) < 500.0);
    }
}

void testErrors()
{
    section("5) URL inválida, porta em uso e recusa dão erro claro");
    for (const char* bad : {"", "foo", "ftp://servidor/x", "rtmp://", "file:///c:/x.mp4", "rtmp://a b/c"}) {
        net::StreamWorker w;
        w.start(pullConfig(bad));
        const bool err = waitUntil(2000, [&] { return w.status().state == net::State::Error; });
        const net::Status s = w.status();
        std::printf("  \"%s\" -> %s\n", bad, net::describeStatus(pullConfig(bad), s).c_str());
        CHECK(err);
        CHECK(s.message.size() > 10);
        const auto t0 = Clock::now();
        w.stop();
        CHECK(msSince(t0) < 500.0);
    }
    {
        Holder h(freePort(false));
        CHECK(h.ok);
        sockaddr_in a{};
        int len = sizeof(a);
        getsockname(h.s, reinterpret_cast<sockaddr*>(&a), &len);
        const int port = ntohs(a.sin_port);
        net::StreamWorker w;
        w.start(listenConfig(port));
        const bool err = waitUntil(3000, [&] { return w.status().state == net::State::Error; });
        const net::Status s = w.status();
        std::printf("  porta em uso -> %s\n", net::describeStatus(listenConfig(port), s).c_str());
        CHECK(err);
        CHECK(s.message.find("em uso") != std::string::npos);
        CHECK(s.retryInSeconds >= 1);
        const auto t0 = Clock::now();
        w.stop();
        CHECK(msSince(t0) < 500.0);
    }
    {
        net::StreamWorker w;
        w.start(listenConfig(0));
        const bool err = waitUntil(2000, [&] { return w.status().state == net::State::Error; });
        std::printf("  porta 0 -> %s\n", net::describeStatus(listenConfig(0), w.status()).c_str());
        CHECK(err);
        w.stop();
    }
    {
        const int port = freePort(false);
        const std::string url = "rtmp://127.0.0.1:" + std::to_string(port) + "/live/x";
        net::StreamWorker w;
        w.start(pullConfig(url));
        const bool lost = waitUntil(6000, [&] { return w.status().state == net::State::NoSignal; });
        const net::Status s = w.status();
        std::printf("  servidor fora -> %s\n", net::describeStatus(pullConfig(url), s).c_str());
        CHECK(lost);
        CHECK(s.retryInSeconds >= 1);
        CHECK(s.message.find("recus") != std::string::npos || s.message.find("inacess") != std::string::npos ||
              s.message.find("Falha") != std::string::npos);
        w.stop();
    }
}

void testBackoff()
{
    section("espera crescente entre tentativas (1, 2, 4... até 10 s)");
    Holder h(freePort(false));
    CHECK(h.ok);
    sockaddr_in a{};
    int len = sizeof(a);
    getsockname(h.s, reinterpret_cast<sockaddr*>(&a), &len);
    const int port = ntohs(a.sin_port);
    net::StreamWorker w;
    w.start(listenConfig(port));
    std::vector<int> waits;
    int last = -1;
    const auto t0 = Clock::now();
    while (msSince(t0) < 16000 && waits.size() < 4) {
        const net::Status s = w.status();
        const bool counting = s.state == net::State::Error && s.retryInSeconds > 0;
        if (counting && (last <= 0 || s.retryInSeconds > last))
            waits.push_back(s.retryInSeconds);
        last = counting ? s.retryInSeconds : 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    w.stop();
    std::printf("  esperas vistas:");
    for (int v : waits)
        std::printf(" %d", v);
    std::printf("\n");
    CHECK(waits.size() >= 4);
    if (waits.size() >= 4)
        CHECK(waits[0] == 1 && waits[1] == 2 && waits[2] == 4 && waits[3] == 8);
}

void testProject()
{
    section("6) salvar e carregar o projeto com a fonte");
    Composition comp = Composition::createDefault(2, 4);
    Clip pull = Clip::generator(kNetStreamId);
    pull.setParam("mode", "pull");
    pull.setParam("url", "rtmp://servidor.exemplo/live/CHAVE-123");
    pull.setParam("loss", "black");
    comp.setClip(0, 0, std::move(pull));
    Clip recv = Clip::generator(kNetStreamId);
    recv.setParam("mode", "listen");
    recv.setParam("protocol", "srt");
    recv.setParam("port", 9100);
    recv.setParam("path", "/live/palco");
    comp.setClip(1, 1, std::move(recv));
    CHECK(comp.clip(0, 0).isLive() && comp.clip(0, 0).keepsAliveInBackground());
    CHECK(comp.clip(0, 0).name == "Stream: servidor.exemplo");
    CHECK(comp.clip(0, 0).name.find("CHAVE") == std::string::npos);
    CHECK(comp.clip(1, 1).name == "Receber: porta 9100");

    OutputSetup out;
    const fs::path dir = fs::temp_directory_path() / "bslstage_streamtest_proj";
    fs::create_directories(dir);
    const std::string text = project::serialize(comp, out, dir);
    CHECK(text.find("\"generator\": \"gen:stream-de-rede\"") != std::string::npos);
    std::string err;
    const fs::path file = dir / "stream.bslstage";
    CHECK(project::saveFile(file, text, &err));

    Composition comp2 = Composition::createDefault(1, 1);
    OutputSetup out2;
    const project::LoadResult r = project::loadFile(file, comp2, out2);
    CHECK(r.ok);
    const Clip& a = comp2.clipAt(0, 0, 0);
    CHECK(a.isGenerator() && a.generatorId == kNetStreamId && a.isLive());
    const net::Config ca = media::NetStreamSource::configFrom(a.params);
    CHECK(ca.mode == net::Mode::Pull && ca.url == "rtmp://servidor.exemplo/live/CHAVE-123");
    CHECK(media::ParamSet(a.paramDescs(), a.params).s("loss") == "black");
    CHECK(a.name == "Stream: servidor.exemplo");
    const Clip& b = comp2.clipAt(0, 1, 1);
    const net::Config cb = media::NetStreamSource::configFrom(b.params);
    CHECK(cb.mode == net::Mode::Listen && cb.protocol == net::ListenProtocol::Srt && cb.port == 9100 && cb.path == "/live/palco");
    CHECK(b.name == "Receber: porta 9100");

    const std::string text2 = project::serialize(comp2, out2, dir);
    CHECK(text2 == text);

    const std::string oldDoc = R"({"format":"bslstage","version":1,"composition":{"width":1280,"height":720,"fps":30,"bpm":120},
        "layers":[{"name":"A"}],"decks":[{"name":"D","columns":2,"clips":[{"layer":0,"column":0,"generator":"gen:padrao-de-teste"}]}]})";
    Composition comp3 = Composition::createDefault(1, 1);
    OutputSetup out3;
    const project::LoadResult r3 = project::parse(oldDoc, dir, comp3, out3);
    CHECK(r3.ok);
    fs::remove_all(dir);
}

GLFWwindow* makeContext()
{
    if (!glfwInit())
        return nullptr;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "streamtest", nullptr, nullptr);
    if (!win)
        return nullptr;
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress))
        return nullptr;
    return win;
}

struct Rgb
{
    int r, g, b;
};

Rgb pixelAt(GLuint tex, int w, int h, int x, int y)
{
    std::vector<unsigned char> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    const unsigned char* p = &px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4];
    return {p[0], p[1], p[2]};
}

void testSourceGl(GLFWwindow* win)
{
    section("fonte completa (textura GL): receber, perder sinal, manter ou preto, fechar rápido");
    if (!win || !encoderAvailable("flv")) {
        std::printf("  PULADO: sem contexto GL ou sem encoder flv1\n");
        ++g_skipped;
        return;
    }
    const int port = freePort(false);
    ParamValues v;
    v.fillDefaults(NetStreamSource::describe(v));
    v.set("mode", "listen");
    v.set("port", port);
    v.set("path", "/live/teste");
    v.set("loss", "black");

    auto src = std::make_unique<NetStreamSource>();
    src->setParams(v);
    CHECK(src->isLive() && !src->supportsReverse());
    CHECK(src->open({}));
    CHECK(src->duration() == 0.0);
    src->seek(5.0);
    CHECK(src->position() == 0.0);

    auto pump = [&](int ms, const std::function<bool()>& stopWhen) {
        const auto t0 = Clock::now();
        while (msSince(t0) < ms) {
            src->update(1.0 / 60.0);
            if (stopWhen())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        return stopWhen();
    };

    CHECK(pump(3000, [&] { return src->status().text.rfind("Aguardando envio em", 0) == 0; }));
    std::printf("  status: %s\n", src->status().text.c_str());

    PubOptions o;
    o.url = "rtmp://127.0.0.1:" + std::to_string(port) + "/live/teste";
    o.format = "flv";
    o.codec = "flv";
    auto pub = std::make_unique<Publisher>(o);
    pub->start();
    auto leftIsRed = [&] {
        if (src->width() != 320 || src->frameCount() < 2)
            return false;
        const Rgb p = pixelAt(src->texture(), 320, 240, 80, 100);
        return p.r > p.b + 100 && p.r > p.g + 80;
    };
    CHECK(pump(12000, leftIsRed));
    std::printf("  status: %s\n", src->status().text.c_str());
    CHECK(src->status().text.rfind("Ao vivo – 320x240", 0) == 0);
    const Rgb left = pixelAt(src->texture(), 320, 240, 80, 100);
    const Rgb right = pixelAt(src->texture(), 320, 240, 240, 100);
    std::printf("  pixel esquerdo (%d,%d,%d) direito (%d,%d,%d)\n", left.r, left.g, left.b, right.r, right.g, right.b);
    CHECK(left.r > left.b + 100 && left.r > left.g + 80);
    CHECK(right.g > right.r + 80 && right.b > right.r + 80);

    pub->stop();
    pub.reset();
    CHECK(pump(10000, [&] { return src->status().text.rfind("Sem sinal", 0) == 0; }));
    std::printf("  status: %s\n", src->status().text.c_str());
    pump(300, [] { return false; });
    const Rgb dark = pixelAt(src->texture(), 320, 240, 80, 100);
    std::printf("  ao perder o sinal (preto): (%d,%d,%d)\n", dark.r, dark.g, dark.b);
    CHECK(dark.r < 10 && dark.g < 10 && dark.b < 10);

    ParamValues hold = src->paramValues();
    hold.set("loss", "hold");
    src->setParams(hold);
    pump(1500, [] { return false; });
    pub = std::make_unique<Publisher>(o);
    pub->start();
    CHECK(pump(15000, [&] { return src->status().text.rfind("Ao vivo", 0) == 0 && leftIsRed(); }));
    pub->stop();
    pub.reset();
    CHECK(pump(10000, [&] { return src->status().text.rfind("Sem sinal", 0) == 0; }));
    pump(300, [] { return false; });
    const Rgb kept = pixelAt(src->texture(), 320, 240, 80, 100);
    std::printf("  ao perder o sinal (manter): (%d,%d,%d)\n", kept.r, kept.g, kept.b);
    CHECK(kept.r > kept.b + 100);

    const auto t0 = Clock::now();
    src.reset();
    const double ms = msSince(t0);
    std::printf("  fechar o clip: %.0f ms\n", ms);
    CHECK(ms < 500.0);
}

}

int main()
{
    av_log_set_level(AV_LOG_ERROR);
    avformat_network_init();

    testHelpers();
    testReceiveRtmp();
    testPullRtmp();
    testSrtAndUdp();
    testStopSpeed();
    testErrors();
    testBackoff();
    testProject();

    GLFWwindow* win = makeContext();
    testSourceGl(win);
    if (win)
        glfwTerminate();

    std::printf("\n%d verificação(ões), %d falha(s), %d pulada(s)\n", g_checks, g_failures, g_skipped);
    return g_failures == 0 ? 0 : 1;
}
