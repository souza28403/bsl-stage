#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SeqDesenho.h"
#include "fx/bsl/SliceInstancer.h"

#include <algorithm>
#include <cmath>

namespace bsl::fx::bsllight {

using namespace media::param;

namespace {

const char* const kFragmentoContorno = R"(#version 330 core
in vec2 vLocal;
in vec2 vImg;
flat in vec4 vRet;
flat in float vInt;
flat in float vIdx;
flat in vec4 vExtra;
out vec4 fragColor;
uniform sampler2D uTex;
uniform int uTop;
uniform int uTransparente;
uniform int uOrig;
uniform int uSuave;
uniform int uAnimar;
uniform int uInv;
uniform int uAlt;
uniform int uAleat;
uniform int uQtd;
uniform int uUsaEnv;
uniform vec2 uTam;
uniform float uEsp;
uniform float uFase;
uniform float uLeque;
uniform float uComp;
uniform float uTotal;
uniform vec4 uCor;
uniform float uEnv[16];
float hash(float n)
{
    return fract(sin(n * 12.9898) * 43758.5453);
}
float perfil(float k)
{
    if (uUsaEnv == 0)
        return k;
    float x = clamp(k, 0.0, 1.0) * 15.0;
    int i = int(floor(x));
    int j = min(i + 1, 15);
    return mix(uEnv[i], uEnv[j], x - float(i));
}
vec4 amostra(vec2 p)
{
    p = clamp(p, vec2(0.0), vec2(1.0));
    return texture(uTex, uTop == 1 ? p : vec2(p.x, 1.0 - p.y));
}
void main()
{
    vec2 sz = max(vRet.zw * uTam, vec2(1.0));
    vec2 lp = vLocal * sz;
    float dl = lp.x;
    float dr = sz.x - lp.x;
    float dt = lp.y;
    float db = sz.y - lp.y;
    float d = min(min(dl, dr), min(dt, db));
    float per = 2.0 * (sz.x + sz.y);
    float u;
    if (d == dt)
        u = lp.x / per;
    else if (d == dr)
        u = (sz.x + lp.y) / per;
    else if (d == db)
        u = (sz.x + sz.y + (sz.x - lp.x)) / per;
    else
        u = (2.0 * sz.x + sz.y + (sz.y - lp.y)) / per;
    float esp = max(uEsp, 1.0);
    float m = uSuave == 1 ? pow(1.0 - clamp(d / esp, 0.0, 1.0), 2.0) : 1.0 - smoothstep(esp - 1.0, esp, d);
    float k = 1.0;
    if (uAnimar == 1) {
        float fase = uFase + uLeque * vIdx / max(uTotal, 1.0);
        if (uAleat == 1)
            fase += hash(vIdx + 1.0);
        bool inverte = (uInv == 1) != (uAlt == 1 && mod(vIdx, 2.0) >= 1.0);
        float dir = inverte ? -1.0 : 1.0;
        float atras = fract((dir * fase - u) * dir);
        atras = fract(atras * float(uQtd));
        k = atras < uComp ? 1.0 - atras / uComp : 0.0;
        k = perfil(k);
    }
    vec4 orig = uOrig == 1 ? amostra(vImg) : vec4(0.0);
    float na = clamp(m * k * uCor.a, 0.0, 1.0);
    vec3 pre = orig.rgb * orig.a * (1.0 - na) + uCor.rgb * na;
    float a = orig.a * (1.0 - na) + na;
    pre *= vInt;
    a *= vInt;
    if (uTransparente == 1)
        fragColor = vec4(a > 0.0 ? pre / a : vec3(0.0), a);
    else
        fragColor = vec4(pre, 1.0);
}
)";

class EfeitoContorno final : public EfeitoSeq
{
public:
    EfeitoContorno() : EfeitoSeq(kContornoId, "Contorno", describeContorno()), desenho_(kFragmentoContorno) {}

protected:
    bool executar(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) override
    {
        if (!desenho_.pronto()) {
            avisar(desenho_.erro());
            return false;
        }
        const media::ParamSet& p = params();
        const auto snap = conjunto();
        const int idVisao = lerInteiro(p, "visao");
        const std::vector<int>* permitidos = snap->uidsDaVisao(idVisao);
        int cw = 0, ch = 0;
        const std::vector<FeedSlice>& slices = slicesAtuais(cw, ch);
        ChaveInstancias chave;
        chave.slices = versaoDosSlices();
        chave.conjunto = snap->versao;
        chave.visao = idVisao;
        chave.intensidade = p.f("intensidade") / 100.0f;
        chave.valida = true;
        if (!chave.igual(chave_)) {
            montarTodas(slices, cw, ch, permitidos, chave.intensidade, instancias_);
            desenho_.carregar(instancias_);
            chave_ = chave;
        }

        const bool transparente = lerFundo(p) == FundoSeq::Transparente;
        bindOutput(output, w, h);
        glClearColor(0.0f, 0.0f, 0.0f, transparente ? 0.0f : 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (desenho_.total() <= 0)
            return true;

        const bool bpm = p.b("seguirBpm");
        const double base = bpm ? ctx.beat : ctx.time;
        const double fase = base * static_cast<double>(p.f("velocidade")) + static_cast<double>(p.f("fase"));
        const media::Color cor = p.c("cor");
        const seq::Envelope* envelope = envelopeDe(*snap, p);
        for (int i = 0; i < 16; ++i)
            env_[i] = envelope ? envelope->amostrar(static_cast<float>(i) / 15.0f) : 0.0f;

        Shader& s = desenho_.shader();
        s.use();
        s.setInt("uTex", 0);
        s.setInt("uTop", ctx.topOrigin ? 1 : 0);
        s.setInt("uTransparente", transparente ? 1 : 0);
        s.setInt("uOrig", p.b("mostrarOriginal") ? 1 : 0);
        s.setInt("uSuave", p.b("suavizar") ? 1 : 0);
        s.setInt("uAnimar", p.b("animar") ? 1 : 0);
        s.setInt("uInv", p.b("inverterDirecao") ? 1 : 0);
        s.setInt("uAlt", p.b("alternar") ? 1 : 0);
        s.setInt("uAleat", p.b("aleatorizar") ? 1 : 0);
        s.setInt("uQtd", p.i("quantidade"));
        s.setInt("uUsaEnv", envelope ? 1 : 0);
        s.setVec2("uTam", static_cast<float>(w), static_cast<float>(h));
        s.setVec2("uMargem", 0.0f, 0.0f);
        s.setFloat("uEsp", p.f("espessura"));
        s.setFloat("uFase", static_cast<float>(fase));
        s.setFloat("uLeque", p.f("leque"));
        s.setFloat("uComp", std::max(p.f("comprimento"), 0.01f));
        s.setFloat("uTotal", static_cast<float>(desenho_.total()));
        s.setVec4("uCor", cor.r, cor.g, cor.b, cor.a);
        definirVetorFloat(s, "uEnv", env_, 16);
        bindTexture(0, inputTex);
        desenho_.desenhar();
        bindTexture(0, 0);
        glUseProgram(0);
        return true;
    }

private:
    DesenhoSlices desenho_;
    std::vector<InstanciaSlice> instancias_;
    ChaveInstancias chave_;
    float env_[16] = {};
};

}

std::vector<media::ParamDesc> describeContorno()
{
    std::vector<media::ParamDesc> d;
    d.push_back(paramVisao());
    d.push_back(makeColor("cor", "Cor", {0.2f, 0.8f, 1.0f, 1.0f}, "Contorno"));
    d.push_back(makeFloat("espessura", "Espessura", 6.0f, 1.0f, 200.0f, "%.0f px", "Contorno"));
    d.push_back(makeBool("suavizar", "Suavizar para dentro", false, "Contorno"));
    d.push_back(makeBool("mostrarOriginal", "Mostrar original", true, "Contorno"));
    d.push_back(makeBool("animar", "Animar", true, "Animação"));
    d.push_back(makeFloat("velocidade", "Velocidade", 0.5f, 0.0f, 10.0f, "%.2f", "Animação"));
    d.back().enabledIf = "animar";
    d.back().tooltip = "Voltas por segundo (ou por batida com Seguir BPM)";
    d.push_back(makeBool("seguirBpm", "Seguir BPM", false, "Animação"));
    d.back().enabledIf = "animar";
    d.push_back(makeFloat("fase", "Fase", 0.0f, 0.0f, 1.0f, "%.2f", "Animação"));
    d.back().enabledIf = "animar";
    d.push_back(makeFloat("leque", "Leque", 0.0f, 0.0f, 1.0f, "%.2f", "Animação"));
    d.back().enabledIf = "animar";
    d.push_back(makeBool("aleatorizar", "Aleatorizar", false, "Animação"));
    d.back().enabledIf = "animar";
    d.push_back(makeFloat("comprimento", "Comprimento", 0.4f, 0.01f, 1.0f, "%.2f", "Animação"));
    d.back().enabledIf = "animar";
    d.push_back(makeInt("quantidade", "Quantidade", 1, 1, 8, "%d", "Animação"));
    d.back().enabledIf = "animar";
    d.push_back(makeBool("inverterDirecao", "Inverter direção", false, "Animação"));
    d.back().enabledIf = "animar";
    d.push_back(makeBool("alternar", "Alternar pares e ímpares", false, "Animação"));
    d.back().enabledIf = "animar";
    d.push_back(paramsCurva().front());
    d.push_back(paramEnvelope("Animação"));
    d.push_back(paramFundo());
    d.push_back(paramIntensidade());
    return d;
}

std::unique_ptr<IEffect> makeContorno()
{
    return std::make_unique<EfeitoContorno>();
}

}
