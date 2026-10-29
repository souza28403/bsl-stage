#pragma once

#include "core/Clip.h"
#include "core/Layer.h"
#include "render/FboPool.h"
#include "render/Shader.h"

#include <glad/gl.h>

#include <memory>

namespace bsl {

// Previa de camada: a imagem de uma camada com o blend e a opacidade dela sobre preto, no aspecto do canvas, numa
// textura propria (a saida nao e tocada). Usa o mesmo shader de blend do Compositor.
class LayerPreview
{
public:
    LayerPreview();
    ~LayerPreview();
    LayerPreview(const LayerPreview&) = delete;
    LayerPreview& operator=(const LayerPreview&) = delete;

    // 'tex' = imagem da fonte (linha 0 = topo, como as fontes e as cadeias de clip), srcW x srcH.
    // Devolve a textura composta (origem embaixo, como o canvas), valida ate a proxima chamada ou release().
    GLuint compose(GLuint tex, int srcW, int srcH, FitMode fit, BlendMode blend, float opacity, int canvasW, int canvasH);

    // Libera as texturas (a previa saiu do modo Camada)
    void release();
    // Libera tudo, shader incluido (chamar com o contexto GL ainda vivo, antes de encerrar)
    void shutdown();

private:
    std::unique_ptr<Shader> shader_;
    GLuint vao_ = 0;
    FboPool pool_;
    FboPool::Lease base_;
    FboPool::Lease out_;
};

} // namespace bsl
