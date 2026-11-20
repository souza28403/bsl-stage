#include "app/ShowCacheQueue.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "app/ShowCacheProc.h"
#include "media/ShowCache.h"

#include <nlohmann/json.hpp>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace bsl {

namespace fs = std::filesystem;

namespace {

struct Probe
{
    int width = 0;
    int height = 0;
    double fps = 0.0;
    double duration = 0.0;
    bool alpha = false;
    bool ok = false;
};

Probe probeVideo(const fs::path& file)
{
    Probe p;
    AVFormatContext* fmt = nullptr;
    const std::string path = paths::toUtf8(file);
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0)
        return p;
    if (avformat_find_stream_info(fmt, nullptr) >= 0) {
        const int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (idx >= 0) {
            const AVStream* st = fmt->streams[idx];
            p.width = st->codecpar->width;
            p.height = st->codecpar->height;
            AVRational r = st->avg_frame_rate;
            if (r.num <= 0 || r.den <= 0)
                r = st->r_frame_rate;
            if (r.num > 0 && r.den > 0)
                p.fps = av_q2d(r);
            if (st->duration > 0)
                p.duration = static_cast<double>(st->duration) * av_q2d(st->time_base);
            if (p.duration <= 0.0 && fmt->duration > 0)
                p.duration = static_cast<double>(fmt->duration) / AV_TIME_BASE;
            const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(st->codecpar->format));
            p.alpha = d && (d->flags & AV_PIX_FMT_FLAG_ALPHA);
            p.ok = p.width > 0 && p.height > 0 && p.fps > 0.0 && p.duration > 0.0;
        }
    }
    avformat_close_input(&fmt);
    return p;
}

void writeText(const fs::path& file, const std::string& text)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

bool readText(const fs::path& file, std::string& text)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    text = ss.str();
    return true;
}

}

ShowCacheQueue::ShowCacheQueue() = default;

ShowCacheQueue::~ShowCacheQueue()
{
    shutdown();
}

void ShowCacheQueue::setCacheDirOverride(const std::string& dirUtf8)
{
    if (override_ != dirUtf8)
        lookups_.clear();
    override_ = dirUtf8;
}

fs::path ShowCacheQueue::cacheDir() const
{
    if (!override_.empty())
        return fs::path(std::u8string(override_.begin(), override_.end()));
    return paths::appDataDir() / "cache" / "show";
}

fs::path ShowCacheQueue::jobFile(const Item& item) const
{
    return cacheDir() / (item.key + ".job.json");
}

fs::path ShowCacheQueue::progressFile(const Item& item) const
{
    return cacheDir() / (item.key + ".progress.json");
}

ShowCacheQueue::Item* ShowCacheQueue::findKey(const std::string& key)
{
    for (Item& it : items_)
        if (it.key == key)
            return &it;
    return nullptr;
}

ShowCacheQueue::Item* ShowCacheQueue::find(const fs::path& source, int maxW, int maxH)
{
    return findKey(media::showCacheKey(source, maxW, maxH));
}

const ShowCacheQueue::Item* ShowCacheQueue::find(const fs::path& source, int maxW, int maxH) const
{
    const std::string key = media::showCacheKey(source, maxW, maxH);
    for (const Item& it : items_)
        if (it.key == key)
            return &it;
    return nullptr;
}

void ShowCacheQueue::enqueue(const fs::path& source, int maxW, int maxH)
{
    if (source.empty())
        return;
    if (Item* existing = find(source, maxW, maxH)) {
        if (existing->status.state != State::Falhou)
            return;
        const std::string oldKey = existing->key;
        std::erase_if(items_, [&](const Item& i) { return i.key == oldKey; });
    }
    lookups_.clear();
    if (media::lookupShowCache(cacheDir(), source, maxW, maxH))
        return;
    Item item;
    item.source = source;
    item.maxW = maxW;
    item.maxH = maxH;
    item.key = media::showCacheKey(source, maxW, maxH);
    if (item.key.empty())
        return;
    item.status.state = State::Pendente;
    items_.push_back(std::move(item));
    startNext();
}

void ShowCacheQueue::cancel(const fs::path& source, int maxW, int maxH)
{
    lookups_.clear();
    Item* it = find(source, maxW, maxH);
    if (!it)
        return;
    if (currentKey_ == it->key) {
        if (proc_)
            proc_->kill();
        proc_.reset();
        currentKey_.clear();
    }
    cleanFiles(*it);
    const std::string removeKey = it->key;
    std::erase_if(items_, [&](const Item& i) { return i.key == removeKey; });
    startNext();
}

ShowCacheQueue::Status ShowCacheQueue::status(const fs::path& source, int maxW, int maxH) const
{
    if (const Item* it = find(source, maxW, maxH))
        return it->status;
    if (lookupGeneration_ != generation_) {
        lookups_.clear();
        lookupGeneration_ = generation_;
    }
    const std::string key = media::showCacheKey(source, maxW, maxH);
    const auto now = std::chrono::steady_clock::now();
    auto found = lookups_.find(key);
    if (found == lookups_.end() || now - found->second.at > std::chrono::seconds(2)) {
        CachedLookup entry;
        entry.ready = media::lookupShowCache(cacheDir(), source, maxW, maxH).has_value();
        entry.at = now;
        found = lookups_.insert_or_assign(key, entry).first;
    }
    if (found->second.ready) {
        Status s;
        s.state = State::Pronto;
        s.progress = 1.0f;
        return s;
    }
    return {};
}

void ShowCacheQueue::setPaused(bool paused)
{
    paused_ = paused;
    if (!paused_)
        startNext();
}

