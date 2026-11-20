#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

namespace bsl {

namespace detail {
class ShowProc;
}

class ShowCacheQueue
{
public:
    enum class State { Nenhum, Pendente, Convertendo, Pronto, Falhou };

    struct Status
    {
        State state = State::Nenhum;
        float progress = 0.0f;
        std::string error;
    };

    ShowCacheQueue();
    ~ShowCacheQueue();
    ShowCacheQueue(const ShowCacheQueue&) = delete;
    ShowCacheQueue& operator=(const ShowCacheQueue&) = delete;

    void setCacheDirOverride(const std::string& dirUtf8);
    std::filesystem::path cacheDir() const;

    void enqueue(const std::filesystem::path& source, int maxW, int maxH);
    void cancel(const std::filesystem::path& source, int maxW, int maxH);
    Status status(const std::filesystem::path& source, int maxW, int maxH) const;
    void setPaused(bool paused);
    bool paused() const { return paused_; }
    void poll();
    void shutdown();

    std::uint64_t generation() const { return generation_; }

private:
    struct Item
    {
        std::filesystem::path source;
        int maxW = 0;
        int maxH = 0;
        std::string key;
        Status status;
    };

    Item* findKey(const std::string& key);
    Item* find(const std::filesystem::path& source, int maxW, int maxH);
    const Item* find(const std::filesystem::path& source, int maxW, int maxH) const;
    void startNext();
    void readProgress(Item& item, std::string* doneJson);
    void finish(Item& item, bool ok, const std::string& error);
    void cleanFiles(const Item& item) const;
    std::filesystem::path jobFile(const Item& item) const;
    std::filesystem::path progressFile(const Item& item) const;

    struct CachedLookup
    {
        bool ready = false;
        std::chrono::steady_clock::time_point at;
    };

    std::deque<Item> items_;
    mutable std::unordered_map<std::string, CachedLookup> lookups_;
    mutable std::uint64_t lookupGeneration_ = 0;
    std::unique_ptr<detail::ShowProc> proc_;
    std::string currentKey_;
    std::string override_;
    bool paused_ = false;
    std::uint64_t generation_ = 0;
};

}
