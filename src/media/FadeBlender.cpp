#include "media/FadeBlender.h"

#include <algorithm>

namespace bsl::media {

namespace {

const char* const kVertex = R"(#version 330 core
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* const kMix = R"(#version 330 core
out vec4 fragColor;
uniform sampler2D uPrev;
uniform sampler2D uCur;
uniform float uT;
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 a = texelFetch(uPrev, p, 0);
    vec4 b = texelFetch(uCur, p, 0);
    float alpha = mix(a.a, b.a, uT);
    vec3 rgb = mix(a.rgb * a.a, b.rgb * b.a, uT);
    fragColor = vec4(alpha > 0.0001 ? rgb / alpha : vec3(0.0), alpha);
}
)";

const char* const kCopy = R"(#version 330 core
out vec4 fragColor;
uniform sampler2D uPrev;
uniform sampler2D uCur;
uniform float uT;
void main()
{
    fragColor = texelFetch(uCur, ivec2(gl_FragCoord.xy), 0);
}
)";

}

FadeBlender::FadeBlender()
{
    mix_ = std::make_unique<Shader>(kVertex, kMix);
    copy_ = std::make_unique<Shader>(kVertex, kCopy);
    glGenVertexArrays(1, &vao_);
}

FadeBlender::~FadeBlender()
{
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
}

void FadeBlender::ensure(int w, int h)
{
    if (prev_ && prev_->width() == w && prev_->height() == h)
        return;
    prev_ = std::make_unique<Fbo>(w, h);
    out_ = std::make_unique<Fbo>(w, h);
}

void FadeBlender::snapshot(GLuint src, int w, int h)
{
    if (!src || w <= 0 || h <= 0)
        return;
    ensure(w, h);

    GLint prevFbo = 0, vp[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, vp);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean blend = glIsEnabled(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);

    prev_->bind();
    glViewport(0, 0, w, h);
    copy_->use();
    copy_->setInt("uCur", 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, src);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);

    if (scissor)
        glEnable(GL_SCISSOR_TEST);
    if (blend)
        glEnable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}

void FadeBlender::begin(GLuint current, double duration)
{
    if (!prev_ || !out_)
        return;
    duration_ = std::max(0.01, duration);
    elapsed_ = 0.0;
    active_ = true;
    draw(current, 0.0f);
}

bool FadeBlender::update(double dt, GLuint current)
{
    if (!active_)
        return false;
    elapsed_ += dt;
    const float t = static_cast<float>(std::min(1.0, elapsed_ / duration_));
    draw(current, t);
    if (t >= 1.0f)
        active_ = false;
    return active_;
}

void FadeBlender::draw(GLuint current, float t)
{
    if (!current || !prev_ || !out_)
        return;
    GLint prevFbo = 0, vp[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, vp);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean blend = glIsEnabled(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);

    out_->bind();
    glViewport(0, 0, out_->width(), out_->height());
    mix_->use();
    mix_->setInt("uPrev", 0);
    mix_->setInt("uCur", 1);
    mix_->setFloat("uT", t);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, prev_->texture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, current);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    if (scissor)
        glEnable(GL_SCISSOR_TEST);
    if (blend)
        glEnable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}

}
