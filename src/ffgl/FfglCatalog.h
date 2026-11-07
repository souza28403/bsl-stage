#pragma once

#include "ffgl/FfglInfo.h"
#include "ffgl/FfglParams.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace bsl::ffgl {

struct Folder
{
    std::filesystem::path path;
    std::string label;
};

struct CatalogEntry
{
    PluginInfo info;
    std::string typeId;
    std::string origin;
    ParamMap map;
};
using EntryPtr = std::shared_ptr<const CatalogEntry>;

struct BlockedPlugin
{
    std::string path;
    std::string fileName;
    std::string origin;
    std::string reason;
    bool session = false;
};

class Catalog
{
public:
    static Catalog& instance();
    ~Catalog();
    void shutdown();

    void configure(std::vector<Folder> folders);
    void rescan();
    void retry(const std::string& pathUtf8);

    bool scanning() const { return scanning_.load(); }
    std::string scanStatus() const;
    bool waitIdle(unsigned timeoutMs);
    std::uint64_t version() const { return version_.load(); }

    std::vector<EntryPtr> entries() const;
    EntryPtr find(const std::string& typeId) const;
    std::vector<BlockedPlugin> blocked() const;
    int ignoredCount() const { return ignored_.load(); }
    bool webBrowserFound() const { return webBrowser_.load(); }
    std::vector<Folder> folders() const;

    void markSessionFault(const std::string& pathUtf8, const std::string& reason);
    bool sessionBlocked(const std::string& pathUtf8, std::string* reason = nullptr) const;
    std::uint64_t faultEpoch() const { return faultEpoch_.load(); }

    void setScanTimeoutMs(unsigned ms) { timeoutMs_ = ms; }
    void setCachePath(std::filesystem::path p);
    void setUseCache(bool on) { useCache_ = on; }
    void clearCache();

private:
    Catalog() = default;

    struct Candidate
    {
        std::filesystem::path path;
        std::string pathUtf8;
        std::string origin;
        std::uint64_t size = 0;
        std::int64_t mtime = 0;
    };
    struct CacheRecord
    {
        std::uint64_t size = 0;
        std::int64_t mtime = 0;
        bool ok = false;
        std::string error;
        PluginInfo info;
    };

    void runScan();
    void loadCache();
    void saveCache() const;
    ScanResult scanOne(const Candidate& c);
    void publish(std::vector<EntryPtr> entries, std::vector<BlockedPlugin> blocked);

    mutable std::mutex mutex_;
    std::vector<Folder> folders_;
    std::vector<EntryPtr> entries_;
    std::vector<BlockedPlugin> blocked_;
    std::vector<BlockedPlugin> sessionBlocked_;
    std::map<std::string, CacheRecord> cache_;
    std::vector<std::string> retryPaths_;
    std::filesystem::path cachePath_;
    std::string status_;

    std::thread thread_;
    std::atomic<bool> scanning_{false};
    std::atomic<bool> restart_{false};
    std::atomic<bool> stop_{false};
    std::atomic<std::uint64_t> version_{1};
    std::atomic<std::uint64_t> faultEpoch_{0};
    std::atomic<int> ignored_{0};
    std::atomic<bool> webBrowser_{false};
    std::atomic<void*> currentProc_{nullptr};
    unsigned timeoutMs_ = 10000;
    bool useCache_ = true;
    bool cacheLoaded_ = false;
};

std::string pluginDisplayName(const PluginInfo& info);

}
