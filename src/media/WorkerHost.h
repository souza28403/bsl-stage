#pragma once

#include "media/WorkerProtocol.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ID3D11Texture2D;
struct ID3D11DeviceContext;

namespace bsl::render {
class D3D11Interop;
}

namespace bsl::media {

using WorkerJson = nlohmann::json;

enum class HostState { Idle, Starting, Running, Restarting, Failed };

struct HostSnapshot
{
    HostState state = HostState::Idle;
    std::string text;
    bool warning = false;
    std::string failure;
    std::string transport;
    std::string transportWhy;
    unsigned long pid = 0;
    int restarts = 0;
    int pages = 0;
    std::string title;
};

struct FrameAccess
{
    enum class Kind { Shm, Texture };
    Kind kind = Kind::Shm;
    int width = 0;
    int height = 0;
    int page = 0;
    std::uint64_t seq = 0;
    const wp::ShmHeader* shmHeader = nullptr;
    const void* shmBase = nullptr;
    ID3D11Texture2D* mailbox = nullptr;
    ID3D11DeviceContext* context = nullptr;
};

class WorkerHost
{
public:
    WorkerHost();
    ~WorkerHost();
    WorkerHost(const WorkerHost&) = delete;
    WorkerHost& operator=(const WorkerHost&) = delete;

    void start(WorkerJson open, const std::shared_ptr<render::D3D11Interop>& interop, const std::string& transport = "auto");
    void stop();
    bool started() const { return core_ != nullptr; }

    void setOpen(WorkerJson open);
    void send(WorkerJson msg);

    std::vector<WorkerJson> drainEvents();
    HostSnapshot snapshot() const;

    bool acquireNewFrame(std::uint64_t& lastSeq, const std::function<void(const FrameAccess&)>& fn);
    std::uint64_t frameCount() const;

    void debugKillWorker();

    static bool waitAllStopped(unsigned timeoutMs);
    static bool processRunning(unsigned long pid);
    static std::wstring workerExePath();

private:
    struct Core;
    std::shared_ptr<Core> core_;
};

}
