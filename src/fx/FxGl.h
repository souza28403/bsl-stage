#pragma once

#include "render/Fbo.h"

#include <glad/gl.h>

namespace bsl::fx {

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

void bindOutput(const Fbo& out, int w, int h);
void bindTexture(int unit, GLuint tex);

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

extern const char* const kFxVertexSource;

}
