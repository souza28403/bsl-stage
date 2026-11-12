#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SeqDesenho.h"
#include "fx/bsl/SliceInstancer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace bsl::fx::bsllight {

using namespace media::param;

namespace {

constexpr int kMaxSegmentos = 32;
constexpr int kMaxPassos = 64;
constexpr float kBase = 0.07f;
constexpr float kPi = 3.14159265358979f;

const char* const kFragmentoNeon = R"(#version 330 core
in vec2 vLocal;
in vec2 vImg;
flat in vec4 vRet;
flat in float vInt;
flat in float vIdx;
flat in vec4 vExtra;
out vec4 fragColor;
uniform int uFundo;
uniform int uModo;
uniform int uN;
uniform int uBarras;
uniform vec2 uTam;
uniform vec2 uRetC;
uniform vec2 uRetH;
uniform float uRaioPct;
uniform float uEsp;
uniform float uBrilho;
uniform float uFlash;
uniform float uRuido;
uniform float uTempo;
uniform float uGeral;
uniform float uBarL;
uniform float uBarR;
uniform float uBarW;
uniform float uBarD;
uniform float uBarH;
uniform vec3 uCor;
uniform vec3 uCorFlash;
uniform float uSeg[32];
float hash(float n)
{
    return fract(sin(n * 12.9898) * 43758.5453);
}
float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float perimetro(vec2 p, vec2 hb, float r)
{
    float L1 = 2.0 * (hb.x - r);
    float L2 = 2.0 * (hb.y - r);
    float A = 1.5707963 * r;
    float P = max(2.0 * L1 + 2.0 * L2 + 4.0 * A, 1.0e-3);
    vec2 qq = abs(p) - (hb - vec2(r));
    float s;
    if (qq.x > 0.0 && qq.y > 0.0) {
        if (p.x >= 0.0 && p.y < 0.0)
            s = L1 + atan(qq.x, qq.y) * r;
        else if (p.x >= 0.0)
            s = L1 + A + L2 + atan(qq.y, qq.x) * r;
        else if (p.y >= 0.0)
            s = 2.0 * L1 + 2.0 * A + L2 + atan(qq.x, qq.y) * r;
        else
            s = 2.0 * L1 + 3.0 * A + 2.0 * L2 + atan(qq.y, qq.x) * r;
    } else if (qq.x > qq.y) {
        if (p.x >= 0.0)
            s = L1 + A + clamp(p.y + (hb.y - r), 0.0, L2);
        else
            s = 2.0 * L1 + 3.0 * A + L2 + clamp((hb.y - r) - p.y, 0.0, L2);
    } else {
        if (p.y < 0.0)
            s = clamp(p.x + (hb.x - r), 0.0, L1);
        else
            s = L1 + 2.0 * A + L2 + clamp((hb.x - r) - p.x, 0.0, L1);
    }
    return fract(s / P);
}
float caixaArredondada(vec2 p, vec2 hb, float r)
{
    vec2 q = abs(p) - (hb - vec2(r));
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main()
{
    vec2 pxp = vImg * uTam;
    vec2 c = uModo == 0 ? uRetC : (vRet.xy + vRet.zw * 0.5) * uTam;
    vec2 hb = uModo == 0 ? uRetH : vRet.zw * uTam * 0.5;
    float menor = min(hb.x, hb.y);
    float r = min(menor, uRaioPct * 0.01 * 2.0 * menor);
    vec2 p = pxp - c;
    if (uFlash > 0.0 && uRuido > 0.0) {
        float linha = floor(pxp.y / 3.0);
        float t = floor(uTempo * 24.0);
        float hr = hash(linha + t * 13.7);
        float desl = step(0.6, hr) * (hash(linha * 3.1 + t) - 0.5) * 2.0;
        p.x += desl * uRuido * uFlash * 0.02 * uTam.x;
    }
    float d = caixaArredondada(p, hb, r);
    float ad = abs(d);
    float seg = perimetro(p, hb, r) * float(uN);
    int i0 = clamp(int(floor(seg)), 0, uN - 1);
    float f = seg - floor(seg);
    int ia = (i0 + uN - 1) % uN;
    int ib = (i0 + 1) % uN;
    float I = uSeg[i0];
    I = mix(I, uSeg[ib], smoothstep(0.75, 1.0, f) * 0.5);
    I = mix(I, uSeg[ia], (1.0 - smoothstep(0.0, 0.25, f)) * 0.5);
    float Ie = mix(I, 1.0, uFlash);
    float pos = f - 0.5;
    float Ig = pos >= 0.0 ? mix(uSeg[i0], uSeg[ib], pos) : mix(uSeg[i0], uSeg[ia], -pos);
    float Ige = mix(Ig, 1.0, uFlash);
    float meio = uEsp * 0.5;
    float lam = 1.5 + uBrilho * 40.0 * (1.0 + 0.2 * uFlash);
    float amp = uBrilho * (0.9 + 0.1 * uFlash);
    float aNucleo = (1.0 - smoothstep(meio - 0.75, meio + 0.75, ad)) * Ie;
    float aGlow = exp(-max(ad - meio, 0.0) / lam) * amp * Ige;
    if (uModo == 0 && uBarras > 0) {
        float larg = uBarW;
        vec2 meioBarra = vec2(larg * 0.5, uBarH);
        for (int k = 0; k < 4; ++k) {
            if (k >= uBarras)
                break;
            float xk = hb.x + float(k + 1) * uBarD + (float(k) + 0.5) * larg;
            float desvanece = 1.0 - 0.15 * float(k);
            for (int lado = 0; lado < 2; ++lado) {
                float sinal = lado == 0 ? -1.0 : 1.0;
                float base = lado == 0 ? uBarL : uBarR;
                float ib2 = mix(base * desvanece, 1.0, uFlash);
                float db = caixaArredondada(p - vec2(sinal * xk, 0.0), meioBarra, larg * 0.5);
                float nb = (1.0 - smoothstep(-0.75, 0.75, db)) * ib2;
                float gb = exp(-max(db, 0.0) / lam) * amp * ib2;
                aNucleo = max(aNucleo, nb);
                aGlow = max(aGlow, gb);
            }
        }
    }
    if (uFlash > 0.0 && uRuido > 0.0) {
        float grao = 1.0 + (hash12(pxp + vec2(fract(uTempo * 7.0) * 91.0, fract(uTempo * 3.0) * 57.0)) - 0.5) * uRuido * uFlash * 0.8;
        aNucleo *= grao;
        aGlow *= mix(1.0, grao, 0.4);
    }
    vec3 corC = mix(uCor, uCorFlash, uFlash);
    vec3 nucleo = mix(corC, vec3(1.0), 0.65);
    vec3 luz = clamp(nucleo * aNucleo + corC * aGlow, 0.0, 1.0);
    float a = clamp(aNucleo + aGlow * (1.0 - clamp(aNucleo, 0.0, 1.0)), 0.0, 1.0);
    luz *= uGeral;
    a *= uGeral;
    if (uFundo == 1) {
        if (a < 0.004)
            discard;
        fragColor = vec4(clamp(luz / max(a, 1.0e-4), 0.0, 1.0), a);
    } else if (uFundo == 0) {
        fragColor = vec4(luz, 1.0);
    } else {
        fragColor = vec4(luz, a);
    }
}
)";

float hashFloat(float n)
{
    const float x = std::sin(n * 12.9898f) * 43758.5453f;
    return x - std::floor(x);
}

class EfeitoBordaNeon final : public EfeitoSeq
{
public:
    EfeitoBordaNeon() : EfeitoSeq(kBordaNeonId, "Borda neon", describeBordaNeon()), desenho_(kFragmentoNeon) {}

protected:
    bool executar(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) override
    {
        if (!desenho_.pronto()) {
            avisar(desenho_.erro());
            return false;
        }
        const media::ParamSet& p = params();
        const std::string fundo = p.s("fundo");
        const int idFundo = fundo == "transparente" ? 1 : fundo == "original" ? 2 : 0;
        const int n = std::clamp(p.i("segmentos"), 4, kMaxSegmentos);
        const float geral = std::clamp(p.f("intensidade") / 100.0f, 0.0f, 1.0f);
        const bool slicesModo = p.s("forma") == "slices";

        atualizarPadrao(p, n);
        atualizarFlash(ctx, p);
        const float flash = valorDoFlash(ctx, p);
        calcularSegmentos(ctx, p, n);

        const float fw = static_cast<float>(w);
        const float fh = static_cast<float>(h);
        const float rw = p.f("retL") / 100.0f * fw;
        const float rh = p.f("retA") / 100.0f * fh;
        const float cx = p.f("retX") / 100.0f * fw + rw * 0.5f;
        const float cy = p.f("retY") / 100.0f * fh + rh * 0.5f;
        const float brilho = p.f("brilho") / 100.0f;
        const float ruido = p.f("flashRuido") / 100.0f;
        const float esp = p.f("espessura");
        const bool barras = !slicesModo && p.b("barras");
        const int qtdBarras = barras ? p.i("barrasQuantidade") : 0;
        const float barL = barras ? p.f("barrasLargura") / 100.0f * rw : 0.0f;
        const float barD = barras ? p.f("barrasDistancia") / 100.0f * rw : 0.0f;
        const float barH = barras ? p.f("barrasAltura") / 100.0f * rh * 0.5f : 0.0f;
        const float extensao = static_cast<float>(qtdBarras) * (barD + barL);

        const float lamMax = 1.5f + brilho * 40.0f * 1.2f;
        const float margemTotal = 3.0f * lamMax + esp * 0.5f + ruido * 0.02f * fw + 2.0f;
        const float margem = slicesModo ? std::min(margemTotal, 16.0f + esp * 0.5f) : margemTotal;

        instancias_.clear();
        if (geral > 0.0f) {
            if (slicesModo) {
                const auto snap = conjunto();
                int cw = 0, ch = 0;
                const std::vector<FeedSlice>& slices = slicesAtuais(cw, ch);
                montarTodas(slices, cw, ch, snap->uidsDaVisao(lerInteiro(p, "visao")), 1.0f, instancias_);
            } else {
                InstanciaSlice inst;
                inst.x = (cx - rw * 0.5f - extensao) / fw;
                inst.y = (cy - rh * 0.5f) / fh;
                inst.largura = (rw + 2.0f * extensao) / fw;
                inst.altura = rh / fh;
                inst.intensidade = 1.0f;
                instancias_.push_back(inst);
            }
        }
        desenho_.carregar(instancias_);

        if (idFundo == 2)
            instanciador().passagemDireta(inputTex, w, h, output);
        bindOutput(output, w, h);
        if (idFundo != 2) {
            glClearColor(0.0f, 0.0f, 0.0f, idFundo == 1 ? 0.0f : 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        if (desenho_.total() <= 0)
            return true;

        const media::Color cor = p.c("cor");
        const media::Color corFlash = p.c("corFlash");
        const float menor = std::min(rw, rh) * 0.5f;
        const float raio = std::min(menor, p.f("raioCanto") / 100.0f * 2.0f * menor);
        const float compL1 = std::max(rw - 2.0f * raio, 0.0f);
        const float compL2 = std::max(rh - 2.0f * raio, 0.0f);
        const float arco = kPi * 0.5f * raio;
        const float perim = std::max(2.0f * compL1 + 2.0f * compL2 + 4.0f * arco, 1.0e-3f);
        const float uDireita = (compL1 + arco + compL2 * 0.5f) / perim;
        const float uEsquerda = (2.0f * compL1 + 3.0f * arco + compL2 * 1.5f) / perim;

        Shader& s = desenho_.shader();
        s.use();
        s.setInt("uTop", ctx.topOrigin ? 1 : 0);
        s.setInt("uFundo", idFundo);
        s.setInt("uModo", slicesModo ? 1 : 0);
        s.setInt("uN", n);
        s.setInt("uBarras", qtdBarras);
        s.setVec2("uTam", fw, fh);
        s.setVec2("uMargem", margem / fw, margem / fh);
        s.setVec2("uRetC", cx, cy);
        s.setVec2("uRetH", rw * 0.5f, rh * 0.5f);
        s.setFloat("uRaioPct", p.f("raioCanto"));
        s.setFloat("uEsp", esp);
        s.setFloat("uBrilho", brilho);
        s.setFloat("uFlash", flash);
        s.setFloat("uRuido", ruido);
        s.setFloat("uTempo", static_cast<float>(std::fmod(ctx.time, 1000.0)));
        s.setFloat("uGeral", geral);
        s.setFloat("uBarL", segmentoEm(uEsquerda, n));
        s.setFloat("uBarR", segmentoEm(uDireita, n));
        s.setFloat("uBarW", barL);
        s.setFloat("uBarD", barD);
        s.setFloat("uBarH", barH);
        s.setVec3("uCor", cor.r, cor.g, cor.b);
        s.setVec3("uCorFlash", corFlash.r, corFlash.g, corFlash.b);
        definirVetorFloat(s, "uSeg", segmentos_, kMaxSegmentos);

        glEnable(GL_BLEND);
        if (idFundo == 2) {
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_ONE, GL_ONE);
        } else {
            glBlendEquation(GL_MAX);
            glBlendFunc(GL_ONE, GL_ONE);
        }
        desenho_.desenhar();
        glBlendEquation(GL_FUNC_ADD);
        glDisable(GL_BLEND);
        glUseProgram(0);
        return true;
    }

private:
    float segmentoEm(float u, int n) const
    {
        const int i = std::clamp(static_cast<int>(std::floor(u * static_cast<float>(n))), 0, n - 1);
        return segmentos_[i];
    }

    void atualizarPadrao(const media::ParamSet& p, int n)
    {
        const std::string padrao = p.s("padrao");
        const std::string texto = padrao == "personalizado" ? p.s("personalizado") : std::string();
        if (padraoValido_ && n == ultimoN_ && padrao == ultimoPadrao_ && texto == ultimoTexto_)
            return;
        padraoValido_ = true;
        ultimoN_ = n;
        ultimoPadrao_ = padrao;
        ultimoTexto_ = texto;
        passos_.clear();
        const auto bit = [](int i) { return 1u << static_cast<unsigned>(i); };
        if (padrao == "opostos") {
            const int metade = std::max(n / 2, 1);
            const int total = (n + 1) / 2;
            for (int s = 0; s < total && static_cast<int>(passos_.size()) < kMaxPassos; ++s)
                passos_.push_back(bit(s % n) | bit((s + metade) % n));
        } else if (padrao == "alternado") {
            std::uint32_t pares = 0, impares = 0;
            for (int i = 0; i < n; ++i)
                (i % 2 == 0 ? pares : impares) |= bit(i);
            for (int s = 0; s < n; ++s)
                passos_.push_back(s % 2 == 0 ? pares : impares);
        } else if (padrao == "personalizado") {
            std::uint32_t atual = 0;
            bool temAtual = false;
            std::string numero;
            const auto fecharNumero = [&]() {
                if (numero.empty())
                    return;
                const int v = std::atoi(numero.c_str());
                numero.clear();
                if (v >= 1 && v <= n) {
                    atual |= bit(v - 1);
                    temAtual = true;
                }
            };
            const auto fecharPasso = [&]() {
                fecharNumero();
                if (temAtual && static_cast<int>(passos_.size()) < kMaxPassos)
                    passos_.push_back(atual);
                atual = 0;
                temAtual = false;
            };
            for (const char ch : texto) {
                if (ch >= '0' && ch <= '9')
                    numero.push_back(ch);
                else if (ch == ';')
                    fecharPasso();
                else
                    fecharNumero();
            }
            fecharPasso();
        }
        if (passos_.empty())
            for (int s = 0; s < n; ++s)
                passos_.push_back(bit(s));
    }

    void atualizarFlash(const EffectContext& ctx, const media::ParamSet& p)
    {
        const int evento = lerInteiro(p, "flash");
        const int automatico = p.i("flashAutomatico");
        const long long compasso =
            automatico > 0 ? static_cast<long long>(std::floor(ctx.beat / (4.0 * automatico))) : 0;
        if (!iniciado_) {
            iniciado_ = true;
        } else {
            if (evento != ultimoEvento_)
                inicioFlash_ = ctx.time;
            if (automatico > 0 && compasso != ultimoCompasso_ && compasso > ultimoCompasso_)
                inicioFlash_ = ctx.time;
        }
        ultimoEvento_ = evento;
        ultimoCompasso_ = compasso;
    }

    float valorDoFlash(const EffectContext& ctx, const media::ParamSet& p) const
    {
        const double bpm = std::max(static_cast<double>(ctx.bpm), 1.0);
        const double duracao = std::max(static_cast<double>(p.f("flashDuracao")), 0.0625) * 60.0 / bpm;
        const double decorrido = ctx.time - inicioFlash_;
        if (decorrido < 0.0 || decorrido >= duracao)
            return 0.0f;
        const float v = static_cast<float>(1.0 - decorrido / duracao);
        return v * v;
    }

    void calcularSegmentos(const EffectContext& ctx, const media::ParamSet& p, int n)
    {
        const int total = static_cast<int>(passos_.size());
        const float nf = static_cast<float>(total);
        const bool bpm = p.b("seguirBpm");
        const double ciclo = bpm ? ctx.beat / std::max(static_cast<double>(p.f("batidasCiclo")), 0.25)
                                 : ctx.time * static_cast<double>(p.f("velocidade"));
        const float pos = static_cast<float>(ciclo - std::floor(ciclo));
        const std::string direcao = p.s("direcao");
        float avancoPasso = pos * nf;
        bool reverso = direcao == "invertida";
        if (direcao == "vaievolta") {
            const float x = pos * 2.0f;
            reverso = x >= 1.0f;
            avancoPasso = (reverso ? x - 1.0f : x) * nf;
        }
        const float cauda = std::max(p.f("comprimentoAceso") * nf, 0.25f);
        for (int j = 0; j < kMaxSegmentos; ++j)
            segmentos_[j] = 0.0f;
        for (int s = 0; s < total; ++s) {
            const int origem = reverso ? total - 1 - s : s;
            float idade = std::fmod(avancoPasso - static_cast<float>(origem), nf);
            if (idade < 0.0f)
                idade += nf;
            float luz = idade <= 1.0f ? 1.0f : std::max(0.0f, 1.0f - (idade - 1.0f) / cauda);
            luz *= std::min(idade / 0.12f, 1.0f);
            if (luz <= 0.0f)
                continue;
            const std::uint32_t mascara = passos_[static_cast<size_t>(s)];
            for (int j = 0; j < n; ++j)
                if (mascara & (1u << static_cast<unsigned>(j)))
                    segmentos_[j] = std::max(segmentos_[j], luz);
        }
        const float cintilacao = p.f("cintilacao") / 100.0f;
        const float quadro = std::floor(static_cast<float>(std::fmod(ctx.time, 1000.0)) * 18.0f);
        for (int j = 0; j < n; ++j) {
            float v = segmentos_[j];
            if (cintilacao > 0.0f)
                v *= 1.0f - cintilacao * 0.6f * hashFloat(quadro + static_cast<float>(j) * 7.13f);
            segmentos_[j] = std::clamp(std::max(v, kBase), 0.0f, 1.0f);
        }
    }

    DesenhoSlices desenho_;
    std::vector<InstanciaSlice> instancias_;
    std::vector<std::uint32_t> passos_;
    std::string ultimoPadrao_;
    std::string ultimoTexto_;
    int ultimoN_ = 0;
    bool padraoValido_ = false;
    float segmentos_[kMaxSegmentos] = {};
    bool iniciado_ = false;
    int ultimoEvento_ = 0;
    long long ultimoCompasso_ = 0;
    double inicioFlash_ = -1.0e9;
};

}

std::vector<media::ParamDesc> describeBordaNeon()
{
    std::vector<media::ParamDesc> d;
    d.push_back(makeEnum("forma", "Forma", {{"retangulo", "Retângulo"}, {"slices", "Slices de uma visão"}}, "retangulo",
                         "Forma"));
    d.push_back(paramVisao());
    d.back().enabledIf = "forma=slices";
    d.push_back(makeFloat("retX", "X", 5.0f, 0.0f, 100.0f, "%.1f %%", "Forma"));
    d.back().enabledIf = "forma=retangulo";
    d.push_back(makeFloat("retY", "Y", 5.0f, 0.0f, 100.0f, "%.1f %%", "Forma"));
    d.back().enabledIf = "forma=retangulo";
    d.push_back(makeFloat("retL", "Largura", 90.0f, 1.0f, 100.0f, "%.1f %%", "Forma"));
    d.back().enabledIf = "forma=retangulo";
    d.push_back(makeFloat("retA", "Altura", 90.0f, 1.0f, 100.0f, "%.1f %%", "Forma"));
    d.back().enabledIf = "forma=retangulo";
    d.push_back(makeFloat("raioCanto", "Raio do canto", 4.0f, 0.0f, 50.0f, "%.1f %%", "Forma"));
    d.push_back(makeFloat("espessura", "Espessura", 6.0f, 1.0f, 100.0f, "%.0f px", "Forma"));
    d.push_back(makeInt("segmentos", "Segmentos", 8, 4, 32, "%d", "Segmentos"));
    d.push_back(makeEnum("padrao", "Padrão",
                         {{"um", "Um por vez"}, {"opostos", "Opostos"}, {"alternado", "Alternado"},
                          {"personalizado", "Personalizado"}},
                         "um", "Segmentos"));
    d.push_back(makeText("personalizado", "Personalizado", "1,3;2,4", false, "Segmentos"));
    d.back().enabledIf = "padrao=personalizado";
    d.push_back(makeFloat("comprimentoAceso", "Comprimento aceso", 0.25f, 0.01f, 1.0f, "%.2f", "Segmentos"));
    d.push_back(makeEnum("direcao", "Direção", {{"normal", "Normal"}, {"invertida", "Invertida"}, {"vaievolta", "Vai e volta"}},
                         "normal", "Segmentos"));
    d.push_back(makeFloat("velocidade", "Velocidade", 0.4f, 0.0f, 10.0f, "%.2f", "Tempo"));
    d.back().enabledIf = "!seguirBpm";
    d.back().tooltip = "Voltas completas por segundo";
    d.push_back(makeBool("seguirBpm", "Seguir BPM", false, "Tempo"));
    d.push_back(makeFloat("batidasCiclo", "Batidas por volta", 4.0f, 0.25f, 64.0f, "%.2f", "Tempo"));
    d.back().enabledIf = "seguirBpm";
    d.push_back(makeBool("barras", "Barras laterais", false, "Barras"));
    d.back().enabledIf = "forma=retangulo";
    d.push_back(makeInt("barrasQuantidade", "Quantidade", 2, 1, 4, "%d", "Barras"));
    d.back().enabledIf = "barras";
    d.push_back(makeFloat("barrasLargura", "Largura", 2.0f, 0.1f, 20.0f, "%.1f %%", "Barras"));
    d.back().enabledIf = "barras";
    d.push_back(makeFloat("barrasDistancia", "Distância", 4.0f, 0.0f, 50.0f, "%.1f %%", "Barras"));
    d.back().enabledIf = "barras";
    d.push_back(makeFloat("barrasAltura", "Altura", 60.0f, 1.0f, 100.0f, "%.0f %%", "Barras"));
    d.back().enabledIf = "barras";
    d.push_back(makeColor("cor", "Cor", {0.35f, 0.75f, 1.0f, 1.0f}, "Cor"));
    d.push_back(makeColor("corFlash", "Cor do flash", {1.0f, 1.0f, 1.0f, 1.0f}, "Cor"));
    d.push_back(makeFloat("brilho", "Brilho", 60.0f, 0.0f, 100.0f, "%.0f %%", "Cor"));
    d.push_back(makeFloat("cintilacao", "Cintilação", 0.0f, 0.0f, 100.0f, "%.0f %%", "Cor"));
    d.push_back(makeCustom("flash", "Flash", "event", media::Json(0), "Flash"));
    d.push_back(makeInt("flashAutomatico", "Flash a cada N compassos", 0, 0, 64, "%d", "Flash"));
    d.push_back(makeFloat("flashDuracao", "Duração do flash", 0.5f, 0.125f, 1.0f, "%.3f batida", "Flash"));
    d.push_back(makeFloat("flashRuido", "Ruído do flash", 30.0f, 0.0f, 100.0f, "%.0f %%", "Flash"));
    d.push_back(makeEnum("fundo", "Fundo", {{"preto", "Preto"}, {"transparente", "Transparente"}, {"original", "Original"}},
                         "preto", "Imagem"));
    d.push_back(paramIntensidade());
    return d;
}

std::unique_ptr<IEffect> makeBordaNeon()
{
    return std::make_unique<EfeitoBordaNeon>();
}

}
