#include "media/Frame.h"
#include "media/HapFrame.h"
#include "media/HapTranscode.h"
#include "media/Snappy.h"
#include "media/VideoDecoder.h"
#include "media/VideoSource.h"
#include "media/YuvConverter.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace bsl::media;
using Clock = std::chrono::steady_clock;
using Bytes = std::vector<std::uint8_t>;

namespace {

int failures = 0;

void check(const std::string& name, bool ok, const std::string& detail = {})
{
    std::printf("%-46s %s  %s\n", name.c_str(), ok ? "OK  " : "FAIL", detail.c_str());
    if (!ok)
        ++failures;
}

double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

Bytes bytes(std::initializer_list<int> v)
{
    Bytes b;
    for (int x : v)
        b.push_back(static_cast<std::uint8_t>(x));
    return b;
}

Bytes text(const char* s)
{
    return Bytes(s, s + std::strlen(s));
}

bool decodesTo(const Bytes& in, const Bytes& want)
{
    Bytes out;
    return snappyDecompress(in.data(), in.size(), out) && out == want;
}

bool fails(const Bytes& in)
{
    Bytes out;
    return !snappyDecompress(in.data(), in.size(), out);
}

void testSnappy()
{
    std::printf("\n[1] Snappy\n");
    check("vazio", decodesTo(bytes({0x00}), {}));
    check("literal", decodesTo(bytes({5, 0x10, 'h', 'e', 'l', 'l', 'o'}), text("hello")));
    check("copia de 1 byte de offset", decodesTo(bytes({9, 0x08, 'a', 'b', 'c', 0x09, 3}), text("abcabcabc")));
    check("copia de 2 bytes de offset", decodesTo(bytes({10, 0x04, 'x', 'y', 0x1E, 2, 0}), text("xyxyxyxyxy")));
    check("copia de 4 bytes de offset", decodesTo(bytes({7, 0x04, 'a', 'b', 0x13, 2, 0, 0, 0}), text("abababa")));
    check("copia sobreposta (rle)", decodesTo(bytes({8, 0x00, 'z', 0x0D, 1}), text("zzzzzzzz")));

    Bytes longLit = bytes({100, 0xF0, 99});
    Bytes expect;
    for (int i = 0; i < 100; ++i) {
        longLit.push_back(static_cast<std::uint8_t>('a' + i % 26));
        expect.push_back(static_cast<std::uint8_t>('a' + i % 26));
    }
    check("literal longo (1 byte de tamanho)", decodesTo(longLit, expect));

    Bytes longLit2 = bytes({0xC8, 0x01, 0xF0, 0xC7});
    Bytes expect2;
    for (int i = 0; i < 200; ++i) {
        longLit2.push_back(static_cast<std::uint8_t>(i));
        expect2.push_back(static_cast<std::uint8_t>(i));
    }
    check("literal longo (tamanho em 1 byte, 200)", decodesTo(longLit2, expect2));

    Bytes lit3 = bytes({0xC8, 0x01, 0xF4, 0xC7, 0x00});
    Bytes expect3;
    for (int i = 0; i < 200; ++i) {
        lit3.push_back(static_cast<std::uint8_t>(i));
        expect3.push_back(static_cast<std::uint8_t>(i));
    }
    check("literal longo (tamanho em 2 bytes)", decodesTo(lit3, expect3));

    check("entrada vazia falha", fails({}));
    check("varint sem fim falha", fails(bytes({0x80, 0x80, 0x80})));
    check("varint gigante falha", fails(bytes({0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00})));
    check("tamanho declarado enorme falha sem alocar", fails(bytes({0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00})));
    check("literal truncado falha", fails(bytes({5, 0x10, 'h', 'e'})));
    check("tamanho do literal truncado falha", fails(bytes({100, 0xF0})));
    check("offset zero falha", fails(bytes({4, 0x00, 'a', 0x01, 0})));
    check("offset maior que a saida falha", fails(bytes({8, 0x04, 'a', 'b', 0x0D, 9})));
    check("copia truncada falha", fails(bytes({8, 0x04, 'a', 'b', 0x1E, 2})));
    check("copia de 4 bytes truncada falha", fails(bytes({7, 0x04, 'a', 'b', 0x13, 2, 0})));
    check("copia alem do tamanho declarado falha", fails(bytes({4, 0x04, 'a', 'b', 0x1E, 2, 0})));
    check("literal alem do tamanho declarado falha", fails(bytes({2, 0x10, 'h', 'e', 'l', 'l', 'o'})));
    check("saida menor que a declarada falha", fails(bytes({9, 0x10, 'h', 'e', 'l', 'l', 'o'})));

    std::mt19937 rng(12345);
    int accepted = 0;
    const Bytes valid = bytes({10, 0x04, 'x', 'y', 0x1E, 2, 0});
    for (int i = 0; i < 40000; ++i) {
        Bytes in;
        if (i % 2 == 0) {
            in.resize(rng() % 64);
            for (auto& b : in)
                b = static_cast<std::uint8_t>(rng());
        } else {
            in = valid;
            in[rng() % in.size()] = static_cast<std::uint8_t>(rng());
        }
        Bytes out;
        if (snappyDecompress(in.data(), in.size(), out))
            ++accepted;
        std::uint8_t tiny[4];
        snappyDecompressInto(in.data(), in.size(), tiny, sizeof(tiny));
    }
    check("fuzz de 40000 entradas sem falhas", true, std::to_string(accepted) + " aceitas");
}

struct Rgb {
    int r, g, b, a;
};

Rgb expand565(unsigned c)
{
    const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return {(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255};
}

void decodeColorBlock(const std::uint8_t* p, bool forceFour, Rgb out[16])
{
    const unsigned c0 = p[0] | (p[1] << 8);
    const unsigned c1 = p[2] | (p[3] << 8);
    Rgb pal[4];
    pal[0] = expand565(c0);
    pal[1] = expand565(c1);
    if (c0 > c1 || forceFour) {
        pal[2] = {(2 * pal[0].r + pal[1].r) / 3, (2 * pal[0].g + pal[1].g) / 3, (2 * pal[0].b + pal[1].b) / 3, 255};
        pal[3] = {(pal[0].r + 2 * pal[1].r) / 3, (pal[0].g + 2 * pal[1].g) / 3, (pal[0].b + 2 * pal[1].b) / 3, 255};
    } else {
        pal[2] = {(pal[0].r + pal[1].r) / 2, (pal[0].g + pal[1].g) / 2, (pal[0].b + pal[1].b) / 2, 255};
        pal[3] = {0, 0, 0, 0};
    }
    const std::uint32_t idx = static_cast<std::uint32_t>(p[4]) | (static_cast<std::uint32_t>(p[5]) << 8) |
                              (static_cast<std::uint32_t>(p[6]) << 16) | (static_cast<std::uint32_t>(p[7]) << 24);
    for (int i = 0; i < 16; ++i)
        out[i] = pal[(idx >> (2 * i)) & 3];
}

void decodeAlphaBlock(const std::uint8_t* p, int out[16])
{
    int a[8];
    a[0] = p[0];
    a[1] = p[1];
    if (a[0] > a[1]) {
        for (int i = 2; i < 8; ++i)
            a[i] = ((8 - i) * a[0] + (i - 1) * a[1]) / 7;
    } else {
        for (int i = 2; i < 6; ++i)
            a[i] = ((6 - i) * a[0] + (i - 1) * a[1]) / 5;
        a[6] = 0;
        a[7] = 255;
    }
    std::uint64_t bits = 0;
    for (int i = 0; i < 6; ++i)
        bits |= static_cast<std::uint64_t>(p[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i)
        out[i] = a[(bits >> (3 * i)) & 7];
}

int clamp255(double v)
{
    return v < 0.0 ? 0 : (v > 255.0 ? 255 : static_cast<int>(std::lround(v)));
}

Bytes decodeDxtToRgba(const Frame& f)
{
    const int w = f.width, h = f.height;
    Bytes out(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    const bool dxt1 = f.format == PixelFormat::DXT1;
    const size_t block = dxt1 ? 8 : 16;
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const std::uint8_t* p = f.planes[0].data() + (static_cast<size_t>(by) * static_cast<size_t>(bw) + static_cast<size_t>(bx)) * block;
            Rgb px[16];
            int alpha[16];
            if (dxt1) {
                decodeColorBlock(p, false, px);
                for (int i = 0; i < 16; ++i)
                    alpha[i] = px[i].a;
            } else {
                decodeAlphaBlock(p, alpha);
                decodeColorBlock(p + 8, true, px);
            }
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x >= w || y >= h)
                    continue;
                std::uint8_t* o = out.data() + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
                if (f.format == PixelFormat::DXT5_YCoCg) {
                    const double cx = px[i].r / 255.0, cy = px[i].g / 255.0, cz = px[i].b / 255.0;
                    const double cw = alpha[i] / 255.0;
                    const double scale = cz * (255.0 / 8.0) + 1.0;
                    const double co = (cx - 0.5 * 256.0 / 255.0) / scale;
                    const double cg = (cy - 0.5 * 256.0 / 255.0) / scale;
                    o[0] = static_cast<std::uint8_t>(clamp255((cw + co - cg) * 255.0));
                    o[1] = static_cast<std::uint8_t>(clamp255((cw + cg) * 255.0));
                    o[2] = static_cast<std::uint8_t>(clamp255((cw - co - cg) * 255.0));
                    o[3] = 255;
                } else {
                    o[0] = static_cast<std::uint8_t>(px[i].r);
                    o[1] = static_cast<std::uint8_t>(px[i].g);
                    o[2] = static_cast<std::uint8_t>(px[i].b);
                    o[3] = static_cast<std::uint8_t>(dxt1 ? px[i].a : alpha[i]);
                }
            }
        }
    }
    return out;
}

