// BSL Volumetric Performer Aura & Fluid Collisions: usa a máscara de pessoa da IA local (MODNet, ver src/ai) não só para
// recortar o DJ, mas para empurrar um fluido 2D ao redor do corpo conforme as bordas da silhueta se mexem.
//
// Por quadro, tudo na GPU e em baixa resolução (1/8, 1/4 ou 1/2 da imagem; padrão 1/4):
//  1. suaviza a máscara (tent 3x3 em passos de 1,5 texel; a borda vem do gradiente dela);
//  2. derivadas espaciais e temporais da máscara (Ix, Iy, It);
//  3. fluxo óptico de Lucas-Kanade de 1 nível sobre a máscara (janela 5x5 com regularização): o movimento das bordas em
//     texels por quadro, mantido e esmaecido entre as atualizações da IA (que roda a 15-30 fps);
//  4. velocidade do fluido: advecção semi-lagrangiana + difusão leve + amortecimento; perto das bordas em movimento (a
//     "espessura da aura" é o alcance disso) a velocidade é puxada para o movimento da borda;
//  5. densidade: advecção semi-lagrangiana pela velocidade, dissipação exponencial e injeção nas bordas que se movem.
// Máscara parada = sem injeção: a densidade e a velocidade só caem. O desenho final (tela cheia) tem 3 visualizações:
// aura/fumaça (gradiente de 2 cores com brilho), partículas (densidade pontilhada, uma passada só) e grade de lasers
// deformada pelo campo de velocidade.
//
// Texturas da simulação: linha 0 = TOPO da imagem (amostradas com imgPos(), como as da IA); RGBA32F (o Fbo só tem 8 bits
// ou 32F).

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <string>

namespace bsl::fx::bsllight {

namespace {

using media::ParamSet;
using namespace media::param;

const char* const kSimHeader = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
)";

// 1. Máscara suavizada (r)
const char* const kSmoothBody = R"(
uniform sampler2D uMask;   // R8 da IA, linha 0 = topo
uniform vec2 uTexel;       // 1 / tamanho da simulacao
uniform float uStep;       // passo dos taps, em texels
void main()
{
    float s = 0.0;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            float w = (2.0 - abs(float(i))) * (2.0 - abs(float(j)));
            s += w * texture(uMask, vUv + vec2(float(i), float(j)) * uTexel * uStep).r;
        }
    fragColor = vec4(s / 16.0, 0.0, 0.0, 1.0);
}
)";

// 2. Derivadas: xy = gradiente (media dos dois quadros), z = diferenca temporal, w = mascara
const char* const kDerivBody = R"(
uniform sampler2D uCur;
uniform sampler2D uPrev;
uniform vec2 uTexel;
float c(vec2 o) { return texture(uCur, vUv + o * uTexel).r; }
float p(vec2 o) { return texture(uPrev, vUv + o * uTexel).r; }
void main()
{
    float ix = 0.25 * ((c(vec2(1.0, 0.0)) - c(vec2(-1.0, 0.0))) + (p(vec2(1.0, 0.0)) - p(vec2(-1.0, 0.0))));
    float iy = 0.25 * ((c(vec2(0.0, 1.0)) - c(vec2(0.0, -1.0))) + (p(vec2(0.0, 1.0)) - p(vec2(0.0, -1.0))));
    float it = c(vec2(0.0)) - p(vec2(0.0));
    fragColor = vec4(ix, iy, it, c(vec2(0.0)));
}
)";

