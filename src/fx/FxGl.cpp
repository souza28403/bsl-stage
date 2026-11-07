#include "fx/FxGl.h"

namespace bsl::fx {

const char* const kFxVertexSource = R"(#version 330 core
out vec2 vUv;
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

void bindOutput(const Fbo& out, int w, int h)
{
    out.bind();
    glViewport(0, 0, w, h);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void bindTexture(int unit, GLuint tex)
{
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, tex);
}

TexBlitter::~TexBlitter()
{
    if (readFbo_)
        glDeleteFramebuffers(1, &readFbo_);
}

void TexBlitter::copy(GLuint srcTex, int srcW, int srcH, const Fbo& dst, bool linear, bool flipY)
{
    if (!readFbo_)
        glGenFramebuffers(1, &readFbo_);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo_);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcTex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.id());
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBlitFramebuffer(0, 0, srcW, srcH, 0, flipY ? dst.height() : 0, dst.width(), flipY ? 0 : dst.height(),
                      GL_COLOR_BUFFER_BIT, linear ? GL_LINEAR : GL_NEAREST);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}