double psnrFrameRows(const Frame& rgba, const Bytes& other)
{
    const size_t row = static_cast<size_t>(rgba.width) * 4;
    double se = 0.0;
    for (int y = 0; y < rgba.height; ++y) {
        const std::uint8_t* a = rgba.planes[0].data() + static_cast<size_t>(y) * static_cast<size_t>(rgba.strides[0]);
        const std::uint8_t* b = other.data() + static_cast<size_t>(y) * row;
        for (size_t i = 0; i < row; ++i) {
            const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
            se += d * d;
        }
    }
    if (se == 0.0)
        return 99.0;
    return 10.0 * std::log10(255.0 * 255.0 / (se / (static_cast<double>(row) * rgba.height)));
}

struct HapFile {
    std::string name;
    fs::path path;
    PixelFormat expect;
};

std::vector<std::unique_ptr<Frame>> pull(const fs::path& p, bool compressed, int count, VideoInfo* info = nullptr)
{
    std::vector<std::unique_ptr<Frame>> out;
    VideoDecoder dec;
    VideoDecoderOptions opt;
    opt.allowCompressed = compressed;
    if (!dec.open(p, opt))
        return out;
    if (info)
        *info = dec.info();
    for (int i = 0; i < count; ++i) {
        auto f = dec.nextFrame(5000);
        if (!f)
            break;
        out.push_back(std::move(f));
    }
    return out;
}

