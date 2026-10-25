#include "media/ImageDecoder.h"

#include "media/FrameConverter.h"
#include "media/MediaFormats.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include <stb_image.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/rational.h>
}

namespace bsl::media {

namespace {

constexpr size_t kMaxAnimationBytes = 768ull * 1024 * 1024;
constexpr double kDefaultDelay = 0.1;

bool readFile(const std::filesystem::path& path, std::vector<unsigned char>& data)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return false;
    const std::streamsize size = f.tellg();
    if (size <= 0)
        return false;
    data.resize(static_cast<size_t>(size));
    f.seekg(0);
    return static_cast<bool>(f.read(reinterpret_cast<char*>(data.data()), size));
}

bool scanAlpha(const Frame& f)
{
    const auto& p = f.planes[0];
    for (size_t i = 3; i < p.size(); i += 4) {
        if (p[i] != 255)
            return true;
    }
    return false;
}

void finish(DecodedImage& out, const char* decoder)
{
    out.decoder = decoder;
    out.hasAlpha = false;
    for (const Frame& f : out.frames) {
        if (scanAlpha(f)) {
            out.hasAlpha = true;
            break;
        }
    }
}

bool decodeStb(const std::filesystem::path& path, DecodedImage& out, std::string& error)
{
    std::vector<unsigned char> file;
    if (!readFile(path, file)) {
        error = "nao foi possivel ler o arquivo";
        return false;
    }
    const int len = static_cast<int>(std::min<size_t>(file.size(), 0x7fffffff));

    if (lowerExtension(path) == "gif") {
        int* delays = nullptr;
        int w = 0, h = 0, z = 0, comp = 0;
        unsigned char* data = stbi_load_gif_from_memory(file.data(), len, &delays, &w, &h, &z, &comp, 4);
        if (data && z > 0) {
            const size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
            out.width = w;
            out.height = h;
            size_t total = 0;
            for (int i = 0; i < z; ++i) {
                total += frameBytes;
                if (i > 0 && total > kMaxAnimationBytes)
                    break;
                Frame f;
                f.allocate(w, h, PixelFormat::RGBA8);
                std::memcpy(f.planes[0].data(), data + static_cast<size_t>(i) * frameBytes, frameBytes);
                f.colorSpace = ColorSpace::BT709;
                f.range = ColorRange::Full;
                double d = (delays && z > 1) ? delays[i] / 1000.0 : 0.0;
                if (z > 1 && d <= 0.011)
                    d = kDefaultDelay; // convencao dos navegadores para delays 0/1
                f.pts = out.totalDuration();
                out.frames.push_back(std::move(f));
                out.delays.push_back(d);
            }
            stbi_image_free(data);
            if (delays)
                stbi_image_free(delays);
            finish(out, "stb");
            return true;
        }
        if (data)
            stbi_image_free(data);
        if (delays)
            stbi_image_free(delays);
        error = stbi_failure_reason() ? stbi_failure_reason() : "gif invalido";
        return false;
    }

    int w = 0, h = 0, comp = 0;
    unsigned char* data = stbi_load_from_memory(file.data(), len, &w, &h, &comp, 4);
    if (!data) {
        error = stbi_failure_reason() ? stbi_failure_reason() : "stb falhou";
        return false;
    }
    Frame f;
    f.allocate(w, h, PixelFormat::RGBA8);
    std::memcpy(f.planes[0].data(), data, f.planes[0].size());
    stbi_image_free(data);
    f.colorSpace = ColorSpace::BT709;
    f.range = ColorRange::Full;
    out.width = w;
    out.height = h;
    out.frames.push_back(std::move(f));
    out.delays.push_back(0.0);
    finish(out, "stb");
    return true;
}

bool decodeFfmpeg(const std::filesystem::path& path, DecodedImage& out, std::string& error)
{
    initFfmpegLogging();
    const auto u8 = path.u8string();
    const std::string p(u8.begin(), u8.end());

    AVFormatContext* fmt = nullptr;
    AVCodecContext* cc = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* fr = nullptr;
    bool ok = false;

    auto cleanup = [&] {
        av_frame_free(&fr);
        av_packet_free(&pkt);
        avcodec_free_context(&cc);
        avformat_close_input(&fmt);
    };

    if (avformat_open_input(&fmt, p.c_str(), nullptr, nullptr) < 0) {
        error = "ffmpeg: nao abriu o arquivo";
        cleanup();
        return false;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        error = "ffmpeg: sem informacao de stream";
        cleanup();
        return false;
    }
    const int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0) {
        error = "ffmpeg: sem stream de imagem";
        cleanup();
        return false;
    }
    AVStream* st = fmt->streams[idx];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        error = "ffmpeg: decoder ausente";
        cleanup();
        return false;
    }
    cc = avcodec_alloc_context3(codec);
    if (!cc || avcodec_parameters_to_context(cc, st->codecpar) < 0 || avcodec_open2(cc, codec, nullptr) < 0) {
        error = "ffmpeg: falha ao abrir o decoder";
        cleanup();
        return false;
    }
    cc->pkt_timebase = st->time_base;
    pkt = av_packet_alloc();
    fr = av_frame_alloc();

    FrameConverter conv;
    const double tb = av_q2d(st->time_base);
    std::vector<double> pts;
    std::vector<double> durs;
    size_t total = 0;
    bool truncated = false;

    auto drain = [&]() {
        while (true) {
            const int r = avcodec_receive_frame(cc, fr);
            if (r != 0)
                break;
            if (!truncated) {
                Frame f;
                if (conv.convert(fr, 0, 0, true, f)) {
                    total += f.planes[0].size();
                    if (!out.frames.empty() && total > kMaxAnimationBytes) {
                        truncated = true;
                    } else {
                        if (out.frames.empty()) {
                            out.width = f.width;
                            out.height = f.height;
                        }
                        if (f.width == out.width && f.height == out.height) {
                            const double t = fr->best_effort_timestamp != AV_NOPTS_VALUE
                                                 ? static_cast<double>(fr->best_effort_timestamp) * tb
                                                 : 0.0;
                            pts.push_back(t);
                            durs.push_back(fr->duration > 0 ? static_cast<double>(fr->duration) * tb : 0.0);
                            f.pts = t;
                            out.frames.push_back(std::move(f));
                        }
                    }
                }
            }
            av_frame_unref(fr);
        }
    };

    while (!truncated && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == idx && avcodec_send_packet(cc, pkt) >= 0)
            drain();
        av_packet_unref(pkt);
    }
    if (!truncated) {
        avcodec_send_packet(cc, nullptr);
        drain();
    }

    if (!out.frames.empty()) {
        const size_t n = out.frames.size();
        out.delays.assign(n, 0.0);
        if (n > 1) {
            for (size_t i = 0; i < n; ++i) {
                double d = durs[i];
                if (d <= 0.0 && i + 1 < n)
                    d = pts[i + 1] - pts[i];
                if (d <= 0.0 && i > 0)
                    d = out.delays[i - 1];
                if (d <= 0.011)
                    d = kDefaultDelay;
                out.delays[i] = d;
            }
            // pts em segundos acumulados, independente do que o demuxer informou
            double acc = 0.0;
            for (size_t i = 0; i < n; ++i) {
                out.frames[i].pts = acc;
                acc += out.delays[i];
            }
        } else {
            out.frames[0].pts = 0.0;
        }
        finish(out, "ffmpeg");
        ok = true;
    } else {
        error = "ffmpeg: nenhum frame decodificado";
    }
    cleanup();
    return ok;
}

} // namespace

bool decodeImage(const std::filesystem::path& path, DecodedImage& out, std::string* error)
{
    out = DecodedImage{};
    std::string err;

    const std::string ext = lowerExtension(path);
    const bool ffmpegFirst = (ext == "webp" || ext == "tif" || ext == "tiff");

    bool ok = false;
    if (ffmpegFirst) {
        ok = decodeFfmpeg(path, out, err);
        if (!ok) {
            out = DecodedImage{};
            std::string err2;
            ok = decodeStb(path, out, err2);
            if (!ok)
                err += " | stb: " + err2;
        }
    } else {
        ok = decodeStb(path, out, err);
        if (!ok) {
            out = DecodedImage{};
            std::string err2;
            ok = decodeFfmpeg(path, out, err2);
            if (!ok)
                err += " | " + err2;
        }
    }
    if (!ok && error)
        *error = err;
    return ok;
}

} // namespace bsl::media
