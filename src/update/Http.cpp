#include "update/Http.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>

namespace bsl::update {

namespace {

std::wstring widen(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(std::max(n, 0)), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

struct Handle
{
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET handle) : h(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle()
    {
        if (h)
            WinHttpCloseHandle(h);
    }
    explicit operator bool() const { return h != nullptr; }
};

bool isOfflineError(DWORD e)
{
    switch (e) {
    case ERROR_WINHTTP_TIMEOUT:
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
    case ERROR_WINHTTP_CONNECTION_ERROR:
    case ERROR_WINHTTP_SECURE_FAILURE:
    case ERROR_WINHTTP_AUTODETECTION_FAILED:
        return true;
    default:
        return false;
    }
}

std::string describe(DWORD e)
{
    switch (e) {
    case ERROR_WINHTTP_TIMEOUT: return "tempo esgotado";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "não foi possível resolver o endereço (sem internet?)";
    case ERROR_WINHTTP_CANNOT_CONNECT: return "não foi possível conectar (sem internet?)";
    case ERROR_WINHTTP_CONNECTION_ERROR: return "conexão interrompida";
    case ERROR_WINHTTP_SECURE_FAILURE: return "falha na conexão segura (TLS)";
    default: return "erro de rede " + std::to_string(static_cast<unsigned long>(e));
    }
}

class WinHttpClient : public IHttp
{
public:
    explicit WinHttpClient(std::string userAgent) : userAgent_(std::move(userAgent)) {}

    HttpResponse get(const std::string& url, int timeoutMs, size_t maxBytes) override
    {
        HttpResponse r;
        std::string sink;
        Request req;
        if (!open(url, timeoutMs, timeoutMs, req, r))
            return r;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs * 2);
        if (!readAll(req, r, [&](const char* data, size_t n) {
                if (sink.size() + n > maxBytes) {
                    r.error = "resposta grande demais";
                    return false;
                }
                sink.append(data, n);
                return std::chrono::steady_clock::now() < deadline || (r.error = "tempo esgotado", false);
            }))
            return r;
        r.body = std::move(sink);
        return r;
    }

    HttpResponse download(const std::string& url, const std::filesystem::path& dest, const ProgressFn& progress) override
    {
        HttpResponse r;
        Request req;
        if (!open(url, 10000, 30000, req, r))
            return r;
        if (r.status < 200 || r.status >= 300) {
            r.error = "o servidor respondeu " + std::to_string(r.status);
            return r;
        }
        std::error_code ec;
        std::filesystem::create_directories(dest.parent_path(), ec);
        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        if (!out) {
            r.error = "não foi possível gravar o arquivo";
            return r;
        }
        int64_t done = 0;
        bool stopped = false;
        const bool ok = readAll(req, r, [&](const char* data, size_t n) {
            out.write(data, static_cast<std::streamsize>(n));
            if (!out) {
                r.error = "falha ao gravar no disco (sem espaço?)";
                return false;
            }
            done += static_cast<int64_t>(n);
            if (progress && !progress(done, req.contentLength)) {
                r.cancelled = true;
                r.error = "cancelado";
                stopped = true;
                return false;
            }
            return true;
        });
        out.close();
        if (ok && req.contentLength > 0 && done != req.contentLength) {
            r.error = "download incompleto";
        }
        if (!ok || !r.error.empty() || stopped) {
            std::filesystem::remove(dest, ec);
            return r;
        }
        if (progress)
            progress(done, req.contentLength > 0 ? req.contentLength : done);
        return r;
    }

private:
    struct Request
    {
        Handle session, connect, request;
        int64_t contentLength = 0;
    };

    bool open(const std::string& url, int connectMs, int receiveMs, Request& req, HttpResponse& r)
    {
        const std::wstring wurl = widen(url);
        URL_COMPONENTSW uc{};
        uc.dwStructSize = sizeof(uc);
        uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
            r.error = "endereço inválido (só https)";
            return false;
        }
        const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
        const std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength + uc.dwExtraInfoLength);