// 3. Lucas-Kanade de 1 nivel (janela 5x5 ponderada): movimento das bordas em texels por quadro
const char* const kFlowBody = R"(
uniform sampler2D uDer;
uniform sampler2D uMPrev;
uniform vec2 uTexel;
uniform float uLambda;     // regularizacao: sem gradiente nao ha fluxo
uniform float uMaxFlow;    // texels por quadro
uniform float uMDecay;     // esmaecimento do movimento mantido entre atualizacoes da IA
uniform float uDead;       // abaixo disso e ruido
void main()
{
    float a = 0.0, b = 0.0, c = 0.0, bx = 0.0, by = 0.0;
    for (int j = -2; j <= 2; ++j)
        for (int i = -2; i <= 2; ++i) {
            float w = (3.0 - abs(float(i))) * (3.0 - abs(float(j))) / 81.0;
            vec3 d = texture(uDer, vUv + vec2(float(i), float(j)) * uTexel).xyz;
            a += w * d.x * d.x;
            b += w * d.x * d.y;
            c += w * d.y * d.y;
            bx += w * d.x * d.z;
            by += w * d.y * d.z;
        }
    a += uLambda;
    c += uLambda;
    float det = max(a * c - b * b, 1.0e-9);
    vec2 flow = vec2(-(c * bx - b * by), -(a * by - b * bx)) / det;
    float len = length(flow);
    if (len > uMaxFlow)
        flow *= uMaxFlow / len;
    vec2 held = texture(uMPrev, vUv).xy * uMDecay;
    vec2 m = length(flow) > length(held) ? flow : held;
    if (length(m) < uDead)
        m = vec2(0.0);
    fragColor = vec4(m, length(m), 1.0);
}
)";

// 4. Velocidade (xy, alturas da imagem por segundo) e forca local (z = movimento espalhado, texels por quadro)
const char* const kVelBody = R"(
uniform sampler2D uV;
uniform sampler2D uM;
uniform vec2 uTexel;
uniform float uDt;
uniform float uAspect;
uniform float uDecay;
uniform float uDiffuse;
uniform float uRadius;       // espessura da aura, em texels
uniform float uTargetScale;  // texels/quadro -> alturas/s (ja com a forca)
uniform float uForceGain;
uniform float uForceBlend;
void main()
{
    vec2 v0 = texture(uV, vUv).xy;
    vec2 back = vUv - uDt * v0 * vec2(1.0 / uAspect, 1.0);
    vec2 v = texture(uV, back).xy;
    vec2 avg = 0.25 * (texture(uV, vUv + vec2(uTexel.x, 0.0)).xy + texture(uV, vUv - vec2(uTexel.x, 0.0)).xy
                     + texture(uV, vUv + vec2(0.0, uTexel.y)).xy + texture(uV, vUv - vec2(0.0, uTexel.y)).xy);
    v = mix(v, avg, uDiffuse) * uDecay;

    // O movimento da borda alcanca a espessura da aura: pega o mais forte da vizinhanca (3 aneis de 8 taps)
    vec2 best = texture(uM, vUv).xy;
    float bestLen = length(best);
    for (int ring = 1; ring <= 3; ++ring) {
        float rr = uRadius * float(ring) / 3.0;
        float fall = 1.0 - 0.3 * float(ring - 1);
        for (int k = 0; k < 8; ++k) {
            float ang = float(k) * 0.785398;
            vec2 m = texture(uM, vUv + vec2(cos(ang), sin(ang)) * rr * uTexel).xy * fall;
            float l = length(m);
            if (l > bestLen) {
                best = m;
                bestLen = l;
            }
        }
    }
    float wF = clamp(bestLen * uForceGain, 0.0, 1.0) * uForceBlend;
    v = mix(v, best * uTargetScale, wF);
    fragColor = vec4(v, bestLen, 1.0);
}
)";

// 5. Densidade (r) e "calor" (g, escolhe a cor no gradiente): advecta, dissipa e recebe das bordas em movimento
const char* const kDenBody = R"(
uniform sampler2D uD;
uniform sampler2D uV;
uniform float uDt;
uniform float uAspect;
uniform float uDecay;
uniform float uHeatDecay;
uniform float uEmit;
void main()
{
    vec4 vv = texture(uV, vUv);
    vec2 back = vUv - uDt * vv.xy * vec2(1.0 / uAspect, 1.0);
    vec4 d = texture(uD, back);
    float dens = d.r * uDecay;
    float heat = d.g * uHeatDecay;
    float e = clamp((vv.z - 0.03) * 5.0, 0.0, 1.0);
    dens = min(dens + e * uEmit, 6.0);
    heat = mix(heat, clamp(length(vv.xy) * 1.4, 0.0, 1.0), clamp(e * 3.0, 0.0, 1.0));
    fragColor = vec4(dens, heat, 0.0, 1.0);
}
)";

