#include "render/LiveThumb.h"

#include <algorithm>

namespace bsl {

namespace {

const char* const kVertexSource = R"(#version 330 core
out vec2 vUv;
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* const kFragmentSource = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uSrc;
uniform vec2 uFit;
uniform vec2 uStep;
void main()
{
    vec2 s = (vUv - 0.5) / uFit + 0.5;
    if (s.x < 0.0 || s.x > 1.0 || s.y < 0.0 || s.y > 1.0) {
        fragColor = vec4(0.0);
        return;
    }
    vec4 acc = vec4(0.0);
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            vec2 o = (vec2(float(i), float(j)) + 0.5) / 4.0 - 0.5;
            vec4 c = texture(uSrc, clamp(s + o * uStep, 0.0, 1.0));
            acc += vec4(c.rgb * c.a, c.a);
        }
    acc /= 16.0;
    fragColor = vec4(acc.a > 0.0 ? acc.rgb / acc.a : vec3(0.0), acc.a);
}
)";

}

LiveThumbnailer::LiveThumbnailer()
{
    shader_ = std::make_unique<Shader>(kVertexSource, kFragmentSource);
    glGenVertexArrays(1, &vao_);
    glGenFramebuffers(1, &fbo_);
}

LiveThumbnailer::~LiveThumbnailer()
{
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
    if (fbo_)
        glDeleteFramebuffers(1, &fbo_);
}

void LiveThumbnailer::blit(GLuint srcTexture, int srcWidth, int srcHeight, Texture& dst)
{
    if (!srcTexture || srcWidth <= 0 || srcHeight <= 0)
        return;

    GLint prevFbo = 0, prevProgram = 0, prevVao = 0, prevActive = 0, prevTex = 0;
    GLint prevViewport[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    const GLboolean blend = glIsEnabled(GL_BLEND);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);

    if (!dst.id) {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kWidth, kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        dst = Texture(id, kWidth, kHeight);
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dst.id, 0);
    glViewport(0, 0, kWidth, kHeight);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);

    const float thumbAspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
    const float srcAspect = static_cast<float>(srcWidth) / static_cast<float>(srcHeight);
    float fx = 1.0f, fy = 1.0f;
    if (srcAspect > thumbAspect)
        fy = thumbAspect / srcAspect;
    else
        fx = srcAspect / thumbAspect;

    shader_->use();
    shader_->setInt("uSrc", 0);
    shader_->setVec2("uFit", fx, fy);
    shader_->setVec2("uStep", 1.0f / (static_cast<float>(kWidth) * fx), 1.0f / (static_cast<float>(kHeight) * fy));
    glBindTexture(GL_TEXTURE_2D, srcTexture);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    glActiveTexture(static_cast<GLenum>(prevActive));
    glBindVertexArray(static_cast<GLuint>(prevVao));
    glUseProgram(static_cast<GLuint>(prevProgram));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (blend)
        glEnable(GL_BLEND);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
    if (depth)
        glEnable(GL_DEPTH_TEST);
}

}
