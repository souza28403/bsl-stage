#pragma once

// Trechos de GLSL compartilhados pelo Compositor (saida) e pela Previa de camada (um passe de blend sobre preto).

#include <string>

namespace bsl::shaders {

inline constexpr const char* const kVertexSource = R"(#version 330 core
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Amostra a fonte encaixada no canvas: 'cuv' = posicao no canvas (y para cima), 'size' = tamanho da fonte na tela em
// fracao do canvas (fit mode). 'top' = 1 quando a linha 0 da textura e o topo da imagem (fontes e cadeias de clip);
// 0 quando e a base (FBOs do canvas e da cadeia de camada).
inline constexpr const char* const kSampleSource = R"(
vec4 sampleSource(sampler2D tex, vec2 cuv, vec2 size, int top)
{
    vec2 suv = (cuv - 0.5) / size + 0.5;
    if (suv.x >= 0.0 && suv.x <= 1.0 && suv.y >= 0.0 && suv.y <= 1.0)
        return texture(tex, vec2(suv.x, top == 1 ? 1.0 - suv.y : suv.y));
    return vec4(0.0);
}
)";

// Um passe por camada: base (acumulado) + fonte -> novo acumulado.
// Alpha da fonte e reto; resultado = mix(base, blend(base, src.rgb), src.a * opacidade).
inline constexpr const char* const kBlendBody = R"(
uniform sampler2D uBase;
uniform sampler2D uSrc;
uniform vec2 uRegion;   // tamanho em pixels da area renderizada (previa em escala)
uniform vec2 uSize;     // tamanho da fonte na tela, em fracao do canvas (fit mode)
uniform int uSrcTop;
uniform float uOpacity;
uniform int uBlend;

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
    vec4 src = sampleSource(uSrc, gl_FragCoord.xy / uRegion, uSize, uSrcTop);
    float a = src.a * uOpacity;
    vec3 rgb = mix(base.rgb, blendColors(base.rgb, src.rgb), a);
    fragColor = vec4(rgb, 1.0);
}
)";

inline std::string fragment(const char* body, bool withSample)
{
    std::string s = "#version 330 core\nout vec4 fragColor;\n";
    if (withSample)
        s += kSampleSource;
    s += body;
    return s;
}

} // namespace bsl::shaders
