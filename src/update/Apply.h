#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace bsl::update {

struct ApplyOptions
{
    std::filesystem::path zipFile;
    std::filesystem::path targetDir;
    std::filesystem::path stagingDir;
    std::filesystem::path backupDir;
    int lockRetries = 20;
    int lockRetryDelayMs = 300;
    std::function<void(const std::string&)> log;
    std::function<void(double fraction, const std::string& phase)> progress;
    std::function<bool(int index, const std::string& rel)> failHook;
};

struct ApplyResult
{
    bool ok = false;
    bool rolledBack = false;
    bool rollbackFailed = false;
    std::string error;
    int replaced = 0;
    int added = 0;
    int skipped = 0;
};

ApplyResult applyZipUpdate(const ApplyOptions& options);

bool isProtectedUserFile(const std::string& relativeName);

bool isDirectoryWritable(const std::filesystem::path& dir);

}
