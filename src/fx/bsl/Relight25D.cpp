// BSL Relight 2.5D: iluminação virtual em vídeo 2D. A profundidade estimada pela IA local (Depth Anything V2 Small, ver
// src/ai) vira um mapa de normais por derivadas (Sobel) com intensidade de relevo; até 4 luzes pontuais 3D (posição X/Y/Z,
// cor, intensidade, raio e falloff, especular e brilho) iluminam a imagem como se ela tivesse volume. Luz ambiente, mistura
// com o original e modo de depuração (ver profundidade ou normais).

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace bsl::fx::bsllight {

namespace {

using media::ParamSet;
using namespace media::param;

constexpr int kLights = 4;

// Coordenadas: x e y em "alturas da imagem" (x de 0 a aspecto, y de cima para baixo), z para fora da tela.
const char* const kRelightBody = R"(
uniform sampler2D uDepth;    // R16F 0..1 (maior = mais perto), linha 0 = topo
uniform float uAspect;
uniform float uBump;         // intensidade do relevo nas normais
uniform float uRelief;       // altura 3D da superficie (em alturas da imagem) quando a profundidade = 1
uniform float uAmbient;
uniform float uMix;
uniform int uDebug;          // 0 = luz, 1 = profundidade, 2 = normais
uniform vec4 uLpos[4];       // xyz = posicao, w = ligada
uniform vec3 uLcol[4];       // cor * intensidade
uniform vec4 uLpar[4];       // x = raio, y = falloff, z = especular, w = brilho (expoente)

float depthAt(vec2 uv) { return texture(uDepth, uv).r; }

void main()
{
    vec2 uv = imgPos();
    vec4 src = samp(uv);
    float d = depthAt(uv);

    // Normais por Sobel sobre a profundidade (inclinacao media por texel -> inclinacao por altura de imagem)
    vec2 ts = 1.0 / vec2(textureSize(uDepth, 0));
    float tl = depthAt(uv + ts * vec2(-1.0, -1.0)), tc = depthAt(uv + ts * vec2(0.0, -1.0)), tr = depthAt(uv + ts * vec2(1.0, -1.0));
    float ml = depthAt(uv + ts * vec2(-1.0, 0.0)),                                          mr = depthAt(uv + ts * vec2(1.0, 0.0));
    float bl = depthAt(uv + ts * vec2(-1.0, 1.0)),  bc = depthAt(uv + ts * vec2(0.0, 1.0)),  br = depthAt(uv + ts * vec2(1.0, 1.0));
    float gx = ((tr + 2.0 * mr + br) - (tl + 2.0 * ml + bl)) / 8.0;
    float gy = ((bl + 2.0 * bc + br) - (tl + 2.0 * tc + tr)) / 8.0;
    vec2 texelUnits = vec2(uAspect * ts.x, ts.y);
    vec3 n = normalize(vec3(-gx * uRelief / texelUnits.x * uBump, -gy * uRelief / texelUnits.y * uBump, 1.0));

    if (uDebug == 1) { fragColor = vec4(vec3(d), src.a); return; }
    if (uDebug == 2) { fragColor = vec4(n * 0.5 + 0.5, src.a); return; }

    vec3 P = vec3(uv.x * uAspect, uv.y, d * uRelief);
    vec3 lit = vec3(0.0);
    vec3 spec = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        if (uLpos[i].w < 0.5)
            continue;
        vec3 l = uLpos[i].xyz - P;
        float dist = max(length(l), 1.0e-4);
        vec3 ld = l / dist;
        float att = pow(clamp(1.0 - dist / max(uLpar[i].x, 1.0e-3), 0.0, 1.0), max(uLpar[i].y, 0.05));
        float nl = dot(n, ld);
        if (nl <= 0.0)
            continue;
        lit += uLcol[i] * (nl * att);
        vec3 h = normalize(ld + vec3(0.0, 0.0, 1.0));
        spec += uLcol[i] * (pow(max(dot(n, h), 0.0), max(uLpar[i].w, 1.0)) * uLpar[i].z * att);
    }
    vec3 result = src.rgb * (uAmbient + lit) + spec;
    fragColor = vec4(clamp(mix(src.rgb, result, uMix), 0.0, 1.0), src.a);
}
)";

struct LightDefault
{
    bool on;
    float x, y, z;       // %
    media::Color color;
    float intensity;     // %
    float radius;        // % da altura
};
constexpr LightDefault kLightDefaults[kLights] = {
    {true, 22.0f, 35.0f, 35.0f, {1.0f, 0.88f, 0.70f, 1.0f}, 130.0f, 130.0f},
    {false, 78.0f, 35.0f, 35.0f, {0.45f, 0.70f, 1.00f, 1.0f}, 130.0f, 130.0f},
    {false, 50.0f, 85.0f, 35.0f, {1.00f, 0.35f, 0.75f, 1.0f}, 110.0f, 120.0f},
    {false, 50.0f, 10.0f, 50.0f, {0.50f, 1.00f, 0.55f, 1.0f}, 110.0f, 120.0f},
};

class RelightEffect final : public AiEffect
{
public:
    RelightEffect() : AiEffect({kRelightId, "BSL Relight 2.5D", "Iluminação"}, describeRelight()) {}

protected:
    std::string fragmentSource() const override { return std::string(kBslPrelude) + kRelightBody; }
    unsigned aiNeed() const override { return ai::kNeedDepth; }

