#include "media/ImageDecoder.h"
#include "media/MediaFormats.h"
#include "media/Thumbnailer.h"
#include "media/VideoDecoder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace bsl::media;
using Clock = std::chrono::steady_clock;

namespace {

struct Result {
    std::string name;
    std::string kind;
    bool ok = false;
    double msPerFrame = 0.0;
    bool alpha = false;
    std::string detail;
};

double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

const char* fmtName(PixelFormat f)
{
    switch (f) {
    case PixelFormat::RGBA8: return "RGBA";
    case PixelFormat::YUV420P: return "YUV420P";
    case PixelFormat::YUVA420P: return "YUVA420P";
    }
    return "?";
}

bool frameHasAlpha(const Frame& f)
{
    if (f.hasAlphaPlane())
        return true;
    if (f.format == PixelFormat::RGBA8) {
        const auto& p = f.planes[0];
        for (size_t i = 3; i < p.size(); i += 4)
            if (p[i] != 255)
                return true;
    }
    return false;
}

Result testVideo(const fs::path& path, bool eco)
{
    Result r;
    r.kind = "video";
    VideoDecoder dec;
    VideoDecoderOptions opt;
    if (eco) {
        opt.lowPower = true;
        opt.targetWidth = 960;
        opt.targetHeight = 540;
    }
    if (!dec.open(path, opt)) {
        r.detail = "open falhou: " + dec.error();
        return r;
    }
    const VideoInfo& vi = dec.info();
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s/%s %dx%d %.2ffps %.2fs %s", vi.codec.c_str(), vi.decoderName.c_str(),
                  vi.width, vi.height, vi.fps, vi.duration, vi.pixelFormat.c_str());
    r.detail = buf;
    r.alpha = vi.hasAlpha;

    int got = 0;
    bool anyAlphaFrame = false;
    std::string outFmt;
    const auto t0 = Clock::now();
    for (int i = 0; i < 60; ++i) {
        auto f = dec.nextFrame(5000);
        if (!f)
            break;
        if (f->empty()) {
            r.detail += " | frame vazio";
            return r;
        }
        if (got == 0)
            outFmt = std::string(fmtName(f->format)) + " " + std::to_string(f->width) + "x" + std::to_string(f->height);
        anyAlphaFrame = anyAlphaFrame || frameHasAlpha(*f);
        ++got;
    }
    const double total = msSince(t0);
    if (got == 0) {
        r.detail += " | nenhum frame";
        return r;
    }
    r.msPerFrame = total / got;
    r.detail += " -> " + outFmt + " x" + std::to_string(got);
    if (vi.hasAlpha && !anyAlphaFrame && outFmt.rfind("YUVA", 0) != 0 && outFmt.rfind("RGBA", 0) != 0) {
        r.detail += " | alpha declarado mas frames sem alpha";
        return r;
    }

    if (vi.duration > 1.0) {
        const double target = vi.duration * 0.5;
        dec.seek(target);
        std::unique_ptr<Frame> f;
        const auto ts = Clock::now();
        while (!f && msSince(ts) < 3000) {
            f = dec.tryGetFrame(target);
            if (!f)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!f || f->pts < target - 0.1 || f->pts > target + 0.5) {
            r.detail += " | seek falhou (pts=" + (f ? std::to_string(f->pts) : std::string("nenhum")) + ")";
            return r;
        }
    }

