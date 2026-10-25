#pragma once

#include "media/Frame.h"

#include <glad/gl.h>

#include <memory>

namespace bsl {
class Fbo;
class Shader;
} // namespace bsl

namespace bsl::media {

// Sobe um frame RGBA8 para a textura 'tex' (cria/redimensiona se preciso). Usa GL_UNPACK_ROW_LENGTH.
void uploadRgbaTexture(GLuint& tex, int& texW, int& texH, const Frame& frame);

// Converte frames YUV420P/YUVA420P em RGBA: sobe os planos em texturas R8 e desenha num FBO
// interno do tamanho do video (BT.601/709, limited/full). Chamar na thread GL.
class YuvConverter {
public:
    YuvConverter();
    ~YuvConverter();
    YuvConverter(const YuvConverter&) = delete;
    YuvConverter& operator=(const YuvConverter&) = delete;

    void convert(const Frame& frame);

    GLuint texture() const;
    int width() const;
    int height() const;

private:
    void uploadPlane(int index, const Frame& frame);

    std::unique_ptr<Shader> shader_;
    std::unique_ptr<Fbo> fbo_;
    GLuint vao_ = 0;
    GLuint planeTex_[4] = {0, 0, 0, 0};
    int planeW_[4] = {0, 0, 0, 0};
    int planeH_[4] = {0, 0, 0, 0};
};

} // namespace bsl::media
