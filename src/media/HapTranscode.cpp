#include "media/HapTranscode.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}

namespace bsl::media {

namespace {

std::string errString(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

std::string toUtf8(const std::filesystem::path& p)
{
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

int fitDown(int v)
{
    return std::max(4, v / 4 * 4);
}

void targetSize(int srcW, int srcH, int maxW, int maxH, int& outW, int& outH)
{
    double scale = 1.0;
    if (maxW > 0)
        scale = std::min(scale, static_cast<double>(maxW) / srcW);
    if (maxH > 0)
        scale = std::min(scale, static_cast<double>(maxH) / srcH);
    outW = fitDown(static_cast<int>(std::floor(srcW * scale)));
    outH = fitDown(static_cast<int>(std::floor(srcH * scale)));
}

struct Session {
    AVFormatContext* in = nullptr;
    AVFormatContext* out = nullptr;
    AVCodecContext* dec = nullptr;
    AVCodecContext* enc = nullptr;
    SwsContext* sws = nullptr;
    AVFrame* decFrame = nullptr;
    AVFrame* encFrame = nullptr;
    AVPacket* inPkt = nullptr;
    AVPacket* outPkt = nullptr;
    bool ioOpened = false;

    ~Session()
    {
        if (sws)
            sws_freeContext(sws);
        av_frame_free(&decFrame);
        av_frame_free(&encFrame);
        av_packet_free(&inPkt);
        av_packet_free(&outPkt);
        avcodec_free_context(&dec);
        avcodec_free_context(&enc);
        if (in)
            avformat_close_input(&in);
        if (out) {
            if (ioOpened)
                avio_closep(&out->pb);
            avformat_free_context(out);
        }
    }
};

}

HapResult transcodeToHap(const HapJob& job, const std::function<void(double)>& progress,
                         const std::atomic<bool>& cancel)
{
    HapResult res;
    Session s;
    namespace fs = std::filesystem;

    fs::path part = job.target;
    part += ".part";
    const std::string partPath = toUtf8(part);
    bool partCreated = false;

    auto fail = [&](const std::string& msg) {
        res.ok = false;
        res.error = msg;
        return res;
    };

    auto finishFail = [&](const std::string& msg) {
        if (s.out && s.ioOpened)
            avio_closep(&s.out->pb);
        s.ioOpened = false;
        if (partCreated) {
            std::error_code ec;
            fs::remove(part, ec);
        }
        return fail(msg);
    };

    const std::string srcPath = toUtf8(job.source);
    int r = avformat_open_input(&s.in, srcPath.c_str(), nullptr, nullptr);
    if (r < 0)
        return fail("Nao foi possivel abrir o video de origem: " + errString(r));
    r = avformat_find_stream_info(s.in, nullptr);
    if (r < 0)
        return fail("Nao foi possivel ler as informacoes do video: " + errString(r));
    const int streamIdx = av_find_best_stream(s.in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIdx < 0)
        return fail("O arquivo nao possui stream de video.");
    AVStream* ist = s.in->streams[streamIdx];
    AVCodecParameters* par = ist->codecpar;

    const AVCodec* decoder = avcodec_find_decoder(par->codec_id);
    bool alphaFlag = false;
    if (par->codec_id == AV_CODEC_ID_VP8 || par->codec_id == AV_CODEC_ID_VP9) {
        const AVDictionaryEntry* e = av_dict_get(ist->metadata, "alpha_mode", nullptr, 0);
        if (e && e->value && std::string(e->value) == "1") {
            const AVCodec* lib =
                avcodec_find_decoder_by_name(par->codec_id == AV_CODEC_ID_VP9 ? "libvpx-vp9" : "libvpx");
            if (lib) {
                decoder = lib;
                alphaFlag = true;
            }
        }
    }
    if (!decoder)
        return fail("Decodificador nao encontrado para o codec do video.");

    s.dec = avcodec_alloc_context3(decoder);
    if (!s.dec || avcodec_parameters_to_context(s.dec, par) < 0)
        return fail("Falha ao criar o contexto do decodificador.");
    s.dec->pkt_timebase = ist->time_base;
    s.dec->thread_count = 0;
    s.dec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    r = avcodec_open2(s.dec, decoder, nullptr);
    if (r < 0)
        return fail("Falha ao abrir o decodificador: " + errString(r));

    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(s.dec->pix_fmt);
    const bool alpha = alphaFlag || (desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA));

    const int srcW = s.dec->width;
    const int srcH = s.dec->height;
    if (srcW <= 0 || srcH <= 0)
        return fail("Dimensoes do video invalidas.");
    int outW = 0;
    int outH = 0;
    targetSize(srcW, srcH, job.maxWidth, job.maxHeight, outW, outH);

    const AVCodec* encoder = avcodec_find_encoder_by_name("hap");
    if (!encoder)
        return fail("Codificador HAP nao encontrado nesta versao do FFmpeg.");

    s.enc = avcodec_alloc_context3(encoder);
    if (!s.enc)
        return fail("Falha ao criar o contexto do codificador.");

    AVPixelFormat encFmt = AV_PIX_FMT_RGBA;
    {
        const void* cfg = nullptr;
        int n = 0;
        if (avcodec_get_supported_config(s.enc, encoder, AV_CODEC_CONFIG_PIX_FORMAT, 0, &cfg, &n) >= 0 && cfg &&
            n > 0) {
            const auto* list = static_cast<const AVPixelFormat*>(cfg);
            bool hasRgba = false;
            for (int i = 0; i < n; ++i)
                if (list[i] == AV_PIX_FMT_RGBA)
                    hasRgba = true;
            if (!hasRgba)
                encFmt = list[0];
        }
    }

    AVRational fr = ist->avg_frame_rate;
    if (fr.num <= 0 || fr.den <= 0)
        fr = av_guess_frame_rate(s.in, ist, nullptr);
    if (fr.num <= 0 || fr.den <= 0)
        fr = {30, 1};

    s.enc->width = outW;
    s.enc->height = outH;
    s.enc->pix_fmt = encFmt;
    s.enc->time_base = ist->time_base;
    s.enc->framerate = fr;
    s.enc->sample_aspect_ratio = par->sample_aspect_ratio;
    s.enc->thread_count = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) / 2);

