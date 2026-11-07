#pragma once

#include "fx/IsfDoc.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace bsl::fx {

struct IsfEntry
{
    std::string id;
    std::filesystem::path file;
    std::string name;
    std::string category;
    std::string description;
    bool isEffect = false;
    std::string error;
    std::shared_ptr<const IsfDoc> doc;
    std::vector<std::string> warnings;
    std::int64_t stampTime = 0;
    std::uint64_t stampSize = 0;
};

class IsfLibrary
{
public:
    static IsfLibrary& instance();
    ~IsfLibrary();

    void configure(std::vector<std::filesystem::path> dirs, const std::filesystem::path& samplesDir = {});
    std::vector<std::filesystem::path> directories() const;
    std::optional<std::string> idForFile(const std::filesystem::path& file) const;

    void rescan();
    void waitScan();
    bool scanning() const { return scanning_.load(); }
    std::uint64_t version() const { return version_.load(); }

    std::vector<IsfEntry> entries() const;
    void startWatcher(int intervalMs = 600);
    void stopWatcher();
    bool checkForChanges(bool immediate = false);

    std::optional<IsfEntry> find(const std::string& id);
    std::shared_ptr<const IsfDoc> doc(const std::string& id);

    static IsfEntry loadFile(const std::filesystem::path& file, const std::string& id);

private:
    IsfLibrary() = default;
    void scanWorker(std::vector<std::filesystem::path> dirs);

    struct FileStamp
    {
        std::string path;
        std::int64_t time = 0;
        std::uint64_t size = 0;
        bool operator==(const FileStamp& o) const { return path == o.path && time == o.time && size == o.size; }
    };
    static std::vector<FileStamp> listStamps(const std::vector<std::filesystem::path>& dirs);

    mutable std::mutex mutex_;
    std::vector<std::filesystem::path> dirs_;
    std::vector<IsfEntry> entries_;
    std::atomic<std::uint64_t> version_{1};
    std::atomic<bool> scanning_{false};
    bool rescanQueued_ = false;
    std::thread worker_;

    std::mutex watchMutex_;
    std::vector<FileStamp> seenStamps_;
    std::vector<FileStamp> pendingStamps_;
    bool seenValid_ = false;
    std::thread watcher_;
    std::mutex stopMutex_;
    std::condition_variable stopCv_;
    bool stopWatcher_ = false;
};

}
