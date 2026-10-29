#pragma once

// IA local opcional (profundidade e segmentacao de pessoa) para os efeitos BSL Light. Roda numa thread propria,
// assincrona: a thread de render so reduz a imagem de entrada para uma textura pequena, le por PBO (sem travar) e
// entrega o resultado do ultimo quadro inferido. Nada sai do PC.
//
// Uso nos efeitos (thread de render, contexto GL atual):
//     AiFrame f = AiEngine::instance().acquire(ctx.sourceId, ctx.frameIndex, inputTex, w, h, ctx.topOrigin, kNeedDepth, ctx.eco);
//     if (f.state != AiState::Ready) { /* passa a imagem direto e mostra aiStatusText(f) */ }
//     // f.depth: R16F 0..1 (maior = mais perto); f.mask: R8 0..1 (pessoa). Linha 0 = TOPO da imagem (use imgPos()).
// Varios efeitos do mesmo clip (mesmo sourceId) dividem a mesma inferencia: o primeiro a pedir no intervalo captura,
// os demais so leem o resultado ja pronto.

#include "ai/AiRuntime.h"

#include <glad/gl.h>

#include <cstdint>
#include <string>
#include <vector>

namespace bsl::ai {

enum AiNeed : unsigned { kNeedDepth = 1u, kNeedPerson = 2u };

enum class AiState
{
    Off,         // desligada (perfil Eco): o efeito passa a imagem direto
    Unavailable, // sem runtime, sem modelo ou falha: idem, com o motivo em AiFrame::message
    Warming,     // carregando o modelo / ainda sem o primeiro resultado
    Ready
};

struct AiFrame
{
    AiState state = AiState::Off;
    GLuint depth = 0; // R16F (0..1, maior = mais perto), linha 0 = topo; 0 se nao pedida
    GLuint mask = 0;  // R8 (0..1, pessoa), linha 0 = topo; 0 se nao pedida
    int depthW = 0, depthH = 0;
    int maskW = 0, maskH = 0;
    std::string message; // motivo (Off/Unavailable/Warming); vazio em Ready
};

// Texto de status para o efeito mostrar (PT); vazio em Ready
std::string aiStatusText(const AiFrame& frame);

struct ModelStats
{
    bool present = false;      // arquivo do modelo existe
    bool loaded = false;
    bool onGpu = false;
    std::string device;        // "NVIDIA ... (DirectML)" ou "CPU"
    std::string error;
    double lastMs = 0.0;
    double avgMs = 0.0;        // media movel
    std::uint64_t runs = 0;
};

struct AiStats
{
    bool enabled = false;      // false = Eco
    int rate = 0;              // fps de inferencia efetivo por fonte
    bool runtimeLoaded = false;
    bool runtimeTried = false;
    std::string runtimeVersion;
    std::string runtimeError;
    std::string gpuName;       // adaptador escolhido (vazio = sem DirectML)
    float inferencesPerSec = 0.0f; // fps de IA medido (todas as fontes)
    int activeSources = 0;
    ModelStats models[kModelKindCount];
    // "Testar"
    bool testRunning = false;
    bool testDone = false;
    std::string testReport;    // resultado em texto (PT)
};

class AiEngine
{
public:
    static AiEngine& instance();
    ~AiEngine();
    AiEngine(const AiEngine&) = delete;
    AiEngine& operator=(const AiEngine&) = delete;

    // --- Configuracao (qualquer thread) ---
    // Taxa de inferencia por fonte do perfil atual (Eco = 0 desliga; Balanceado 15; Maximo 30)
    void setProfileRate(int fps);
    // Substitui a taxa do perfil (0 = usar a do perfil)
    void setRateOverride(int fps);
    // Onde rodar os modelos. Mudar descarta as sessoes (recarregam na proxima inferencia)
    void setDevicePref(DevicePref pref);
    bool enabled() const;
    int rate() const; // fps efetivo (0 = desligada)
    // Confere de novo se os arquivos dos modelos existem (a checagem e feita no maximo a cada 2 s)
    void refreshModelPresence();

    // --- Thread de render (contexto GL atual) ---
    // 'need' = AiNeed. Nao bloqueia. 'eco' = ctx.eco do quadro (true: devolve Off sem tocar em nada).
    AiFrame acquire(std::uint64_t sourceId, std::uint64_t frameIndex, GLuint inputTex, int w, int h, bool topOrigin,
                    unsigned need, bool eco = false);

    // Testes e depuracao: usa estas texturas (R16F / R8, linha 0 = topo) no lugar da inferencia para a fonte.
    // As texturas continuam do chamador. 0 em ambas desfaz.
    void setOverride(std::uint64_t sourceId, GLuint depthTex, GLuint maskTex);

    // --- Painel Ver > IA (qualquer thread) ---
    AiStats stats() const;
    // Carrega os modelos (se preciso) e roda uma imagem sintetica na thread da IA, mesmo no Eco (acao explicita)
    void requestSelfTest();

    // Para a thread e libera texturas/FBOs/sessoes. Chamar com o contexto GL atual (fim do app / dos testes).
    void shutdown();

private:
    AiEngine();
    struct Impl;
    Impl* impl_;
};

} // namespace bsl::ai