    r = avformat_alloc_output_context2(&s.out, nullptr, "mov", partPath.c_str());
    if (r < 0 || !s.out)
        return fail("Falha ao criar o container de saida: " + errString(r));
    if (s.out->oformat->flags & AVFMT_GLOBALHEADER)
        s.enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary* encOpts = nullptr;
    av_dict_set(&encOpts, "format", alpha ? "hap_alpha" : (job.light ? "hap" : "hap_q"), 0);
    av_dict_set(&encOpts, "compressor", "snappy", 0);
    av_dict_set(&encOpts, "chunks", "8", 0);
    av_dict_set_int(&encOpts, "thread_count", s.enc->thread_count, 0);
    r = avcodec_open2(s.enc, encoder, &encOpts);
    av_dict_free(&encOpts);
    if (r < 0)
        return fail("Falha ao abrir o codificador HAP: " + errString(r));

    AVStream* ost = avformat_new_stream(s.out, nullptr);
    if (!ost)
        return fail("Falha ao criar o stream de saida.");
    r = avcodec_parameters_from_context(ost->codecpar, s.enc);
    if (r < 0)
        return fail("Falha ao configurar o stream de saida: " + errString(r));
    ost->time_base = s.enc->time_base;
    ost->avg_frame_rate = fr;
    ost->r_frame_rate = fr;

    r = avio_open(&s.out->pb, partPath.c_str(), AVIO_FLAG_WRITE);
    if (r < 0)
        return fail("Nao foi possivel criar o arquivo de saida: " + errString(r));
    s.ioOpened = true;
    partCreated = true;

    r = avformat_write_header(s.out, nullptr);
    if (r < 0)
        return finishFail("Falha ao gravar o cabecalho do arquivo: " + errString(r));

    s.decFrame = av_frame_alloc();
    s.encFrame = av_frame_alloc();
    s.inPkt = av_packet_alloc();
    s.outPkt = av_packet_alloc();
    if (!s.decFrame || !s.encFrame || !s.inPkt || !s.outPkt)
        return finishFail("Sem memoria.");
    s.encFrame->format = encFmt;
    s.encFrame->width = outW;
    s.encFrame->height = outH;
    r = av_frame_get_buffer(s.encFrame, 0);
    if (r < 0)
        return finishFail("Sem memoria para o quadro de saida: " + errString(r));

    double durationSec = 0.0;
    if (ist->duration > 0 && ist->duration != AV_NOPTS_VALUE)
        durationSec = ist->duration * av_q2d(ist->time_base);
    else if (s.in->duration > 0)
        durationSec = s.in->duration / static_cast<double>(AV_TIME_BASE);
    const int64_t startPts = ist->start_time != AV_NOPTS_VALUE ? ist->start_time : 0;

