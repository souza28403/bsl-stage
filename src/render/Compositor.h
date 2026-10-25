#pragma once

#include "core/Composition.h"
#include "render/Fbo.h"
#include "render/Shader.h"

namespace bsl {

// Compoe as camadas ativas da Composition no canvas (FBOs ping-pong, o shader le o destino como textura).
// Substitui o antigo Canvas de teste da Fase 0.
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
    void render(const Composition& comp, float previewScale);

    GLuint texture() const { return fbo_[current_].texture(); }
    int width() const { return fbo_[0].width(); }
    int height() const { return fbo_[0].height(); }

    // Fracao da textura que contem a imagem da ultima chamada de render()
    float uvMax() const { return renderedScale_; }

private:
    Fbo fbo_[2];
    int current_ = 0; // FBO com o resultado acumulado
    Shader shader_;
    GLuint vao_ = 0; // triangulo de tela cheia (vertices vindos de gl_VertexID)
    float renderedScale_ = 1.0f;
};

} // namespace bsl
