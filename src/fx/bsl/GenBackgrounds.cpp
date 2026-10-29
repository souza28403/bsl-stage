// BSL Background (plugin FFGL "GBG1", GenBackgrounds): fundos generativos em tempo real, 22 estilos num so fragment shader,
// 3 cores, detalhe, densidade, suavidade, distorcao e rotacao; vinheta e grao de filme no final. Reescrito do original.

#include "fx/bsl/BslShaderSource.h"
#include "fx/bsl/BslSources.h"

#include <algorithm>

namespace bsl::fx::bsllight {

namespace {

using media::ParamSet;
using namespace media::param;

// Port do fragment shader original (GLSL 4.10 -> 3.30). As cores chegam em RGB (o plugin recebia HSB e convertia).
const char* const kBackgroundBody = R"(
uniform float iTime;
uniform int iStyle;
uniform float iSpeed;
uniform float iScale;
uniform vec3 iColor1;
uniform vec3 iColor2;
uniform vec3 iColor3; // fundo

uniform float iDetail;
uniform float iDensity;
uniform float iSoftness;
uniform float iDistortion;
uniform float iRotation;

#define PI 3.14159265359

vec2 hash( vec2 p ) {
    p = vec2( dot(p,vec2(127.1,311.7)), dot(p,vec2(269.5,183.3)) );
    return -1.0 + 2.0*fract(sin(p)*43758.5453123);
}

float random(vec2 p) {
    return fract(sin(dot(p.xy, vec2(12.9898,78.233))) * 43758.5453123);
}

float noise( in vec2 p ) {
    const float K1 = 0.366025404;
    const float K2 = 0.211324865;
    vec2 i = floor( p + (p.x+p.y)*K1 );
    vec2 a = p - i + (i.x+i.y)*K2;
    vec2 o = (a.x>a.y) ? vec2(1.0,0.0) : vec2(0.0,1.0);
    vec2 b = a - o + K2;
    vec2 c = a - 1.0 + 2.0*K2;
    vec3 h = max( 0.5-vec3(dot(a,a), dot(b,b), dot(c,c) ), 0.0 );
    vec3 n = h*h*h*h*vec3( dot(a,hash(i+0.0)), dot(b,hash(i+o)), dot(c,hash(i+1.0)));
    return dot( n, vec3(70.0) );
}

float fbm(vec2 x) {
    float v = 0.0;
    float a = 0.5;
    vec2 shift = vec2(100.0);
    mat2 rot = mat2(cos(0.5), sin(0.5), -sin(0.5), cos(0.50));
    for (int i = 0; i < 5; ++i) {
        v += a * noise(x);
        x = rot * x * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

// 0: Waves
vec3 fxWaves(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    float w = sin(pos.y * (2.0 + iDensity * 10.0) + noise(pos + t) * (1.0 + iDetail * 5.0) + t);
    float s = max(0.01, iSoftness);
    float line = smoothstep(s, 0.0, abs(w));
    vec3 fg = mix(c1, c2, clamp((pos.y + 0.5), 0.0, 1.0));
    return mix(c3, fg, line);
}

// 1: Cells
vec3 fxCells(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    float d = 1.0;
    for(int i = 0; i < 3; i++) {
        d = min(d, length(fract(pos * (2.0+iDensity*5.0) + noise(pos + float(i)*10.0 + t)*0.5) - 0.5));
    }
    float s = max(0.01, iSoftness);
    float val = smoothstep(s*0.5, s*2.0, d);
    vec3 fg = mix(c1, c2, val);
    return mix(c3, fg, smoothstep(0.0, 0.5, d));
}

// 2: Stars
vec3 fxStars(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    vec2 p = pos * (5.0 + iDensity*10.0);
    float n = noise(p * 20.0 - vec2(0.0, t * 5.0));
    float star = smoothstep(1.0 - iDensity*0.5, 1.0, n);
    float glow = smoothstep(1.0 - iSoftness - iDensity*0.5, 1.0, n) * 0.5;
    vec3 col = c1 * star + c2 * glow;
    return mix(c3, col + c1*0.2*noise(p+t*0.1), max(star, glow) + 0.1);
}

// 3: Clouds
vec3 fxClouds(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    vec2 q = vec2(fbm(pos + t*0.1), fbm(pos + vec2(1.0)));
    vec2 r = vec2(fbm(pos + q + vec2(1.7,9.2)+ t*0.15), fbm(pos + q + vec2(8.3,2.8)+ t*0.12));
    float f = fbm(pos+r);
    float s = max(0.1, iSoftness*2.0);
    float d = max(0.1, iDensity*2.0);
    float mask = smoothstep(d - s, d + s, f);
    return mix(c3, mix(c1, c2, f), mask);
}

// 4: Plasma
vec3 fxPlasma(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    vec2 p = pos * (5.0 + iDetail*10.0);
    float v1 = sin(p.x + t);
    float v2 = sin(p.y + t);
    float v3 = sin(p.x + p.y + t);
    float v4 = sin(length(p) + t);
    float val = (v1 + v2 + v3 + v4) * 0.25;
    float mixv = smoothstep(0.5-iSoftness, 0.5+iSoftness, val * 0.5 + 0.5);
    vec3 base = mix(c3, c1, clamp(mixv*2.0, 0.0, 1.0));
    return mix(base, c2, clamp((mixv-0.5)*2.0, 0.0, 1.0));
}

// 5: Matrix
vec3 fxMatrix(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    vec2 p = pos * (10.0 + iDensity*20.0);
    p.y += t * 5.0;
    vec2 id = floor(p);
    float r = hash(id).x;
    p.y += r * 10.0;
    float tail = fract(p.y);
    float drop = smoothstep(0.8 - iSoftness*0.5, 1.0, tail) * smoothstep(0.9, 1.0, fract(r + t*0.1));
    float mask = drop + smoothstep(0.95 - iSoftness*0.2, 1.0, tail)*1.5;
    return mix(c3, mix(c2, c1, drop), clamp(mask, 0.0, 1.0));
}

// 6: Spiral
vec3 fxSpiral(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    float a = atan(pos.y, pos.x);
    float r = length(pos);
    float dens = 10.0 + iDensity * 30.0;
    float val = sin(a * (2.0 + floor(iDetail*8.0)) - r * dens + t * 5.0);
    float s = max(0.01, iSoftness);
    float mask = smoothstep(-s, s, val);
    return mix(c3, mix(c1, c2, r), mask);
}

// 7: Cyber Grid
vec3 fxCyberGrid(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    vec2 p = pos;
    p.y += 0.2 - iDensity*0.1;
    if (p.y > 0.0) {
        float n = noise(p * (20.0+iDetail*20.0) - vec2(t*0.2, 0.0));
        return mix(c3, c2, smoothstep(0.95-iSoftness*0.1, 1.0, n));
    }
    vec2 g = vec2(p.x / abs(p.y), 1.0 / abs(p.y) - t * 2.0);
    vec2 grid = abs(fract(g * (1.0+iDensity*2.0)) - 0.5);
    float lines = min(grid.x, grid.y);
    float s = max(0.001, iSoftness*0.05);
    float glow = s / (lines + 0.002);
    float mask = smoothstep(0.0, 0.5, abs(p.y));
    return mix(c3, mix(c1, c2, sin(g.y)*0.5+0.5), clamp(glow*mask, 0.0, 1.0));
}

// 8: Liquid Aurora
vec3 fxLiquidAurora(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    vec2 p = pos * (1.0 + iDensity*4.0);
    float q = fbm(p - t * 0.1);
    vec2 r = vec2(fbm(p + q + t * 0.2), fbm(p + q - t * 0.1));
    float f = fbm(p + r * (1.0 + iDetail*3.0));
    float s = max(0.1, iSoftness);
    float glow = smoothstep(0.5-s, 0.5+s, f);
    vec3 col = mix(c1, c2, glow);
    col += mix(c2, c1, smoothstep(0.4, 0.6, noise(r * 5.0))) * 0.4;
    return mix(c3, col * (0.7 + 0.5 * glow), max(0.3, glow));
}

// 9: Balatro
vec3 fxBalatro(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    vec2 p = pos * (2.0 + iDensity*5.0);
    float r = length(p);
    float a = atan(p.y, p.x);
    float v = sin(r * 5.0 - t * 2.0 + a * floor(2.0+iDetail*5.0));
    v = abs(v);
    float s = max(0.01, iSoftness);
    float mask = smoothstep(0.2-s, 0.2+s, v);
    vec3 fg = mix(c1, c2, fract(r - t*0.5));
    return mix(c3, fg, mask);
}

// 10: Beams
vec3 fxBeams(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    vec2 p = pos * (1.0 + iDensity*10.0);
    float n = noise(vec2(p.x, t*0.5));
    float beam = smoothstep(0.6-iSoftness, 1.0, n + sin(p.x*10.0+t)*0.2);
    vec3 fg = mix(c1, c2, fract(p.x*0.1 + t*0.2));
    return mix(c3, fg + fg*beam, clamp(beam*2.0, 0.0, 1.0));
}

// 11: CRT Warp
vec3 fxCRTWarp(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    vec2 p = pos;
    float r = dot(p,p);
    p = p * (1.0 + r*(0.5+iDistortion));
    float lines = sin(p.y * (100.0 + iDensity*200.0) - t*10.0);
    float mask = smoothstep(0.0, 0.5+iSoftness, lines);
    vec3 fg = vec3(0.0);
    fg.r = noise(p*2.0 + t);
    fg.g = noise(p*2.0 + t + 0.02);
    fg.b = noise(p*2.0 + t + 0.04);
    fg = mix(c1, c2, (fg.r+fg.g+fg.b)/3.0);
    return mix(c3, fg, mask);
}

// 12: Dither
vec3 fxDither(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    vec2 p = pos * (50.0 + iDensity*100.0);
    float val = noise(pos * (1.0 + iDetail*5.0) + t);
    float dither = fract(sin(dot(floor(p), vec2(12.9898,78.233)))*43758.5453) * 0.5;
    float res = step(val, dither + iSoftness);
    return mix(c1, c2, res);
}

// 13: Floating Lines
vec3 fxFloatingLines(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    float y = fract(pos.y * (2.0 + iDensity*10.0) + t) - 0.5;
    float d = abs(y);
    float s = max(0.001, iSoftness*0.1);
    float mask = smoothstep(s, 0.0, d - (0.01 + iDetail*0.1));
    vec3 fg = mix(c1, c2, fract(pos.x + t));
    return mix(c3, fg, mask);
}

// 14: Galaxy
vec3 fxGalaxy(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    float r = length(pos);
    float a = atan(pos.y, pos.x);
    float arms = floor(2.0 + iDensity*6.0);
    float spiral = sin(a*arms - r*(10.0+iDetail*20.0) + t*2.0);
    float mask = smoothstep(0.5, 1.0, spiral * (1.0 - r*2.0));
    vec3 fg = mix(c1, c2, r);
    return mix(c3, fg, max(0.0, mask + iSoftness*0.5));
}

// 15: Gradient Blinds
vec3 fxGradientBlinds(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    vec2 p = pos * (1.0 + iDensity*5.0);
    float val = fract(p.x + p.y + t);
    float mask = smoothstep(iSoftness, 1.0-iSoftness, val);
    return mix(c3, mix(c1, c2, val), mask);
}

// 16: Grid Scan
vec3 fxGridScan(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    vec2 p = pos * (5.0 + iDensity*20.0);
    vec2 grid = abs(fract(p) - 0.5);
    float line = min(grid.x, grid.y);
    float mask = smoothstep(0.05+iSoftness*0.1, 0.0, line);
    float scanner = smoothstep(0.9, 1.0, fract(pos.y - t));
    return mix(c3, mix(c1, c2, scanner), max(mask*0.2, mask * scanner));
}

// 17: Prism
vec3 fxPrism(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    vec2 p = pos * (2.0 + iDensity*5.0);
    float a = atan(p.y, p.x);
    float r = length(p);
    float shape = abs(cos(a*3.0 + t) * sin(r*5.0 - t));
    float mask = smoothstep(0.5-iSoftness, 0.5+iSoftness, shape);
    return mix(c3, mix(c1, c2, fract(shape)), mask);
}

// 18: Radar
vec3 fxRadar(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    float r = length(pos);
    float a = atan(pos.y, pos.x);
    float sweep = fract((a + PI)/(2.0*PI) - t);
    float mask = smoothstep(0.0, 0.1 + iSoftness, sweep) * smoothstep(1.0, 0.8, sweep);
    float circ = smoothstep(0.9, 1.0, fract(r * (5.0+iDensity*10.0)));
    return mix(c3, mix(c1, c2, r), max(mask * smoothstep(1.0, 0.0, r), circ*0.2));
}

// 19: Ripple Grid
vec3 fxRippleGrid(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    float r = length(pos);
    vec2 p = pos + normalize(pos) * sin(r*(10.0+iDetail*20.0) - t*5.0) * (0.1+iDistortion*0.5);
    p *= (5.0 + iDensity*10.0);
    vec2 grid = abs(fract(p) - 0.5);
    float line = min(grid.x, grid.y);
    float mask = smoothstep(0.05+iSoftness*0.1, 0.0, line);
    return mix(c3, mix(c1, c2, r), mask);
}

// 20: Silk
vec3 fxSilk(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    float y = pos.y;
    float x = pos.x;
    float wave = 0.0;
    int count = 2 + int(iDetail * 5.0);
    for(int i=1; i<=6; i++) {
        if(i > count) break;
        wave += sin(x * float(i) * (1.0+iDensity*2.0) + t * 0.5) / float(i);
    }
    float d = abs(y - wave*0.2);
    float mask = smoothstep(0.1+iSoftness*0.5, 0.0, d);
    return mix(c3, mix(c1, c2, clamp(wave, 0.0, 1.0)), mask);
}

// 21: Acid Squares
vec3 fxAcidSquares(vec2 pos, float t, vec3 c1, vec3 c2, vec3 c3) {
    vec2 p = pos * (5.0 + iDensity*10.0);
    p += vec2(sin(p.y+t), cos(p.x+t)) * (0.5+iDistortion);
    vec2 grid = floor(p);
    float val = mod(grid.x + grid.y, 2.0);
    float mask = smoothstep(0.5-iSoftness, 0.5+iSoftness, val);
    return mix(c1, c2, mask);
}

void main()
{
    vec2 uv = glUv();
    float t = iTime * iSpeed * 5.0;

    vec3 c1 = iColor1;
    vec3 c2 = iColor2;
    vec3 c3 = iColor3;

    float aspect = uSize.x / max(1.0, uSize.y);
    vec2 pos = uv - 0.5;
    pos.x *= aspect;
    pos *= max(0.1, iScale * 5.0);

    float a = iRotation * PI * 2.0;
    mat2 rot = mat2(cos(a), sin(a), -sin(a), cos(a));
    pos = rot * pos;

    if(iDistortion > 0.0) {
        float dx = fbm(pos * 2.0 + t) - 0.5;
        float dy = fbm(pos * 2.0 - t) - 0.5;
        pos += vec2(dx, dy) * iDistortion * 2.0;
    }

    vec3 col = c3;

    if (iStyle == 0) col = fxWaves(pos, t, c1, c2, c3);
    else if (iStyle == 1) col = fxCells(pos, t, c1, c2, c3);
    else if (iStyle == 2) col = fxStars(pos, t, c1, c2, c3);
    else if (iStyle == 3) col = fxClouds(pos, t, c1, c2, c3);
    else if (iStyle == 4) col = fxPlasma(pos, t, c1, c2, c3);
    else if (iStyle == 5) col = fxMatrix(pos, t, c1, c2, c3);
    else if (iStyle == 6) col = fxSpiral(pos, t, c1, c2, c3);
    else if (iStyle == 7) col = fxCyberGrid(pos, t, c1, c2, c3);
    else if (iStyle == 8) col = fxLiquidAurora(pos, t, c1, c2, c3);
    else if (iStyle == 9) col = fxBalatro(pos, t, c1, c2, c3);
    else if (iStyle == 10) col = fxBeams(pos, t, c1, c2, c3);
    else if (iStyle == 11) col = fxCRTWarp(pos, t, c1, c2, c3);
    else if (iStyle == 12) col = fxDither(pos, t, c1, c2, c3);
    else if (iStyle == 13) col = fxFloatingLines(pos, t, c1, c2, c3);
    else if (iStyle == 14) col = fxGalaxy(pos, t, c1, c2, c3);
    else if (iStyle == 15) col = fxGradientBlinds(pos, t, c1, c2, c3);
    else if (iStyle == 16) col = fxGridScan(pos, t, c1, c2, c3);
    else if (iStyle == 17) col = fxPrism(pos, t, c1, c2, c3);
    else if (iStyle == 18) col = fxRadar(pos, t, c1, c2, c3);
    else if (iStyle == 19) col = fxRippleGrid(pos, t, c1, c2, c3);
    else if (iStyle == 20) col = fxSilk(pos, t, c1, c2, c3);
    else if (iStyle == 21) col = fxAcidSquares(pos, t, c1, c2, c3);

    // Vinheta e grao de filme
    vec2 nuv = uv - 0.5;
    float vig = smoothstep(0.8, 0.2, length(nuv));
    col *= vig;
    col += (random(uv + t) - 0.5) * 0.08;

    fragColor = vec4(col, 1.0);
}
)";

const std::vector<media::ParamOption> kStyles = {
    {"waves", "Ondas (Waves)"},          {"cells", "Células (Cells)"},          {"stars", "Estrelas (Stars)"},
    {"clouds", "Nuvens (Clouds)"},       {"plasma", "Plasma"},                  {"matrix", "Matrix"},
    {"spiral", "Espiral (Spiral)"},      {"cybergrid", "Cyber Grid"},           {"aurora", "Aurora líquida"},
    {"balatro", "Balatro"},              {"beams", "Feixes (Beams)"},           {"crt", "CRT Warp"},
    {"dither", "Dither"},                {"lines", "Linhas flutuantes"},        {"galaxy", "Galáxia"},
    {"blinds", "Persianas em degradê"},  {"gridscan", "Grid Scan"},             {"prism", "Prisma"},
    {"radar", "Radar"},                  {"ripple", "Grade de ondas (Ripple)"}, {"silk", "Seda (Silk)"},
    {"acid", "Quadrados ácidos"}};

class GenBackgroundsSource final : public BslShaderSource
{
public:
    using BslShaderSource::BslShaderSource;
    std::vector<media::ParamDesc> paramDescs() const override { return describeGenBackgrounds(); }

protected:
    std::string fragmentBody() const override { return kBackgroundBody; }

    void setUniforms(Shader& s, double time) override
    {
        const ParamSet p = currentParams();
        int style = 0;
        const std::string cur = p.s("style");
        for (size_t i = 0; i < kStyles.size(); ++i)
            if (kStyles[i].value == cur)
                style = static_cast<int>(i);
        s.setFloat("iTime", static_cast<float>(time));
        s.setInt("iStyle", style);
        // O shader usa t = iTime * iSpeed * 5 e pos *= max(0.1, iScale * 5): os parametros ja vem em unidades reais
        s.setFloat("iSpeed", p.f("speed") / 5.0f);
        s.setFloat("iScale", p.f("scale") / 5.0f);
        const media::Color c1 = p.c("color1"), c2 = p.c("color2"), c3 = p.c("background");
        s.setVec3("iColor1", c1.r, c1.g, c1.b);
        s.setVec3("iColor2", c2.r, c2.g, c2.b);
        s.setVec3("iColor3", c3.r, c3.g, c3.b);
        s.setFloat("iDetail", p.f("detail") * 0.01f);
        s.setFloat("iDensity", p.f("density") * 0.01f);
        s.setFloat("iSoftness", p.f("softness") * 0.01f);
        s.setFloat("iDistortion", p.f("distortion") * 0.01f);
        s.setFloat("iRotation", p.f("rotation") / 360.0f);
    }
};

} // namespace

std::vector<media::ParamDesc> describeGenBackgrounds()
{
    std::vector<media::ParamDesc> d;
    d.push_back(makeEnum("style", "Estilo", kStyles, "waves"));
    d.push_back(makeFloat("speed", "Velocidade", 2.5f, 0.0f, 5.0f, "%.2f x"));
    d.push_back(makeFloat("scale", "Escala", 2.5f, 0.1f, 5.0f, "%.2f"));
    d.back().tooltip = "Aumenta ou diminui o tamanho das formas";
    d.push_back(makeFloat("rotation", "Rotação", 0.0f, 0.0f, 360.0f, "%.0f °"));

    d.push_back(makeColor("color1", "Cor 1", {1.0f, 0.0f, 0.0f, 1.0f}, "Cores"));
    d.push_back(makeColor("color2", "Cor 2", {0.0f, 1.0f, 1.0f, 1.0f}));
    d.push_back(makeColor("background", "Cor de fundo", {0.0f, 0.0f, 0.0f, 1.0f}));
    d.back().tooltip = "A cor base da tela. Preto no brilho = fundo escuro";

    d.push_back(makeFloat("detail", "Detalhe", 50.0f, 0.0f, 100.0f, "%.0f %%", "Controles PRO"));
    d.back().tooltip = "Complexidade do padrão (ex.: oitavas do ruído). Valores altos pesam mais";
    d.push_back(makeFloat("density", "Densidade", 50.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Quantidade de elementos na tela";
    d.push_back(makeFloat("softness", "Suavidade", 50.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Suaviza as bordas e adiciona glow";
    d.push_back(makeFloat("distortion", "Distorção", 0.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Dobra o espaço com ruído. Com 0 o padrão fica reto";
    return d;
}

std::unique_ptr<media::ISource> makeGenBackgrounds(int canvasW, int canvasH)
{
    return std::make_unique<GenBackgroundsSource>(canvasW, canvasH);
}

} // namespace bsl::fx::bsllight