    std::int64_t frames = 0;
    int64_t lastPts = AV_NOPTS_VALUE;
    int64_t frameDur = av_rescale_q(1, av_inv_q(fr), ist->time_base);
    if (frameDur < 1)
        frameDur = 1;
    double lastReport = -1.0;

    auto drain = [&]() -> int {
        for (;;) {
            const int e = avcodec_receive_packet(s.enc, s.outPkt);
            if (e == AVERROR(EAGAIN) || e == AVERROR_EOF)
                return 0;
            if (e < 0)
                return e;
            s.outPkt->stream_index = ost->index;
            av_packet_rescale_ts(s.outPkt, s.enc->time_base, ost->time_base);
            const int w = av_interleaved_write_frame(s.out, s.outPkt);
            if (w < 0)
                return w;
        }
    };

    auto encodeFrame = [&](AVFrame* f) -> int {
        int64_t pts = f->best_effort_timestamp;
        if (pts == AV_NOPTS_VALUE)
            pts = f->pts;
        if (pts == AV_NOPTS_VALUE)
            pts = lastPts == AV_NOPTS_VALUE ? startPts : lastPts + frameDur;
        if (lastPts != AV_NOPTS_VALUE && pts <= lastPts)
            pts = lastPts + 1;
        lastPts = pts;

        s.sws = sws_getCachedContext(s.sws, f->width, f->height, static_cast<AVPixelFormat>(f->format), outW, outH,
                                     encFmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
        if (!s.sws)
            return AVERROR(EINVAL);
        int e = av_frame_make_writable(s.encFrame);
        if (e < 0)
            return e;
        sws_scale(s.sws, f->data, f->linesize, 0, f->height, s.encFrame->data, s.encFrame->linesize);
        s.encFrame->pts = pts;
        e = avcodec_send_frame(s.enc, s.encFrame);
        if (e < 0)
            return e;
        ++frames;
        if (progress && durationSec > 0.0) {
            const double t = (pts - startPts) * av_q2d(ist->time_base);
            const double p = std::clamp(t / durationSec, 0.0, 1.0);
            if (lastReport < 0.0 || p - lastReport >= 0.001) {
                lastReport = p;
                progress(p);
            }
        }
        return drain();
    };

    auto receiveAll = [&]() -> int {
        for (;;) {
            const int e = avcodec_receive_frame(s.dec, s.decFrame);
            if (e == AVERROR(EAGAIN) || e == AVERROR_EOF)
                return 0;
            if (e < 0)
                return e;
            const int c = encodeFrame(s.decFrame);
            av_frame_unref(s.decFrame);
            if (c < 0)
                return c;
        }
    };

    for (;;) {
        if (cancel.load())
            return finishFail("Conversao cancelada.");
        r = av_read_frame(s.in, s.inPkt);
        if (r < 0)
            break;
        if (s.inPkt->stream_index != streamIdx) {
            av_packet_unref(s.inPkt);
            continue;
        }
        r = avcodec_send_packet(s.dec, s.inPkt);
        av_packet_unref(s.inPkt);
        if (r < 0 && r != AVERROR(EAGAIN))
            return finishFail("Falha ao decodificar o video: " + errString(r));
        r = receiveAll();
        if (r < 0)
            return finishFail("Falha ao converter para HAP: " + errString(r));
    }

    avcodec_send_packet(s.dec, nullptr);
    r = receiveAll();
    if (r < 0)
        return finishFail("Falha ao converter para HAP: " + errString(r));
    r = avcodec_send_frame(s.enc, nullptr);
    if (r < 0 && r != AVERROR_EOF)
        return finishFail("Falha ao finalizar o codificador: " + errString(r));
    r = drain();
    if (r < 0)
        return finishFail("Falha ao gravar o arquivo: " + errString(r));
    if (frames == 0)
        return finishFail("Nenhum quadro de video foi decodificado.");

    r = av_write_trailer(s.out);
    if (r < 0)
        return finishFail("Falha ao finalizar o arquivo: " + errString(r));
    r = avio_closep(&s.out->pb);
    s.ioOpened = false;
    if (r < 0)
        return finishFail("Falha ao fechar o arquivo de saida: " + errString(r));

    std::error_code ec;
    fs::remove(job.target, ec);
    fs::rename(part, job.target, ec);
    if (ec) {
        fs::remove(part, ec);
        return fail("Nao foi possivel renomear o arquivo final.");
    }

    if (progress)
        progress(1.0);
    res.ok = true;
    res.width = outW;
    res.height = outH;
    res.alpha = alpha;
    res.frames = frames;
    res.fps = av_q2d(fr);
    return res;
}

}
