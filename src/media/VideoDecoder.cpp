#include "media/VideoDecoder.h"

#include "media/FrameConverter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
}

namespace bsl::media {

namespace {

struct Queued {
    std::unique_ptr<Frame> frame;
    int epoch = 0;
};

std::string errString(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

} // namespace

struct VideoDecoder::Impl {
    // --- FFmpeg (a partir do open(), so a thread de decodificacao mexe nisto) ---
    AVFormatContext* fmt = nullptr;
    AVCodecContext* cc = nullptr;
    int streamIdx = -1;
    AVRational timeBase = {1, 1000};
    double startTime = 0.0;
    FrameConverter converter;
    int outW = 0;
    int outH = 0;

    std::atomic<bool> abortIo{false}; // interrompe leituras bloqueadas (dispositivo sumiu) no close()

    VideoInfo info;
    VideoDecoderOptions options;
    std::string error;
    bool opened = false;

    // --- Sincronizacao ---
    mutable std::mutex m;
    std::condition_variable cvWorker;
    std::condition_variable cvConsumer;
    std::deque<Queued> queue;
    bool stop = false;
    bool loop = false;
    bool loopBroken = false;   // a volta ao inicio nao produziu frames: desiste do loop nativo
    bool eof = false;          // fim alcancado; aguarda seek/loop
    bool seekPending = false;
    double seekTarget = 0.0;
    int seekGen = 0;
    int epoch = 0;             // carimbo dos frames produzidos (incrementa a cada volta do loop)

    // --- Estado do consumidor (protegido por m) ---
    int consumerEpoch = 0;
    bool delivered = false;
    double lastT = 0.0;

    std::thread worker;

    bool canLoop() const { return loop && info.duration > 0.0 && !loopBroken; }

    void freeFfmpeg()
    {
        if (cc)
            avcodec_free_context(&cc);
        if (fmt)
            avformat_close_input(&fmt);
    }

    bool openFile(const std::filesystem::path& path);
    void run();
    void seekInternal(double target, double& discardBefore);
    std::unique_ptr<Frame> takeDue(double clipTime); // m deve estar travado
};

namespace {
int ioInterrupt(void* opaque)
{
    return static_cast<std::atomic<bool>*>(opaque)->load() ? 1 : 0;
}
} // namespace

bool VideoDecoder::Impl::openFile(const std::filesystem::path& path)
{
    initFfmpegLogging();
    const auto u8 = path.u8string();
    const std::string p(u8.begin(), u8.end());

    fmt = avformat_alloc_context();
    if (!fmt) {
        error = "sem memoria";
        return false;
    }
    fmt->interrupt_callback.callback = &ioInterrupt;
    fmt->interrupt_callback.opaque = &abortIo;

    const AVInputFormat* inputFormat = nullptr;
    if (!options.inputFormat.empty()) {
        static std::once_flag devicesOnce;
        std::call_once(devicesOnce, [] { avdevice_register_all(); });
        inputFormat = av_find_input_format(options.inputFormat.c_str());
        if (!inputFormat) {
            avformat_free_context(fmt);
            fmt = nullptr;
            error = "dispositivo/formato de entrada desconhecido: " + options.inputFormat;
            return false;
        }
    }
    AVDictionary* openOpts = nullptr;
    for (const auto& [k, v] : options.inputOptions)
        av_dict_set(&openOpts, k.c_str(), v.c_str(), 0);
    if (options.live) {
        av_dict_set(&openOpts, "fflags", "nobuffer", 0);
        av_dict_set(&openOpts, "analyzeduration", "500000", 0);
        av_dict_set(&openOpts, "probesize", "2000000", 0);
    }

    int r = avformat_open_input(&fmt, p.c_str(), inputFormat, &openOpts);
    av_dict_free(&openOpts);
    if (r < 0) {
        error = "avformat_open_input: " + errString(r);
        return false;
    }
    r = avformat_find_stream_info(fmt, nullptr);
    if (r < 0) {
        error = "avformat_find_stream_info: " + errString(r);
        return false;
    }
    streamIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIdx < 0) {
        error = "sem stream de video";
        return false;
    }
    AVStream* st = fmt->streams[streamIdx];
    AVCodecParameters* par = st->codecpar;
    timeBase = st->time_base;

    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    bool alphaFlag = false;

