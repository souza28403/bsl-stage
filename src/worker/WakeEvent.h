#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace bsl::worker {

class WakeEvent
{
public:
#ifdef _WIN32
    WakeEvent() { h_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); }
    ~WakeEvent()
    {
        if (h_)
            CloseHandle(h_);
    }
    void set() { SetEvent(h_); }
    bool wait(unsigned ms) { return WaitForSingleObject(h_, ms) == WAIT_OBJECT_0; }
    HANDLE native() const { return h_; }

private:
    HANDLE h_ = nullptr;
#else
    WakeEvent()
    {
        int fds[2];
        if (pipe(fds) == 0) {
            rd_ = fds[0];
            wr_ = fds[1];
            for (int fd : fds) {
                fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
                fcntl(fd, F_SETFD, FD_CLOEXEC);
            }
        }
    }
    ~WakeEvent()
    {
        if (rd_ >= 0)
            close(rd_);
        if (wr_ >= 0)
            close(wr_);
    }
    void set()
    {
        const char b = 1;
        ssize_t r;
        do {
            r = write(wr_, &b, 1);
        } while (r < 0 && errno == EINTR);
    }
    bool wait(unsigned ms)
    {
        pollfd p{rd_, POLLIN, 0};
        int r;
        do {
            r = poll(&p, 1, static_cast<int>(ms));
        } while (r < 0 && errno == EINTR);
        if (r <= 0)
            return false;
        drain();
        return true;
    }
    int readFd() const { return rd_; }
    void drain()
    {
        char buf[64];
        while (read(rd_, buf, sizeof(buf)) > 0) {
        }
    }

private:
    int rd_ = -1;
    int wr_ = -1;
#endif
public:
    WakeEvent(const WakeEvent&) = delete;
    WakeEvent& operator=(const WakeEvent&) = delete;
};

}
