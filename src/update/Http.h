#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bsl::update {

struct HttpResponse
{
    int status = 0;
    std::string body;
    std::string error;
    bool offline = false;
    bool cancelled = false;
    bool ok() const { return status >= 200 && status < 300 && error.empty(); }
};

using ProgressFn = std::function<bool(int64_t done, int64_t total)>;

class IHttp
{
public:
    virtual ~IHttp() = default;
    virtual HttpResponse get(const std::string& url, int timeoutMs, size_t maxBytes) = 0;
    virtual HttpResponse download(const std::string& url, const std::filesystem::path& dest, const ProgressFn& progress) = 0;
};

std::unique_ptr<IHttp> makeWinHttp(const std::string& userAgent);

class FakeHttp : public IHttp
{
public:
    void addBody(const std::string& url, std::string body, int status = 200);
    void addFile(const std::string& url, const std::filesystem::path& file);
    void addOffline(const std::string& url);
    void setChunks(int chunks) { chunks_ = chunks; }
    void setChunkDelayMs(int ms) { delayMs_ = ms; }

    HttpResponse get(const std::string& url, int timeoutMs, size_t maxBytes) override;
    HttpResponse download(const std::string& url, const std::filesystem::path& dest, const ProgressFn& progress) override;

    std::vector<std::string> requests;

private:
    struct Entry
    {
        std::string body;
        std::filesystem::path file;
        int status = 200;
        bool offline = false;
    };
    std::map<std::string, Entry> entries_;
    int chunks_ = 4;
    int delayMs_ = 0;
};

}
