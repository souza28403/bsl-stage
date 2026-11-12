#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SeqDesenho.h"
#include "fx/bsl/SliceInstancer.h"

#include <algorithm>
#include <cmath>

namespace bsl::fx::bsllight {

using namespace media::param;

namespace {

const char* const kFragmentoLadrilhos = R"(#version 330 core
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
uniform int uModo;
uniform int uForma;
uniform int uAleat;
uniform int uSoBorda;
uniform vec2 uTam;
uniform float uTamanho;
uniform float uFase;
uniform float uLeque;
uniform float uComp;
uniform float uSemente;
uniform float uRaio;
uniform float uSuav;
uniform float uGradeEsp;
uniform float uTotal;
uniform vec4 uGradeCor;
float hash2(vec2 p, float s)
{
    return fract(sin(dot(p, vec2(127.1, 311.7)) + s * 74.7) * 43758.5453);
}
vec3 matiz(float h)
{
    vec3 k = clamp(abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    return k;
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
    float tam = max(uTamanho, 2.0);
    vec2 tile = floor(lp / tam);
    vec2 f = lp / tam - tile;
    vec2 nt = max(ceil(sz / tam), vec2(1.0));
    float h = hash2(tile + vIdx * 17.0, uSemente);
    float desloc = uAleat == 1 ? h : (tile.x + tile.y) / (nt.x + nt.y);
    float v = fract(uFase + uLeque * vIdx / max(uTotal, 1.0) + desloc);
    float b = v < uComp ? 1.0 - v / uComp : 0.0;

    vec4 orig = uOrig == 1 ? amostra(vImg) : vec4(0.0);
    vec3 col;
    float ta = 1.0;
    if (uModo == 0) {
        col = matiz(fract(uFase + desloc));
    } else if (uModo == 1) {
        col = vec3(1.0);
    } else if (uModo == 2) {
        vec2 centro = vRet.xy + (tile + 0.5) * tam / uTam;
        centro = clamp(centro, vRet.xy, vRet.xy + vRet.zw);
        vec4 c = amostra(centro);
        col = c.rgb;
        ta = c.a;
    } else {
        col = vec3(0.15, 1.0, 0.35);
        b *= b * step(0.35, hash2(tile + vIdx * 7.0, uSemente + 3.0));
    }

    vec2 q = f - 0.5;
    float d;
    if (uForma == 0) {
        float r = clamp(uRaio * 0.5, 0.0, 0.5);
        vec2 e = abs(q) - (0.5 - r);
        d = length(max(e, 0.0)) + min(max(e.x, e.y), 0.0) - r;
    } else {
        d = length(q) - 0.5 * mix(1.0, 0.3, uRaio);
    }
    float aa = 1.0 / tam;
    float borda = max(uSuav * 0.5, aa);
    float cov = 1.0 - smoothstep(-borda, 0.0, d);
    if (uSoBorda == 1) {
        float larg = max(0.08, aa * 2.0);
        cov *= smoothstep(-larg - borda, -larg, d);
    }
    float at = cov * b * ta;
    vec3 pre = orig.rgb * orig.a * (1.0 - at) + col * at;
    float a = orig.a * (1.0 - at) + at;

    if (uGradeEsp > 0.0) {
        vec2 fp = f * tam;
        float dg = min(min(fp.x, tam - fp.x), min(fp.y, tam - fp.y));
        float g = (1.0 - smoothstep(uGradeEsp * 0.5 - 0.5, uGradeEsp * 0.5 + 0.5, dg)) * uGradeCor.a;
        pre = pre * (1.0 - g) + uGradeCor.rgb * g;
        a = a * (1.0 - g) + g;
    }
    pre *= vInt;
    a *= vInt;
    if (uTransparente == 1)
        fragColor = vec4(a > 0.0 ? pre / a : vec3(0.0), a);
    else
        fragColor = vec4(pre, 1.0);
}
)";

class EfeitoLadrilhos final : public EfeitoSeq
{
public:
    EfeitoLadrilhos() : EfeitoSeq(kLadrilhosId, "Ladrilhos", describeLadrilhos()), desenho_(kFragmentoLadrilhos) {}

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

        const std::string modo = p.s("modo");
        const int idModo = modo == "minimo" ? 1 : modo == "textura" ? 2 : modo == "matriz" ? 3 : 0;
        const double base = p.b("seguirBpm") ? ctx.beat : ctx.time;
        const double fase = base * static_cast<double>(p.f("velocidade")) + static_cast<double>(p.f("fase"));
        const media::Color grade = p.c("gradeCor");

