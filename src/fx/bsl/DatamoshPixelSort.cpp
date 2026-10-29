// BSL Datamosh & Pixel Sort (plugin FFGL "DMSH"): pixel sorting por limiar de luma, datamosh por feedback, contorno neon
// (Sobel), mascara circular de protecao e ghost trails; graves e agudos modulam tudo. Reescrito do original.
//
// O modo "Resolume FFT" do plugin nao tinha reatividade (nao registrava o buffer FFT e zerava as bandas). Aqui o audio
// vem sempre do AudioAnalyzer do app (Ver > Audio), entao o efeito reage em qualquer configuracao.

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"

#include <algorithm>

namespace bsl::fx::bsllight {

namespace {

using media::ParamSet;
using namespace media::param;

// Port do fragment shader original (GLSL 4.10 -> 3.30). As direcoes e os vizinhos do Sobel foram convertidos para o
// espaco da imagem (y para baixo) mantendo o resultado visual do OpenGL (y para cima) do plugin.
const char* const kDatamoshBody = R"(
uniform sampler2D uFb;
uniform float uSortThreshold;
uniform float uSortLength;
uniform int uSortDirection;     // 0 horizontal, 1 vertical, 2 diagonal
uniform float uMoshAmount;
uniform float uMoshTolerance;
uniform float uProtectRadius;
uniform float uProtectFeather;
uniform vec2 uProtectPos;       // 0..1, y de cima para baixo
uniform float uNeonEdges;
uniform vec3 uNeonColor;
uniform float uGhost;
uniform float uBass;
uniform float uHigh;

vec4 fbP(vec2 p) { vec4 c = texture(uFb, toTex(p)); c.rgb *= c.a; return c; }

vec3 rgb2yuv(vec3 rgb)
{
    return vec3(0.299 * rgb.r + 0.587 * rgb.g + 0.114 * rgb.b,
                -0.14713 * rgb.r - 0.28886 * rgb.g + 0.436 * rgb.b,
                0.615 * rgb.r - 0.51499 * rgb.g - 0.10001 * rgb.b);
}

void main()
{
    vec2 uv = imgPos();
    vec4 cam = sampP(uv);

    // 1. Pixel sorting (limiar de luma + rastro)
    vec4 sortedCol = cam;
    float effSortLen = uSortLength * (1.0 + uHigh * 2.5);
    if (effSortLen > 0.01) {
        vec2 dir = vec2(1.0, 0.0);
        if (uSortDirection == 1)
            dir = vec2(0.0, -1.0);
        else if (uSortDirection == 2)
            dir = normalize(vec2(1.0, -1.0));
        float dynamicThresh = clamp(uSortThreshold - (uBass * 0.3), 0.0, 1.0);
        for (int i = 1; i < 28; i++) {
            vec4 sCol = sampP(uv - dir * (float(i) * 0.005 * effSortLen));
            float sLuma = dot(sCol.rgb, vec3(0.299, 0.587, 0.114));
            if (sLuma > dynamicThresh) {
                sortedCol = sCol;
                break;
            }
        }
    }

    // 2. Datamosh temporal
    vec4 lastFrame = fbP(uv);
    float effMosh = uMoshAmount * (1.0 + uBass * 1.5);
    vec4 finalCol = sortedCol;
    if (effMosh > 0.01) {
        vec3 currYUV = rgb2yuv(sortedCol.rgb);
        vec3 lastYUV = rgb2yuv(lastFrame.rgb);
        vec2 flow = vec2(currYUV.y - lastYUV.y, currYUV.z - lastYUV.z);
        vec4 moshedCol = fbP(uv - vec2(flow.x, -flow.y) * effMosh * 0.25);
        float diff = length(currYUV - lastYUV);
        if (diff < uMoshTolerance)
            finalCol = moshedCol;                                   // simula a perda do I-frame
        else
            finalCol = mix(moshedCol, sortedCol, 0.5 + uBass * 0.5);
    }

    // 3. Contorno neon (Sobel)
    if (uNeonEdges > 0.0) {
        vec2 tx = 1.0 / uSize;
        float tl = length(sampP(uv + vec2(-tx.x, -tx.y)).rgb);
        float t  = length(sampP(uv + vec2(0.0, -tx.y)).rgb);
        float tr = length(sampP(uv + vec2(tx.x, -tx.y)).rgb);
        float l  = length(sampP(uv + vec2(-tx.x, 0.0)).rgb);
        float r  = length(sampP(uv + vec2(tx.x, 0.0)).rgb);
        float bl = length(sampP(uv + vec2(-tx.x, tx.y)).rgb);
        float b  = length(sampP(uv + vec2(0.0, tx.y)).rgb);
        float br = length(sampP(uv + vec2(tx.x, tx.y)).rgb);
        float sobelX = tl + 2.0 * l + bl - tr - 2.0 * r - br;
        float sobelY = tl + 2.0 * t + tr - bl - 2.0 * b - br;
        float edge = length(vec2(sobelX, sobelY));
        edge = smoothstep(0.1, 0.8, edge) * 2.0;
        edge *= (1.0 + uBass * 1.5);
        vec3 neonCol = uNeonColor * edge;
        vec3 darkBg = finalCol.rgb * 0.15;
        finalCol.rgb = mix(finalCol.rgb, darkBg + neonCol, uNeonEdges);
    }

    // 4. Mascara de protecao do DJ (circulo com a imagem original)
    if (uProtectRadius > 0.0) {
        float aspect = uSize.x / uSize.y;
        vec2 p = imgPos();
        p.x *= aspect;
        vec2 center = uProtectPos;
        center.x *= aspect;
        float dist = distance(p, center);
        float feather = max(0.01, uProtectFeather);
        float maskVal = smoothstep(uProtectRadius - feather, uProtectRadius, dist);
        finalCol = mix(cam, finalCol, maskVal);
    }

    // 5. Ghost blur (rastro temporal)
    if (uGhost > 0.0)
        finalCol = mix(finalCol, fbP(uv), uGhost);

    fragColor = toStraight(finalCol.rgb, cam.a);
}
)";

class DatamoshEffect final : public BslEffect
{
public:
    DatamoshEffect() : BslEffect({kDatamoshId, "BSL Datamosh & Pixel Sort", "Reativo ao áudio"}, describeDatamosh()) {}

protected:
    std::string fragmentSource() const override { return std::string(kBslPrelude) + kDatamoshBody; }

