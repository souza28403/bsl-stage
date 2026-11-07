#include "dmx/DmxNet.h"

#include "app/Log.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <set>

namespace bsl::dmx {

namespace {

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

std::string wideToUtf8(const wchar_t* w)
{
    if (!w || !*w)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
        return {};
    std::string out(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::uint32_t hostIp(const sockaddr_in& a)
{
    return ntohl(a.sin_addr.s_addr);
}

sockaddr_in makeAddr(std::uint32_t ipHost, std::uint16_t port)
{
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(ipHost);
    return a;
}

bool parseIp(const std::string& text, std::uint32_t& ipHost)
{
    in_addr addr{};
    if (text.empty() || inet_pton(AF_INET, text.c_str(), &addr) != 1)
        return false;
    ipHost = ntohl(addr.s_addr);
    return true;
}

std::array<std::uint8_t, 4> ipBytes(std::uint32_t ipHost)
{
    return {static_cast<std::uint8_t>(ipHost >> 24), static_cast<std::uint8_t>((ipHost >> 16) & 0xFF),
            static_cast<std::uint8_t>((ipHost >> 8) & 0xFF), static_cast<std::uint8_t>(ipHost & 0xFF)};
}

void setNonBlocking(SOCKET s)
{
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
}

SOCKET openUdp(std::uint32_t bindIpHost, std::uint16_t port, bool broadcast, std::string* error)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        if (error)
            *error = "socket(): erro " + std::to_string(WSAGetLastError());
        return INVALID_SOCKET;
    }
    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    if (broadcast) {
        BOOL on = TRUE;
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on));
    }
    int rcvbuf = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf));
    DWORD bytes = 0;
    BOOL newBehavior = FALSE;
    WSAIoctl(s, SIO_UDP_CONNRESET, &newBehavior, sizeof(newBehavior), nullptr, 0, &bytes, nullptr, nullptr);
    sockaddr_in a = makeAddr(bindIpHost, port);
    if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        if (error)
            *error = "bind(" + ipToString(bindIpHost) + ":" + std::to_string(port) + "): erro " + std::to_string(WSAGetLastError());
        closesocket(s);
        return INVALID_SOCKET;
    }
    setNonBlocking(s);
    return s;
}

}

std::vector<NetInterface> listInterfaces(bool includeLoopback)
{
    std::vector<NetInterface> out;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf(size);
    ULONG rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                    reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                  reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (rc == NO_ERROR) {
        for (const IP_ADAPTER_ADDRESSES* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp)
                continue;
            const bool loop = a->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
            if (loop && !includeLoopback)
                continue;
            for (const IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
                if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET)
                    continue;
                const sockaddr_in* sin = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
                NetInterface n;
                n.name = wideToUtf8(a->FriendlyName);
                n.loopback = loop;
                n.ipHost = ntohl(sin->sin_addr.s_addr);
                n.ip = ipToString(n.ipHost);
                const int prefix = std::clamp<int>(u->OnLinkPrefixLength, 0, 32);
                n.maskHost = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
                n.mask = ipToString(n.maskHost);
                if (a->PhysicalAddressLength == 6)
                    std::memcpy(n.mac.data(), a->PhysicalAddress, 6);
                if (n.ipHost == 0 || (n.ipHost >> 16) == 0xA9FE)
                    continue;
                out.push_back(std::move(n));
            }
        }
    }
    if (includeLoopback && std::none_of(out.begin(), out.end(), [](const NetInterface& n) { return n.loopback; })) {
        NetInterface n;
        n.name = "Loopback";
        n.loopback = true;
        n.ipHost = 0x7F000001u;
        n.ip = "127.0.0.1";
        n.maskHost = 0xFF000000u;
        n.mask = "255.0.0.0";
        out.push_back(std::move(n));
    }
    return out;
}

nlohmann::json netConfigToJson(const NetConfig& c)
{
    return {{"enabled", c.enabled},
            {"interface", c.interfaceIp},
            {"universes", c.universesText},
            {"artNet", c.artNet},
            {"sacn", c.sacn},
            {"merge", c.merge == MergeMode::Ltp ? "ltp" : "htp"},
            {"timeout", c.timeoutSec},
            {"onLoss", c.loss == LossPolicy::Release ? "release" : "hold"},
            {"artNetFirstUniverse", c.artNetFirstUniverse},
            {"artNetPriority", c.artNetPriority},
            {"nodeName", c.nodeName}};
}