void testQuality(const std::vector<HapFile>& files)
{
    std::printf("\n[2] DXT em CPU contra avcodec\n");
    for (const HapFile& hf : files) {
        VideoInfo info;
        auto dxt = pull(hf.path, true, 10, &info);
        auto ref = pull(hf.path, false, 10);
        check(hf.name + ": decoder entrega DXT", !dxt.empty() && dxt[0]->format == hf.expect && info.decoderName == "hap-dxt",
              info.decoderName + " " + std::to_string(dxt.size()) + " quadros");
        check(hf.name + ": caminho avcodec entrega RGBA", !ref.empty() && ref[0]->format == PixelFormat::RGBA8);
        if (dxt.empty() || ref.size() != dxt.size())
            continue;
        double worst = 99.0;
        bool ptsOk = true;
        for (size_t i = 0; i < dxt.size(); ++i) {
            const Bytes mine = decodeDxtToRgba(*dxt[i]);
            worst = std::min(worst, psnrFrameRows(*ref[i], mine));
            if (std::fabs(dxt[i]->pts - ref[i]->pts) > 1e-6)
                ptsOk = false;
        }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "pior PSNR %.2f dB em %zu quadros", worst, dxt.size());
        check(hf.name + ": PSNR > 35 dB", worst > 35.0, buf);
        check(hf.name + ": pts iguais ao avcodec", ptsOk);
    }
}

