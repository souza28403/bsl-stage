#pragma once

// Servidor HTTP minimo (Winsock, HTTP/1.1, uma resposta por conexao) para o "Controle remoto pela rede" dos plugins BSL
// Light nativos (Slice Refiner 3, WebBackgrounds, WebMotion): serve o painel web e a API JSON para tablet/celular, como
// os plugins FFGL originais. Sem dependencias alem do Winsock; cada conexao roda numa thread curta.

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
    std::string method; // "GET", "POST", "OPTIONS"...
    std::string path;   // sem a query, ja sem o '#'
    std::string query;  // depois do '?'
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

// Chamado numa thread do servidor (varias ao mesmo tempo): o handler cuida da propria exclusao mutua
using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

class HttpServer
{
public:
    HttpServer() = default;
    ~HttpServer() { stop(); }
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Escuta em todas as interfaces (ou so 127.0.0.1). false se a porta estiver ocupada (erro em 'error').
    bool start(int port, HttpHandler handler, std::string* error = nullptr, bool loopbackOnly = false);
    // Para de aceitar, espera as conexoes em andamento terminarem e fecha. Seguro chamar de novo.
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

// --- Utilidades ---
std::string urlDecode(const std::string& s); // %XX e '+' -> ' '
std::string urlEncode(const std::string& s);
// "a=1&b=x%20y" (query ou corpo application/x-www-form-urlencoded)
std::map<std::string, std::string> parseForm(const std::string& s);
bool readWholeFile(const std::filesystem::path& file, std::string& out, size_t maxBytes = 64u << 20);
const char* mimeForExtension(const std::string& ext); // com ponto, minusculas

} // namespace bsl::net
