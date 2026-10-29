#pragma once

// Pecas de GL compartilhadas pelos efeitos (nativos, ISF): triangulo de tela cheia, ligar a saida, copiar texturas.

#include "render/Fbo.h"

#include <glad/gl.h>

namespace bsl::fx {

// VAO vazio para desenhar o triangulo de tela cheia (vertices vindos de gl_VertexID). Um por objeto que desenha.
class ScreenVao
{
public:
    ScreenVao() { glGenVertexArrays(1, &vao_); }
    ~ScreenVao()
    {
        if (vao_)
            glDeleteVertexArrays(1, &vao_);
    }
    ScreenVao(const ScreenVao&) = delete;
    ScreenVao& operator=(const ScreenVao&) = delete;

    void draw() const
    {
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }

private:
    GLuint vao_ = 0;
};

// Liga 'out' como destino, viewport (0,0,w,h) e o estado de GL que os passes de efeito esperam
void bindOutput(const Fbo& out, int w, int h);
// Textura 2D na unidade 'unit'
void bindTexture(int unit, GLuint tex);

// Copia (com escala e filtro) uma textura para um FBO, por glBlitFramebuffer. 'flipY' inverte na vertical.
class TexBlitter
{
public:
    TexBlitter() = default;
    ~TexBlitter();
    TexBlitter(const TexBlitter&) = delete;
    TexBlitter& operator=(const TexBlitter&) = delete;

    void copy(GLuint srcTex, int srcW, int srcH, const Fbo& dst, bool linear = false, bool flipY = false);

private:
    GLuint readFbo_ = 0;
};

// Vertex shader padrao dos efeitos: triangulo de tela cheia; vUv = fragCoord / tamanho (linha 0 = v 0)
extern const char* const kFxVertexSource;

} // namespace bsl::fx