    void prepare(const EffectContext& ctx, int w, int h) override
    {
        fb_.ensure(w, h);
        const ParamSet& p = params();

        const double dt = std::clamp(ctx.dt, 0.001, 0.1);
        const float targetBass = std::clamp(bandBass(ctx) * (p.f("bassReact") * 0.01f) * 2.0f, 0.0f, 2.0f);
        const float targetHigh = std::clamp(bandHigh(ctx) * (p.f("highReact") * 0.01f) * 2.0f, 0.0f, 2.0f);
        bass_ = riseFallFollow(bass_, targetBass, dt, 12.0f);
        high_ = riseFallFollow(high_, targetHigh, dt, 18.0f);
    }

    void render(const EffectContext&, int, int) override
    {
        const ParamSet& p = params();
        bindTexture(1, fb_.texture());
        Shader& s = shader();
        s.setInt("uFb", 1);
        s.setFloat("uSortThreshold", p.f("sortThreshold") * 0.01f);
        s.setFloat("uSortLength", p.f("sortLength") * 0.01f);
        s.setInt("uSortDirection", enumIndex(p, "sortDirection"));
        s.setFloat("uMoshAmount", p.f("moshAmount") * 0.01f);
        s.setFloat("uMoshTolerance", 0.02f + p.f("moshDecay") * 0.01f * 0.3f);
        s.setFloat("uProtectRadius", p.f("protectRadius") * 0.01f);
        s.setFloat("uProtectFeather", p.f("protectFeather") * 0.01f);
        s.setVec2("uProtectPos", p.f("protectX") * 0.01f, p.f("protectY") * 0.01f);
        s.setFloat("uNeonEdges", p.f("neonEdges") * 0.01f);
        const media::Color neon = p.c("neonColor");
        s.setVec3("uNeonColor", neon.r, neon.g, neon.b);
        s.setFloat("uGhost", p.f("ghostBlur") * 0.01f); // o limite de 94% ja esta na faixa do parametro
        s.setFloat("uBass", bass_);
        s.setFloat("uHigh", high_);
    }