void ShowCacheQueue::cleanFiles(const Item& item) const
{
    std::error_code ec;
    fs::remove(jobFile(item), ec);
    fs::remove(progressFile(item), ec);
    fs::path part = media::showCacheVideo(cacheDir(), item.key);
    part += ".part";
    fs::remove(part, ec);
}

void ShowCacheQueue::finish(Item& item, bool ok, const std::string& error)
{
    item.status.state = ok ? State::Pronto : State::Falhou;
    item.status.progress = ok ? 1.0f : item.status.progress;
    item.status.error = error;
    const std::string name = paths::toUtf8(item.source.filename());
    if (ok)
        log::info("Otimização para show concluída: \"" + name + "\"");
    else
        log::warn("Otimização para show falhou em \"" + name + "\": " + error);
    cleanFiles(item);
    ++generation_;
}

void ShowCacheQueue::startNext()
{
    if (paused_ || proc_)
        return;
    for (Item& item : items_) {
        if (item.status.state != State::Pendente)
            continue;
        const fs::path dir = cacheDir();
        std::error_code ec;
        fs::create_directories(dir, ec);

        const Probe probe = probeVideo(item.source);
        if (probe.ok) {
            int w = probe.width, h = probe.height;
            if (item.maxW > 0 && w > item.maxW) {
                h = std::max(1, static_cast<int>(static_cast<double>(h) * item.maxW / w));
                w = item.maxW;
            }
            if (item.maxH > 0 && h > item.maxH) {
                w = std::max(1, static_cast<int>(static_cast<double>(w) * item.maxH / h));
                h = item.maxH;
            }
            const std::int64_t frames = static_cast<std::int64_t>(probe.duration * probe.fps) + 1;
            const std::uintmax_t need = media::estimateShowCacheBytes(w, h, frames, probe.alpha);
            const fs::space_info sp = fs::space(dir, ec);
            if (!ec && need > 0 && sp.available < need) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(need) / (1024.0 * 1024.0 * 1024.0));
                item.status.state = State::Falhou;
                item.status.error = std::string("Espaço insuficiente no disco do cache (precisa de ~") + buf + " GB)";
                log::warn(item.status.error);
                ++generation_;
                continue;
            }
        }

        const fs::path target = media::showCacheVideo(dir, item.key);
        const nlohmann::json job = {{"source", paths::toUtf8(item.source)},
                                    {"target", paths::toUtf8(target)},
                                    {"maxWidth", item.maxW},
                                    {"maxHeight", item.maxH}};
        writeText(jobFile(item), job.dump());
        fs::remove(progressFile(item), ec);

        std::string error;
        proc_ = detail::ShowProc::start(jobFile(item), progressFile(item), error);
        if (!proc_) {
            item.status.state = State::Falhou;
            item.status.error = error.empty() ? "Não foi possível iniciar a conversão" : error;
            fs::remove(jobFile(item), ec);
            ++generation_;
            continue;
        }
        item.status.state = State::Convertendo;
        item.status.progress = 0.0f;
        currentKey_ = item.key;
        return;
    }
}

void ShowCacheQueue::readProgress(Item& item, std::string* doneJson)
{
    std::string text;
    if (!readText(progressFile(item), text) || text.empty())
        return;
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object())
        return;
    const std::string state = j.value("state", std::string());
    if (state == "running") {
        item.status.progress = std::clamp(static_cast<float>(j.value("progress", 0.0)), 0.0f, 1.0f);
    } else if (state == "done" || state == "failed") {
        if (doneJson)
            *doneJson = text;
    }
}

void ShowCacheQueue::poll()
{
    if (!proc_) {
        startNext();
        return;
    }
    Item* item = findKey(currentKey_);
    if (!item) {
        proc_->kill();
        proc_.reset();
        currentKey_.clear();
        startNext();
        return;
    }
    std::string done;
    readProgress(*item, &done);
    int exitCode = -1;
    if (proc_->running(exitCode))
        return;
    if (done.empty())
        readProgress(*item, &done);
    proc_.reset();
    currentKey_.clear();

    bool ok = false;
    std::string error = "A conversão terminou sem concluir";
    const nlohmann::json j = done.empty() ? nlohmann::json() : nlohmann::json::parse(done, nullptr, false);
    if (j.is_object()) {
        const std::string state = j.value("state", std::string());
        if (state == "done" && exitCode == 0) {
            media::ShowCacheInfo info;
            info.width = j.value("width", 0);
            info.height = j.value("height", 0);
            info.alpha = j.value("alpha", false);
            info.frames = j.value("frames", std::int64_t{0});
            info.fps = j.value("fps", 0.0);
            std::error_code ec;
            info.sourceSize = fs::file_size(item->source, ec);
            if (ec)
                info.sourceSize = 0;
            const auto t = fs::last_write_time(item->source, ec);
            info.sourceMtime = ec ? 0 : static_cast<std::int64_t>(t.time_since_epoch().count());
            const bool videoOk = fs::is_regular_file(media::showCacheVideo(cacheDir(), item->key), ec);
            if (videoOk && media::writeShowCacheSidecar(cacheDir(), item->key, item->source, info))
                ok = true;
            else
                error = "Não foi possível gravar o cache de show";
        } else if (state == "failed") {
            error = j.value("error", error);
        }
    }
    finish(*item, ok, ok ? std::string() : error);
    startNext();
}

void ShowCacheQueue::shutdown()
{
    if (proc_) {
        proc_->kill();
        proc_.reset();
        if (Item* it = findKey(currentKey_))
            cleanFiles(*it);
        currentKey_.clear();
    }
}

}
