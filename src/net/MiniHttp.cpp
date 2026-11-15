#include "net/MiniHttp.h"

#include "app/Log.h"

#include "platform/Socket.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace bsl::net {

namespace {

constexpr size_t kMaxHeader = 64 * 1024;
constexpr size_t kMaxBody = 8u << 20;

std::mutex g_wsaMutex;
int g_wsaUsers = 0;

bool wsaAcquire()
{
    std::lock_guard lk(g_wsaMutex);
    if (g_wsaUsers == 0) {
        if (!sock::startup())
            return false;
    }
    ++g_wsaUsers;
    return true;
}

void wsaRelease()
{
    std::lock_guard lk(g_wsaMutex);
    if (g_wsaUsers > 0 && --g_wsaUsers == 0)
        sock::cleanup();
}

void sendAll(sock::Handle s, const char* data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        const int n = send(s, data + sent, static_cast<int>(std::min<size_t>(len - sent, 1u << 20)), sock::kSendFlags);
        if (n <= 0)
            return;
        sent += static_cast<size_t>(n);
    }
}

const char* reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    default: return "OK";
    }
}

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void respond(sock::Handle s, const HttpResponse& r)
{
    std::string h = "HTTP/1.1 " + std::to_string(r.status) + " " + reason(r.status) + "\r\n";
    if (r.status != 204 && !r.contentType.empty())
        h += "Content-Type: " + r.contentType + "\r\n";
    h += "Content-Length: " + std::to_string(r.status == 204 ? 0 : r.body.size()) + "\r\n";
    h += "Access-Control-Allow-Origin: *\r\n";
    h += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    h += "Access-Control-Allow-Headers: *, Content-Type\r\n";
    h += "Access-Control-Allow-Private-Network: true\r\n";
    h += "Access-Control-Max-Age: 600\r\n";
    h += "Cache-Control: no-cache, no-store, must-revalidate\r\n";
    for (const auto& [k, v] : r.headers)
        h += k + ": " + v + "\r\n";
    h += "Connection: close\r\n\r\n";
    sendAll(s, h.data(), h.size());
    if (r.status != 204 && !r.body.empty())
        sendAll(s, r.body.data(), r.body.size());
}

}

bool HttpServer::start(int port, HttpHandler handler, std::string* error, bool loopbackOnly)
{
    stop();
    if (port < 1 || port > 65535) {
        if (error)
            *error = "porta inválida";
        return false;
    }
    if (!wsaAcquire()) {
        if (error)
            *error = "Winsock indisponível";
        return false;
    }
    sock::Handle ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == sock::kInvalid) {
        wsaRelease();
        if (error)
            *error = "não foi possível criar o socket";
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(ls, 16) != 0) {
        const int code = sock::lastError();
        sock::close(ls);
        wsaRelease();
        if (error)
            *error = sock::isAddrInUse(code) ? "porta " + std::to_string(port) + " ocupada"
                                           : "não foi possível escutar na porta " + std::to_string(port);
        return false;
    }
    handler_ = std::move(handler);
    port_ = port;
    listen_.store(static_cast<unsigned long long>(ls));
    running_.store(true);
    thread_ = std::thread([this] { acceptLoop(); });
    return true;
}

void HttpServer::stop()
{
    if (!running_.exchange(false)) {
        if (thread_.joinable())
            thread_.join();
        return;
    }
    if (thread_.joinable())
        thread_.join();
    const unsigned long long ls = listen_.exchange(~0ull);
    if (ls != ~0ull)
        sock::close(static_cast<sock::Handle>(ls));
    {
        std::unique_lock lk(activeMutex_);
        activeCv_.wait_for(lk, std::chrono::seconds(6), [this] { return active_ == 0; });
    }
    wsaRelease();
    handler_ = nullptr;
}

void HttpServer::acceptLoop()
{
    const sock::Handle ls = static_cast<sock::Handle>(listen_.load());
    while (running_.load()) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(ls, &rd);
        timeval tv{0, 100000};
        const int r = select(sock::selectArg(ls), &rd, nullptr, nullptr, &tv);
        if (r <= 0)
            continue;
        sock::Handle c = accept(ls, nullptr, nullptr);
        if (c == sock::kInvalid)
            continue;
        {
            std::lock_guard lk(activeMutex_);
            ++active_;
        }
        std::thread([this, c] {
            serve(static_cast<unsigned long long>(c));
            std::lock_guard lk(activeMutex_);
            --active_;
            activeCv_.notify_all();
        }).detach();
    }
}

