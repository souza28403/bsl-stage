#include "render/TestPatternSource.h"

#include <stdexcept>

namespace bsl {

namespace {

const char* const kVertexSource = R"(#version 330 core
out vec2 vUv;
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    // linha 0 da textura = topo da imagem; uv com y para cima
    vUv = vec2(p.x, 1.0 - p.y);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Padrao de teste: barras de cor, rampa, grade, circulo que se move e moldura
const char* const kFragmentSource = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform float uTime;
uniform vec2 uRes;

vec3 hsv(float h, float s, float v)
{
    vec3 k = clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return v * mix(vec3(1.0), k, s);
}

void main()
{
    vec2 uv = vUv;
    float aspect = uRes.x / uRes.y;

    vec3 col;
    if (uv.y > 0.30) {
        int i = clamp(int(floor(uv.x * 7.0)), 0, 6);
        vec3 bars[7] = vec3[7](
            vec3(0.92), vec3(0.92, 0.92, 0.0), vec3(0.0, 0.92, 0.92), vec3(0.0, 0.92, 0.0),
            vec3(0.92, 0.0, 0.92), vec3(0.92, 0.0, 0.0), vec3(0.0, 0.0, 0.92));
        col = bars[i];
    } else if (uv.y > 0.15) {
        col = vec3(uv.x);
    } else {
        col = hsv(uv.x + uTime * 0.1, 0.85, 0.95);
    }

    // Grade de celulas quadradas (9 linhas)
    vec2 g = uv * vec2(aspect, 1.0) * 9.0;
    vec2 d = abs(fract(g - 0.5) - 0.5) / fwidth(g);
    float line = 1.0 - clamp(min(d.x, d.y) - 0.5, 0.0, 1.0);
    col = mix(col, vec3(1.0), line * 0.35);

    // Circulo em movimento (Lissajous)
    vec2 p = (uv - 0.5) * vec2(aspect, 1.0);
    vec2 c = vec2(sin(uTime * 0.8) * 0.45 * aspect, cos(uTime * 1.1) * 0.30);
    float dist = length(p - c);
    float px = fwidth(dist);
    float fill = 1.0 - smoothstep(0.12 - px, 0.12 + px, dist);
    float ring = smoothstep(0.115 - px, 0.115, dist) * (1.0 - smoothstep(0.12, 0.12 + px, dist));
    col = mix(col, vec3(0.08, 0.09, 0.10), fill * 0.75);
    col = mix(col, vec3(1.0), ring);

    // Moldura nas bordas
    vec2 e = min(uv, 1.0 - uv) * uRes;
    if (min(e.x, e.y) < 3.0)
        col = vec3(1.0);

    fragColor = vec4(col, 1.0);
}
)";

} // namespace

TestPatternSource::TestPatternSource(int width, int height) : width_(width), height_(height) {}

TestPatternSource::~TestPatternSource()
{
    close();
}

bool TestPatternSource::open(const std::filesystem::path& /*path*/, std::string* error)
{
    close();
    try {
        fbo_ = std::make_unique<Fbo>(width_, height_);
        shader_ = std::make_unique<Shader>(kVertexSource, kFragmentSource);
    } catch (const std::exception& e) {
        fbo_.reset();
        shader_.reset();
        if (error)
            *error = e.what();
        return false;
    }
    glGenVertexArrays(1, &vao_);
    clock_ = media::ClipClock{};
    time_ = 0.0;
    render();
    return true;
}

void TestPatternSource::close()
{
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    shader_.reset();
    fbo_.reset();
}

void TestPatternSource::update(double dt)
{
    if (!fbo_)
        return;
    // Sem duracao: o relogio do ISource nao anda, entao o tempo e proprio
    if (clock_.playing)
        time_ += dt * clock_.speed;
    render();
}

void TestPatternSource::render()
{
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    fbo_->bind();
    glViewport(0, 0, width_, height_);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    shader_->use();
    shader_->setFloat("uTime", static_cast<float>(time_));
    shader_->setVec2("uRes", static_cast<float>(width_), static_cast<float>(height_));
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);

    Fbo::unbind();
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

} // namespace bsl
