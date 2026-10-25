#include "worker/PipeChannel.h"

#include "worker/WorkerUtil.h"

#include <vector>

namespace bsl::worker {

PipeChannel::~PipeChannel()
{
    broken_ = true;
    if (stopEvent_)
        SetEvent(stopEvent_);
    if (pipe_ != INVALID_HANDLE_VALUE)
        CancelIoEx(pipe_, nullptr);
    if (reader_.joinable())
        reader_.join();
    if (pipe_ != INVALID_HANDLE_VALUE)
        CloseHandle(pipe_);
    if (stopEvent_)
        CloseHandle(stopEvent_);
}

bool PipeChannel::connect(const std::wstring& pipeName, HANDLE wake, std::string* error)
{
    wake_ = wake;
    const ULONGLONG deadline = GetTickCount64() + 10000;
    for (;;) {
        pipe_ = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe_ != INVALID_HANDLE_VALUE)
            break;
        const DWORD err = GetLastError();
        if (err == ERROR_PIPE_BUSY)
            WaitNamedPipeW(pipeName.c_str(), 500);
        else
            Sleep(50);
        if (GetTickCount64() > deadline) {
            if (error)
                *error = "não foi possível conectar ao pipe (erro " + std::to_string(err) + ")";
            return false;
        }
    }
    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    reader_ = std::thread([this] { readLoop(); });
    return true;
}

void PipeChannel::readLoop()
{
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::string pending;
    std::vector<char> buf(64 * 1024);
    while (!broken_.load()) {
        OVERLAPPED ov{};
        ov.hEvent = ev;
        ResetEvent(ev);
        DWORD got = 0;
        BOOL ok = ReadFile(pipe_, buf.data(), static_cast<DWORD>(buf.size()), &got, &ov);
        if (!ok) {
            if (GetLastError() != ERROR_IO_PENDING)
                break;
            HANDLE waits[2] = {ev, stopEvent_};
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
                CancelIoEx(pipe_, &ov);
                GetOverlappedResult(pipe_, &ov, &got, TRUE);
                break;
            }
            if (!GetOverlappedResult(pipe_, &ov, &got, FALSE))
                break;
        }
        if (got == 0)
            break;
        pending.append(buf.data(), got);
        size_t nl;
        bool any = false;
        while ((nl = pending.find('\n')) != std::string::npos) {
            const std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            if (line.empty())
                continue;
            Json j = Json::parse(line, nullptr, false);
            if (j.is_object()) {
                std::lock_guard lk(queueMutex_);
                queue_.push_back(std::move(j));
                any = true;
            }
        }
        if (any && wake_)
            SetEvent(wake_);
    }
    CloseHandle(ev);
    broken_ = true;
    if (wake_)
        SetEvent(wake_);
}

bool PipeChannel::send(const Json& msg)
{
    if (broken_.load() || pipe_ == INVALID_HANDLE_VALUE)
        return false;
    std::string line = msg.dump(-1, ' ', false, Json::error_handler_t::replace);
    line.push_back('\n');

    std::lock_guard lk(writeMutex_);
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    OVERLAPPED ov{};
    ov.hEvent = ev;
    DWORD written = 0;
    bool ok = WriteFile(pipe_, line.data(), static_cast<DWORD>(line.size()), &written, &ov) != FALSE;
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        // O app drena o pipe sempre; se nao andar em 10 s ele travou
        if (WaitForSingleObject(ev, 10000) == WAIT_OBJECT_0) {
            ok = GetOverlappedResult(pipe_, &ov, &written, FALSE) != FALSE;
        } else {
            CancelIoEx(pipe_, &ov);
            GetOverlappedResult(pipe_, &ov, &written, TRUE);
            ok = false;
        }
    }
    CloseHandle(ev);
    if (!ok || written != line.size()) {
        broken_ = true;
        if (wake_)
            SetEvent(wake_);
        return false;
    }
    return true;
}

void PipeChannel::log(const char* level, const std::string& text)
{
    send(Json{{"t", "log"}, {"level", level}, {"msg", text}});
}

bool PipeChannel::poll(Json& out)
{
    std::lock_guard lk(queueMutex_);
    if (queue_.empty())
        return false;
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

} // namespace bsl::worker