        const std::wstring ua = widen(userAgent_);
        req.session.h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
        if (!req.session)
            req.session.h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS, 0);
        if (!req.session) {
            r.error = "WinHTTP indisponível";
            return false;
        }
        WinHttpSetTimeouts(req.session.h, connectMs, connectMs, connectMs, receiveMs);
        req.connect.h = WinHttpConnect(req.session.h, host.c_str(), uc.nPort, 0);
        if (!req.connect) {
            fail(r, GetLastError());
            return false;
        }
        req.request.h = WinHttpOpenRequest(req.connect.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!req.request) {
            fail(r, GetLastError());
            return false;
        }
        DWORD disable = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
        WinHttpSetOption(req.request.h, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
        DWORD redirects = 8;
        WinHttpSetOption(req.request.h, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &redirects, sizeof(redirects));

        const wchar_t* headers = host == L"api.github.com"
                                     ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n"
                                     : L"Accept: application/octet-stream\r\n";
        if (!WinHttpSendRequest(req.request.h, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            || !WinHttpReceiveResponse(req.request.h, nullptr)) {
            fail(r, GetLastError());
            return false;
        }
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(req.request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX);
        r.status = static_cast<int>(status);
        wchar_t lenBuf[32] = {};
        size = sizeof(lenBuf);
        if (WinHttpQueryHeaders(req.request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &size,
                                WINHTTP_NO_HEADER_INDEX))
            req.contentLength = _wtoi64(lenBuf);
        if (r.status == 403 || r.status == 429)
            r.error = "limite de consultas do GitHub atingido; tente mais tarde";
        else if (r.status == 404)
            r.error = "nada publicado neste endereço (404)";
        else if (r.status < 200 || r.status >= 300)
            r.error = "o servidor respondeu " + std::to_string(r.status);
        if (!r.error.empty() && r.status != 0) {
            return false;
        }
        return true;
    }

    template <class Sink>
    bool readAll(Request& req, HttpResponse& r, Sink&& sink)
    {
        std::string chunk;
        while (true) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req.request.h, &avail)) {
                fail(r, GetLastError());
                return false;
            }
            if (avail == 0)
                return true;
            chunk.resize(std::min<DWORD>(avail, 256 * 1024));
            DWORD got = 0;
            if (!WinHttpReadData(req.request.h, chunk.data(), static_cast<DWORD>(chunk.size()), &got)) {
                fail(r, GetLastError());
                return false;
            }
            if (got == 0)
                return true;
            if (!sink(chunk.data(), got))
                return false;
        }
    }

    static void fail(HttpResponse& r, DWORD e)
    {
        r.offline = isOfflineError(e);
        r.error = describe(e);
    }

    std::string userAgent_;
};

}

std::unique_ptr<IHttp> makeWinHttp(const std::string& userAgent)
{
    return std::make_unique<WinHttpClient>(userAgent);
}

void FakeHttp::addBody(const std::string& url, std::string body, int status)
{
    Entry e;
    e.body = std::move(body);
    e.status = status;
    entries_[url] = std::move(e);
}

void FakeHttp::addFile(const std::string& url, const std::filesystem::path& file)
{
    Entry e;
    e.file = file;
    entries_[url] = std::move(e);
}

void FakeHttp::addOffline(const std::string& url)
{
    Entry e;
    e.offline = true;
    entries_[url] = std::move(e);
}

HttpResponse FakeHttp::get(const std::string& url, int, size_t maxBytes)
{
    requests.push_back(url);
    HttpResponse r;
    const auto it = entries_.find(url);
    if (it == entries_.end()) {
        r.status = 404;
        r.error = "nada publicado neste endereço (404)";
        return r;
    }
    const Entry& e = it->second;
    if (e.offline) {
        r.offline = true;
        r.error = "não foi possível conectar (sem internet?)";
        return r;
    }
    std::string body = e.body;
    if (!e.file.empty()) {
        std::ifstream in(e.file, std::ios::binary);
        body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    r.status = e.status;
    if (body.size() > maxBytes) {
        r.error = "resposta grande demais";
        return r;
    }
    r.body = std::move(body);
    if (r.status < 200 || r.status >= 300)
        r.error = "o servidor respondeu " + std::to_string(r.status);
    return r;
}

HttpResponse FakeHttp::download(const std::string& url, const std::filesystem::path& dest, const ProgressFn& progress)
{
    requests.push_back(url);
    HttpResponse r;
    const auto it = entries_.find(url);
    if (it == entries_.end()) {
        r.status = 404;
        r.error = "nada publicado neste endereço (404)";
        return r;
    }
    const Entry& e = it->second;
    if (e.offline) {
        r.offline = true;
        r.error = "não foi possível conectar (sem internet?)";
        return r;
    }
    std::string data = e.body;
    if (!e.file.empty()) {
        std::ifstream in(e.file, std::ios::binary);
        if (!in) {
            r.status = 404;
            r.error = "arquivo de teste ausente";
            return r;
        }
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) {
        r.error = "não foi possível gravar o arquivo";
        return r;
    }
    const int chunks = std::max(1, chunks_);
    const size_t step = std::max<size_t>(1, (data.size() + static_cast<size_t>(chunks) - 1) / static_cast<size_t>(chunks));
    size_t pos = 0;
    while (pos < data.size()) {
        const size_t n = std::min(step, data.size() - pos);
        out.write(data.data() + pos, static_cast<std::streamsize>(n));
        pos += n;
        if (delayMs_ > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs_));
        if (progress && !progress(static_cast<int64_t>(pos), static_cast<int64_t>(data.size()))) {
            out.close();
            std::filesystem::remove(dest, ec);
            r.cancelled = true;
            r.error = "cancelado";
            return r;
        }
    }
    r.status = e.status;
    return r;
}

}
