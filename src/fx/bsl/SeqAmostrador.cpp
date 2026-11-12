#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SeqDesenho.h"
#include "fx/bsl/SliceInstancer.h"

#include <algorithm>
#include <cmath>
#include <exception>

namespace bsl::fx::bsllight {

using namespace media::param;

namespace {

constexpr int kLarguraMaxima = 512;

const char* const kVerticeAmostragem = R"(#version 330 core
layout(location = 0) in vec2 aCanto;
layout(location = 1) in vec4 aRet;
layout(location = 2) in vec2 aInt;
layout(location = 3) in vec4 aExtra;
uniform float uN;
flat out vec4 vRet;
flat out float vIdx;
void main()
{
    float x0 = aInt.y / uN;
    float x1 = (aInt.y + 1.0) / uN;
    float x = mix(x0, x1, aCanto.x);
    vRet = aRet;
    vIdx = aInt.y;
    gl_Position = vec4(x * 2.0 - 1.0, aCanto.y * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* const kFragmentoAmostragem = R"(#version 330 core
flat in vec4 vRet;
flat in float vIdx;
out vec4 fragColor;
uniform sampler2D uRed;
uniform sampler2D uAnt;
uniform int uTop;
uniform int uModo;
uniform int uTemAnt;
uniform vec2 uTamRed;
uniform float uK;
vec2 coord(vec2 p)
{
    p = clamp(p, vec2(0.0), vec2(1.0));
    return uTop == 1 ? p : vec2(p.x, 1.0 - p.y);
}
void main()
{
    vec2 centro = vRet.xy + vRet.zw * 0.5;
    vec2 tr = vRet.zw * uTamRed;
    float lod = max(log2(max(sqrt(tr.x * tr.y), 1.0)), 0.0);
    vec4 c0 = textureLod(uRed, coord(centro), 0.0);
    vec4 cm = textureLod(uRed, coord(centro), lod);
    vec4 c = uModo == 0 ? c0 : (uModo == 1 ? cm : mix(c0, cm, 0.5));
    if (uTemAnt == 1) {
        vec4 ant = texelFetch(uAnt, ivec2(int(vIdx), 0), 0);
        c = mix(c, ant, uK);
    }
    fragColor = c;
}
)";

const char* const kFragmentoCor = R"(#version 330 core
in vec2 vLocal;
in vec2 vImg;
flat in vec4 vRet;
flat in float vInt;
flat in float vIdx;
flat in vec4 vExtra;
out vec4 fragColor;
uniform sampler2D uCores;
uniform int uLuma;
uniform int uTransparente;
uniform float uGanho;
uniform vec4 uTinta;
void main()
{
    vec4 c = texelFetch(uCores, ivec2(int(vIdx), 0), 0);
    vec3 rgb = c.rgb;
    if (uLuma == 1)
        rgb = uTinta.rgb * dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb = clamp(rgb * uGanho, 0.0, 1.0);
    float a = c.a * (uLuma == 1 ? uTinta.a : 1.0);
    if (uTransparente == 1)
        fragColor = vec4(rgb, a * vInt);
    else
        fragColor = vec4(rgb * a * vInt, 1.0);
}
)";

class EfeitoAmostrador final : public EfeitoSeq
{
public:
    EfeitoAmostrador()
        : EfeitoSeq(kAmostradorId, "Amostrador de slices", describeAmostrador()),
          amostragem_(kFragmentoAmostragem, kVerticeAmostragem),
          cor_(kFragmentoCor)
    {
    }

protected:
    bool executar(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) override
    {
        if (!amostragem_.pronto()) {
            avisar(amostragem_.erro());
            return false;
        }
        if (!cor_.pronto()) {
            avisar(cor_.erro());
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
            amostragem_.carregar(instancias_);
            cor_.carregar(instancias_);
            chave_ = chave;
            temAnterior_ = false;
        }

        const bool transparente = lerFundo(p) == FundoSeq::Transparente;
        const int total = amostragem_.total();
        if (total <= 0) {
            bindOutput(output, w, h);
            glClearColor(0.0f, 0.0f, 0.0f, transparente ? 0.0f : 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            return true;
        }

        try {
            garantirRecursos(w, h, total);
        } catch (const std::exception& e) {
            avisar(std::string("Falha ao criar o amostrador: ") + e.what());
            return false;
        }

        blit_.copy(inputTex, w, h, *reduzido_, true);
        glBindTexture(GL_TEXTURE_2D, reduzido_->texture());
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);

        const int atual = indice_;
        const int anterior = 1 - indice_;
        const std::string modo = p.s("amostragem");
        const float suav = std::clamp(p.f("suavizacao"), 0.0f, 0.98f);
        float k = suav;
        if (suav > 0.0f && ctx.dt > 0.0)
            k = static_cast<float>(std::pow(static_cast<double>(suav), ctx.dt * 60.0));

        bindOutput(*par_[atual], total, 1);
        Shader& sa = amostragem_.shader();
        sa.use();
        sa.setInt("uRed", 0);
        sa.setInt("uAnt", 1);
        sa.setInt("uTop", ctx.topOrigin ? 1 : 0);
        sa.setInt("uModo", modo == "centro" ? 0 : modo == "mistura" ? 2 : 1);
        sa.setInt("uTemAnt", (temAnterior_ && suav > 0.0f) ? 1 : 0);
        sa.setVec2("uTamRed", static_cast<float>(reduzido_->width()), static_cast<float>(reduzido_->height()));
        sa.setFloat("uK", k);
        sa.setFloat("uN", static_cast<float>(total));
        bindTexture(1, par_[anterior]->texture());
        bindTexture(0, reduzido_->texture());
        amostragem_.desenhar();
        bindTexture(1, 0);
        bindTexture(0, 0);

        bindOutput(output, w, h);
        glClearColor(0.0f, 0.0f, 0.0f, transparente ? 0.0f : 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        const media::Color tinta = p.c("tinta");
        Shader& sc = cor_.shader();
        sc.use();
        sc.setInt("uCores", 0);
        sc.setInt("uTop", ctx.topOrigin ? 1 : 0);
        sc.setInt("uLuma", p.s("cor") == "luminancia" ? 1 : 0);
        sc.setInt("uTransparente", transparente ? 1 : 0);
        sc.setFloat("uGanho", p.f("ganho") / 100.0f);
        sc.setVec4("uTinta", tinta.r, tinta.g, tinta.b, tinta.a);
        sc.setVec2("uMargem", 0.0f, 0.0f);
        bindTexture(0, par_[atual]->texture());
        cor_.desenhar();
        bindTexture(0, 0);
        glUseProgram(0);

        indice_ = anterior;
        temAnterior_ = true;
        return true;
    }

private:
    void garantirRecursos(int w, int h, int total)
    {
        const int rw = std::max(1, std::min(w, kLarguraMaxima));
        const int rh = std::max(1, static_cast<int>(std::lround(static_cast<double>(h) * rw / std::max(w, 1))));
        if (!reduzido_ || reduzido_->width() != rw || reduzido_->height() != rh) {
            reduzido_ = std::make_unique<Fbo>(rw, rh);
            glBindTexture(GL_TEXTURE_2D, reduzido_->texture());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (!par_[0] || total > capacidade_ || (capacidade_ > 256 && total * 4 < capacidade_)) {
            capacidade_ = ((total + 63) / 64) * 64;
            par_[0] = std::make_unique<Fbo>(capacidade_, 1, true);
            par_[1] = std::make_unique<Fbo>(capacidade_, 1, true);
            temAnterior_ = false;
        }
    }

    DesenhoSlices amostragem_;
    DesenhoSlices cor_;
    TexBlitter blit_;
    std::unique_ptr<Fbo> reduzido_;
    std::unique_ptr<Fbo> par_[2];
    int capacidade_ = 0;
    int indice_ = 0;
    bool temAnterior_ = false;
    std::vector<InstanciaSlice> instancias_;
    ChaveInstancias chave_;
};

}

std::vector<media::ParamDesc> describeAmostrador()
{
    std::vector<media::ParamDesc> d;
    d.push_back(paramVisao());
    d.push_back(makeEnum("amostragem", "Amostragem", {{"centro", "Centro"}, {"media", "Média"}, {"mistura", "Mistura"}},
                         "media", "Amostragem"));
    d.push_back(makeEnum("cor", "Cor", {{"cor", "Cor"}, {"luminancia", "Luminância"}}, "cor", "Amostragem"));
    d.push_back(makeColor("tinta", "Tinta", {1.0f, 1.0f, 1.0f, 1.0f}, "Amostragem"));
    d.back().enabledIf = "cor=luminancia";
    d.push_back(makeFloat("ganho", "Ganho", 100.0f, 0.0f, 400.0f, "%.0f %%", "Amostragem"));
    d.push_back(makeFloat("suavizacao", "Suavização no tempo", 0.0f, 0.0f, 1.0f, "%.2f", "Amostragem"));
    d.push_back(paramFundo());
    d.push_back(paramIntensidade());
    return d;
}

std::unique_ptr<IEffect> makeAmostrador()
{
    return std::make_unique<EfeitoAmostrador>();
}

}
