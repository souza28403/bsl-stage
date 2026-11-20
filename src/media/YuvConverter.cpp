#include "media/YuvConverter.h"

#include "render/Fbo.h"
#include "render/Shader.h"

#include <cstring>

namespace bsl::media {

namespace {

const char* kVertexSrc = R"(#version 330 core
out vec2 vUv;
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kFragmentSrc = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uY;
uniform sampler2D uU;
uniform sampler2D uV;
uniform sampler2D uA;
uniform int uHasAlpha;
uniform vec2 uLuma;
uniform vec2 uChroma;
uniform vec4 uCoef;
void main()
{
    float y  = (texture(uY, vUv).r - uLuma.y) * uLuma.x;
    float cb = (texture(uU, vUv).r - uChroma.y) * uChroma.x;
    float cr = (texture(uV, vUv).r - uChroma.y) * uChroma.x;
    vec3 rgb = vec3(y + uCoef.x * cr,
                    y - uCoef.y * cb - uCoef.z * cr,
                    y + uCoef.w * cb);
    float a = (uHasAlpha != 0) ? texture(uA, vUv).r : 1.0;
    fragColor = vec4(clamp(rgb, 0.0, 1.0), a);
}
)";

const char* kYcocgFragmentSrc = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
void main()
{
    vec4 c = texture(uTex, vUv);
    float scale = c.z * (255.0 / 8.0) + 1.0;
    float co = (c.x - 0.5 * 256.0 / 255.0) / scale;
    float cg = (c.y - 0.5 * 256.0 / 255.0) / scale;
    float y = c.w;
    fragColor = vec4(clamp(vec3(y + co - cg, y + cg, y - co - cg), 0.0, 1.0), 1.0);
}
)";

void setupTexture(GLuint tex)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

struct GlStateGuard {
    GLint viewport[4];
    GLint drawFbo, program, vao, activeTex;
    GLint unitTex[4];
    GLboolean blend, scissor, depth, cull;
    GLint unpackAlign, unpackRow;

    GlStateGuard()
    {
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
        for (int i = 0; i < 4; ++i) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &unitTex[i]);
        }
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlign);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpackRow);
        blend = glIsEnabled(GL_BLEND);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
        depth = glIsEnabled(GL_DEPTH_TEST);
        cull = glIsEnabled(GL_CULL_FACE);
    }
    ~GlStateGuard()
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glUseProgram(static_cast<GLuint>(program));
        glBindVertexArray(static_cast<GLuint>(vao));
        for (int i = 0; i < 4; ++i) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(unitTex[i]));
        }
        glActiveTexture(static_cast<GLenum>(activeTex));
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlign);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, unpackRow);
        if (blend) glEnable(GL_BLEND);
        if (scissor) glEnable(GL_SCISSOR_TEST);
        if (depth) glEnable(GL_DEPTH_TEST);
        if (cull) glEnable(GL_CULL_FACE);
    }
};

}

void uploadRgbaTexture(GLuint& tex, int& texW, int& texH, const Frame& frame)
{
    GLint prevTex = 0, prevAlign = 0, prevRow = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &prevRow);

    if (!tex) {
        glGenTextures(1, &tex);
        setupTexture(tex);
        texW = texH = 0;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame.strides[0] / 4);
    if (texW != frame.width || texH != frame.height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, frame.width, frame.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     frame.planes[0].data());
        texW = frame.width;
        texH = frame.height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height, GL_RGBA, GL_UNSIGNED_BYTE,
                        frame.planes[0].data());
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, prevRow);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
}

YuvConverter::YuvConverter()
{
    shader_ = std::make_unique<Shader>(kVertexSrc, kFragmentSrc);
    glGenVertexArrays(1, &vao_);
}

YuvConverter::~YuvConverter()
{
    for (GLuint& t : planeTex_) {
        if (t)
            glDeleteTextures(1, &t);
    }
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
}

GLuint YuvConverter::texture() const
{
    return fbo_ ? fbo_->texture() : 0;
}

int YuvConverter::width() const
{
    return fbo_ ? fbo_->width() : 0;
}

int YuvConverter::height() const
{
    return fbo_ ? fbo_->height() : 0;
}

void YuvConverter::uploadPlane(int i, const Frame& frame)
{
    const size_t s = static_cast<size_t>(i);
    const int w = frame.planeWidth(i);
    const int h = frame.planeHeight(i);
    if (!planeTex_[s]) {
        glGenTextures(1, &planeTex_[s]);
        setupTexture(planeTex_[s]);
        planeW_[s] = planeH_[s] = 0;
    }
    glBindTexture(GL_TEXTURE_2D, planeTex_[s]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame.strides[s]);
    if (planeW_[s] != w || planeH_[s] != h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, frame.planes[s].data());
        planeW_[s] = w;
        planeH_[s] = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, frame.planes[s].data());
    }
}

