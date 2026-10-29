#pragma once

// Uma instancia de plugin FFGL em execucao no processo (efeito ou fonte). Roda no thread do contexto GL. Toda chamada ao
// plugin passa pelo SEH de ffgl::Plugin: em falha a instancia fica "faulted" (vira passagem direta, status vermelho) e o
// plugin e bloqueado na sessao (Catalog::markSessionFault) ate o usuario reativar na janela Plugins.
//
// Sequencia: FF_INITIALISE_V2 (uma vez por DLL, no primeiro uso) -> FF_INSTANTIATE_GL -> FF_SET_HOSTINFO ->
// parametros -> por quadro FF_SET_BEATINFO, FF_SET_TIME e FF_PROCESS_OPENGL (FF_RESIZE quando o tamanho muda) ->
// FF_DEINSTANTIATE_GL (e FF_DEINITIALISE quando e a ultima instancia da DLL).
//
// Os plugins FFGL trabalham em alpha premultiplicado e com a linha 0 na base da imagem: o host converte a entrada
// (e desfaz na saida) e inverte na vertical quando a textura de entrada/saida tem a linha 0 no topo (ctx.topOrigin).

#include "ffgl/FfglCatalog.h"
#include "ffgl/FfglParams.h"
#include "fx/FxGl.h"
#include "fx/IEffect.h"
#include "render/Shader.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bsl::ffgl {

class Plugin;

class FfglInstance final : public fx::IEffect
{
public:
    explicit FfglInstance(EntryPtr entry);
    ~FfglInstance() override;

    // --- fx::IEffect ---
    std::string id() const override { return entry_->typeId; }
    std::string name() const override { return pluginDisplayName(entry_->info); }
    std::string category() const override { return entry_->origin; }
    std::vector<media::ParamDesc> paramDescs() const override { return entry_->map.descs; }
    void setParams(const media::ParamSet& params) override;
    void process(const fx::EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) override;
    bool needsInput() const override { return entry_->info.type != kTypeSource; }
    media::SourceStatus status() const override;
    bool pollParamChanges(media::ParamValues& values) override;

    bool faulted() const { return faulted_; }
    const std::string& faultReason() const { return faultReason_; }
    // Contadores de teste: quantos quadros chegaram ao plugin, ultimo BPM e fase do compasso enviados
    std::uint64_t framesProcessed() const { return frames_; }
    float lastBpm() const { return lastBpm_; }
    float lastBarPhase() const { return lastBarPhase_; }

private:
    bool ensureReady(int w, int h); // false = passagem direta
    void fault(const std::string& what, unsigned long code);
    void fail(const std::string& message); // erro sem excecao (plugin recusou); nao bloqueia o plugin
    void pass(GLuint inputTex, int w, int h, Fbo& output);
    void flushParams();
    void convert(GLuint src, int w, int h, const Fbo& dst, bool unpremultiply, bool flip);
    void teardown(bool callPlugin);

    EntryPtr entry_;
    std::shared_ptr<Plugin> plugin_;
    bool pluginHeld_ = false;
    void* instance_ = nullptr; // FFInstanceID devolvido pelo FF_INSTANTIATE_GL
    int width_ = 0, height_ = 0;

    bool faulted_ = false;
    std::string faultReason_;
    std::string failMessage_;       // plugin recusou (sem excecao): texto do status
    std::uint64_t seenEpoch_ = 0;   // Catalog::faultEpoch no ultimo quadro (para reativar)

    // Parametros: desejado (UI) x o que o plugin tem
    std::vector<PluginValue> desired_;
    std::vector<PluginValue> sent_;
    std::vector<char> sentKnown_;       // 0 = nunca enviado
    std::vector<float> eventSeen_;      // ultimo contador de evento visto (por indice FFGL)
    std::vector<int> pulseOff_;         // eventos a baixar no proximo quadro
    bool haveParams_ = false;
    std::chrono::steady_clock::time_point lastPoll_{};

    std::unique_ptr<Shader> convert_;
    std::unique_ptr<FboPool> ownPool_; // so quando o chamador nao da um pool em EffectContext
    fx::ScreenVao vao_;
    fx::TexBlitter blit_;
    std::uint64_t frames_ = 0;
    float lastBpm_ = 0.0f, lastBarPhase_ = 0.0f;
};

} // namespace bsl::ffgl
