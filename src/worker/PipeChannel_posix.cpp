#include "worker/PipeChannel.h"

#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <vector>

namespace bsl::worker {

PipeChannel::~PipeChannel()
{
    broken_ = true;
    stop_.set();
    if (reader_.joinable())
        reader_.join();
    if (fd_ >= 0)
        close(fd_);
}

bool PipeChannel::connect(const std::string& endpoint, WakeEvent* wake, std::string* error)
{
    wake_ = wake;
    char* end = nullptr;
    const long fd = std::strtol(endpoint.c_str(), &end, 10);
    if (endpoint.empty() || !end || *end != '\0' || fd < 0) {
        if (error)
            *error = "descritor de comunicação inválido";
        return false;
    }
    fd_ = static_cast<int>(fd);
    reader_ = std::thread([this] { readLoop(); });
    return true;
}

void PipeChannel::notify()
{
    if (wake_)
        wake_->set();
}

void PipeChannel::readLoop()
{
    std::string pending;
    std::vector<char> buf(64 * 1024);
    while (!broken_.load()) {
        pollfd fds[2] = {{fd_, POLLIN, 0}, {stop_.readFd(), POLLIN, 0}};
        const int pr = ::poll(fds, 2, -1);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (fds[1].revents & POLLIN)
            break;
        if (!(fds[0].revents & (POLLIN | POLLHUP | POLLERR)))
            continue;
        const ssize_t got = recv(fd_, buf.data(), buf.size(), 0);
        if (got < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            break;
        }
        if (got == 0)
            break;
        pending.append(buf.data(), static_cast<size_t>(got));
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
        if (any)
            notify();
    }
    broken_ = true;
    notify();
}

bool PipeChannel::send(const Json& msg)
{
    if (broken_.load() || fd_ < 0)
        return false;
    std::string line = msg.dump(-1, ' ', false, Json::error_handler_t::replace);
    line.push_back('\n');

    std::lock_guard lk(writeMutex_);
    size_t off = 0;
    bool ok = true;
    while (off < line.size()) {
        const ssize_t n = ::send(fd_, line.data() + off, line.size() - off, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) {
            off += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd p{fd_, POLLOUT, 0};
            if (::poll(&p, 1, 10000) > 0)
                continue;
        }
        ok = false;
        break;
    }
    if (!ok) {
        broken_ = true;
        notify();
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

}
