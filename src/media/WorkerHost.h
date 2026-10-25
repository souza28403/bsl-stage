#pragma once

// Lado do app do processo auxiliar (bslstage_worker.exe): inicia, vigia e relanca o worker, fala com ele por
// named pipe e recebe os quadros (textura D3D11 compartilhada ou memoria compartilhada).
//
// Tudo que pode demorar (iniciar o processo, esperar o pipe, heartbeat, reinicios) roda numa thread propria:
// a UI nunca espera pelo worker. Se o worker travar ou morrer, a fonte mantem o ultimo quadro, mostra
// "Reiniciando…" e o host o relanca ate 3 vezes (espera 1 s, 2 s, 4 s); depois disso fica em "Falhou".
// O worker entra num job object com KILL_ON_JOB_CLOSE: se o app morrer, os workers morrem juntos.

#include "media/WorkerProtocol.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ID3D11Texture2D;
struct ID3D11DeviceContext;

namespace bsl::render {
class D3D11Interop;
}

namespace bsl::media {

using WorkerJson = nlohmann::json;

enum class HostState { Idle, Starting, Running, Restarting, Failed };

struct HostSnapshot
{
    HostState state = HostState::Idle;
    std::string text;           // texto para a celula ("Iniciando…", "Convertendo…", "Reiniciando… (2/3)", "Falhou: …")
    bool warning = false;       // texto em destaque (reinicio/falha)
    std::string failure;        // motivo da falha definitiva
    std::string transport;      // "texture" | "shm" | "" (ainda nao escolhido)
    std::string transportWhy;   // por que (para o log/teste)
    unsigned long pid = 0;      // pid do worker atual (0 = nenhum)
    int restarts = 0;           // reinicios feitos desde o inicio
    int pages = 0;              // paginas do documento (PDF), 0 = desconhecido
    std::string title;
};

// Acesso a um quadro novo (valido so durante o callback de acquireNewFrame)
struct FrameAccess
{
    enum class Kind { Shm, Texture };
    Kind kind = Kind::Shm;
    int width = 0;
    int height = 0;
    int page = 0;
    std::uint64_t seq = 0;                    // contador proprio do host (cresce a cada quadro)
    // Shm: cabecalho + buffers (leitura com seqlock)
    const wp::ShmHeader* shmHeader = nullptr;
    const void* shmBase = nullptr;
    // Texture: textura D3D11 "caixa de correio" no device do app (ja copiada da compartilhada)
    ID3D11Texture2D* mailbox = nullptr;
    ID3D11DeviceContext* context = nullptr;
};

class WorkerHost
{
public:
    WorkerHost();
    ~WorkerHost(); // pede o encerramento sem esperar (ver waitAllStopped)
    WorkerHost(const WorkerHost&) = delete;
    WorkerHost& operator=(const WorkerHost&) = delete;

    // 'open' = corpo da mensagem "open" (kind, path/url, w, h, params...); luid e transport sao acrescentados aqui.
    // 'interop' pode ser nullptr (so memoria compartilhada). 'transport': "auto", "texture" ou "shm".
    void start(WorkerJson open, const std::shared_ptr<render::D3D11Interop>& interop, const std::string& transport = "auto");
    // Pede o encerramento (nao bloqueia): o worker recebe "quit" e, se nao sair logo, e encerrado.
    void stop();
    bool started() const { return core_ != nullptr; }

    // Troca o corpo usado quando o worker for relancado (estado atual: pagina, parametros, tamanho)
    void setOpen(WorkerJson open);
    // Enfileira uma mensagem para o worker (nao bloqueia). Descartada se nao ha worker no momento.
    void send(WorkerJson msg);

    // Eventos do worker para a fonte (doc, error nao fatal...). Chamar na thread principal.
    std::vector<WorkerJson> drainEvents();
    HostSnapshot snapshot() const;

    // Se ha quadro mais novo que 'lastSeq', chama fn com o acesso (travado) e atualiza lastSeq.
    bool acquireNewFrame(std::uint64_t& lastSeq, const std::function<void(const FrameAccess&)>& fn);
    std::uint64_t frameCount() const;

    // Testes: mata o worker a forca (como um crash)
    void debugKillWorker();

    // Espera todas as threads supervisoras (e os workers) terminarem; true se acabaram dentro do prazo.
    // Chamar ao fechar o app e nos testes (com o contexto GL ainda vivo ou nao: nao usa GL).
    static bool waitAllStopped(unsigned timeoutMs);
    static bool processRunning(unsigned long pid);
    static std::wstring workerExePath();

private:
    struct Core;
    std::shared_ptr<Core> core_;
};

} // namespace bsl::media
