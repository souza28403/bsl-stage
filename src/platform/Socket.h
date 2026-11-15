#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace bsl::sock {

#ifdef _WIN32
using Handle = SOCKET;
using Len = int;
constexpr Handle kInvalid = INVALID_SOCKET;
constexpr int kSendFlags = 0;
#else
using Handle = int;
using Len = socklen_t;
constexpr Handle kInvalid = -1;
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
#endif

bool startup();
void cleanup();

void close(Handle s);
void shutdownSend(Handle s);
int lastError();
bool isAddrInUse(int code);
bool isAccessDenied(int code);

void setNonBlocking(Handle s);
void setReuseAddr(Handle s);
void setBroadcast(Handle s);
void setRecvBuffer(Handle s, int bytes);
void setTimeoutsMs(Handle s, int recvMs, int sendMs);
void disableUdpConnReset(Handle s);

int selectArg(Handle maxHandle);

struct LocalInterface {
    std::string name;
    std::uint32_t ipHost = 0;
    std::uint32_t maskHost = 0;
    bool loopback = false;
    bool hasMac = false;
    std::array<std::uint8_t, 6> mac{};
};

std::vector<LocalInterface> localInterfaces();

}
