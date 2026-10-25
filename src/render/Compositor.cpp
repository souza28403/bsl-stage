#include "render/Compositor.h"

#include <algorithm>
#include <utility>

namespace bsl {

namespace {

const char* const kVertexSource = R"(#version 330 core
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Um passe por camada: base (acumulado) + fonte -> novo acumulado.
// Alpha da fonte e reto; resultado = mix(base, blend(base, src.rgb), src.a * opacidade).
const char* const kFragmentSource = R"(#version 330 core
out vec4 fragColor;

uniform sampler2D uBase;
uniform sampler2D uSrc;
uniform vec2 uRegion;   // tamanho em pixels da area renderizada (previa em escala)
uniform vec2 uSize;     // tamanho da fonte na tela, em fracao do canvas (fit mode)
uniform float uOpacity;
uniform int uBlend;
uniform float uFade;    // master: escurece o resultado (1 = sem efeito)

vec3 blendColors(vec3 b, vec3 s)
{
    switch (uBlend) {
    case 1: return min(b + s, 1.0);                       // Adicionar
    case 2: return max(b - s, 0.0);                       // Subtrair
    case 3: return b * s;                                 // Multiplicar
    case 4: return 1.0 - (1.0 - b) * (1.0 - s);          // Tela
    case 5: return mix(2.0 * b * s,                       // Sobrepor
                       1.0 - 2.0 * (1.0 - b) * (1.0 - s), step(0.5, b));
    case 6: return max(b, s);                             // Clarear
    case 7: return min(b, s);                             // Escurecer
    case 8: return abs(b - s);                            // Diferenca
    default: return s;                                    // Normal
    }
}

void main()
{
    vec4 base = texelFetch(uBase, ivec2(gl_FragCoord.xy), 0);

    vec2 cuv = gl_FragCoord.xy / uRegion;            // canvas, y para cima
    vec2 suv = (cuv - 0.5) / uSize + 0.5;            // fonte, y para cima
    vec4 src = vec4(0.0);
    if (suv.x >= 0.0 && suv.x <= 1.0 && suv.y >= 0.0 && suv.y <= 1.0)
        src = texture(uSrc, vec2(suv.x, 1.0 - suv.y)); // textura da fonte: v=0 no topo

    float a = src.a * uOpacity;
    vec3 rgb = mix(base.rgb, blendColors(base.rgb, src.rgb), a);
    fragColor = vec4(rgb * uFade, 1.0);
}
)";

// Tamanho da fonte na tela, em fracao do canvas
void fitSize(FitMode mode, int cw, int ch, int sw, int sh, float& outX, float& outY)
{
    const float fcw = static_cast<float>(cw), fch = static_cast<float>(ch);
    const float fsw = static_cast<float>(std::max(sw, 1)), fsh = static_cast<float>(std::max(sh, 1));
    float kx = 1.0f, ky = 1.0f;
    switch (mode) {
    case FitMode::Fit:
        kx = ky = std::min(fcw / fsw, fch / fsh);
        break;
    case FitMode::Fill:
        kx = ky = std::max(fcw / fsw, fch / fsh);
        break;
    case FitMode::Stretch:
        kx = fcw / fsw;
        ky = fch / fsh;
        break;
    case FitMode::Original:
        break;
    }
    outX = fsw * kx / fcw;
    outY = fsh * ky / fch;
}

} // namespace

Compositor::Compositor(int width, int height)
    : fbo_{Fbo(width, height), Fbo(width, height)}, shader_(kVertexSource, kFragmentSource)
{
    glGenVertexArrays(1, &vao_);
}

Compositor::~Compositor()
{
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
}

void Compositor::resize(int width, int height)
{
    if (width == fbo_[0].width() && height == fbo_[0].height())
        return;
    Fbo a(width, height);
    Fbo b(width, height);
    fbo_[0] = std::move(a);
    fbo_[1] = std::move(b);
    current_ = 0;
}

void Compositor::render(const Composition& comp, float previewScale)
{
    renderedScale_ = std::clamp(previewScale, 0.1f, 1.0f);
    const int w = std::max(1, static_cast<int>(static_cast<float>(width()) * renderedScale_));
    const int h = std::max(1, static_cast<int>(static_cast<float>(height()) * renderedScale_));
    // Escala exata para os UVs baterem com a area renderizada
    renderedScale_ = static_cast<float>(w) / static_cast<float>(width());

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glViewport(0, 0, w, h);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);

    // Fundo preto opaco
    current_ = 0;
    fbo_[0].bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    bool anySolo = false;
    for (const Layer& l : comp.layers)
        anySolo = anySolo || l.solo;

    shader_.use();
    shader_.setInt("uBase", 0);
    shader_.setInt("uSrc", 1);
    shader_.setVec2("uRegion", static_cast<float>(w), static_cast<float>(h));
    shader_.setFloat("uFade", 1.0f);
    glBindVertexArray(vao_);

    for (int i = 0; i < comp.layerCount(); ++i) {
        const Layer& layer = comp.layers[static_cast<size_t>(i)];
        if (comp.masterBypass || layer.bypass || (anySolo && !layer.solo))
            continue;
        const Clip* clip = comp.activeClip(i);
        if (!clip || !clip->isOpen() || clip->source->texture() == 0)
            continue;
        const media::ISource& src = *clip->source;

        float sx = 1.0f, sy = 1.0f;
        fitSize(clip->fit, comp.width, comp.height, src.width(), src.height(), sx, sy);
        shader_.setVec2("uSize", sx, sy);
        shader_.setFloat("uOpacity", std::clamp(layer.opacity, 0.0f, 1.0f));
        shader_.setInt("uBlend", static_cast<int>(layer.blendMode));

        const int dst = 1 - current_;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fbo_[current_].texture());
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, src.texture());
        fbo_[dst].bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);
        current_ = dst;
    }

    // Master: um passe final sem fonte (opacidade 0) que so aplica o fade
    const float master = std::clamp(comp.masterOpacity, 0.0f, 1.0f);
    if (!comp.masterBypass && master < 0.999f) {
        shader_.setVec2("uSize", 1.0f, 1.0f);
        shader_.setFloat("uOpacity", 0.0f);
        shader_.setInt("uBlend", 0);
        shader_.setFloat("uFade", master);
        const int dst = 1 - current_;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fbo_[current_].texture());
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, fbo_[current_].texture());
        fbo_[dst].bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);
        current_ = dst;
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    Fbo::unbind();
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

} // namespace bsl
