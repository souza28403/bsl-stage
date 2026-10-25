#pragma once

// Lado do worker do canal de controle: named pipe (I/O sobreposta) com JSON por linha.
// Uma thread le e enfileira; send() pode ser chamado de qualquer thread.

#include <nlohmann/json.hpp>

#include <windows.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace bsl::worker {

using Json = nlohmann::json;

class PipeChannel
{
public:
    PipeChannel() = default;
    ~PipeChannel();
    PipeChannel(const PipeChannel&) = delete;
    PipeChannel& operator=(const PipeChannel&) = delete;

    // Conecta ao pipe criado pelo app (tenta por alguns segundos) e inicia a thread de leitura.
    // 'wake' e sinalizado quando chega mensagem ou o pipe cai.
    bool connect(const std::wstring& pipeName, HANDLE wake, std::string* error);

    // Envia uma mensagem; false se o pipe caiu.
    bool send(const Json& msg);
    void log(const char* level, const std::string& text);

    // Proxima mensagem recebida (nao bloqueia)
    bool poll(Json& out);
    bool broken() const { return broken_.load(); }

private:
    void readLoop();

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE wake_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    std::thread reader_;
    std::mutex writeMutex_;
    std::mutex queueMutex_;
    std::deque<Json> queue_;
    std::atomic<bool> broken_{false};
};

} // namespace bsl::worker