    // WEBM com alpha: o decoder nativo ignora o canal alpha; libvpx entrega yuva420p.
    if (par->codec_id == AV_CODEC_ID_VP8 || par->codec_id == AV_CODEC_ID_VP9) {
        const AVDictionaryEntry* e = av_dict_get(st->metadata, "alpha_mode", nullptr, 0);
        if (e && e->value && std::string(e->value) == "1") {
            const AVCodec* lib =
                avcodec_find_decoder_by_name(par->codec_id == AV_CODEC_ID_VP9 ? "libvpx-vp9" : "libvpx");
            if (lib) {
                codec = lib;
                alphaFlag = true;
            }
        }
    }
    if (!codec) {
        error = "decoder nao encontrado para o codec";
        return false;
    }

    cc = avcodec_alloc_context3(codec);
    if (!cc || avcodec_parameters_to_context(cc, par) < 0) {
        error = "falha ao criar contexto do codec";
        return false;
    }
    cc->pkt_timebase = st->time_base;
    cc->thread_count = 0; // auto
    if (options.live) {
        // Frame threading acrescenta quadros de latencia
        cc->thread_type = FF_THREAD_SLICE;
        cc->flags |= AV_CODEC_FLAG_LOW_DELAY;
    } else {
        cc->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }
    if (options.lowPower)
        cc->skip_loop_filter = AVDISCARD_ALL;

    r = avcodec_open2(cc, codec, nullptr);
    if (r < 0) {
        error = "avcodec_open2: " + errString(r);
        return false;
    }

    info.codec = avcodec_get_name(par->codec_id);
    info.decoderName = codec->name;
    info.width = cc->width;
    info.height = cc->height;
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(cc->pix_fmt);
    info.pixelFormat = desc ? desc->name : "?";
    info.hasAlpha = alphaFlag || (desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA));

    AVRational fr = av_guess_frame_rate(fmt, st, nullptr);
    if (fr.num <= 0 || fr.den <= 0)
        fr = st->avg_frame_rate;
    info.fps = (fr.num > 0 && fr.den > 0) ? av_q2d(fr) : 25.0;

    if (st->duration != AV_NOPTS_VALUE && st->duration > 0)
        info.duration = static_cast<double>(st->duration) * av_q2d(st->time_base);
    else if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
        info.duration = static_cast<double>(fmt->duration) / AV_TIME_BASE;
    startTime = (st->start_time != AV_NOPTS_VALUE) ? static_cast<double>(st->start_time) * av_q2d(st->time_base) : 0.0;

    // Downscale do perfil Eco
    outW = outH = 0;
    if (options.lowPower && options.targetWidth > 0 && options.targetHeight > 0 &&
        (info.width > options.targetWidth || info.height > options.targetHeight)) {
        const double s = std::min(static_cast<double>(options.targetWidth) / info.width,
                                  static_cast<double>(options.targetHeight) / info.height);
        outW = std::max(2, static_cast<int>(std::lround(info.width * s)) & ~1);
        outH = std::max(2, static_cast<int>(std::lround(info.height * s)) & ~1);
    }
    return true;
}

