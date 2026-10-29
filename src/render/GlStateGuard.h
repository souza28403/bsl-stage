#pragma once

#include <glad/gl.h>

namespace bsl {

// Guarda o estado GL que uma passada auxiliar (conversão, readback) costuma mexer e o restaura ao sair:
// FBO de desenho/leitura, viewport, programa, VAO, texturas das unidades 0-3, buffers de pixel pack/unpack,
// alinhamentos e flags de blend/scissor/depth/cull.
class GlStateGuard
{
public:
    GlStateGuard()
    {
        glGetIntegerv(GL_VIEWPORT, viewport_);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo_);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo_);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao_);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex_);
        for (int i = 0; i < 4; ++i) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &unitTex_[i]);
        }
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuf_);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuf_);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlign_);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpackRow_);
        glGetIntegerv(GL_PACK_ALIGNMENT, &packAlign_);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &packRow_);
        blend_ = glIsEnabled(GL_BLEND);
        scissor_ = glIsEnabled(GL_SCISSOR_TEST);
        depth_ = glIsEnabled(GL_DEPTH_TEST);
        cull_ = glIsEnabled(GL_CULL_FACE);
    }
    ~GlStateGuard()
    {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(packBuf_));
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpackBuf_));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo_));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFbo_));
        glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
        glUseProgram(static_cast<GLuint>(program_));
        glBindVertexArray(static_cast<GLuint>(vao_));
        for (int i = 0; i < 4; ++i) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(unitTex_[i]));
        }
        glActiveTexture(static_cast<GLenum>(activeTex_));
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlign_);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, unpackRow_);
        glPixelStorei(GL_PACK_ALIGNMENT, packAlign_);
        glPixelStorei(GL_PACK_ROW_LENGTH, packRow_);
        setCap(GL_BLEND, blend_);
        setCap(GL_SCISSOR_TEST, scissor_);
        setCap(GL_DEPTH_TEST, depth_);
        setCap(GL_CULL_FACE, cull_);
    }
    GlStateGuard(const GlStateGuard&) = delete;
    GlStateGuard& operator=(const GlStateGuard&) = delete;

private:
    static void setCap(GLenum cap, GLboolean on)
    {
        if (on)
            glEnable(cap);
        else
            glDisable(cap);
    }

    GLint viewport_[4] = {};
    GLint drawFbo_ = 0, readFbo_ = 0, program_ = 0, vao_ = 0, activeTex_ = 0;
    GLint unitTex_[4] = {};
    GLint packBuf_ = 0, unpackBuf_ = 0;
    GLint unpackAlign_ = 4, unpackRow_ = 0, packAlign_ = 4, packRow_ = 0;
    GLboolean blend_ = GL_FALSE, scissor_ = GL_FALSE, depth_ = GL_FALSE, cull_ = GL_FALSE;
};

} // namespace bsl