void YuvConverter::convert(const Frame& frame)
{
    if (!frame.isYuv() || frame.empty())
        return;

    GlStateGuard guard;

    if (!fbo_ || fbo_->width() != frame.width || fbo_->height() != frame.height)
        fbo_ = std::make_unique<Fbo>(frame.width, frame.height);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const int planes = Frame::planeCount(frame.format);
    for (int i = 0; i < planes; ++i) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
        uploadPlane(i, frame);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    const bool hasAlpha = frame.hasAlphaPlane();
    if (!hasAlpha) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, planeTex_[0]);
    }

    const bool full = frame.range == ColorRange::Full;
    const bool b709 = frame.colorSpace == ColorSpace::BT709;

    shader_->use();
    const GLuint prog = shader_->id();
    glUniform1i(glGetUniformLocation(prog, "uY"), 0);
    glUniform1i(glGetUniformLocation(prog, "uU"), 1);
    glUniform1i(glGetUniformLocation(prog, "uV"), 2);
    glUniform1i(glGetUniformLocation(prog, "uA"), 3);
    glUniform1i(glGetUniformLocation(prog, "uHasAlpha"), hasAlpha ? 1 : 0);
    if (full) {
        glUniform2f(glGetUniformLocation(prog, "uLuma"), 1.0f, 0.0f);
        glUniform2f(glGetUniformLocation(prog, "uChroma"), 1.0f, 128.0f / 255.0f);
    } else {
        glUniform2f(glGetUniformLocation(prog, "uLuma"), 255.0f / 219.0f, 16.0f / 255.0f);
        glUniform2f(glGetUniformLocation(prog, "uChroma"), 255.0f / 224.0f, 128.0f / 255.0f);
    }
    if (b709)
        glUniform4f(glGetUniformLocation(prog, "uCoef"), 1.5748f, 0.187324f, 0.468124f, 1.8556f);
    else
        glUniform4f(glGetUniformLocation(prog, "uCoef"), 1.402f, 0.344136f, 0.714136f, 1.772f);

    fbo_->bind();
    glViewport(0, 0, frame.width, frame.height);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

bool DxtConverter::supported()
{
    return GLAD_GL_EXT_texture_compression_s3tc != 0 && glad_glCompressedTexImage2D != nullptr &&
           glad_glCompressedTexSubImage2D != nullptr;
}

DxtConverter::DxtConverter()
{
    glGenVertexArrays(1, &vao_);
}

DxtConverter::~DxtConverter()
{
    if (tex_)
        glDeleteTextures(1, &tex_);
    if (pbo_[0])
        glDeleteBuffers(kPboCount, pbo_);
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
}

GLuint DxtConverter::texture() const
{
    if (ycocg_)
        return fbo_ ? fbo_->texture() : 0;
    return tex_;
}

void DxtConverter::upload(const Frame& frame)
{
    if (!frame.isCompressed() || frame.empty())
        return;

    GlStateGuard guard;

    const GLenum internal = frame.format == PixelFormat::DXT1 ? GL_COMPRESSED_RGB_S3TC_DXT1_EXT
                                                              : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    if (!tex_) {
        glGenTextures(1, &tex_);
        setupTexture(tex_);
        texFormat_ = 0;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    const GLsizei bytes = static_cast<GLsizei>(frame.planes[0].size());
    GLint prevPbo = 0;
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &prevPbo);
    const void* src = frame.planes[0].data();
    if (width_ != frame.width || height_ != frame.height || texFormat_ != internal) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, internal, frame.width, frame.height, 0, bytes, src);
        texFormat_ = internal;
        width_ = frame.width;
        height_ = frame.height;
    } else {
        if (!pbo_[0])
            glGenBuffers(kPboCount, pbo_);
        pboIndex_ = (pboIndex_ + 1) % kPboCount;
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_[pboIndex_]);
        if (pboBytes_ != bytes) {
            for (int i = 0; i < kPboCount; ++i) {
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_[i]);
                glBufferData(GL_PIXEL_UNPACK_BUFFER, bytes, nullptr, GL_STREAM_DRAW);
            }
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_[pboIndex_]);
            pboBytes_ = bytes;
        }
        void* dst = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, bytes, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        if (dst) {
            std::memcpy(dst, src, static_cast<size_t>(bytes));
            glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
            glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height, internal, bytes, nullptr);
        } else {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height, internal, bytes, src);
        }
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(prevPbo));

    ycocg_ = frame.format == PixelFormat::DXT5_YCoCg;
    if (!ycocg_)
        return;

    if (!shader_)
        shader_ = std::make_unique<Shader>(kVertexSrc, kYcocgFragmentSrc);
    if (!fbo_ || fbo_->width() != frame.width || fbo_->height() != frame.height)
        fbo_ = std::make_unique<Fbo>(frame.width, frame.height);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    shader_->use();
    glUniform1i(glGetUniformLocation(shader_->id(), "uTex"), 0);
    fbo_->bind();
    glViewport(0, 0, frame.width, frame.height);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

}
