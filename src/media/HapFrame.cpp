#include "media/HapFrame.h"

#include "media/Snappy.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace bsl::media {

namespace {

constexpr std::uint8_t kCompressorNone = 0xA;
constexpr std::uint8_t kCompressorSnappy = 0xB;
constexpr std::uint8_t kCompressorComplex = 0xC;

constexpr std::uint8_t kFormatDxt1 = 0xB;
constexpr std::uint8_t kFormatDxt5 = 0xE;
constexpr std::uint8_t kFormatYCoCgDxt5 = 0xF;

constexpr std::uint8_t kSectionContainer = 0x01;
constexpr std::uint8_t kSectionCompressors = 0x02;
constexpr std::uint8_t kSectionSizes = 0x03;
constexpr std::uint8_t kSectionOffsets = 0x04;

constexpr std::size_t kMaxChunks = 4096;

struct Section {
    const std::uint8_t* body = nullptr;
    std::size_t bodyLen = 0;
    std::size_t total = 0;
    std::uint8_t type = 0;
};

std::uint32_t le32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool readSection(const std::uint8_t* d, std::size_t size, Section& s)
{
    if (size < 4)
        return false;
    std::size_t header = 4;
    std::size_t len = static_cast<std::size_t>(d[0]) | (static_cast<std::size_t>(d[1]) << 8) |
                      (static_cast<std::size_t>(d[2]) << 16);
    if (len == 0) {
        if (size < 8)
            return false;
        len = le32(d + 4);
        header = 8;
    }
    if (len > size - header)
        return false;
    s.type = d[3];
    s.body = d + header;
    s.bodyLen = len;
    s.total = header + len;
    return true;
}

bool isHandledFormat(std::uint8_t f, HapTexture& out)
{
    switch (f) {
    case kFormatDxt1:
        out = HapTexture::DXT1;
        return true;
    case kFormatDxt5:
        out = HapTexture::DXT5;
        return true;
    case kFormatYCoCgDxt5:
        out = HapTexture::YCoCgDXT5;
        return true;
    default:
        return false;
    }
}

struct ChunkTask {
    const std::uint8_t* in = nullptr;
    std::size_t inLen = 0;
    std::size_t outOffset = 0;
    std::size_t outLen = 0;
    bool snappy = false;
};

bool runTask(const ChunkTask& t, std::uint8_t* out)
{
    if (!t.snappy) {
        std::memcpy(out + t.outOffset, t.in, t.outLen);
        return true;
    }
    return snappyDecompressInto(t.in, t.inLen, out + t.outOffset, t.outLen);
}

class ChunkPool {
public:
    ChunkPool()
    {
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
        const unsigned workers = std::min(7u, hw > 1 ? hw - 1 : 0u);
        for (unsigned i = 0; i < workers; ++i)
            threads_.emplace_back([this] { workerLoop(); });
    }

    ~ChunkPool()
    {
        {
            std::lock_guard lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : threads_)
            t.join();
    }

    bool run(const std::vector<ChunkTask>& tasks, std::uint8_t* out)
    {
        std::unique_lock job(jobMutex_, std::try_to_lock);
        if (!job.owns_lock() || threads_.empty() || tasks.size() < 2) {
            for (const ChunkTask& t : tasks)
                if (!runTask(t, out))
                    return false;
            return true;
        }
        {
            std::lock_guard lk(m_);
            tasks_ = &tasks;
            out_ = out;
            next_.store(0);
            remaining_.store(tasks.size());
            failed_.store(false);
            ++generation_;
        }
        cv_.notify_all();
        work(&tasks, out);
        std::unique_lock lk(m_);
        doneCv_.wait(lk, [&] { return remaining_.load() == 0 && active_ == 0; });
        tasks_ = nullptr;
        return !failed_.load();
    }

private:
    void work(const std::vector<ChunkTask>* tasks, std::uint8_t* out)
    {
        while (true) {
            const std::size_t i = next_.fetch_add(1);
            if (i >= tasks->size())
                return;
            if (!runTask((*tasks)[i], out))
                failed_.store(true);
            if (remaining_.fetch_sub(1) == 1) {
                std::lock_guard lk(m_);
                doneCv_.notify_all();
            }
        }
    }

    void workerLoop()
    {
        unsigned seen = 0;
        while (true) {
            const std::vector<ChunkTask>* tasks = nullptr;
            std::uint8_t* out = nullptr;
            {
                std::unique_lock lk(m_);
                cv_.wait(lk, [&] { return stop_ || generation_ != seen; });
                if (stop_)
                    return;
                seen = generation_;
                tasks = tasks_;
                out = out_;
                if (tasks)
                    ++active_;
            }
            if (!tasks)
                continue;
            work(tasks, out);
            std::lock_guard lk(m_);
            --active_;
            doneCv_.notify_all();
        }
    }