void HttpServer::serve(unsigned long long socketValue)
{
    sock::Handle s = static_cast<sock::Handle>(socketValue);
    sock::setTimeoutsMs(s, 3000, 8000);

    auto finish = [&] {
        sock::shutdownSend(s);
        sock::close(s);
    };

    std::string req;
    char buf[16384];
    size_t headerEnd = std::string::npos;
    while ((headerEnd = req.find("\r\n\r\n")) == std::string::npos) {
        const int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) {
            finish();
            return;
        }
        req.append(buf, static_cast<size_t>(n));
        if (req.size() > kMaxHeader && req.find("\r\n\r\n") == std::string::npos) {
            finish();
            return;
        }
    }
    const std::string head = req.substr(0, headerEnd);
    size_t contentLength = 0;
    {
        const std::string low = lower(head);
        const size_t p = low.find("\r\ncontent-length:");
        if (p != std::string::npos)
            contentLength = static_cast<size_t>(std::strtoull(low.c_str() + p + 17, nullptr, 10));
    }
    if (contentLength > kMaxBody) {
        HttpResponse r;
        r.json("{\"ok\":false,\"error\":\"too large\"}", 413);
        respond(s, r);
        finish();
        return;
    }
    const size_t needed = headerEnd + 4 + contentLength;
    while (req.size() < needed) {
        const int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        req.append(buf, static_cast<size_t>(n));
    }

    HttpRequest request;
    const size_t sp1 = head.find(' ');
    const size_t sp2 = sp1 == std::string::npos ? sp1 : head.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
        HttpResponse r;
        r.text("bad request", 400);
        respond(s, r);
        finish();
        return;
    }
    request.method = head.substr(0, sp1);
    std::string target = head.substr(sp1 + 1, sp2 - sp1 - 1);
    if (const size_t hash = target.find('#'); hash != std::string::npos)
        target.resize(hash);
    if (const size_t q = target.find('?'); q != std::string::npos) {
        request.query = target.substr(q + 1);
        target.resize(q);
    }
    request.path = target;
    if (req.size() > headerEnd + 4)
        request.body = req.substr(headerEnd + 4, contentLength);

    HttpResponse response;
    if (request.method == "OPTIONS") {
        response.status = 204;
    } else {
        try {
            if (handler_)
                handler_(request, response);
        } catch (const std::exception& e) {
            response.json(std::string("{\"ok\":false,\"error\":\"") + "internal" + "\"}", 500);
            log::warn(std::string("HTTP: o tratador falhou: ") + e.what());
        }
    }
    respond(s, response);
    finish();
}

std::string urlDecode(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out.push_back(' ');
        } else if (s[i] == '%' && i + 2 < s.size() + 0 && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
            out.push_back(static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2])));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::string urlEncode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

std::map<std::string, std::string> parseForm(const std::string& s)
{
    std::map<std::string, std::string> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t amp = s.find('&', pos);
        if (amp == std::string::npos)
            amp = s.size();
        const std::string pair = s.substr(pos, amp - pos);
        if (!pair.empty()) {
            const size_t eq = pair.find('=');
            if (eq == std::string::npos)
                out[urlDecode(pair)] = "";
            else
                out[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
        }
        pos = amp + 1;
    }
    return out;
}

bool readWholeFile(const std::filesystem::path& file, std::string& out, size_t maxBytes)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0 || static_cast<size_t>(size) > maxBytes)
        return false;
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0)
        in.read(out.data(), size);
    return static_cast<bool>(in) || size == 0;
}

const char* mimeForExtension(const std::string& ext)
{
    if (ext == ".html" || ext == ".htm")
        return "text/html; charset=utf-8";
    if (ext == ".js" || ext == ".mjs")
        return "text/javascript; charset=utf-8";
    if (ext == ".css")
        return "text/css; charset=utf-8";
    if (ext == ".json")
        return "application/json; charset=utf-8";
    if (ext == ".svg")
        return "image/svg+xml";
    if (ext == ".png")
        return "image/png";
    if (ext == ".jpg" || ext == ".jpeg")
        return "image/jpeg";
    if (ext == ".webp")
        return "image/webp";
    if (ext == ".woff2")
        return "font/woff2";
    return "application/octet-stream";
}

}