    if (vi.duration > 1.0 && vi.fps >= 10.0) {
        dec.setLoop(true);
        const double start = std::max(0.0, vi.duration - 0.4);
        dec.seek(start);
        int before = 0, after = 0;
        bool wrapped = false;
        double lastPos = start;
        const auto tl = Clock::now();
        while (msSince(tl) < 1500) {
            const double pos = std::fmod(start + msSince(tl) / 1000.0, vi.duration);
            if (pos < lastPos)
                wrapped = true;
            lastPos = pos;
            auto f = dec.tryGetFrame(pos);
            if (f) {
                if (!wrapped)
                    ++before;
                else if (f->pts <= pos + 0.05)
                    ++after;
                else {
                    r.detail += " | loop: frame com pts=" + std::to_string(f->pts) + " apos a volta";
                    return r;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        if (!wrapped || after < 5) {
            r.detail += " | loop falhou (antes=" + std::to_string(before) + " depois=" + std::to_string(after) + ")";
            return r;
        }
        r.detail += " | loop ok";
    }

    r.ok = true;
    return r;
}

Result testImage(const fs::path& path)
{
    Result r;
    r.kind = "image";
    DecodedImage img;
    std::string err;
    const auto t0 = Clock::now();
    if (!decodeImage(path, img, &err)) {
        r.detail = "decode falhou: " + err;
        return r;
    }
    const double total = msSince(t0);
    r.msPerFrame = total / static_cast<double>(img.frames.size());
    r.alpha = img.hasAlpha;
    char buf[200];
    std::snprintf(buf, sizeof(buf), "%s %dx%d frames=%zu dur=%.2fs", img.decoder.c_str(), img.width, img.height,
                  img.frames.size(), img.totalDuration());
    r.detail = buf;
    r.ok = !img.frames.empty() && img.delays.size() == img.frames.size() && !img.frames[0].empty();
    return r;
}

}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s <pasta> [--eco]\n", argv[0]);
        return 2;
    }
    const fs::path dir = fs::path(argv[1]);
    const bool eco = argc > 2 && std::string(argv[2]) == "--eco";
    if (!fs::is_directory(dir)) {
        std::fprintf(stderr, "pasta invalida: %s\n", argv[1]);
        return 2;
    }

    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file())
            files.push_back(e.path());
    std::sort(files.begin(), files.end());

    int failures = 0;
    int tested = 0;
    std::printf("%-28s %-6s %-4s %10s %-5s  %s\n", "arquivo", "tipo", "ok", "ms/frame", "alpha", "detalhe");
    for (const fs::path& p : files) {
        const MediaKind kind = classify(p);
        if (kind != MediaKind::Video && kind != MediaKind::Image)
            continue;
        Result r = (kind == MediaKind::Video) ? testVideo(p, eco) : testImage(p);
        const auto u8 = p.filename().u8string();
        r.name.assign(u8.begin(), u8.end());
        ++tested;

        if (r.ok && r.name.find("alpha") != std::string::npos && !r.alpha) {
            r.ok = false;
            r.detail += " | esperava alpha";
        }
        if (!r.ok)
            ++failures;
        std::printf("%-28s %-6s %-4s %10.2f %-5s  %s\n", r.name.c_str(), r.kind.c_str(), r.ok ? "OK" : "FAIL",
                    r.msPerFrame, r.alpha ? "sim" : "nao", r.detail.c_str());
        std::fflush(stdout);
    }
    {
        Thumbnailer thumbs;
        int requested = 0;
        for (const fs::path& p : files) {
            if (classify(p) == MediaKind::Video || classify(p) == MediaKind::Image) {
                thumbs.request(p);
                ++requested;
            }
        }
        int received = 0;
        int thumbBad = 0;
        const auto t0 = Clock::now();
        while (received < requested && msSince(t0) < 20000) {
            if (auto t = thumbs.poll()) {
                ++received;
                const bool ok = t->ok && t->frame.width == 160 && t->frame.height == 90 &&
                                t->frame.format == PixelFormat::RGBA8;
                if (!ok) {
                    ++thumbBad;
                    std::printf("miniatura FAIL: %s\n", t->path.filename().string().c_str());
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        std::printf("\nminiaturas 160x90: %d/%d recebidas, %d invalida(s), %.0f ms total\n", received, requested,
                    thumbBad, msSince(t0));
        if (received != requested || thumbBad)
            ++failures;
    }

    std::printf("\n%d arquivo(s), %d falha(s)\n", tested, failures);
    return (failures == 0 && tested > 0) ? 0 : 1;
}
