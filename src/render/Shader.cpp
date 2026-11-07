#include "render/Shader.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace bsl {

namespace {

GLuint compile(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Falha ao compilar shader: ") + log);
    }
    return shader;
}

}

Shader::Shader(const char* vertexSource, const char* fragmentSource)
{
    const GLuint vs = compile(GL_VERTEX_SHADER, vertexSource);
    GLuint fs = 0;
    try {
        fs = compile(GL_FRAGMENT_SHADER, fragmentSource);
    } catch (...) {
        glDeleteShader(vs);
        throw;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        glDeleteProgram(program_);
        program_ = 0;
        throw std::runtime_error(std::string("Falha ao linkar shader: ") + log);
    }
}

Shader::~Shader()
{
    if (program_)
        glDeleteProgram(program_);
}

Shader::Shader(Shader&& other) noexcept : program_(std::exchange(other.program_, 0)) {}

Shader& Shader::operator=(Shader&& other) noexcept
{
    if (this != &other) {
        if (program_)
            glDeleteProgram(program_);
        program_ = std::exchange(other.program_, 0);
    }
    return *this;
}

void Shader::use() const
{
    glUseProgram(program_);
}

void Shader::setFloat(const char* name, float v) const
{
    glUniform1f(glGetUniformLocation(program_, name), v);
}

void Shader::setInt(const char* name, int v) const
{
    glUniform1i(glGetUniformLocation(program_, name), v);
}

void Shader::setVec2(const char* name, float x, float y) const
{
    glUniform2f(glGetUniformLocation(program_, name), x, y);
}

void Shader::setVec3(const char* name, float x, float y, float z) const
{
    glUniform3f(glGetUniformLocation(program_, name), x, y, z);
}

void Shader::setVec4(const char* name, float x, float y, float z, float w) const
{
    glUniform4f(glGetUniformLocation(program_, name), x, y, z, w);
}

void Shader::setMat3RowMajor(const char* name, const float* rows) const
{
    glUniformMatrix3fv(glGetUniformLocation(program_, name), 1, GL_TRUE, rows);
}

void Shader::setVec3Array(const char* name, const float* values, int count) const
{
    glUniform3fv(glGetUniformLocation(program_, name), count, values);
}

}
