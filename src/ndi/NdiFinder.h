#pragma once

// Lista de fontes NDI da rede, atualizada numa thread de fundo (a UI só lê o último resultado).

#include "ndi/NdiRuntime.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bsl::ndi {

struct SourceInfo
{
    std::string name; // "MAQUINA (Nome)"
    std::string url;  // endereço (pode vir vazio)
    bool operator==(const SourceInfo&) const = default;
};

class Finder
{
public:
    explicit Finder(Runtime& runtime);
    ~Finder(); // para a thread
    Finder(const Finder&) = delete;
    Finder& operator=(const Finder&) = delete;

    // Finder global (usa Runtime::instance())
    static Finder& shared();
    // Para a thread do finder global, se existir (chamar no encerramento do app)
    static void shutdownShared();

    // Inicia a busca em segundo plano (idempotente). Sem runtime não faz nada.
    void start();
    void stop();
    bool running() const { return running_.load(); }

    std::vector<SourceInfo> sources() const;
    // Aumenta a cada vez que a lista muda
    unsigned long long generation() const { return generation_.load(); }

private:
    void loop();

    Runtime& rt_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::atomic<unsigned long long> generation_{0};
    mutable std::mutex mutex_;
    std::vector<SourceInfo> sources_;
};

} // namespace bsl::ndi