// Desenho final, na resolucao do efeito
const char* const kAuraBody = R"(
uniform sampler2D uDen;     // r = densidade, g = calor
uniform sampler2D uVel;     // rg = velocidade, b = movimento espalhado
uniform sampler2D uPer;     // mascara da pessoa (IA)
uniform sampler2D uDbgA;    // depuracao: mascara suavizada (r)
uniform sampler2D uDbgB;    // depuracao: movimento das bordas (rg)
uniform int uMode;          // 0 aura/fumaca, 1 particulas, 2 grade de lasers
uniform int uDebug;         // 0 desligado, 1 mascara, 2 movimento, 3 velocidade, 4 densidade
uniform int uOnly;          // 1 = so o efeito
uniform int uCut;           // 1 = pessoa por cima
uniform vec3 uColA;
uniform vec3 uColB;
uniform float uIntensity;
uniform float uGlow;
uniform float uAspect;
uniform float uDot;         // tamanho do ponto (px em 1080p)
uniform float uCells;       // linhas da grade na altura
uniform float uLine;        // espessura das linhas (px)
uniform float uWarp;        // deformacao da grade

float hash21(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
vec3 palette(float t) { return mix(uColA, uColB, smoothstep(0.0, 1.0, t)); }

vec3 aura(vec2 uv)
{
    vec4 dv = texture(uDen, uv);
    float a = 1.0 - exp(-dv.r * uIntensity * 1.6);
    float t = clamp(dv.g * 0.7 + a * 0.5, 0.0, 1.0);
    vec3 col = palette(t);
    return col * a * (1.0 + uGlow * 1.6) + vec3(pow(a, 4.0)) * uGlow * 0.25;
}

vec3 particles(vec2 uv)
{
    vec2 px = uv * uSize;
    float cs = max(uDot * uSize.y / 1080.0, 1.5) * 2.2;
    vec2 cell = floor(px / cs);
    vec2 jit = (vec2(hash21(cell), hash21(cell + 7.3)) - 0.5) * 0.4 * cs;
    vec2 center = (cell + 0.5) * cs + jit;
    vec4 dv = texture(uDen, center / uSize);
    float d = dv.r * uIntensity;
    float tw = 0.65 + 0.35 * sin(uTime * 6.0 + hash21(cell + 1.7) * 6.2831);
    float r = 0.3 * cs * clamp(d * 1.4, 0.0, 1.0) * (0.6 + 0.4 * hash21(cell + 3.1));
    float dotv = smoothstep(r, r - 1.2, length(px - center)) * step(0.06, d);
    vec3 col = palette(clamp(dv.g * 0.8 + d * 0.25, 0.0, 1.0));
    return col * dotv * tw * (1.0 + uGlow);
}

vec3 lasers(vec2 uv)
{
    vec2 vel = texture(uVel, uv).xy;
    vec2 disp = texture(uVel, uv - vel * uWarp * 0.5).xy * uWarp;     // duas iteracoes: a grade acompanha o fluxo
    vec2 g = (vec2(uv.x * uAspect, uv.y) - disp) * uCells;
    vec2 gd = abs(fract(g + 0.5) - 0.5);
    vec2 fw = max(fwidth(g), vec2(1.0e-4));
    vec2 a = 1.0 - smoothstep(vec2(0.0), fw * uLine, gd);
    float line = max(a.x, a.y);
    vec4 dv = texture(uDen, uv);
    float speed = length(vel);
    float act = clamp(dv.r * uIntensity * 1.2 + speed * 0.8, 0.0, 1.0);
    vec3 col = palette(clamp(dv.g * 0.7 + speed * 0.5, 0.0, 1.0));
    return col * line * (0.10 + act) * (1.0 + uGlow) + col * a.x * a.y * act * uGlow;
}

void main()
{
    vec2 uv = imgPos();
    vec4 src = samp(uv);
    if (uDebug == 1) { fragColor = vec4(vec3(texture(uDbgA, uv).r), 1.0); return; }
    if (uDebug == 2) {
        vec2 m = texture(uDbgB, uv).xy;
        fragColor = vec4(clamp(m.x * 0.5, 0.0, 1.0), clamp(abs(m.y) * 0.5, 0.0, 1.0), clamp(-m.x * 0.5, 0.0, 1.0), 1.0);
        return;
    }
    if (uDebug == 3) {
        vec2 v = texture(uVel, uv).xy;
        fragColor = vec4(clamp(v.x * 0.6, 0.0, 1.0), clamp(abs(v.y) * 0.6, 0.0, 1.0), clamp(-v.x * 0.6, 0.0, 1.0), 1.0);
        return;
    }
    if (uDebug == 4) { fragColor = vec4(vec3(clamp(texture(uDen, uv).r * 0.5, 0.0, 1.0)), 1.0); return; }

    vec3 add = uMode == 0 ? aura(uv) : (uMode == 1 ? particles(uv) : lasers(uv));
    vec3 ps = src.rgb * src.a;                       // premultiplicado
    float pm = uCut == 1 ? smoothstep(0.35, 0.65, texture(uPer, uv).r) : 0.0;
    vec3 P;
    float A;
    if (uOnly == 1) {
        P = add * (1.0 - pm) + ps * pm;              // so o efeito (a pessoa por cima, se ligado)
        A = src.a * pm;
    } else {
        // aura por cima da imagem: soma e escurece um pouco por baixo, para aparecer tambem sobre fundo claro
        float cover = clamp(max(add.r, max(add.g, add.b)), 0.0, 1.0);
        vec3 over = ps * (1.0 - 0.75 * cover) + add;
        P = mix(over, ps, pm);                       // atras da pessoa se ligado
        A = src.a;
    }
    fragColor = toStraight(P, A);
}
)";