    void prepareAi(const EffectContext&, int w, int h, const ai::AiFrame& frame) override
    {
        depthTex_ = frame.depth;
        aspect_ = static_cast<float>(w) / static_cast<float>(std::max(h, 1));
    }

    void render(const EffectContext&, int, int) override
    {
        const ParamSet& p = params();
        bindTexture(1, depthTex_);
        Shader& s = shader();
        s.setInt("uDepth", 1);
        s.setFloat("uAspect", aspect_);
        s.setFloat("uBump", p.f("bump") * 0.01f);
        s.setFloat("uRelief", p.f("relief") * 0.01f);
        s.setFloat("uAmbient", p.f("ambient") * 0.01f);
        s.setFloat("uMix", p.f("mix") * 0.01f);
        const int dbg = enumIndex(p, "debug");
        s.setInt("uDebug", dbg);
        for (int i = 0; i < kLights; ++i) {
            const std::string pre = "l" + std::to_string(i + 1) + "_";
            const std::string idx = "[" + std::to_string(i) + "]";
            const bool on = p.b(pre + "on");
            s.setVec4(("uLpos" + idx).c_str(), p.f(pre + "x") * 0.01f * aspect_, p.f(pre + "y") * 0.01f, p.f(pre + "z") * 0.01f,
                      on ? 1.0f : 0.0f);
            const media::Color c = p.c(pre + "color");
            const float k = p.f(pre + "intensity") * 0.01f;
            s.setVec3(("uLcol" + idx).c_str(), c.r * k, c.g * k, c.b * k);
            s.setVec4(("uLpar" + idx).c_str(), p.f(pre + "radius") * 0.01f, p.f(pre + "falloff"), p.f(pre + "spec") * 0.01f,
                      p.f(pre + "shine"));
        }
    }

private:
    GLuint depthTex_ = 0;
    float aspect_ = 1.0f;
};

} // namespace

std::vector<media::ParamDesc> describeRelight()
{
    std::vector<media::ParamDesc> d;
    d.push_back(makeFloat("bump", "Intensidade do relevo", 100.0f, 0.0f, 400.0f, "%.0f %%", "Relevo (IA)"));
    d.back().tooltip = "Quanto a profundidade da IA entorta as normais. Mais relevo = luz mais dramática sobre rosto e corpo";
    d.push_back(makeFloat("relief", "Altura do relevo", 30.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Altura 3D da cena (em % da altura da imagem): afasta as luzes do fundo e aproxima as do primeiro plano";
    d.push_back(makeFloat("ambient", "Luz ambiente", 25.0f, 0.0f, 100.0f, "%.0f %%", "Mistura"));
    d.back().tooltip = "Quanto da imagem original continua visível onde nenhuma luz chega";
    d.push_back(makeFloat("mix", "Mistura com o original", 100.0f, 0.0f, 100.0f, "%.0f %%"));
    d.push_back(makeEnum("debug", "Depuração", {{"off", "Imagem iluminada"}, {"depth", "Ver profundidade"}, {"normal", "Ver normais"}}, "off"));
    d.back().tooltip = "Mostra o que a IA enxergou: a profundidade (claro = perto) ou o mapa de normais";

    for (int i = 0; i < kLights; ++i) {
        const LightDefault& def = kLightDefaults[i];
        const std::string pre = "l" + std::to_string(i + 1) + "_";
        const std::string group = "Luz " + std::to_string(i + 1);
        const std::string on = pre + "on";
        d.push_back(makeBool(on, "Ligada", def.on, group));
        auto add = [&](media::ParamDesc desc, const char* tip = nullptr) {
            desc.enabledIf = on;
            if (tip)
                desc.tooltip = tip;
            d.push_back(std::move(desc));
        };
        add(makeFloat(pre + "x", "Posição X", def.x, -50.0f, 150.0f, "%.0f %%"), "0 % = borda esquerda da imagem, 100 % = borda direita");
        add(makeFloat(pre + "y", "Posição Y", def.y, -50.0f, 150.0f, "%.0f %%"), "0 % = topo da imagem, 100 % = base");
        add(makeFloat(pre + "z", "Posição Z", def.z, 5.0f, 200.0f, "%.0f %%"), "Distância da luz à imagem, em % da altura. Perto = foco pequeno e forte");
        // Fase 5 (DMX / Art-Net): mapear os canais de cor e de intensidade de cada luz nestes parametros
        // (l1_color / l1_intensity ... l4_color / l4_intensity); eles sao parametros normais, como qualquer outro.
        add(makeColor(pre + "color", "Cor", def.color));
        add(makeFloat(pre + "intensity", "Intensidade", def.intensity, 0.0f, 400.0f, "%.0f %%"));
        add(makeFloat(pre + "radius", "Raio", def.radius, 5.0f, 400.0f, "%.0f %%"), "Alcance da luz, em % da altura da imagem");
        add(makeFloat(pre + "falloff", "Queda (falloff)", 1.5f, 0.2f, 6.0f, "%.2f"), "Maior = a luz some mais rápido com a distância");
        add(makeFloat(pre + "spec", "Especular", 60.0f, 0.0f, 300.0f, "%.0f %%"), "Reflexo brilhante (como pele oleosa ou metal)");
        add(makeFloat(pre + "shine", "Brilho", 32.0f, 1.0f, 128.0f, "%.0f"), "Dureza do reflexo: maior = ponto de luz menor e mais forte");
    }
    return d;
}

std::unique_ptr<IEffect> makeRelight()
{
    return std::make_unique<RelightEffect>();
}

} // namespace bsl::fx::bsllight
