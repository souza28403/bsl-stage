#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SeqDesenho.h"
#include "fx/bsl/SliceInstancer.h"

#include <algorithm>

namespace bsl::fx::bsllight {

using namespace media::param;

namespace {

const char* const kFragmentoVarredura = R"(#version 330 core
in vec2 vLocal;
in vec2 vImg;
flat in vec4 vRet;
flat in float vInt;
flat in float vIdx;
flat in vec4 vExtra;
out vec4 fragColor;
uniform sampler2D uTex;
uniform int uTop;
uniform int uEscala;
uniform int uTransparente;
uniform int uTrocar;
uniform float uComp;
uniform vec4 uCaixa;
vec4 amostra(vec2 p)
{
    p = clamp(p, vec2(0.0), vec2(1.0));
    return texture(uTex, uTop == 1 ? p : vec2(p.x, 1.0 - p.y));
}
void main()
{
    vec2 p = vImg;
    if (uEscala == 0)
        p = (vImg - uCaixa.xy) / max(uCaixa.zw, vec2(1.0e-6));
    vec4 c = amostra(p);
    float s = uTrocar == 1 ? vLocal.y : vLocal.x;
    float comp = max(uComp, 0.02);
    float cabeca = vExtra.x * (1.0 + comp);
    float t = (s - (cabeca - comp)) / comp;
    float k = t > 0.0 && t < 1.0 ? smoothstep(0.0, 1.0, 1.0 - abs(2.0 * t - 1.0)) : 0.0;
    if (uTransparente == 1)
        fragColor = vec4(c.rgb, c.a * k * vInt);
    else
        fragColor = vec4(c.rgb * c.a * k * vInt, 1.0);
}
)";

class EfeitoVarredura final : public EfeitoSeq
{
public:
    EfeitoVarredura() : EfeitoSeq(kVarreduraId, "Varredura", describeVarredura()), desenho_(kFragmentoVarredura) {}

protected:
    bool executar(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) override
    {
        if (!desenho_.pronto()) {
            avisar(desenho_.erro());
            return false;
        }
        const media::ParamSet& p = params();
        const auto snap = conjunto();
        const seq::Sequencia* sequencia = snap->sequencia(lerInteiro(p, "sequencia"));
        if (!sequencia)
            return false;
        const std::vector<int>* permitidos = snap->uidsDaVisao(lerInteiro(p, "visao"));
        const seq::Envelope* envelope = envelopeDe(*snap, p);

        seq::ParametrosVarredura pv;
        pv.avanco = lerAvanco(p);
        pv.passosPorSegundo = p.f("velocidade");
        pv.batidasPorPasso = p.f("batidas");
        pv.passoFixo = std::max(p.i("passoFixo") - 1, 0);
        pv.sobreposicao = p.f("sobreposicao");
        pv.voltas = p.i("voltas");
        pv.inverter = p.b("inverter");
        pv.alternarParesImpares = p.b("alternar");
        pv.usarEnvelope = envelope != nullptr;
        seq::calcularVarredura(*sequencia, pv, envelope, relogio(ctx), permitidos, itens_);

        int cw = 0, ch = 0;
        const std::vector<FeedSlice>& slices = slicesAtuais(cw, ch);
        const float geral = std::clamp(p.f("intensidade") / 100.0f, 0.0f, 1.0f);
        instancias_.clear();
        float indice = 0.0f;
        if (cw > 0 && ch > 0 && geral > 0.0f) {
            for (const FeedSlice& s : slices) {
                if (s.uid <= 0 || s.w <= 0.0f || s.h <= 0.0f)
                    continue;
                const auto it = std::lower_bound(itens_.begin(), itens_.end(), s.uid,
                                                 [](const seq::ItemVarredura& a, int uid) { return a.uid < uid; });
                if (it == itens_.end() || it->uid != s.uid || !it->ativo)
                    continue;
                InstanciaSlice inst = instanciaDe(s, cw, ch, geral, indice++);
                inst.extra[0] = it->progresso;
                instancias_.push_back(inst);
            }
        }
        desenho_.carregar(instancias_);

        const bool transparente = lerFundo(p) == FundoSeq::Transparente;
        bindOutput(output, w, h);
        glClearColor(0.0f, 0.0f, 0.0f, transparente ? 0.0f : 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (desenho_.total() <= 0)
            return true;

        float caixa[4];
        calcularCaixa(instancias_, caixa);
        Shader& s = desenho_.shader();
        s.use();
        s.setInt("uTex", 0);
        s.setInt("uTop", ctx.topOrigin ? 1 : 0);
        s.setInt("uEscala", lerEscala(p) == EscalaSeq::Preencher ? 0 : 1);
        s.setInt("uTransparente", transparente ? 1 : 0);
        s.setInt("uTrocar", p.b("trocarEixos") ? 1 : 0);
        s.setFloat("uComp", p.f("comprimentoBorda"));
        s.setVec4("uCaixa", caixa[0], caixa[1], caixa[2], caixa[3]);
        s.setVec2("uMargem", 0.0f, 0.0f);
        bindTexture(0, inputTex);
        desenho_.desenhar();
        bindTexture(0, 0);
        glUseProgram(0);
        return true;
    }

private:
    DesenhoSlices desenho_;
    std::vector<seq::ItemVarredura> itens_;
    std::vector<InstanciaSlice> instancias_;
};

}

std::vector<media::ParamDesc> describeVarredura()
{
    std::vector<media::ParamDesc> d;
    d.push_back(paramSequencia());
    d.push_back(paramVisao());
    for (media::ParamDesc& p : paramsAvanco())
        d.push_back(std::move(p));
    d.push_back(makeFloat("comprimentoBorda", "Comprimento da borda", 0.3f, 0.0f, 1.0f, "%.2f", "Varredura"));
    d.push_back(makeFloat("sobreposicao", "Sobreposição", 0.0f, 0.0f, 16.0f, "%.2f passos", "Varredura"));
    d.push_back(makeInt("voltas", "Voltas", 1, 1, seq::kMaxVoltas, "%d", "Varredura"));
    d.push_back(makeBool("inverter", "Inverter direção", false, "Varredura"));
    d.push_back(makeBool("alternar", "Alternar pares e ímpares", false, "Varredura"));
    d.push_back(makeBool("trocarEixos", "Trocar horizontal e vertical", false, "Varredura"));
    d.push_back(paramsCurva().front());
    d.push_back(paramEnvelope("Varredura"));
    d.push_back(paramEscala(false));
    d.push_back(paramFundo());
    d.push_back(paramIntensidade());
    return d;
}

std::unique_ptr<IEffect> makeVarredura()
{
    return std::make_unique<EfeitoVarredura>();
}

}
