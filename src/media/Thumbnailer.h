#pragma once

#include "media/Frame.h"

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

namespace bsl::media {

struct Thumbnail {
    std::filesystem::path path;
    Frame frame;     // RGBA8 width x height (miniatura com bordas transparentes se o aspecto diferir)
    bool ok = false; // false = falhou (frame vazio); entregue para a UI nao esperar para sempre
};

// Thread de fundo que gera miniaturas RGBA (padrao 160x90). O upload para GL e do main thread.
class Thumbnailer {
public:
    explicit Thumbnailer(int width = 160, int height = 90);
    ~Thumbnailer();
    Thumbnailer(const Thumbnailer&) = delete;
    Thumbnailer& operator=(const Thumbnailer&) = delete;

    // Enfileira (ignora pedidos duplicados ainda pendentes).
    void request(const std::filesystem::path& path);

    // Proxima miniatura pronta, se houver (nao bloqueia).
    std::optional<Thumbnail> poll();

    // Pedidos ainda nao entregues via poll().
    size_t pending() const;

private:
    void run(bool docs);
    Thumbnail generate(const std::filesystem::path& path);

    int width_;
    int height_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::filesystem::path> requests_;
    std::deque<std::filesystem::path> docRequests_; // PDF/Office: processo auxiliar, pode demorar (outra thread)
    std::set<std::filesystem::path> queued_;
    std::deque<Thumbnail> results_;
    size_t inFlight_ = 0;
    bool stop_ = false;
    std::thread thread_;
    std::thread docThread_;
};

} // namespace bsl::media
