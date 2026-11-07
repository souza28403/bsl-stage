#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace bsl::ffgl {

struct ImportResult
{
    int dlls = 0;
    int folders = 0;
    int failures = 0;
    std::string message;
    bool ok() const { return failures == 0 && dlls > 0; }
};

ImportResult importPlugins(const std::filesystem::path& source, const std::filesystem::path& dest);

class ImportJob
{
public:
    ~ImportJob();
    void start(const std::filesystem::path& source, const std::filesystem::path& dest);
    bool running() const { return running_.load(); }
    bool takeResult(ImportResult& out);

private:
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    bool have_ = false;
    ImportResult result_;
};

}
