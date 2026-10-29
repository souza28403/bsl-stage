#pragma once

// Interface de efeito de video da Fase 4: a mesma para os efeitos nativos, os shaders ISF e (Fase 4B) os plugins FFGL.
// O Compositor so conhece IEffect; quem cria as instancias e o EffectRegistry a partir do typeId do EffectInstance.

#include "audio/AudioTypes.h"
#include "media/SourceParams.h"
#include "render/Fbo.h"
#include "render/FboPool.h"

#include <glad/gl.h>

#include <cstdint>
#include <string>
#include <vector>

namespace bsl::fx {

// Em qual cadeia o efeito esta rodando (o Compositor preenche; Unknown em testes e fora do Compositor)
enum class ChainKind { Unknown, Clip, Layer, Master };

// Tudo que um efeito pode precisar saber do quadro atual. Preenchido pelo Compositor.
struct EffectContext
{
    double time = 0.0;       // segundos desde o inicio do app
    double dt = 0.0;         // segundos desde o quadro anterior
    std::uint64_t frameIndex = 0;
    float bpm = 120.0f;
    double beat = 0.0;       // batidas desde o ultimo RESYNC (continuo)
    float beatPhase = 0.0f;  // 0..1 dentro da batida
    float barPhase = 0.0f;   // 0..1 dentro do compasso (4 batidas)
    int canvasWidth = 1920;  // tamanho do canvas da composicao (nao o da textura em processamento)
    int canvasHeight = 1080;
    // Orientacao da textura de entrada/saida: true = a linha 0 e o TOPO da imagem (texturas de fonte, cadeias de
    // clip e de camada); false = a linha 0 e a base (canvas do Compositor, cadeia master). O efeito escreve na mesma
    // convencao que recebeu.
    bool topOrigin = true;
    bool eco = false;        // perfil Eco: desfoque e efeitos pesados rodam em resolucao reduzida
    bool preview = false;    // copia da cadeia na Previa (nao e a saida): efeitos com servidor de rede nao o abrem
    FboPool* pool = nullptr; // FBOs temporarios (nunca nulo quando chamado pelo Compositor)
    // Identifica a fonte da cadeia (clip, camada ou master) para o cache da IA: efeitos de IA da mesma cadeia dividem a
    // mesma inferencia (ai::AiEngine). 0 = sem identificacao (cada efeito de IA usa uma fonte propria).
    std::uint64_t sourceId = 0;
    // Cadeia e camada do efeito (o Z-Slice publica a mascara de zonas da camada ou do master para a saida)
    ChainKind chain = ChainKind::Unknown;
    int layerIndex = -1;     // camada da cadeia do clip ou da camada (0 = a de baixo); -1 no master
    // Analise de audio do quadro (AudioAnalyzer::snapshot()); tudo zero se nao ha dispositivo
    audio::AudioSnapshot audio;
};

class IEffect
{
public:
    virtual ~IEffect() = default;

    virtual std::string id() const = 0;       // typeId (ex.: "native:blur")
    virtual std::string name() const = 0;     // PT
    virtual std::string category() const = 0; // PT

    virtual std::vector<media::ParamDesc> paramDescs() const = 0;
    // Chamado antes de process() quando os valores mudam (e na primeira vez)
    virtual void setParams(const media::ParamSet& params) = 0;

    // Le 'inputTex' (w x h; 0 se !needsInput) e escreve em 'output' (mesmo tamanho w x h). O efeito liga o proprio
    // FBO/viewport e pode mexer em qualquer estado do GL: o Compositor o restaura depois. Nao deve lancar excecao:
    // em falha, copia a entrada para a saida e informa em status().
    virtual void process(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) = 0;

    virtual bool needsInput() const { return true; } // false = gerador (ignora a entrada)
    virtual bool isLive() const { return false; }    // fonte externa ao vivo (nunca duplicar a instancia)
    virtual media::SourceStatus status() const { return {}; }

    // Parametros que o proprio efeito mudou por conta propria (ex.: plugin FFGL com interface web). Chamado a cada
    // quadro depois de process(); o efeito decide a cadencia. Grava em 'values' so o que mudou e devolve true.
    // Nao deve devolver o que o usuario acabou de mandar por setParams().
    virtual bool pollParamChanges(media::ParamValues& values)
    {
        (void)values;
        return false;
    }
};

} // namespace bsl::fx
