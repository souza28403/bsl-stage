#pragma once

#include "core/Composition.h"
#include "fx/FxGl.h"
#include "render/Fbo.h"
#include "render/FboPool.h"
#include "render/Shader.h"

#include <chrono>
#include <cstdint>

namespace bsl {

// Compoe as camadas ativas da Composition no canvas (FBOs ping-pong, o shader le o destino como textura) e aplica as
// cadeias de efeitos (Fase 4): a do clip no tamanho da fonte (antes do encaixe), a da camada no tamanho do canvas
// (antes do blend) e a do master depois de todas as camadas. Os FBOs temporarios dos efeitos vem de um pool por tamanho.
class Compositor
{
public:
    Compositor(int width, int height);
    ~Compositor();
    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;

    // Recria os FBOs com o novo tamanho do canvas (conteudo descartado ate o proximo render)
    void resize(int width, int height);

    // Renderiza so a regiao (0,0)-(w*previewScale, h*previewScale) dos FBOs:
    // perfis mais leves gastam menos fill-rate. Veja uvMax().
    // 'eco' (perfil Eco): desfoque e efeitos pesados rodam em resolucao reduzida e a cadeia do clip trabalha no
    // tamanho do canvas no maximo.
    void render(Composition& comp, float previewScale, bool eco = false);

    GLuint texture() const { return fbo_[current_].texture(); }
    int width() const { return fbo_[0].width(); }
    int height() const { return fbo_[0].height(); }

    // Fracao da textura que contem a imagem da ultima chamada de render()
    float uvMax() const { return renderedScale_; }

    FboPool& pool() { return pool_; }

private:
    Fbo fbo_[2];
    int current_ = 0; // FBO com o resultado acumulado
    Shader shader_;   // blend de uma camada sobre o acumulado
    Shader fit_;      // fonte -> canvas (encaixe), para quando a camada tem efeitos
    Shader finish_;   // master: efeitos/fade
    GLuint vao_ = 0; // triangulo de tela cheia (vertices vindos de gl_VertexID)
    float renderedScale_ = 1.0f;

    FboPool pool_;
    fx::TexBlitter blit_;
    std::chrono::steady_clock::time_point lastRender_{};
    bool haveLast_ = false;
    double time_ = 0.0;
    std::uint64_t frame_ = 0;
};

} // namespace bsl
