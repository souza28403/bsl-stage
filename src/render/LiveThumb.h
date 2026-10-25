#pragma once

#include "render/Fbo.h"
#include "render/Shader.h"
#include "render/Texture.h"

#include <memory>

namespace bsl {

// Reduz a textura de uma fonte para uma miniatura 160x90 (proporcao da fonte preservada, resto transparente)
// com media de 4x4 amostras por pixel. Usada nas celulas de geradores/fontes ao vivo (no maximo 1x/s por clip).
class LiveThumbnailer
{
public:
    static constexpr int kWidth = 160;
    static constexpr int kHeight = 90;

    LiveThumbnailer();
    ~LiveThumbnailer();
    LiveThumbnailer(const LiveThumbnailer&) = delete;
    LiveThumbnailer& operator=(const LiveThumbnailer&) = delete;

    // Cria 'dst' na primeira vez. O estado GL do chamador e restaurado.
    void blit(GLuint srcTexture, int srcWidth, int srcHeight, Texture& dst);

private:
    std::unique_ptr<Shader> shader_;
    GLuint vao_ = 0;
    GLuint fbo_ = 0;
};

} // namespace bsl