NetConfig netConfigFromJson(const nlohmann::json& j)
{
    NetConfig c;
    if (!j.is_object())
        return c;
    c.enabled = j.value("enabled", false);
    c.interfaceIp = j.value("interface", std::string());
    c.universesText = j.value("universes", std::string("1-4"));
    if (c.universesText.size() > 256)
        c.universesText.resize(256);
    c.artNet = j.value("artNet", true);
    c.sacn = j.value("sacn", true);
    c.merge = j.value("merge", std::string("htp")) == "ltp" ? MergeMode::Ltp : MergeMode::Htp;
    c.timeoutSec = std::clamp(j.value("timeout", 2.5), 0.5, 60.0);
    c.loss = j.value("onLoss", std::string("hold")) == "release" ? LossPolicy::Release : LossPolicy::Hold;
    c.artNetFirstUniverse = std::clamp(j.value("artNetFirstUniverse", 0), 0, 1);
    c.artNetPriority = static_cast<std::uint8_t>(std::clamp(j.value("artNetPriority", 100), 0, 200));
    c.nodeName = j.value("nodeName", std::string("BSL Stage"));
    if (c.nodeName.empty() || c.nodeName.size() > 17)
        c.nodeName = c.nodeName.empty() ? "BSL Stage" : c.nodeName.substr(0, 17);
    return c;
}

const UniverseView* Snapshot::find(int universe) const
{
    for (const UniverseView& u : universes)
        if (u.universe == universe)
            return &u;
    return nullptr;
}

UniverseView* Snapshot::find(int universe)
{
    for (UniverseView& u : universes)
        if (u.universe == universe)
            return &u;
    return nullptr;
}

bool Snapshot::receiving(double seconds) const
{
    for (const UniverseView& u : universes)
        if (u.lastFrame > 0.0 && now - u.lastFrame <= seconds)
            return true;
    return false;
}

struct Receiver::Impl
{
    NetConfig cfg;
    std::vector<int> universes;
    std::set<int> universeSet;
    MergeConfig mergeCfg;
    std::uint16_t artPort = kArtNetPort;
    std::uint16_t sacnPort = kSacnPort;

    SOCKET art = INVALID_SOCKET;
    std::vector<SOCKET> sacn;
    bool wsaStarted = false;

    NetInterface iface;
    bool hasIface = false;
    bool filterBySubnet = false;
    std::vector<NetInterface> allIfaces;

    std::thread thread;
    std::atomic<bool> stopFlag{false};

    std::mutex m;
    struct Rate
    {
        std::uint64_t windowCount = 0;
        double windowStart = 0.0;
        float pps = 0.0f;
    };
    std::map<int, UniverseMerger> mergers;
    std::map<int, Rate> rates;
    std::string status;
    bool artOk = false, sacnOk = false;
    int joined = 0, failed = 0;
    std::uint64_t totalPackets = 0, polls = 0, replies = 0;

    void run();
    void handleArt(const std::uint8_t* p, std::size_t n, const sockaddr_in& from, double now);
    void handleSacn(const std::uint8_t* p, std::size_t n, const sockaddr_in& from, double now);
    void deliver(const DmxFrame& frame, int universe, std::uint32_t ip, double now);
    void sendPollReplies(const sockaddr_in& to);
    void tickAll(double now);
    bool acceptSource(std::uint32_t ip) const;
    std::uint32_t localIpFor(std::uint32_t peerIp) const;
    int portAddressOf(int universe) const { return universe - 1 + cfg.artNetFirstUniverse; }
    int universeOfPort(int portAddress) const { return portAddress + 1 - cfg.artNetFirstUniverse; }
};

Receiver::Receiver() : impl_(std::make_unique<Impl>()) {}

Receiver::~Receiver()
{
    stop();
}

double Receiver::clock()
{
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count() + 1.0;
}

void Receiver::setPorts(std::uint16_t artNetPort, std::uint16_t sacnPort)
{
    artPort_ = artNetPort;
    sacnPort_ = sacnPort;
}

