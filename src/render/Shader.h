#pragma once

#include <glad/gl.h>

namespace bsl {

class Shader
{
public:
    Shader(const char* vertexSource, const char* fragmentSource);
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    void use() const;
    void setFloat(const char* name, float v) const;
    void setInt(const char* name, int v) const;
    void setVec2(const char* name, float x, float y) const;
    void setVec3(const char* name, float x, float y, float z) const;
    void setVec4(const char* name, float x, float y, float z, float w) const;
    void setMat3RowMajor(const char* name, const float* rows) const;
    void setVec3Array(const char* name, const float* values, int count) const;

    GLuint id() const { return program_; }

private:
    GLuint program_ = 0;
};

}