bool readPackets(const fs::path& path, std::vector<Bytes>& packets, int& w, int& h, size_t limit)
{
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.string().c_str(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    const int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    w = fmt->streams[idx]->codecpar->width;
    h = fmt->streams[idx]->codecpar->height;
    AVPacket* pkt = av_packet_alloc();
    while (packets.size() < limit && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == idx)
            packets.emplace_back(pkt->data, pkt->data + pkt->size);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    return !packets.empty();
}

void testTiming(const HapFile& hf)
{
    std::printf("\n[3] Tempo do caminho novo (parse + snappy, sem GPU)\n");
    std::vector<Bytes> packets;
    int w = 0, h = 0;
    if (!readPackets(hf.path, packets, w, h, 100000)) {
        check("ler pacotes para temporizar", false);
        return;
    }
    Bytes tex;
    HapTexture format = HapTexture::DXT1;
    for (const Bytes& p : packets)
        hapParseFrame(p.data(), p.size(), w, h, format, tex);
    const auto t0 = Clock::now();
    bool ok = true;
    int n = 0;
    for (int rep = 0; rep < 3; ++rep) {
        for (const Bytes& p : packets) {
            ok = ok && hapParseFrame(p.data(), p.size(), w, h, format, tex) == HapStatus::Ok;
            ++n;
        }
    }
    const double ms = msSince(t0) / std::max(1, n);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%dx%d, %d quadros, %.3f ms/quadro", w, h, n, ms);
    check("parse + snappy abaixo de 2 ms/quadro", ok && ms < 2.0, buf);

    auto t1 = Clock::now();
    auto ref = pull(hf.path, false, 60);
    const double refMs = msSince(t1) / static_cast<double>(std::max<size_t>(1, ref.size()));
    t1 = Clock::now();
    auto dx = pull(hf.path, true, 60);
    const double dxMs = msSince(t1) / static_cast<double>(std::max<size_t>(1, dx.size()));
    std::printf("    VideoDecoder, 60 quadros: avcodec %.2f ms/quadro, DXT %.2f ms/quadro\n", refMs, dxMs);
}

void testCorruption(const HapFile& hf)
{
    std::printf("\n[4] Quadros corrompidos\n");
    std::vector<Bytes> packets;
    int w = 0, h = 0;
    if (!readPackets(hf.path, packets, w, h, 1)) {
        check("ler pacote", false);
        return;
    }
    const Bytes& first = packets.front();
    HapTexture format = HapTexture::DXT1;
    Bytes tex;
    check("quadro integro aceito", hapParseFrame(first.data(), first.size(), w, h, format, tex) == HapStatus::Ok);
    check("dimensao errada rejeitada", hapParseFrame(first.data(), first.size(), w / 2, h, format, tex) != HapStatus::Ok);
    bool allRejected = true;
    for (size_t cut : {size_t(0), size_t(3), size_t(4), size_t(8), size_t(20), first.size() / 2, first.size() - 1})
        if (hapParseFrame(first.data(), cut, w, h, format, tex) == HapStatus::Ok)
            allRejected = false;
    check("quadro truncado rejeitado", allRejected);

    std::mt19937 rng(777);
    int accepted = 0;
    for (int i = 0; i < 3000; ++i) {
        Bytes m = first;
        const size_t pos = rng() % std::min<size_t>(m.size(), 256);
        m[pos] = static_cast<std::uint8_t>(rng());
        if (hapParseFrame(m.data(), m.size(), w, h, format, tex) == HapStatus::Ok)
            ++accepted;
    }
    check("corrupcao do cabecalho sem falhas", true, std::to_string(accepted) + " aceitos de 3000");

    const Bytes multi = {0x04, 0x00, 0x00, 0x0D, 0x00, 0x00, 0x00, 0x00};
    check("HAP Q Alpha devolve nao suportado",
          hapParseFrame(multi.data(), multi.size(), w, h, format, tex) == HapStatus::Unsupported);
    const Bytes bc7 = {0x04, 0x00, 0x00, 0xAC, 0x00, 0x00, 0x00, 0x00};
    check("BC7 devolve nao suportado", hapParseFrame(bc7.data(), bc7.size(), w, h, format, tex) == HapStatus::Unsupported);
}

bool readTexture(GLuint tex, int w, int h, Bytes& out)
{
    out.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    return glGetError() == GL_NO_ERROR;
}

void testGl(const std::vector<HapFile>& files)
{
    std::printf("\n[5] Upload + shader YCoCg na GPU\n");
    if (!glfwInit()) {
        std::printf("pulado: glfwInit falhou\n");
        return;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "haptest", nullptr, nullptr);
    if (!win) {
        std::printf("pulado: sem contexto GL 3.3\n");
        glfwTerminate();
        return;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress)) {
        std::printf("pulado: gladLoadGL falhou\n");
        glfwDestroyWindow(win);
        glfwTerminate();
        return;
    }
    std::printf("GL: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    check("extensao S3TC disponivel", DxtConverter::supported());
    if (DxtConverter::supported()) {
        for (const HapFile& hf : files) {
            auto dxt = pull(hf.path, true, 5);
            auto ref = pull(hf.path, false, 5);
            if (dxt.empty() || dxt.size() != ref.size()) {
                check(hf.name + ": quadros para a GPU", false);
                continue;
            }
            DxtConverter conv;
            double worst = 99.0;
            bool readOk = true;
            for (size_t i = 0; i < dxt.size(); ++i) {
                conv.upload(*dxt[i]);
                Bytes px;
                readOk = readTexture(conv.texture(), conv.width(), conv.height(), px) && readOk;
                worst = std::min(worst, psnrFrameRows(*ref[i], px));
            }
            char buf[96];
            std::snprintf(buf, sizeof(buf), "pior PSNR %.2f dB", worst);
            check(hf.name + ": GPU contra avcodec, PSNR > 35 dB", readOk && worst > 35.0, buf);
        }

        for (const HapFile& hf : files) {
            VideoSource vs;
            std::string err;
            if (!vs.open(hf.path, &err)) {
                check(hf.name + ": VideoSource abre", false, err);
                continue;
            }
            const GLuint blank = vs.texture();
            const auto t0 = Clock::now();
            while (vs.texture() == blank && msSince(t0) < 3000) {
                vs.update(0.02);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            auto ref = pull(hf.path, false, 40);
            Bytes px;
            bool good = vs.texture() != 0 && vs.width() > 1 && readTexture(vs.texture(), vs.width(), vs.height(), px);
            double p = 0.0;
            if (good && !ref.empty() && ref[0]->width == vs.width()) {
                for (const auto& r : ref)
                    p = std::max(p, psnrFrameRows(*r, px));
                good = p > 35.0;
            } else {
                good = false;
            }
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%dx%d PSNR %.2f dB", vs.width(), vs.height(), p);
            check(hf.name + ": VideoSource entrega textura", good, buf);
            vs.close();
        }
    }
    glfwDestroyWindow(win);
    glfwTerminate();
}

void benchUpload(const fs::path& path)
{
    if (!glfwInit())
        return;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "haptest", nullptr, nullptr);
    if (!win) {
        glfwTerminate();
        return;
    }
    glfwMakeContextCurrent(win);
    gladLoadGL(glfwGetProcAddress);
    auto frames = pull(path, true, 12);
    if (frames.size() < 4 || !frames[0]->isCompressed()) {
        std::printf("upload: sem quadros DXT\n");
    } else {
        DxtConverter conv;
        conv.upload(*frames[0]);
        glFinish();
        double call = 0.0, total = 0.0;
        int n = 0;
        for (size_t i = 1; i < frames.size(); ++i) {
            const auto t0 = Clock::now();
            conv.upload(*frames[i]);
            const double c = msSince(t0);
            glFinish();
            call += c;
            total += msSince(t0);
            ++n;
        }
        std::printf("upload + shader: chamada %.3f ms, ate a GPU terminar %.3f ms\n", call / n, total / n);
    }
    glfwDestroyWindow(win);
    glfwTerminate();
}

int benchFile(const fs::path& path, int count)
{
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.string().c_str(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(fmt, nullptr) < 0) {
        std::printf("nao abriu %s\n", path.string().c_str());
        return 2;
    }
    const int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int w = fmt->streams[idx]->codecpar->width, h = fmt->streams[idx]->codecpar->height;
    std::vector<Bytes> packets;
    AVPacket* pkt = av_packet_alloc();
    double readMs = 0.0;
    while (static_cast<int>(packets.size()) < count) {
        const auto t0 = Clock::now();
        const int r = av_read_frame(fmt, pkt);
        readMs += msSince(t0);
        if (r < 0)
            break;
        if (pkt->stream_index == idx)
            packets.emplace_back(pkt->data, pkt->data + pkt->size);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    const int n = static_cast<int>(packets.size());
    if (n == 0)
        return 2;
    double bytesAvg = 0;
    for (const Bytes& p : packets)
        bytesAvg += static_cast<double>(p.size());
    bytesAvg /= n;
    std::printf("%dx%d, %d quadros, %.2f MB/quadro\n", w, h, n, bytesAvg / 1048576.0);
    std::printf("av_read_frame:            %.3f ms/quadro\n", readMs / n);

    Bytes tex;
    HapTexture format = HapTexture::DXT1;
    hapParseFrame(packets[0].data(), packets[0].size(), w, h, format, tex);
    {
        const auto t0 = Clock::now();
        for (int rep = 0; rep < 3; ++rep)
            for (const Bytes& p : packets)
                hapParseFrame(p.data(), p.size(), w, h, format, tex);
        std::printf("parse completo (buffer reaproveitado): %.3f ms/quadro\n", msSince(t0) / (3.0 * n));
    }
    {
        const auto t0 = Clock::now();
        for (int rep = 0; rep < 3; ++rep) {
            for (const Bytes& p : packets) {
                Bytes fresh;
                hapParseFrame(p.data(), p.size(), w, h, format, fresh);
            }
        }
        std::printf("parse completo (buffer novo por quadro): %.3f ms/quadro\n", msSince(t0) / (3.0 * n));
    }
    {
        Bytes dst(tex.size());
        const auto t0 = Clock::now();
        for (int rep = 0; rep < 3; ++rep)
            for (int i = 0; i < n; ++i)
                std::memcpy(dst.data(), tex.data(), tex.size());
        std::printf("memcpy de %.1f MB:         %.3f ms\n", static_cast<double>(tex.size()) / 1048576.0, msSince(t0) / (3.0 * n));
    }
    {
        const auto t0 = Clock::now();
        for (int rep = 0; rep < 3; ++rep) {
            for (int i = 0; i < n; ++i) {
                Bytes fresh(tex.size());
                fresh[static_cast<size_t>(i)] = 1;
            }
        }
        std::printf("alocar + zerar %.1f MB:    %.3f ms\n", static_cast<double>(tex.size()) / 1048576.0, msSince(t0) / (3.0 * n));
    }
    return 0;
}

bool makeHap(const fs::path& src, const fs::path& dst, bool light = false)
{
    HapJob job;
    job.source = src;
    job.target = dst;
    job.light = light;
    std::atomic<bool> cancel{false};
    const HapResult r = transcodeToHap(job, [](double) {}, cancel);
    if (!r.ok)
        std::printf("transcodeToHap falhou (%s): %s\n", src.string().c_str(), r.error.c_str());
    return r.ok && fs::exists(dst);
}

int convertFile(const fs::path& src, const fs::path& dst, bool light)
{
    const auto t0 = Clock::now();
    const bool ok = makeHap(src, dst, light);
    std::printf("conversao %s: %s em %.1f s\n", light ? "hap (DXT1)" : "hap_q", ok ? "ok" : "falhou", msSince(t0) / 1000.0);
    return ok ? 0 : 1;
}

int qualityFile(const fs::path& src, const fs::path& hap)
{
    const int wanted[] = {0, 15, 40, 80};
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, src.string().c_str(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(fmt, nullptr) < 0)
        return 2;
    const int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const AVCodec* codec = avcodec_find_decoder(fmt->streams[idx]->codecpar->codec_id);
    AVCodecContext* cc = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(cc, fmt->streams[idx]->codecpar);
    cc->thread_count = 0;
    avcodec_open2(cc, codec, nullptr);
    std::vector<Bytes> refs;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    SwsContext* sws = nullptr;
    int decoded = 0;
    const int last = wanted[sizeof(wanted) / sizeof(wanted[0]) - 1];
    bool done = false;
    while (!done && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == idx && avcodec_send_packet(cc, pkt) >= 0) {
            while (avcodec_receive_frame(cc, fr) >= 0) {
                for (int wn : wanted) {
                    if (wn == decoded) {
                        sws = sws_getCachedContext(sws, fr->width, fr->height, static_cast<AVPixelFormat>(fr->format),
                                                   fr->width, fr->height, AV_PIX_FMT_RGBA, SWS_BICUBIC, nullptr, nullptr,
                                                   nullptr);
                        Bytes rgba(static_cast<size_t>(fr->width) * static_cast<size_t>(fr->height) * 4);
                        uint8_t* dst[4] = {rgba.data(), nullptr, nullptr, nullptr};
                        int dstStride[4] = {fr->width * 4, 0, 0, 0};
                        sws_scale(sws, fr->data, fr->linesize, 0, fr->height, dst, dstStride);
                        refs.push_back(std::move(rgba));
                    }
                }
                ++decoded;
                if (decoded > last)
                    done = true;
            }
        }
        av_packet_unref(pkt);
    }
    sws_freeContext(sws);
    av_frame_free(&fr);
    av_packet_free(&pkt);
    avcodec_free_context(&cc);
    avformat_close_input(&fmt);

    auto dxt = pull(hap, true, last + 1);
    if (dxt.size() <= static_cast<size_t>(last) || refs.size() != sizeof(wanted) / sizeof(wanted[0])) {
        std::printf("quadros insuficientes\n");
        return 2;
    }
    double sum = 0.0;
    for (size_t i = 0; i < refs.size(); ++i) {
        const Frame& f = *dxt[static_cast<size_t>(wanted[i])];
        const Bytes mine = decodeDxtToRgba(f);
        double se = 0.0;
        for (size_t k = 0; k < mine.size(); ++k) {
            if ((k & 3) == 3)
                continue;
            const double d = static_cast<double>(mine[k]) - static_cast<double>(refs[i][k]);
            se += d * d;
        }
        const double p = 10.0 * std::log10(255.0 * 255.0 / (se / (static_cast<double>(mine.size()) * 0.75)));
        std::printf("quadro %d: formato %s, PSNR RGB contra o original %.2f dB\n", wanted[i],
                    f.format == PixelFormat::DXT1 ? "DXT1" : "YCoCg DXT5", p);
        sum += p;
    }
    std::printf("PSNR medio %.2f dB\n", sum / static_cast<double>(refs.size()));
    return 0;
}

}