bool Receiver::start(const NetConfig& config, const std::vector<int>& extraUniverses, std::string* error)
{
    stop();
    impl_ = std::make_unique<Impl>();
    Impl& d = *impl_;
    d.cfg = config;
    d.artPort = artPort_;
    d.sacnPort = sacnPort_;

    auto fail = [&](const std::string& msg) {
        if (error)
            *error = msg;
        d.status = msg;
        return false;
    };

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return fail("Winsock não iniciou");
    d.wsaStarted = true;

    d.universes = parseUniverseList(config.universesText);
    for (int u : extraUniverses)
        if (u >= 1 && u <= kMaxUniverse && std::find(d.universes.begin(), d.universes.end(), u) == d.universes.end())
            d.universes.push_back(u);
    std::sort(d.universes.begin(), d.universes.end());
    d.universeSet.insert(d.universes.begin(), d.universes.end());

    d.mergeCfg.mode = config.merge;
    d.mergeCfg.timeoutSec = config.timeoutSec;
    d.mergeCfg.loss = config.loss;
    d.mergeCfg.artNetPriority = config.artNetPriority;
    for (int u : d.universes)
        d.mergers[u].configure(d.mergeCfg);

    d.allIfaces = listInterfaces(true);
    std::string ifaceNote;
    if (!config.interfaceIp.empty()) {
        std::uint32_t want = 0;
        if (parseIp(config.interfaceIp, want)) {
            for (const NetInterface& n : d.allIfaces)
                if (n.ipHost == want) {
                    d.iface = n;
                    d.hasIface = true;
                }
        }
        if (!d.hasIface)
            ifaceNote = " (placa " + config.interfaceIp + " não encontrada: escutando em todas)";
    }
    d.filterBySubnet = d.hasIface && !d.iface.loopback;
    const std::uint32_t bindIp = (d.hasIface && d.iface.loopback) ? d.iface.ipHost : 0u;

    std::string artErr, sacnErr;
    if (config.artNet) {
        d.art = openUdp(bindIp, d.artPort, true, &artErr);
        d.artOk = d.art != INVALID_SOCKET;
    }
    if (config.sacn) {
        constexpr std::size_t kPerSocket = 16;
        const std::size_t sockets = std::max<std::size_t>(1, (d.universes.size() + kPerSocket - 1) / kPerSocket);
        for (std::size_t s = 0; s < sockets; ++s) {
            SOCKET sock = openUdp(bindIp, d.sacnPort, false, s == 0 ? &sacnErr : nullptr);
            if (sock == INVALID_SOCKET)
                break;
            d.sacn.push_back(sock);
            const bool multicast = !(d.hasIface && d.iface.loopback);
            if (!multicast)
                continue;
            for (std::size_t i = s * kPerSocket; i < std::min(d.universes.size(), (s + 1) * kPerSocket); ++i) {
                bool any = false;
                auto join = [&](std::uint32_t ifaceIp) {
                    ip_mreq req{};
                    req.imr_multiaddr.s_addr = htonl(sacnMulticastAddress(d.universes[i]));
                    req.imr_interface.s_addr = htonl(ifaceIp);
                    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&req), sizeof(req)) == 0)
                        any = true;
                };
                if (d.hasIface) {
                    join(d.iface.ipHost);
                } else {
                    for (const NetInterface& n : d.allIfaces)
                        if (!n.loopback)
                            join(n.ipHost);
                    if (!any)
                        join(0u);
                }
                (any ? d.joined : d.failed) += 1;
            }
        }
        d.sacnOk = !d.sacn.empty();
    }

    if (!d.artOk && !d.sacnOk) {
        std::string msg = "Não foi possível abrir as portas";
        if (!artErr.empty())
            msg += " - Art-Net: " + artErr;
        if (!sacnErr.empty())
            msg += " - sACN: " + sacnErr;
        for (SOCKET s : d.sacn)
            closesocket(s);
        d.sacn.clear();
        if (d.art != INVALID_SOCKET)
            closesocket(d.art);
        d.art = INVALID_SOCKET;
        WSACleanup();
        d.wsaStarted = false;
        return fail(msg);
    }

    std::string where = d.hasIface ? d.iface.ip + " (" + d.iface.name + ")" : std::string("todas as placas");
    d.status = std::string("Escutando ") + std::to_string(d.universes.size()) + " universo(s) em " + where;
    if (config.artNet)
        d.status += d.artOk ? " | Art-Net " + std::to_string(d.artPort) : " | Art-Net indisponível";
    if (config.sacn)
        d.status += d.sacnOk ? " | sACN " + std::to_string(d.sacnPort) : " | sACN indisponível";
    d.status += ifaceNote;
    if (!artErr.empty() && config.artNet)
        d.status += " [" + artErr + "]";
    if (!sacnErr.empty() && config.sacn)
        d.status += " [" + sacnErr + "]";

    d.stopFlag = false;
    d.thread = std::thread([&d] { d.run(); });
    running_ = true;
    log::info("DMX: " + d.status);
    return true;
}