// Estado da simulacao: ping-pong de FBOs RGBA32F no tamanho reduzido
struct Pair
{
    std::unique_ptr<Fbo> f[2];
    int cur = 0;
    Fbo& now() { return *f[cur]; }
    Fbo& prev() { return *f[1 - cur]; }
    Fbo& next() { return *f[1 - cur]; }
    void swap() { cur = 1 - cur; }
};

void clearFbo(const Fbo& fbo)
{
    fbo.bind();
    glViewport(0, 0, fbo.width(), fbo.height());
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

class AuraEffect final : public AiEffect
{
public:
    AuraEffect() : AiEffect({kAuraId, "BSL Performer Aura & Fluid", "Reativo ao áudio"}, describeAura()) {}

    media::SourceStatus status() const override
    {
        const media::SourceStatus base = AiEffect::status();
        if (base.level != media::SourceStatus::Level::Ok || simError_.empty())
            return base;
        return {media::SourceStatus::Level::Warning, simError_};
    }

protected:
    std::string fragmentSource() const override { return std::string(kBslPrelude) + kAuraBody; }
    unsigned aiNeed() const override { return ai::kNeedPerson; }

    void prepareAi(const EffectContext& ctx, int w, int h, const ai::AiFrame& frame) override
    {
        maskTex_ = frame.mask;
        aspect_ = static_cast<float>(w) / static_cast<float>(std::max(h, 1));
        if (!ensureSim(w, h) || !ensureShaders()) {
            passThroughThisFrame();
            return;
        }
        float dt = static_cast<float>(std::clamp(ctx.dt, 0.0, 0.1));
        if (dt <= 0.0f)
            dt = 1.0f / 60.0f;
        const ParamSet& p = params();
        boost_ = p.b("audioReact") ? 1.0f + bandBass(ctx) * p.f("audioAmount") * 0.01f * 2.5f : 1.0f;
        stepSim(dt);
    }

    void render(const EffectContext&, int, int) override
    {
        const ParamSet& p = params();
        Shader& s = shader();
        bindTexture(1, den_.now().texture());
        bindTexture(2, vel_.now().texture());
        bindTexture(3, maskTex_);
        bindTexture(4, smooth_.now().texture());
        bindTexture(5, motion_.now().texture());
        s.setInt("uDen", 1);
        s.setInt("uVel", 2);
        s.setInt("uPer", 3);
        s.setInt("uDbgA", 4);
        s.setInt("uDbgB", 5);
        s.setInt("uMode", enumIndex(p, "mode"));
        s.setInt("uDebug", enumIndex(p, "debug"));
        s.setInt("uOnly", p.b("onlyEffect") ? 1 : 0);
        s.setInt("uCut", p.b("cutPerson") ? 1 : 0);
        const media::Color a = p.c("colorA"), b = p.c("colorB");
        s.setVec3("uColA", a.r, a.g, a.b);
        s.setVec3("uColB", b.r, b.g, b.b);
        s.setFloat("uIntensity", p.f("intensity") * 0.01f);
        s.setFloat("uGlow", p.f("glow") * 0.01f);
        s.setFloat("uAspect", aspect_);
        s.setFloat("uDot", p.f("dotSize"));
        s.setFloat("uCells", p.f("gridCells"));
        s.setFloat("uLine", p.f("lineWidth"));
        s.setFloat("uWarp", p.f("warp") * 0.0012f);
    }

    void afterDraw(const EffectContext&, int, int, const Fbo&) override
    {
        for (int unit = 5; unit >= 4; --unit)
            bindTexture(unit, 0);
    }

private:
    bool ensureSim(int w, int h)
    {
        const int div = enumIndex(params(), "quality") == 0 ? 8 : (enumIndex(params(), "quality") == 2 ? 2 : 4);
        const int sw = std::clamp(w / div, 48, 640);
        const int sh = std::max(27, static_cast<int>(std::lround(static_cast<double>(sw) * h / std::max(w, 1))));
        if (smooth_.f[0] && simW_ == sw && simH_ == sh)
            return true;
        try {
            auto make = [&] { return std::make_unique<Fbo>(sw, sh, true); };
            for (Pair* pr : {&smooth_, &motion_, &vel_, &den_}) {
                pr->f[0] = make();
                pr->f[1] = make();
                pr->cur = 0;
                clearFbo(*pr->f[0]);
                clearFbo(*pr->f[1]);
            }
            deriv_ = make();
            clearFbo(*deriv_);
        } catch (const std::exception& e) {
            simError_ = std::string("Falha ao criar o fluido: ") + e.what();
            smooth_.f[0].reset();
            return false;
        }
        simW_ = sw;
        simH_ = sh;
        primed_ = false;
        simError_.clear();
        return true;
    }

    bool ensureShaders()
    {
        if (shadersTried_)
            return smoothSh_ != nullptr;
        shadersTried_ = true;
        try {
            auto build = [&](const char* body) { return std::make_unique<Shader>(kFxVertexSource, (std::string(kSimHeader) + body).c_str()); };
            smoothSh_ = build(kSmoothBody);
            derivSh_ = build(kDerivBody);
            flowSh_ = build(kFlowBody);
            velSh_ = build(kVelBody);
            denSh_ = build(kDenBody);
        } catch (const std::exception& e) {
            simError_ = std::string("Falha no shader do fluido: ") + e.what();
            smoothSh_.reset();
            return false;
        }
        return true;
    }

    // Uma passada de simulacao: liga 'dst', o programa e as texturas (unidades 0..), e desenha
    void pass(Shader& s, const Fbo& dst, std::initializer_list<GLuint> textures)
    {
        bindOutput(dst, simW_, simH_);
        s.use();
        int unit = 0;
        for (GLuint t : textures)
            bindTexture(unit++, t);
        simVao_.draw();
    }

    void stepSim(float dt)
    {
        const ParamSet& p = params();
        const float texX = 1.0f / static_cast<float>(simW_), texY = 1.0f / static_cast<float>(simH_);
        const float dissip = p.f("dissipation") * 0.01f;

        // 1. mascara suavizada
        smoothSh_->use();
        smoothSh_->setInt("uMask", 0);
        smoothSh_->setVec2("uTexel", texX, texY);
        smoothSh_->setFloat("uStep", 1.5f);
        pass(*smoothSh_, smooth_.next(), {maskTex_});
        smooth_.swap();
        if (!primed_) { // primeiro quadro: sem diferenca temporal (senao a pessoa inteira "entraria" de uma vez)
            blit_.copy(smooth_.now().texture(), simW_, simH_, smooth_.prev());
            primed_ = true;
        }

        // 2. derivadas
        derivSh_->use();
        derivSh_->setInt("uCur", 0);
        derivSh_->setInt("uPrev", 1);
        derivSh_->setVec2("uTexel", texX, texY);
        pass(*derivSh_, *deriv_, {smooth_.now().texture(), smooth_.prev().texture()});

        // 3. fluxo optico (movimento das bordas)
        flowSh_->use();
        flowSh_->setInt("uDer", 0);
        flowSh_->setInt("uMPrev", 1);
        flowSh_->setVec2("uTexel", texX, texY);
        flowSh_->setFloat("uLambda", 0.004f);
        flowSh_->setFloat("uMaxFlow", 4.0f);
        flowSh_->setFloat("uMDecay", std::exp(-dt * 8.0f));
        flowSh_->setFloat("uDead", 0.03f);
        pass(*flowSh_, motion_.next(), {deriv_->texture(), motion_.now().texture()});
        motion_.swap();

        // 4. velocidade
        const float strength = p.f("motionForce") * 0.01f * boost_;
        const float radius = std::max(1.0f, p.f("thickness") * 0.01f * static_cast<float>(simH_));
        velSh_->use();
        velSh_->setInt("uV", 0);
        velSh_->setInt("uM", 1);
        velSh_->setVec2("uTexel", texX, texY);
        velSh_->setFloat("uDt", dt);
        velSh_->setFloat("uAspect", aspect_);
        velSh_->setFloat("uDecay", std::exp(-dt * (1.2f + dissip * 4.0f)));
        velSh_->setFloat("uDiffuse", 0.15f);
        velSh_->setFloat("uRadius", radius);
        velSh_->setFloat("uTargetScale", strength * 60.0f / static_cast<float>(simH_));
        velSh_->setFloat("uForceGain", 6.0f);
        velSh_->setFloat("uForceBlend", 1.0f - std::exp(-dt * 20.0f));
        pass(*velSh_, vel_.next(), {vel_.now().texture(), motion_.now().texture()});
        vel_.swap();

        // 5. densidade
        const float k = 0.12f + 8.0f * dissip * dissip; // dissipacao por segundo
        denSh_->use();
        denSh_->setInt("uD", 0);
        denSh_->setInt("uV", 1);
        denSh_->setFloat("uDt", dt);
        denSh_->setFloat("uAspect", aspect_);
        denSh_->setFloat("uDecay", std::exp(-dt * k));
        denSh_->setFloat("uHeatDecay", std::exp(-dt * (k + 0.4f)));
        denSh_->setFloat("uEmit", 3.0f * dt * boost_);
        pass(*denSh_, den_.next(), {den_.now().texture(), vel_.now().texture()});
        den_.swap();

        for (int unit = 1; unit >= 0; --unit)
            bindTexture(unit, 0);
        glUseProgram(0);
    }

    GLuint maskTex_ = 0;
    float aspect_ = 1.0f;
    float boost_ = 1.0f;
    int simW_ = 0, simH_ = 0;
    bool primed_ = false;
    bool shadersTried_ = false;
    std::string simError_;
    Pair smooth_, motion_, vel_, den_;
    std::unique_ptr<Fbo> deriv_;
    std::unique_ptr<Shader> smoothSh_, derivSh_, flowSh_, velSh_, denSh_;
    ScreenVao simVao_;
    TexBlitter blit_;
};

} // namespace