    std::vector<std::thread> threads_;
    std::mutex jobMutex_;
    std::mutex m_;
    std::condition_variable cv_;
    std::condition_variable doneCv_;
    const std::vector<ChunkTask>* tasks_ = nullptr;
    std::uint8_t* out_ = nullptr;
    std::atomic<std::size_t> next_{0};
    std::atomic<std::size_t> remaining_{0};
    std::atomic<bool> failed_{false};
    unsigned generation_ = 0;
    int active_ = 0;
    bool stop_ = false;
};

ChunkPool& chunkPool()
{
    static ChunkPool pool;
    return pool;
}

HapStatus parseChunks(const Section& top, std::uint8_t* out, std::size_t expected)
{
    Section container;
    if (!readSection(top.body, top.bodyLen, container) || container.type != kSectionContainer)
        return HapStatus::Invalid;
    const std::uint8_t* data = top.body + container.total;
    const std::size_t dataLen = top.bodyLen - container.total;

    const std::uint8_t* compressors = nullptr;
    const std::uint8_t* sizes = nullptr;
    const std::uint8_t* offsets = nullptr;
    std::size_t compressorCount = 0;
    std::size_t sizeBytes = 0;
    std::size_t offsetBytes = 0;

    std::size_t pos = 0;
    while (pos < container.bodyLen) {
        Section s;
        if (!readSection(container.body + pos, container.bodyLen - pos, s))
            return HapStatus::Invalid;
        pos += s.total;
        if (s.type == kSectionCompressors) {
            compressors = s.body;
            compressorCount = s.bodyLen;
        } else if (s.type == kSectionSizes) {
            sizes = s.body;
            sizeBytes = s.bodyLen;
        } else if (s.type == kSectionOffsets) {
            offsets = s.body;
            offsetBytes = s.bodyLen;
        }
    }
    if (!compressors || !sizes || compressorCount == 0 || compressorCount > kMaxChunks ||
        sizeBytes != compressorCount * 4 || (offsets && offsetBytes != compressorCount * 4))
        return HapStatus::Invalid;

    static thread_local std::vector<ChunkTask> tasks;
    tasks.clear();
    std::size_t running = 0;
    std::size_t produced = 0;
    for (std::size_t i = 0; i < compressorCount; ++i) {
        const std::size_t compSize = le32(sizes + i * 4);
        const std::size_t start = offsets ? le32(offsets + i * 4) : running;
        if (start > dataLen || compSize > dataLen - start)
            return HapStatus::Invalid;
        running = start + compSize;
        ChunkTask t;
        t.in = data + start;
        t.inLen = compSize;
        t.outOffset = produced;
        if (compressors[i] == kCompressorNone) {
            t.outLen = compSize;
        } else if (compressors[i] == kCompressorSnappy) {
            std::size_t len = 0;
            if (!snappyUncompressedLength(t.in, compSize, len))
                return HapStatus::Invalid;
            t.outLen = len;
            t.snappy = true;
        } else {
            return HapStatus::Unsupported;
        }
        if (t.outLen > expected - produced)
            return HapStatus::Invalid;
        produced += t.outLen;
        tasks.push_back(t);
    }
    if (produced != expected)
        return HapStatus::Invalid;
    if (!chunkPool().run(tasks, out))
        return HapStatus::Invalid;
    return HapStatus::Ok;
}

}

std::size_t hapTextureSize(HapTexture format, int width, int height)
{
    if (width <= 0 || height <= 0)
        return 0;
    const std::size_t bw = (static_cast<std::size_t>(width) + 3) / 4;
    const std::size_t bh = (static_cast<std::size_t>(height) + 3) / 4;
    return bw * bh * (format == HapTexture::DXT1 ? 8u : 16u);
}

HapStatus hapParseFrame(const std::uint8_t* data, std::size_t size, int width, int height, HapTexture& format,
                        std::vector<std::uint8_t>& texture)
{
    if (!data || width <= 0 || height <= 0)
        return HapStatus::Invalid;
    Section top;
    if (!readSection(data, size, top))
        return HapStatus::Invalid;

    const std::uint8_t compressor = top.type >> 4;
    const std::uint8_t texFormat = top.type & 0x0F;
    HapTexture fmt = HapTexture::DXT1;
    if (!isHandledFormat(texFormat, fmt))
        return HapStatus::Unsupported;
    if (compressor != kCompressorNone && compressor != kCompressorSnappy && compressor != kCompressorComplex)
        return HapStatus::Unsupported;

    const std::size_t expected = hapTextureSize(fmt, width, height);
    texture.resize(expected);
    HapStatus st = HapStatus::Invalid;
    if (compressor == kCompressorNone) {
        if (top.bodyLen == expected) {
            std::memcpy(texture.data(), top.body, expected);
            st = HapStatus::Ok;
        }
    } else if (compressor == kCompressorSnappy) {
        if (snappyDecompressInto(top.body, top.bodyLen, texture.data(), expected))
            st = HapStatus::Ok;
    } else {
        st = parseChunks(top, texture.data(), expected);
    }
    if (st == HapStatus::Ok)
        format = fmt;
    return st;
}

}
