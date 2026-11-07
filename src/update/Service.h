#pragma once

#include "update/Http.h"
#include "update/Policy.h"
#include "update/Release.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace bsl::update {

enum class Phase
{
    Idle,
    Available,
    Downloading,
    ConfirmNoChecksum,
    Ready,
};

struct Status
{
    Phase phase = Phase::Idle;
    bool checking = false;
    std::string checkNote;
    bool hasUpdate = false;
    Release release;
    AssetKind assetKind = AssetKind::None;
    std::string assetName;
    int64_t assetSize = 0;
    int64_t done = 0, total = 0;
    bool verifying = false;
    std::string error;
    std::string checkError;
    bool checkOffline = false;
    bool upToDate = false;
    bool checksumVerified = false;
    std::string sha256;
    std::filesystem::path stagedFile;
    bool restartRequested = false;
};

struct Brief
{
    Phase phase = Phase::Idle;
    bool checking = false;
    bool hasUpdate = false;
    std::string version;
    double progress = 0.0;
};

struct CheckOutcome
{
    bool ok = false;
    bool offline = false;
    bool manual = false;
    int64_t checkedUnix = 0;
    std::string error;
};

bool installedByMsi();

class Service
{
public:
    Service();
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    void configure(std::string currentVersion, std::string repo, std::filesystem::path updatesDir);
    void setHttp(std::unique_ptr<IHttp> http);
    void setInstalledByMsi(bool on) { installedByMsi_ = on; }
    bool installedByMsi() const { return installedByMsi_; }
    const SemVer& current() const { return current_; }
    const std::string& repo() const { return repo_; }
    const std::filesystem::path& updatesDir() const { return dir_; }

    void loadPersistedState(const Prefs& prefs);
    std::string takeLastUpdateMessage(bool* ok);

    void startCheck(const Prefs& prefs, bool manual);
    bool takeCheckOutcome(CheckOutcome& out);
    void startDownload();
    void cancel();
    void acceptWithoutChecksum();
    void discard();
    void remindLater() { reminded_ = true; }
    bool reminded() const { return reminded_; }
    void requestRestart(bool on) { restart_ = on; }
    bool restartRequested() const { return restart_; }

    Status status() const;
    Brief brief() const;
    bool busy() const { return worker_busy_; }

    bool launchInstallerOnExit(bool restartAfter, std::string* error);
    bool readyToInstall() const;

    void shutdown();

private:
    void runCheck(Prefs prefs, bool manual);
    void runDownload(Release release, AssetChoice choice);
    void setError(const std::string& text);
    void joinWorker();
    void savePending(const Status& st);
    void clearPending(bool deleteFiles);
    std::string pendingPath() const;
    void adopt(const Release& release, const Prefs& prefs);

    mutable std::mutex m_;
    Status st_;
    std::unique_ptr<IHttp> http_;
    std::filesystem::path dir_;
    std::string repo_;
    SemVer current_;
    std::string currentText_;
    std::atomic<bool> installedByMsi_{false};

    std::thread worker_;
    std::atomic<bool> worker_busy_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<bool> reminded_{false};
    std::atomic<bool> restart_{false};
    bool outcomePending_ = false;
    CheckOutcome outcome_;
};

}