int main(int argc, char** argv)
{
    if (argc > 2 && std::string(argv[1]) == "--bench") {
        const int rc = benchFile(argv[2], argc > 3 ? std::atoi(argv[3]) : 60);
        benchUpload(argv[2]);
        return rc;
    }
    if (argc > 3 && std::string(argv[1]) == "--convert")
        return convertFile(argv[2], argv[3], argc > 4);
    if (argc > 3 && std::string(argv[1]) == "--quality")
        return qualityFile(argv[2], argv[3]);
    const fs::path media = argc > 1 ? fs::path(argv[1]) : fs::path("testmedia");
    const fs::path tmp = fs::temp_directory_path() / "bslstage-haptest";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp);

    testSnappy();

    std::printf("\nGerando arquivos HAP de teste em %s\n", tmp.string().c_str());
    std::vector<HapFile> files;
    const auto t0 = Clock::now();
    if (makeHap(media / "h264_1080p.mp4", tmp / "q1080.mov"))
        files.push_back({"hap_q 1080p", tmp / "q1080.mov", PixelFormat::DXT5_YCoCg});
    if (makeHap(media / "vp9_alpha.webm", tmp / "alpha.mov"))
        files.push_back({"hap_alpha 640x360", tmp / "alpha.mov", PixelFormat::DXT5});
    std::printf("transcodificacao: %.0f ms\n", msSince(t0));
    check("arquivos HAP gerados", files.size() == 2);

    if (!files.empty()) {
        testQuality(files);
        testTiming(files.front());
        testCorruption(files.front());
        testGl(files);
    }

    fs::remove_all(tmp, ec);
    std::printf("\n%d falha(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
