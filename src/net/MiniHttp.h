#pragma once

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace bsl::net {

struct HttpRequest
{
    std::string method;
    std::string path;
    std::string query;
    std::string body;
};

struct HttpResponse
{
    int status = 200;
    std::string contentType = "text/plain; charset=utf-8";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;

    void json(std::string text, int code = 200)
    {
        status = code;
        contentType = "application/json; charset=utf-8";
        body = std::move(text);
    }
    void text(std::string text, int code = 200)
    {
        status = code;
        contentType = "text/plain; charset=utf-8";
        body = std::move(text);
    }
};

using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

class HttpServer
{
public:
    HttpServer() = default;
    ~HttpServer() { stop(); }
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    bool start(int port, HttpHandler handler, std::string* error = nullptr, bool loopbackOnly = false);
    void stop();
    bool running() const { return running_.load(); }
    int port() const { return port_; }

private:
    void acceptLoop();
    void serve(unsigned long long socketValue);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<unsigned long long> listen_{~0ull};
    int port_ = 0;
    HttpHandler handler_;

    std::mutex activeMutex_;
    std::condition_variable activeCv_;
    int active_ = 0;
};

std::string urlDecode(const std::string& s);
std::string urlEncode(const std::string& s);
std::map<std::string, std::string> parseForm(const std::string& s);
bool readWholeFile(const std::filesystem::path& file, std::string& out, size_t maxBytes = 64u << 20);
const char* mimeForExtension(const std::string& ext);

}