void Receiver::stop()
{
    Impl& d = *impl_;
    if (d.thread.joinable()) {
        d.stopFlag = true;
        d.thread.join();
    }
    for (SOCKET s : d.sacn)
        closesocket(s);
    d.sacn.clear();
    if (d.art != INVALID_SOCKET) {
        closesocket(d.art);
        d.art = INVALID_SOCKET;
    }
    if (d.wsaStarted) {
        WSACleanup();
        d.wsaStarted = false;
    }
    running_ = false;
}

bool Receiver::poll(Snapshot& out)
{
    Impl& d = *impl_;
    std::unique_lock<std::mutex> lock(d.m, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    out.running = running_.load();
    out.now = clock();
    out.status = d.status;
    out.artNetOk = d.artOk;
    out.sacnOk = d.sacnOk;
    out.multicastJoined = d.joined;
    out.multicastFailed = d.failed;
    out.totalPackets = d.totalPackets;
    out.artPolls = d.polls;
    out.artReplies = d.replies;
    out.universes.resize(d.mergers.size());
    std::size_t i = 0;
    for (const auto& [number, merger] : d.mergers) {
        UniverseView& v = out.universes[i++];
        v.universe = number;
        v.data = merger.output();
        v.everSeen = merger.everSeen();
        v.active = merger.active();
        v.lost = merger.lost();
        v.released = merger.lost() && d.cfg.loss == LossPolicy::Release;
        v.lastFrame = merger.lastFrame();
        v.packets = merger.packets();
        const auto it = d.rates.find(number);
        v.packetsPerSec = it != d.rates.end() ? it->second.pps : 0.0f;
        v.sources = merger.sources();
    }
    return true;
}

bool Receiver::Impl::acceptSource(std::uint32_t ip) const
{
    if (!filterBySubnet)
        return true;
    return (ip & iface.maskHost) == (iface.ipHost & iface.maskHost);
}

std::uint32_t Receiver::Impl::localIpFor(std::uint32_t peerIp) const
{
    if (hasIface)
        return iface.ipHost;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    std::uint32_t result = 0;
    if (s != INVALID_SOCKET) {
        sockaddr_in to = makeAddr(peerIp, 9);
        if (connect(s, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0) {
            sockaddr_in me{};
            int len = sizeof(me);
            if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0)
                result = hostIp(me);
        }
        closesocket(s);
    }
    if (result == 0)
        for (const NetInterface& n : allIfaces)
            if (!n.loopback) {
                result = n.ipHost;
                break;
            }
    return result;
}

void Receiver::Impl::sendPollReplies(const sockaddr_in& to)
{
    if (art == INVALID_SOCKET)
        return;
    const std::uint32_t peer = hostIp(to);
    const std::uint32_t local = localIpFor(peer);
    ArtNodeInfo base;
    base.ip = ipBytes(local);
    base.shortName = cfg.nodeName;
    base.longName = cfg.nodeName + " - VJ / media server";
    for (const NetInterface& n : allIfaces)
        if (n.ipHost == local)
            base.mac = n.mac;

    std::map<std::pair<int, int>, std::vector<int>> groups;
    for (int u : universes) {
        const int pa = portAddressOf(u);
        if (pa < 0 || pa > 32767)
            continue;
        groups[{(pa >> 8) & 0x7F, (pa >> 4) & 0x0F}].push_back(pa & 0x0F);
    }
    if (groups.empty())
        groups[{0, 0}] = {};

    bool receivingNow = false;
    {
        std::lock_guard<std::mutex> lock(m);
        const double now = clock();
        for (const auto& [number, merger] : mergers)
            if (merger.lastFrame() > 0.0 && now - merger.lastFrame() < 2.0)
                receivingNow = true;
    }

    int bind = 1;
    int sent = 0;
    for (const auto& [key, ports] : groups) {
        for (std::size_t first = 0; first < std::max<std::size_t>(ports.size(), 1); first += 4) {
            ArtNodeInfo info = base;
            info.net = key.first;
            info.sub = key.second;
            info.bindIndex = bind++;
            info.receiving = receivingNow;
            for (std::size_t i = first; i < std::min(ports.size(), first + 4); ++i)
                info.universes.push_back(ports[i]);
            info.numPortsTotal = static_cast<std::uint8_t>(std::min<std::size_t>(universes.size(), 255));
            char report[64];
            std::snprintf(report, sizeof(report), "#0001 [%04d] BSL Stage recebendo DMX (%zu universos)", sent,
                          universes.size());
            info.report = report;
            const std::vector<std::uint8_t> bytes = buildArtPollReply(info);
            sendto(art, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0,
                   reinterpret_cast<const sockaddr*>(&to), sizeof(to));
            if (hasIface && !iface.loopback) {
                const std::uint32_t bcast = (iface.ipHost & iface.maskHost) | ~iface.maskHost;
                sockaddr_in b = makeAddr(bcast, artPort);
                sendto(art, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0,
                       reinterpret_cast<const sockaddr*>(&b), sizeof(b));
            }
            ++sent;
            std::lock_guard<std::mutex> lock(m);
            ++replies;
        }
    }
}

void Receiver::Impl::deliver(const DmxFrame& frame, int universe, std::uint32_t ip, double now)
{
    if (!universeSet.count(universe))
        return;
    std::lock_guard<std::mutex> lock(m);
    auto it = mergers.find(universe);
    if (it == mergers.end())
        return;
    if (it->second.ingest(frame, ip, now)) {
        ++totalPackets;
        ++rates[universe].windowCount;
    }
}

void Receiver::Impl::handleArt(const std::uint8_t* p, std::size_t n, const sockaddr_in& from, double now)
{
    const std::uint16_t op = artNetOpCode(p, n);
    if (op == kArtOpDmx) {
        DmxFrame f;
        if (!parseArtDmx(p, n, f))
            return;
        deliver(f, universeOfPort(f.universe), hostIp(from), now);
    } else if (op == kArtOpPoll) {
        ArtPollInfo info;
        if (!parseArtPoll(p, n, info))
            return;
        {
            std::lock_guard<std::mutex> lock(m);
            ++polls;
        }
        sendPollReplies(from);
    }
}

void Receiver::Impl::handleSacn(const std::uint8_t* p, std::size_t n, const sockaddr_in& from, double now)
{
    DmxFrame f;
    if (!parseSacnData(p, n, f))
        return;
    deliver(f, f.universe, hostIp(from), now);
}

void Receiver::Impl::tickAll(double now)
{
    std::lock_guard<std::mutex> lock(m);
    for (auto& [number, merger] : mergers) {
        merger.tick(now);
        Rate& r = rates[number];
        if (r.windowStart == 0.0)
            r.windowStart = now;
        if (now - r.windowStart >= 1.0) {
            r.pps = static_cast<float>(static_cast<double>(r.windowCount) / (now - r.windowStart));
            r.windowCount = 0;
            r.windowStart = now;
        }
    }
}

void Receiver::Impl::run()
{
    std::vector<std::uint8_t> buf(2048);
    while (!stopFlag.load()) {
        fd_set rd;
        FD_ZERO(&rd);
        if (art != INVALID_SOCKET)
            FD_SET(art, &rd);
        for (SOCKET s : sacn)
            FD_SET(s, &rd);
        timeval tv{0, 40000};
        const int r = select(0, &rd, nullptr, nullptr, &tv);
        if (r > 0) {
            auto drain = [&](SOCKET s, bool isArt) {
                if (!FD_ISSET(s, &rd))
                    return;
                for (int guard = 0; guard < 256; ++guard) {
                    sockaddr_in from{};
                    int len = sizeof(from);
                    const int got = recvfrom(s, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0,
                                             reinterpret_cast<sockaddr*>(&from), &len);
                    if (got <= 0)
                        break;
                    if (!acceptSource(hostIp(from)))
                        continue;
                    const double now = clock();
                    if (isArt)
                        handleArt(buf.data(), static_cast<std::size_t>(got), from, now);
                    else
                        handleSacn(buf.data(), static_cast<std::size_t>(got), from, now);
                }
            };
            if (art != INVALID_SOCKET)
                drain(art, true);
            for (SOCKET s : sacn)
                drain(s, false);
        } else if (r < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        tickAll(clock());
    }
}

}
