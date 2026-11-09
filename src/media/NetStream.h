#pragma once

#include "media/Frame.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace bsl::media::net {

enum class Mode { Pull, Listen };
enum class ListenProtocol { Rtmp, Srt };

struct Config {
    Mode mode = Mode::Pull;
    std::string url;
    ListenProtocol protocol = ListenProtocol::Rtmp;
    int port = 1935;
    std::string path = "/live/stream1";
    std::string bindAddress = "0.0.0.0";
};

enum class State { Stopped, Connecting, Waiting, Live, NoSignal, Error };

struct Status {
    State state = State::Stopped;
    std::string message;
    std::string address;
    int retryInSeconds = 0;
    bool hadSignal = false;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    double kbps = 0.0;
    std::string codec;
    unsigned long long frames = 0;
};

constexpr int kDefaultRtmpPort = 1935;
constexpr int kDefaultSrtPort = 9000;

std::vector<std::string> localIPv4Addresses();

std::string normalizePath(const std::string& path);
std::string sanitizeName(const std::string& name);

std::string listenUrl(const Config& cfg);
std::string displayAddress(const Config& cfg, const std::string& host);

std::string validateConfig(const Config& cfg);
std::string redactUrl(const std::string& url);
std::string urlFromShortcut(const std::filesystem::path& file);
bool looksLikeStreamUrl(const std::string& text);

std::string describeStatus(const Config& cfg, const Status& st);

bool portAvailable(const std::string& bindAddress, int port, bool udp, std::string* error = nullptr);

class StreamWorker {
public:
    StreamWorker();
    ~StreamWorker();
    StreamWorker(const StreamWorker&) = delete;
    StreamWorker& operator=(const StreamWorker&) = delete;

    void start(const Config& cfg);
    void stop(int graceMs = 250);
    bool running() const;

    bool takeFrame(Frame& out);
    Status status() const;
    const Config& config() const { return cfg_; }

    struct Shared;

private:
    std::shared_ptr<Shared> shared_;
    Config cfg_;
    std::thread thread_;
};

}