std::vector<media::ParamDesc> describeAura()
{
    std::vector<media::ParamDesc> d;
    d.push_back(makeEnum("mode", "Visualização", {{"aura", "Aura / fumaça"}, {"particles", "Partículas"}, {"lasers", "Grade de lasers"}}, "aura", "Fluido (IA)"));
    d.back().tooltip = "Aura: densidade colorida em gradiente. Partículas: densidade pontilhada. Lasers: linhas de grade deformadas pelo fluxo";
    d.push_back(makeColor("colorA", "Cor A", {0.10f, 0.55f, 1.00f, 1.0f}));
    d.back().tooltip = "Cor do fluido devagar / ralo";
    d.push_back(makeColor("colorB", "Cor B", {1.00f, 0.20f, 0.75f, 1.0f}));
    d.back().tooltip = "Cor do fluido rápido / denso";
    d.push_back(makeFloat("intensity", "Intensidade", 100.0f, 0.0f, 300.0f, "%.0f %%"));
    d.push_back(makeFloat("glow", "Brilho", 60.0f, 0.0f, 200.0f, "%.0f %%"));
    d.push_back(makeFloat("motionForce", "Força do movimento", 100.0f, 0.0f, 400.0f, "%.0f %%"));
    d.back().tooltip = "Quanto o movimento das bordas da silhueta empurra o fluido";
    d.push_back(makeFloat("thickness", "Espessura da aura", 8.0f, 1.0f, 30.0f, "%.0f %%"));
    d.back().tooltip = "Alcance da borda em movimento ao redor do corpo, em % da altura da imagem";
    d.push_back(makeFloat("dissipation", "Dissipação", 40.0f, 0.0f, 100.0f, "%.0f %%"));
    d.back().tooltip = "Quão rápido o fluido some. 100 % apaga quase na hora; com a pessoa parada ele sempre acaba sumindo";
    d.push_back(makeEnum("quality", "Qualidade do fluido", {{"low", "Baixa (1/8 da resolução)"}, {"mid", "Média (1/4 da resolução)"}, {"high", "Alta (1/2 da resolução)"}}, "mid"));
    d.back().tooltip = "O fluido roda em resolução reduzida para ser leve; mais resolução = detalhe mais fino e mais custo de GPU";

    d.push_back(makeBool("onlyEffect", "Só o efeito (sem a imagem)", false, "Composição"));
    d.back().tooltip = "Desligado: a aura é somada à imagem. Ligado: só o efeito, com transparência onde não há aura";
    d.push_back(makeBool("cutPerson", "Pessoa por cima da aura", true));
    d.back().tooltip = "Recorta a pessoa pela máscara da IA e a coloca na frente da aura";

    d.push_back(makeFloat("dotSize", "Tamanho dos pontos", 5.0f, 2.0f, 24.0f, "%.0f px", "Partículas"));
    d.back().enabledIf = "mode=particles";
    d.back().tooltip = "Tamanho em pixels de uma imagem 1080p";
    d.push_back(makeFloat("gridCells", "Linhas da grade", 28.0f, 6.0f, 90.0f, "%.0f", "Grade de lasers"));
    d.back().enabledIf = "mode=lasers";
    d.push_back(makeFloat("lineWidth", "Espessura das linhas", 1.4f, 0.5f, 4.0f, "%.1f"));
    d.back().enabledIf = "mode=lasers";
    d.push_back(makeFloat("warp", "Deformação da grade", 100.0f, 0.0f, 300.0f, "%.0f %%"));
    d.back().enabledIf = "mode=lasers";
    d.back().tooltip = "Quanto o campo de velocidade entorta as linhas";

    d.push_back(makeBool("audioReact", "Reagir ao áudio", false, "Áudio"));
    d.back().tooltip = "Os graves aumentam a força do movimento e a emissão da aura";
    d.push_back(makeFloat("audioAmount", "Reforço dos graves", 100.0f, 0.0f, 300.0f, "%.0f %%"));
    d.back().enabledIf = "audioReact";
    d.push_back(makeFloat("gain", "Ganho do áudio", 2.1f, 0.1f, 4.1f, "%.2f x"));
    d.back().enabledIf = "audioReact";
    d.push_back(makeFloat("gate", "Noise gate", 0.0075f, 0.0f, 0.15f, "%.4f"));
    d.back().enabledIf = "audioReact";

    d.push_back(makeEnum("debug", "Depuração", {{"off", "Efeito"}, {"mask", "Ver máscara suavizada"}, {"motion", "Ver movimento das bordas"}, {"velocity", "Ver velocidade"}, {"density", "Ver densidade"}}, "off", "Depuração"));
    d.back().tooltip = "Mostra o que o efeito enxerga: máscara, fluxo óptico das bordas, velocidade e densidade do fluido";
    return d;
}

std::unique_ptr<IEffect> makeAura()
{
    return std::make_unique<AuraEffect>();
}

} // namespace bsl::fx::bsllight
