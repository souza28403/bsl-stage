#include "worker/FrameSink.h"

#include "media/WorkerProtocol.h"
#include "worker/PixelOps.h"
#include "worker/WorkerUtil.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>

namespace bsl::worker {

namespace {

std::atomic<unsigned> g_shmCounter{0};

class ShmSink final : public FrameSink
{
public:
    explicit ShmSink(PipeChannel* ch) : ch_(ch) {}
    ~ShmSink() override { release(); }

    const char* kind() const override { return "shm"; }

    bool resize(int w, int h) override
    {
        if (w <= 0 || h <= 0 || w > 16384 || h > 16384)
            return false;
        release();
        const std::size_t total = wp::shmTotalBytes(w, h);
        name_ = "/bslw" + std::to_string(static_cast<unsigned long>(getpid())) + "_" + std::to_string(g_shmCounter.fetch_add(1));
        const int fd = shm_open(name_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0) {
            name_.clear();
            return false;
        }
        if (ftruncate(fd, static_cast<off_t>(total)) != 0) {
            close(fd);
            release();
            return false;
        }
        void* view = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (view == MAP_FAILED) {
            release();
            return false;
        }
        view_ = view;
        bytes_ = total;
        std::memset(view_, 0, sizeof(wp::ShmHeader));
        auto* hdr = static_cast<wp::ShmHeader*>(view_);
        hdr->magic = wp::kShmMagic;
        hdr->version = wp::kProtocolVersion;
        hdr->width = static_cast<std::uint32_t>(w);
        hdr->height = static_cast<std::uint32_t>(h);
        __atomic_store_n(&hdr->latest, static_cast<wp::ShmLong>(-1), __ATOMIC_RELEASE);
        width_ = w;
        height_ = h;
        ch_->send(Json{{"t", "shm"}, {"w", w}, {"h", h}, {"name", name_}});
        return true;
    }

    bool submitBgra(const std::uint8_t* bgra, int stride, bool premultiplied, int page) override
    {
        if (!view_)
            return false;
        auto* hdr = static_cast<wp::ShmHeader*>(view_);
        const wp::ShmLong latest = __atomic_load_n(&hdr->latest, __ATOMIC_ACQUIRE);
        const int idx = latest == 0 ? 1 : 0;
        __atomic_add_fetch(&hdr->seq[idx], 1, __ATOMIC_SEQ_CST);
        std::uint8_t* dst = wp::shmBuffer(view_, width_, height_, idx);
        for (int y = 0; y < height_; ++y) {
            const std::uint8_t* s = bgra + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
            std::uint8_t* d = dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) * 4u;
            if (premultiplied) {
                unpremultiplyRow(s, d, width_);
            } else {
                std::memcpy(d, s, static_cast<std::size_t>(width_) * 4u);
            }
            for (int x = 0; x < width_; ++x)
                std::swap(d[x * 4], d[x * 4 + 2]);
        }
        const wp::ShmLong64 seq = __atomic_add_fetch(&hdr->seq[idx], 1, __ATOMIC_SEQ_CST);
        __atomic_store_n(&hdr->latest, static_cast<wp::ShmLong>(idx), __ATOMIC_SEQ_CST);
        ch_->send(Json{{"t", "frame"}, {"i", idx}, {"seq", static_cast<long long>(seq)}, {"page", page}});
        return true;
    }

private:
    void release()
    {
        if (view_) {
            munmap(view_, bytes_);
            view_ = nullptr;
            bytes_ = 0;
        }
        if (!name_.empty()) {
            shm_unlink(name_.c_str());
            name_.clear();
        }
    }

    PipeChannel* ch_;
    std::string name_;
    void* view_ = nullptr;
    std::size_t bytes_ = 0;
};

}

std::unique_ptr<FrameSink> createShmSink(const SinkRequest& request)
{
    return std::make_unique<ShmSink>(request.channel);
}

std::unique_ptr<FrameSink> createFrameSink(const SinkRequest& request)
{
    std::string why;
    if (request.transport == "texture") {
        request.channel->send(
            Json{{"t", "error"}, {"msg", "textura compartilhada indisponível nesta plataforma"}, {"fatal", true}});
        return nullptr;
    }
    why = request.transport == "shm" ? "memória compartilhada pedida" : "textura compartilhada indisponível nesta plataforma";
    auto shm = createShmSink(request);
    request.channel->send(Json{{"t", "transport"}, {"kind", "shm"}, {"why", why}});
    return shm;
}

}