        Shader& s = desenho_.shader();
        s.use();
        s.setInt("uTex", 0);
        s.setInt("uTop", ctx.topOrigin ? 1 : 0);
        s.setInt("uTransparente", transparente ? 1 : 0);
        s.setInt("uOrig", p.b("mostrarOriginal") ? 1 : 0);
        s.setInt("uModo", idModo);
        s.setInt("uForma", p.s("forma") == "circulo" ? 1 : 0);
        s.setInt("uAleat", p.b("aleatorizar") ? 1 : 0);
        s.setInt("uSoBorda", p.b("soBorda") ? 1 : 0);
        s.setVec2("uTam", static_cast<float>(w), static_cast<float>(h));
        s.setVec2("uMargem", 0.0f, 0.0f);
        s.setFloat("uTamanho", p.f("tamanho"));
        s.setFloat("uFase", static_cast<float>(fase));
        s.setFloat("uLeque", p.f("leque"));
        s.setFloat("uComp", std::max(p.f("comprimento"), 0.01f));
        s.setFloat("uSemente", static_cast<float>(p.i("semente")));
        s.setFloat("uRaio", p.f("raio"));
        s.setFloat("uSuav", p.f("suavidade"));
        s.setFloat("uGradeEsp", p.f("gradeEspessura"));
        s.setFloat("uTotal", static_cast<float>(desenho_.total()));
        s.setVec4("uGradeCor", grade.r, grade.g, grade.b, grade.a);
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
};

}

std::vector<media::ParamDesc> describeLadrilhos()
{
    std::vector<media::ParamDesc> d;
    d.push_back(paramVisao());
    d.push_back(makeFloat("tamanho", "Tamanho", 32.0f, 4.0f, 512.0f, "%.0f px", "Ladrilhos"));
    d.push_back(makeEnum("modo", "Modo",
                         {{"arcoiris", "Arco-íris"}, {"minimo", "Mínimo"}, {"textura", "Textura"}, {"matriz", "Matriz"}},
                         "arcoiris", "Ladrilhos"));
    d.push_back(makeFloat("velocidade", "Velocidade", 0.5f, 0.0f, 10.0f, "%.2f", "Animação"));
    d.back().tooltip = "Ciclos por segundo (ou por batida com Seguir BPM)";
    d.push_back(makeBool("seguirBpm", "Seguir BPM", false, "Animação"));
    d.push_back(makeFloat("fase", "Fase", 0.0f, 0.0f, 1.0f, "%.2f", "Animação"));
    d.push_back(makeInt("semente", "Semente", 1, 1, 9999, "%d", "Animação"));
    d.push_back(makeBool("aleatorizar", "Aleatorizar", false, "Animação"));
    d.push_back(makeFloat("leque", "Leque", 0.0f, 0.0f, 1.0f, "%.2f", "Animação"));
    d.push_back(makeFloat("comprimento", "Comprimento", 1.0f, 0.01f, 1.0f, "%.2f", "Animação"));
    d.push_back(makeEnum("forma", "Forma", {{"quadrado", "Quadrado"}, {"circulo", "Círculo"}}, "quadrado", "Forma"));
    d.push_back(makeFloat("raio", "Raio", 0.2f, 0.0f, 1.0f, "%.2f", "Forma"));
    d.back().tooltip = "Quadrado: arredondamento dos cantos. Círculo: quanto o círculo encolhe";
    d.push_back(makeFloat("suavidade", "Suavidade", 0.1f, 0.0f, 1.0f, "%.2f", "Forma"));
    d.push_back(makeBool("soBorda", "Só borda", false, "Forma"));
    d.push_back(makeFloat("gradeEspessura", "Espessura da grade", 0.0f, 0.0f, 16.0f, "%.1f px", "Grade"));
    d.push_back(makeColor("gradeCor", "Cor da grade", {0.0f, 0.0f, 0.0f, 1.0f}, "Grade"));
    d.push_back(makeBool("mostrarOriginal", "Mostrar original", false, "Imagem"));
    d.push_back(paramFundo());
    d.push_back(paramIntensidade());
    return d;
}

std::unique_ptr<IEffect> makeLadrilhos()
{
    return std::make_unique<EfeitoLadrilhos>();
}

}
