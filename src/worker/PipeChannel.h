#pragma once

#include "worker/WakeEvent.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace bsl::worker {

using Json = nlohmann::json;

class PipeChannel
{
public:
    PipeChannel() = default;
    ~PipeChannel();
    PipeChannel(const PipeChannel&) = delete;
    PipeChannel& operator=(const PipeChannel&) = delete;

    bool connect(const std::string& endpoint, WakeEvent* wake, std::string* error);

    bool send(const Json& msg);
    void log(const char* level, const std::string& text);

    bool poll(Json& out);
    bool broken() const { return broken_.load(); }

private:
    void readLoop();
    void notify();

#ifdef _WIN32
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE stopEvent_ = nullptr;
#else
    int fd_ = -1;
    WakeEvent stop_;
#endif
    WakeEvent* wake_ = nullptr;
    std::thread reader_;
    std::mutex writeMutex_;
    std::mutex queueMutex_;
    std::deque<Json> queue_;
    std::atomic<bool> broken_{false};
};

}
