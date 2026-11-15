#include "platform/Socket.h"

#include "platform/Utf.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <iphlpapi.h>
#include <mstcpip.h>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#ifdef __linux__
#include <netpacket/packet.h>
#else
#include <net/if_dl.h>
#endif
#endif

namespace bsl::sock {

#ifdef _WIN32

bool startup()
{
    WSADATA d{};
    return WSAStartup(MAKEWORD(2, 2), &d) == 0;
}

void cleanup()
{
    WSACleanup();
}

void close(Handle s)
{
    closesocket(s);
}

void shutdownSend(Handle s)
{
    shutdown(s, SD_SEND);
}

int lastError()
{
    return WSAGetLastError();
}

bool isAddrInUse(int code)
{
    return code == WSAEADDRINUSE;
}

bool isAccessDenied(int code)
{
    return code == WSAEACCES;
}

void setNonBlocking(Handle s)
{
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
}

void setTimeoutsMs(Handle s, int recvMs, int sendMs)
{
    const DWORD rt = static_cast<DWORD>(recvMs), st = static_cast<DWORD>(sendMs);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rt), sizeof(rt));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&st), sizeof(st));
}

void disableUdpConnReset(Handle s)
{
    DWORD bytes = 0;
    BOOL newBehavior = FALSE;
    WSAIoctl(s, SIO_UDP_CONNRESET, &newBehavior, sizeof(newBehavior), nullptr, 0, &bytes, nullptr, nullptr);
}

std::vector<LocalInterface> localInterfaces()
{
    std::vector<LocalInterface> out;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf(size);
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG rc = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (rc != NO_ERROR)
        return out;
    for (const IP_ADAPTER_ADDRESSES* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp)
            continue;
        const bool loop = a->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
        for (const IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            const sockaddr_in* sin = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
            LocalInterface n;
            n.name = a->FriendlyName ? bslplat::wideToUtf8(a->FriendlyName) : std::string();
            n.loopback = loop;
            n.ipHost = ntohl(sin->sin_addr.s_addr);
            const int prefix = std::clamp<int>(u->OnLinkPrefixLength, 0, 32);
            n.maskHost = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
            if (a->PhysicalAddressLength == 6) {
                std::memcpy(n.mac.data(), a->PhysicalAddress, 6);
                n.hasMac = true;
            }
            out.push_back(std::move(n));
        }
    }
    return out;
}

#else

bool startup()
{
    return true;
}

void cleanup() {}

void close(Handle s)
{
    ::close(s);
}

void shutdownSend(Handle s)
{
    ::shutdown(s, SHUT_WR);
}

int lastError()
{
    return errno;
}

bool isAddrInUse(int code)
{
    return code == EADDRINUSE;
}

bool isAccessDenied(int code)
{
    return code == EACCES;
}

void setNonBlocking(Handle s)
{
    const int fl = fcntl(s, F_GETFL, 0);
    if (fl >= 0)
        fcntl(s, F_SETFL, fl | O_NONBLOCK);
}

void setTimeoutsMs(Handle s, int recvMs, int sendMs)
{
    timeval rt{recvMs / 1000, (recvMs % 1000) * 1000};
    timeval st{sendMs / 1000, (sendMs % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof(rt));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &st, sizeof(st));
}

void disableUdpConnReset(Handle) {}

std::vector<LocalInterface> localInterfaces()
{
    std::vector<LocalInterface> out;
    ifaddrs* head = nullptr;
    if (getifaddrs(&head) != 0)
        return out;
    for (const ifaddrs* a = head; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !(a->ifa_flags & IFF_UP))
            continue;
        LocalInterface n;
        n.name = a->ifa_name ? a->ifa_name : "";
        n.loopback = (a->ifa_flags & IFF_LOOPBACK) != 0;
        n.ipHost = ntohl(reinterpret_cast<const sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr);
        if (a->ifa_netmask)
            n.maskHost = ntohl(reinterpret_cast<const sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr);
        for (const ifaddrs* b = head; b; b = b->ifa_next) {
            if (!b->ifa_addr || !b->ifa_name || n.name != b->ifa_name)
                continue;
#ifdef __linux__
            if (b->ifa_addr->sa_family == AF_PACKET) {
                const auto* ll = reinterpret_cast<const sockaddr_ll*>(b->ifa_addr);
                if (ll->sll_halen == 6) {
                    std::memcpy(n.mac.data(), ll->sll_addr, 6);
                    n.hasMac = true;
                }
            }
#else
            if (b->ifa_addr->sa_family == AF_LINK) {
                const auto* dl = reinterpret_cast<const sockaddr_dl*>(b->ifa_addr);
                if (dl->sdl_alen == 6) {
                    std::memcpy(n.mac.data(), LLADDR(dl), 6);
                    n.hasMac = true;
                }
            }
#endif
        }
        out.push_back(std::move(n));
    }
    freeifaddrs(head);
    return out;
}

#endif

void setReuseAddr(Handle s)
{
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));
}

void setBroadcast(Handle s)
{
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on));
}

void setRecvBuffer(Handle s, int bytes)
{
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bytes), sizeof(bytes));
}

int selectArg(Handle maxHandle)
{
#ifdef _WIN32
    (void)maxHandle;
    return 0;
#else
    return static_cast<int>(maxHandle) + 1;
#endif
}

}
