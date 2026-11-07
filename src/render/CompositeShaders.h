#pragma once

#include <string>

namespace bsl::shaders {

inline constexpr const char* const kVertexSource = R"(#version 330 core
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

inline constexpr const char* const kSampleSource = R"(
vec4 sampleSource(sampler2D tex, vec2 cuv, vec2 size, int top)
{
    vec2 suv = (cuv - 0.5) / size + 0.5;
    if (suv.x >= 0.0 && suv.x <= 1.0 && suv.y >= 0.0 && suv.y <= 1.0)
        return texture(tex, vec2(suv.x, top == 1 ? 1.0 - suv.y : suv.y));
    return vec4(0.0);
}
)";

inline constexpr const char* const kBlendBody = R"(
uniform sampler2D uBase;
uniform sampler2D uSrc;
uniform vec2 uRegion;
uniform vec2 uSize;
uniform int uSrcTop;
uniform float uOpacity;
uniform int uBlend;
uniform int uXf;
uniform vec4 uXfBox;
uniform vec4 uXfPivot;
uniform vec4 uXfCrop;
uniform vec2 uCanvas;

vec2 transformedUv(vec2 cuv, out float coverage)
{
    vec2 p = vec2(cuv.x * uCanvas.x, (1.0 - cuv.y) * uCanvas.y);
    vec2 d = p - uXfPivot.xy;
    vec2 l = uXfPivot.xy + vec2(uXfPivot.z * d.x + uXfPivot.w * d.y, -uXfPivot.w * d.x + uXfPivot.z * d.y) - uXfBox.xy;
    float edge = min(min(l.x - uXfCrop.x, uXfBox.z - uXfCrop.z - l.x), min(l.y - uXfCrop.y, uXfBox.w - uXfCrop.w - l.y));
    coverage = clamp(edge + 0.5, 0.0, 1.0);
    vec2 uv = clamp(l / uXfBox.zw, 0.0, 1.0);
    return vec2(uv.x, 1.0 - uv.y);
}

vec3 blendColors(vec3 b, vec3 s)
{
    switch (uBlend) {
    case 1: return min(b + s, 1.0);
    case 2: return max(b - s, 0.0);
    case 3: return b * s;
    case 4: return 1.0 - (1.0 - b) * (1.0 - s);
    case 5: return mix(2.0 * b * s,
                       1.0 - 2.0 * (1.0 - b) * (1.0 - s), step(0.5, b));
    case 6: return max(b, s);
    case 7: return min(b, s);
    case 8: return abs(b - s);
    default: return s;
    }
}

void main()
{
    vec4 base = texelFetch(uBase, ivec2(gl_FragCoord.xy), 0);
    vec2 cuv = gl_FragCoord.xy / uRegion;
    float coverage = 1.0;
    if (uXf != 0)
        cuv = transformedUv(cuv, coverage);
    vec4 src = sampleSource(uSrc, cuv, uSize, uSrcTop);
    float a = src.a * uOpacity * coverage;
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

}