    void afterDraw(const EffectContext&, int w, int h, const Fbo& output) override { fb_.capture(output, w, h); }

private:
    FeedbackBuffer fb_;
    float bass_ = 0.0f;
    float high_ = 0.0f;
};

} // namespace

std::vector<media::ParamDesc> describeDatamosh()
{
    std::vector<media::ParamDesc> d;
    d.push_back(makeFloat("gain", "Ganho do áudio", 2.1f, 0.1f, 4.1f, "%.2f x", "Áudio"));
    d.back().tooltip = "Multiplica as bandas do analisador (Ver > Áudio) só para este efeito";
    d.push_back(makeFloat("gate", "Noise gate", 0.0075f, 0.0f, 0.15f, "%.4f"));
    d.back().tooltip = "Corta o ruído de fundo: bandas abaixo deste nível viram zero";

    d.push_back(makeFloat("sortThreshold", "Limiar do sort", 70.0f, 0.0f, 100.0f, "%.0f %%", "Pixel sorting"));
    d.back().tooltip = "Luminosidade a partir da qual os pixels começam a escorrer. O grave reduz o limiar";
    d.push_back(makeFloat("sortLength", "Comprimento do sort", 0.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Tamanho do rastro do escorrido; os agudos o alongam";
    d.push_back(makeEnum("sortDirection", "Direção do sort",
                         {{"horizontal", "Horizontal"}, {"vertical", "Vertical"}, {"diagonal", "Diagonal"}}, "horizontal"));

    d.push_back(makeFloat("moshAmount", "Quantidade de mosh", 0.0f, 0.0f, 100.0f, "%.0f %%", "Datamosh"));
    d.back().tooltip = "Quanto do quadro anterior sobrevive e se contorce";
    d.push_back(makeFloat("moshDecay", "Persistência do mosh", 50.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Tolerância do datamosh: valores altos demoram a atualizar a imagem e esticam as cores";

    d.push_back(makeFloat("protectRadius", "Raio da proteção", 0.0f, 0.0f, 100.0f, "%.0f %%", "Proteção do DJ"));
    d.back().tooltip = "Círculo que mantém a câmera original (em % da altura). 0 desliga";
    d.push_back(makeFloat("protectFeather", "Suavidade da borda", 20.0f, 0.0f, 100.0f, "%.0f %%"));
    d.push_back(makeFloat("protectX", "Proteção X", 50.0f, 0.0f, 100.0f, "%.0f %%"));
    d.push_back(makeFloat("protectY", "Proteção Y", 50.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "0 % = topo da imagem";

    d.push_back(makeFloat("neonEdges", "Contorno neon", 0.0f, 0.0f, 100.0f, "%.0f %%", "Contorno neon"));
    d.back().tooltip = "Filtro Sobel. Em 100 % o fundo escurece e só os contornos brilham";
    d.push_back(makeColor("neonColor", "Cor do neon", {1.0f, 0.0f, 0.0f, 1.0f}));

    d.push_back(makeFloat("ghostBlur", "Ghost blur", 0.0f, 0.0f, 94.0f, "%.0f %%", "Rastro fantasma"));
    d.back().tooltip = "Rastro temporal dos movimentos (limitado a 94 % para a tela não estourar)";

    d.push_back(makeFloat("bassReact", "Reação aos graves", 50.0f, 0.0f, 100.0f, "%.0f %%", "Reatividade"));
    d.back().tooltip = "Quanto o grave amplifica o escorrido e o mosh";
    d.push_back(makeFloat("highReact", "Reação aos agudos", 50.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Quanto os agudos alongam o escorrido";
    return d;
}

std::unique_ptr<IEffect> makeDatamosh()
{
    return std::make_unique<DatamoshEffect>();
}

} // namespace bsl::fx::bsllight