void VideoDecoder::Impl::seekInternal(double target, double& discardBefore)
{
    const double t = std::max(0.0, target) + startTime;
    const int64_t ts = static_cast<int64_t>(t / av_q2d(timeBase));
    av_seek_frame(fmt, streamIdx, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(cc);
    discardBefore = target > 0.0 ? target - 0.5 / std::max(1.0, info.fps) : -1e9;
}

void VideoDecoder::Impl::run()
{
    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    int localGen = 0;
    double discardBefore = -1e9;
    const double frameDur = 1.0 / std::max(1.0, info.fps);
    double lastPts = -frameDur;
    bool draining = false;
    int framesSinceLoop = 1;

    while (true) {
        double seekTo = 0.0;
        bool doSeek = false;
        bool doLoop = false;
        {
            std::unique_lock lk(m);
            cvWorker.wait(lk, [&] {
                return stop || seekPending || (eof && canLoop()) ||
                       (!eof && (options.live || queue.size() < static_cast<size_t>(std::max(1, options.queueSize))));
            });
            if (stop)
                break;
            if (seekPending) {
                seekTo = seekTarget;
                seekPending = false;
                localGen = seekGen;
                doSeek = true;
                eof = false;
            } else if (eof && canLoop()) {
                doLoop = true;
                eof = false;
                if (localGen == seekGen)
                    ++epoch;
            }
        }
        if (doSeek) {
            seekInternal(seekTo, discardBefore);
            draining = false;
            lastPts = seekTo - frameDur;
            framesSinceLoop = 1; // seek explicito nao aciona a protecao de loop vazio
        } else if (doLoop) {
            seekInternal(0.0, discardBefore);
            draining = false;
            lastPts = -frameDur;
            framesSinceLoop = 0;
        }

        const int r = avcodec_receive_frame(cc, fr);
        if (r == 0) {
            double t;
            if (fr->best_effort_timestamp != AV_NOPTS_VALUE)
                t = static_cast<double>(fr->best_effort_timestamp) * av_q2d(timeBase) - startTime;
            else
                t = lastPts + frameDur;
            lastPts = t;
            if (t >= discardBefore) {
                auto frame = std::make_unique<Frame>();
                if (converter.convert(fr, outW, outH, false, *frame)) {
                    frame->pts = t;
                    ++framesSinceLoop;
                    std::lock_guard lk(m);
                    if (!stop && localGen == seekGen && !seekPending) {
                        if (options.live) // descarta os atrasados: o dispositivo nunca espera pela UI
                            while (queue.size() >= static_cast<size_t>(std::max(1, options.queueSize)))
                                queue.pop_front();
                        queue.push_back({std::move(frame), epoch});
                        cvConsumer.notify_all();
                    }
                }
            }
            av_frame_unref(fr);
        } else if (r == AVERROR(EAGAIN)) {
            const int rr = av_read_frame(fmt, pkt);
            if (rr >= 0) {
                if (pkt->stream_index == streamIdx)
                    avcodec_send_packet(cc, pkt);
                av_packet_unref(pkt);
            } else if (options.live) {
                // Dispositivo parou/sumiu: fim definitivo (a fonte tenta reabrir)
                std::lock_guard lk(m);
                eof = true;
                cvConsumer.notify_all();
            } else if (!draining) {
                avcodec_send_packet(cc, nullptr);
                draining = true;
            } else {
                std::lock_guard lk(m);
                eof = true;
                cvConsumer.notify_all();
            }
        } else {
            // AVERROR_EOF (decoder drenado) ou erro irrecuperavel: fim da volta
            std::lock_guard lk(m);
            eof = true;
            if (loop && framesSinceLoop == 0)
                loopBroken = true;
            cvConsumer.notify_all();
        }
    }
    av_frame_free(&fr);
    av_packet_free(&pkt);
}

std::unique_ptr<Frame> VideoDecoder::Impl::takeDue(double t)
{
    if (info.duration > 0.0 && lastT - t > info.duration * 0.5) {
        // relogio deu a volta (loop): descarta o que sobrou da volta anterior
        ++consumerEpoch;
        while (!queue.empty() && queue.front().epoch < consumerEpoch)
            queue.pop_front();
    }
    lastT = t;

    std::unique_ptr<Frame> best;
    while (!queue.empty()) {
        Queued& head = queue.front();
        if (head.epoch > consumerEpoch)
            break; // frame da proxima volta: espera o relogio dar a volta
        if (head.frame->pts <= t + 0.001) {
            best = std::move(head.frame);
            queue.pop_front();
        } else {
            break;
        }
    }
    if (!best && !delivered && !queue.empty() && queue.front().epoch <= consumerEpoch) {
        best = std::move(queue.front().frame);
        queue.pop_front();
    }
    if (best)
        delivered = true;
    return best;
}

// ---------------------------------------------------------------------------

VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}

