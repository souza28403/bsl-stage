#pragma once

#include <glad/gl.h>

namespace bsl {

// Programa GLSL (vertex + fragment). Lanca std::runtime_error se falhar.
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
    // Matriz 3x3 com 9 floats em ordem de linhas
    void setMat3RowMajor(const char* name, const float* rows) const;
    // Vetor de vec3 (n elementos, 3 floats cada)
    void setVec3Array(const char* name, const float* values, int count) const;

    GLuint id() const { return program_; }

private:
    GLuint program_ = 0;
};

} // namespace bsl