VideoDecoder::~VideoDecoder()
{
    close();
}

bool VideoDecoder::open(const std::filesystem::path& path, const VideoDecoderOptions& options)
{
    close();
    Impl& d = *impl_;
    d.options = options;
    d.abortIo = false;
    d.error.clear();
    d.info = {};
    if (!d.openFile(path)) {
        d.freeFfmpeg();
        return false;
    }
    {
        std::lock_guard lk(d.m);
        d.stop = false;
        d.eof = false;
        d.loopBroken = false;
        d.seekPending = false;
        d.queue.clear();
        d.epoch = d.consumerEpoch = 0;
        d.delivered = false;
        d.lastT = 0.0;
        d.opened = true;
    }
    d.worker = std::thread([this] { impl_->run(); });
    return true;
}

void VideoDecoder::close()
{
    Impl& d = *impl_;
    d.abortIo = true;
    if (d.worker.joinable()) {
        {
            std::lock_guard lk(d.m);
            d.stop = true;
        }
        d.cvWorker.notify_all();
        d.worker.join();
    }
    d.freeFfmpeg();
    std::lock_guard lk(d.m);
    d.queue.clear();
    d.opened = false;
}

bool VideoDecoder::isOpen() const
{
    return impl_->opened;
}

const VideoInfo& VideoDecoder::info() const
{
    return impl_->info;
}

const std::string& VideoDecoder::error() const
{
    return impl_->error;
}

void VideoDecoder::seek(double seconds)
{
    Impl& d = *impl_;
    if (!d.opened || d.options.live)
        return;
    if (d.info.duration > 0.0)
        seconds = std::min(seconds, std::max(0.0, d.info.duration - 0.001));
    seconds = std::max(0.0, seconds);
    {
        std::lock_guard lk(d.m);
        d.seekPending = true;
        d.seekTarget = seconds;
        d.loopBroken = false;
        ++d.seekGen;
        ++d.epoch;
        d.consumerEpoch = d.epoch;
        d.queue.clear();
        d.delivered = false;
        d.lastT = seconds;
    }
    d.cvWorker.notify_all();
}

void VideoDecoder::setLoop(bool loop)
{
    Impl& d = *impl_;
    if (d.options.live)
        return;
    {
        std::lock_guard lk(d.m);
        if (d.loop == loop)
            return;
        d.loop = loop;
    }
    d.cvWorker.notify_all();
}

std::unique_ptr<Frame> VideoDecoder::tryGetFrame(double clipTime)
{
    Impl& d = *impl_;
    std::unique_ptr<Frame> f;
    {
        std::lock_guard lk(d.m);
        f = d.takeDue(clipTime);
    }
    if (f)
        d.cvWorker.notify_one();
    return f;
}

std::unique_ptr<Frame> VideoDecoder::tryGetLatest()
{
    Impl& d = *impl_;
    std::unique_ptr<Frame> f;
    {
        std::lock_guard lk(d.m);
        if (d.queue.empty())
            return nullptr;
        f = std::move(d.queue.back().frame);
        d.queue.clear();
        d.delivered = true;
    }
    d.cvWorker.notify_one();
    return f;
}

std::unique_ptr<Frame> VideoDecoder::nextFrame(int timeoutMs)
{
    Impl& d = *impl_;
    std::unique_lock lk(d.m);
    d.cvConsumer.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                          [&] { return !d.queue.empty() || (d.eof && !d.canLoop()) || d.stop; });
    if (d.queue.empty())
        return nullptr;
    auto f = std::move(d.queue.front().frame);
    d.queue.pop_front();
    d.delivered = true;
    lk.unlock();
    d.cvWorker.notify_one();
    return f;
}

bool VideoDecoder::atEnd() const
{
    const Impl& d = *impl_;
    std::lock_guard lk(d.m);
    return d.eof && !d.canLoop() && d.queue.empty();
}

} // namespace bsl::media
