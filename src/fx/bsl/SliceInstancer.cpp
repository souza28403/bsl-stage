#include "fx/bsl/SliceInstancer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bsl::fx::bsllight {

namespace {

using media::Json;
using media::ParamDesc;
using media::ParamSet;

const char* const kVerticeInstanciado = R"(#version 330 core
layout(location = 0) in vec2 aCanto;
layout(location = 1) in vec4 aRet;
layout(location = 2) in vec2 aInt;
layout(location = 3) in vec4 aExtra;
uniform int uTop;
out vec2 vLocal;
out vec2 vImg;
flat out float vInt;
flat out float vIdx;
flat out vec4 vExtra;
void main()
{
    vec2 img = aRet.xy + aCanto * aRet.zw;
    vLocal = aCanto;
    vImg = img;
    vInt = aInt.x;
    vIdx = aInt.y;
    vExtra = aExtra;
    vec2 uv = uTop == 1 ? img : vec2(img.x, 1.0 - img.y);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* const kFragmentoInstanciado = R"(#version 330 core
in vec2 vLocal;
in vec2 vImg;
flat in float vInt;
flat in float vIdx;
flat in vec4 vExtra;
out vec4 fragColor;
uniform sampler2D uTex;
uniform sampler2DArray uArr;
uniform int uTop;
uniform int uEscala;
uniform int uTransparente;
uniform vec4 uCaixa;
vec4 amostra(vec2 p)
{
    p = clamp(p, vec2(0.0), vec2(1.0));
    vec2 q = uTop == 1 ? p : vec2(p.x, 1.0 - p.y);
    if (vExtra.y > 0.5)
        return texture(uArr, vec3(q, vExtra.x));
    return texture(uTex, q);
}
void main()
{
    vec2 p = vImg;
    if (uEscala == 0)
        p = (vImg - uCaixa.xy) / max(uCaixa.zw, vec2(1.0e-6));
    else if (uEscala == 2)
        p = vLocal;
    vec4 c = amostra(p);
    if (uTransparente == 1)
        fragColor = vec4(c.rgb, c.a * vInt);
    else
        fragColor = vec4(c.rgb * c.a * vInt, 1.0);
}
)";

constexpr int kFloatsPorInstancia = 10;

}

int lerInteiro(const ParamSet& params, const std::string& id)
{
    const Json raw = params.raw(id);
    if (raw.is_number())
        return static_cast<int>(std::lround(raw.get<double>()));
    const ParamDesc* d = params.desc(id);
    return (d && d->def.is_number()) ? static_cast<int>(std::lround(d->def.get<double>())) : 0;
}

SliceInstancer::SliceInstancer() = default;

SliceInstancer::~SliceInstancer()
{
    liberar();
}

void SliceInstancer::liberar()
{
    if (dados_)
        glDeleteBuffers(1, &dados_);
    if (cantos_)
        glDeleteBuffers(1, &cantos_);
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
    dados_ = cantos_ = vao_ = 0;
    shader_.reset();
}

void SliceInstancer::criar()
{
    tentou_ = true;
    try {
        shader_ = std::make_unique<Shader>(kVerticeInstanciado, kFragmentoInstanciado);
    } catch (const std::exception& e) {
        erro_ = std::string("Falha no shader dos slices: ") + e.what();
        shader_.reset();
        return;
    }
    const float cantos[8] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &cantos_);
    glGenBuffers(1, &dados_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, cantos_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cantos), cantos, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, dados_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(float)) * kFloatsPorInstancia, nullptr, GL_STREAM_DRAW);
    const GLsizei passo = static_cast<GLsizei>(sizeof(float) * kFloatsPorInstancia);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, passo, reinterpret_cast<const void*>(0));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, passo, reinterpret_cast<const void*>(sizeof(float) * 4));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, passo, reinterpret_cast<const void*>(sizeof(float) * 6));
    glVertexAttribDivisor(3, 1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

bool SliceInstancer::pronto()
{
    if (!tentou_)
        criar();
    return shader_ != nullptr;
}

void SliceInstancer::passagemDireta(GLuint entrada, int w, int h, Fbo& saida)
{
    blit_.copy(entrada, w, h, saida);
}

void SliceInstancer::desenhar(GLuint entrada, int w, int h, Fbo& saida, const ConfigDesenho& config,
                              const std::vector<InstanciaSlice>& instancias)
{
    ultimas_ = 0;
    if (!pronto()) {
        passagemDireta(entrada, w, h, saida);
        return;
    }
    const int total = static_cast<int>(std::min<size_t>(instancias.size(), static_cast<size_t>(kMaxInstancias)));
    const bool transparente = config.fundo == FundoSeq::Transparente;

    bindOutput(saida, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, transparente ? 0.0f : 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (total <= 0)
        return;

    std::vector<float>& buffer = buffer_;
    buffer.resize(static_cast<size_t>(total) * kFloatsPorInstancia);
    for (int i = 0; i < total; ++i) {
        const InstanciaSlice& s = instancias[static_cast<size_t>(i)];
        float* p = buffer.data() + static_cast<size_t>(i) * kFloatsPorInstancia;
        p[0] = s.x;
        p[1] = s.y;
        p[2] = s.largura;
        p[3] = s.altura;
        p[4] = s.intensidade;
        p[5] = s.indice;
        p[6] = s.extra[0];
        p[7] = s.extra[1];
        p[8] = s.extra[2];
        p[9] = s.extra[3];
    }

    glBindBuffer(GL_ARRAY_BUFFER, dados_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(buffer.size() * sizeof(float)), buffer.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    if (!transparente) {
        glEnable(GL_BLEND);
        glBlendEquation(GL_MAX);
        glBlendFunc(GL_ONE, GL_ONE);
    }
    shader_->use();
    shader_->setInt("uTex", 0);
    shader_->setInt("uArr", 1);
    shader_->setInt("uTop", config.topOrigin ? 1 : 0);
    shader_->setInt("uEscala", static_cast<int>(config.escala));
    shader_->setInt("uTransparente", transparente ? 1 : 0);
    shader_->setVec4("uCaixa", config.caixa[0], config.caixa[1], config.caixa[2], config.caixa[3]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, config.arranjo);
    bindTexture(0, entrada);
    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, total);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    bindTexture(0, 0);
    glUseProgram(0);
    if (!transparente) {
        glBlendEquation(GL_FUNC_ADD);
        glDisable(GL_BLEND);
    }
    ultimas_ = total;
}

InstanciaSlice instanciaDe(const FeedSlice& slice, int canvasW, int canvasH, float intensidade, float indice)
{
    const float invW = 1.0f / static_cast<float>(std::max(canvasW, 1));
    const float invH = 1.0f / static_cast<float>(std::max(canvasH, 1));
    InstanciaSlice inst;
    inst.x = slice.x * invW;
    inst.y = slice.y * invH;
    inst.largura = slice.w * invW;
    inst.altura = slice.h * invH;
    inst.intensidade = intensidade;
    inst.indice = indice;
    return inst;
}

void montarInstancias(const std::vector<FeedSlice>& slices, int canvasW, int canvasH,
                      const std::vector<seq::ItemIntensidade>& intensidades, const std::vector<int>* permitidos,
                      float intensidadeGeral, std::vector<InstanciaSlice>& saida)
{
    saida.clear();
    if (canvasW <= 0 || canvasH <= 0 || intensidades.empty())
        return;
    const float geral = std::clamp(intensidadeGeral, 0.0f, 1.0f);
    float indice = 0.0f;
    for (const FeedSlice& s : slices) {
        if (s.uid <= 0)
            continue;
        if (permitidos && !std::binary_search(permitidos->begin(), permitidos->end(), s.uid))
            continue;
        const auto it = std::lower_bound(intensidades.begin(), intensidades.end(), s.uid,
                                         [](const seq::ItemIntensidade& a, int uid) { return a.uid < uid; });
        if (it == intensidades.end() || it->uid != s.uid)
            continue;
        const float v = std::clamp(it->valor, 0.0f, 1.0f) * geral;
        if (v <= 0.0f || s.w <= 0.0f || s.h <= 0.0f)
            continue;
        saida.push_back(instanciaDe(s, canvasW, canvasH, v, indice++));
    }
}

std::vector<InstanciaSlice> montarInstancias(const std::vector<FeedSlice>& slices, int canvasW, int canvasH,
                                             const std::vector<seq::ItemIntensidade>& intensidades,
                                             const std::vector<int>* permitidos, float intensidadeGeral)
{
    std::vector<InstanciaSlice> saida;
    saida.reserve(std::min<size_t>(slices.size(), intensidades.size()));
    montarInstancias(slices, canvasW, canvasH, intensidades, permitidos, intensidadeGeral, saida);
    return saida;
}

void calcularCaixa(const std::vector<InstanciaSlice>& instancias, float caixa[4])
{
    if (instancias.empty()) {
        caixa[0] = caixa[1] = 0.0f;
        caixa[2] = caixa[3] = 1.0f;
        return;
    }
    float x0 = 1.0e9f, y0 = 1.0e9f, x1 = -1.0e9f, y1 = -1.0e9f;
    for (const InstanciaSlice& s : instancias) {
        x0 = std::min(x0, s.x);
        y0 = std::min(y0, s.y);
        x1 = std::max(x1, s.x + s.largura);
        y1 = std::max(y1, s.y + s.altura);
    }
    caixa[0] = x0;
    caixa[1] = y0;
    caixa[2] = std::max(x1 - x0, 1.0e-6f);
    caixa[3] = std::max(y1 - y0, 1.0e-6f);
}

EfeitoSeq::EfeitoSeq(const char* id, const char* nome, std::vector<media::ParamDesc> descs)
    : id_(id), nome_(nome), descs_(std::move(descs))
{
}

const media::ParamSet& EfeitoSeq::params() const
{
    static thread_local std::optional<media::ParamSet> reserva;
    if (params_)
        return *params_;
    reserva.emplace(descs_, media::ParamValues());
    return *reserva;
}

media::SourceStatus EfeitoSeq::status() const
{
    if (!aviso_.empty())
        return {media::SourceStatus::Level::Warning, aviso_};
    if (!instanciador_.erro().empty())
        return {media::SourceStatus::Level::Warning, instanciador_.erro()};
    return {};
}

bool EfeitoSeq::validarReferencias()
{
    const ParamSet& p = params();
    const auto snap = conjunto();
    if (p.desc("sequencia") && !snap->sequencia(lerInteiro(p, "sequencia"))) {
        aviso_ = "Sequência não encontrada. A imagem passa direto.";
        return false;
    }
    if (p.desc("visao") && !snap->visaoExiste(lerInteiro(p, "visao"))) {
        aviso_ = "Visão não encontrada. A imagem passa direto.";
        return false;
    }
    if (p.desc("envelope") && p.desc("curva") && p.s("curva") == "envelope" &&
        !snap->envelope(lerInteiro(p, "envelope"))) {
        aviso_ = "Envelope não encontrado. A imagem passa direto.";
        return false;
    }
    return true;
}

const std::vector<FeedSlice>& EfeitoSeq::slicesAtuais(int& canvasW, int& canvasH)
{
    SliceFeed& feed = SliceFeed::instance();
    const std::uint64_t v = feed.version();
    if (!temSlices_ || v != versaoSlices_) {
        slices_ = feed.slices();
        canvasW_ = feed.canvasWidth();
        canvasH_ = feed.canvasHeight();
        versaoSlices_ = v;
        temSlices_ = true;
    }
    canvasW = canvasW_;
    canvasH = canvasH_;
    return slices_;
}

seq::Relogio EfeitoSeq::relogio(const EffectContext& ctx) const
{
    seq::Relogio r;
    r.segundos = ctx.time;
    r.batidas = ctx.beat;
    return r;
}

void EfeitoSeq::desenharIntensidades(const EffectContext& ctx, GLuint entrada, int w, int h, Fbo& saida,
                                     const std::vector<seq::ItemIntensidade>& intensidades,
                                     const std::vector<int>* permitidos)
{
    const ParamSet& p = params();
    int cw = 0, ch = 0;
    const std::vector<FeedSlice>& slices = slicesAtuais(cw, ch);
    montarInstancias(slices, cw, ch, intensidades, permitidos, p.f("intensidade") / 100.0f, instancias_);
    ConfigDesenho config;
    config.escala = lerEscala(p);
    config.fundo = lerFundo(p);
    config.topOrigin = ctx.topOrigin;
    calcularCaixa(instancias_, config.caixa);
    instanciador_.desenhar(entrada, w, h, saida, config, instancias_);
}

bool EfeitoSeq::executar(const EffectContext&, GLuint, int, int, Fbo&)
{
    return false;
}

void EfeitoSeq::process(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output)
{
    if (inputTex == 0)
        return;
    aviso_.clear();
    if (!validarReferencias() || !instanciador_.pronto() || !executar(ctx, inputTex, w, h, output))
        instanciador_.passagemDireta(inputTex, w, h, output);
}

seq::ModoAvanco lerAvanco(const ParamSet& params)
{
    const std::string v = params.s("avanco");
    if (v == "bpm")
        return seq::ModoAvanco::Bpm;
    if (v == "fixo")
        return seq::ModoAvanco::Fixo;
    return seq::ModoAvanco::Velocidade;
}

seq::ModoCurva lerModoCurva(const ParamSet& params)
{
    return params.s("curva") == "envelope" ? seq::ModoCurva::Envelope : seq::ModoCurva::Sustentacao;
}

seq::ParametrosTempo lerParametrosTempo(const ParamSet& params)
{
    seq::ParametrosTempo t;
    t.avanco = lerAvanco(params);
    t.passosPorSegundo = params.f("velocidade");
    t.batidasPorPasso = params.f("batidas");
    t.passoFixo = std::max(params.i("passoFixo") - 1, 0);
    const std::string d = params.s("direcao");
    t.direcao = d == "tras" ? seq::DirecaoSeq::Tras
                : d == "vaievolta" ? seq::DirecaoSeq::VaiEVolta
                : d == "aleatorio" ? seq::DirecaoSeq::Aleatorio
                                   : seq::DirecaoSeq::Frente;
    t.deslocamento = params.i("deslocamento");
    t.ecos = params.i("ecos");
    t.sustentacao = params.f("sustentacao");
    t.liberacao = params.f("liberacao");
    t.curva = lerModoCurva(params);
    t.semente = params.i("semente");
    return t;
}

const seq::Envelope* envelopeDe(const SequenceSnapshot& snap, const ParamSet& params)
{
    if (lerModoCurva(params) != seq::ModoCurva::Envelope)
        return nullptr;
    return snap.envelope(lerInteiro(params, "envelope"));
}

media::ParamDesc paramSequencia()
{
    media::ParamDesc d = media::param::makeCustom("sequencia", "Sequência", "seq-ref", Json(0), "Sequência");
    d.max = 0.0;
    d.tooltip = "Sequência de passos de slices usada pelo efeito";
    return d;
}

media::ParamDesc paramVisao()
{
    media::ParamDesc d = media::param::makeCustom("visao", "Visão", "view-ref", Json(0), "Sequência");
    d.max = 0.0;
    d.tooltip = "Conjunto de slices em que o efeito age. Tudo = todos os slices";
    return d;
}

media::ParamDesc paramEnvelope(const char* grupo)
{
    media::ParamDesc d = media::param::makeCustom("envelope", "Envelope", "env-ref", Json(0), grupo);
    d.max = 0.0;
    d.enabledIf = "curva=envelope";
    d.tooltip = "Curva da biblioteca de envelopes";
    return d;
}

media::ParamDesc paramEscala(bool comEsticar)
{
    std::vector<media::ParamOption> opcoes = {{"preencher", "Preencher"}, {"mascara", "Máscara"}};
    if (comEsticar)
        opcoes.push_back({"esticar", "Esticar"});
    media::ParamDesc d = media::param::makeEnum("escala", "Escala", std::move(opcoes), "mascara", "Imagem");
    d.tooltip = "Preencher: o quadro inteiro na caixa dos slices acesos. Máscara: o vídeo no lugar, só nos acesos. "
                "Esticar: o quadro inteiro em cada slice aceso";
    return d;
}

media::ParamDesc paramFundo()
{
    media::ParamDesc d = media::param::makeEnum("fundo", "Fundo", {{"preto", "Preto"}, {"transparente", "Transparente"}},
                                                "preto", "Imagem");
    d.tooltip = "Transparente com mistura Normal deixa as camadas de baixo aparecerem nos slices apagados";
    return d;
}

media::ParamDesc paramIntensidade()
{
    media::ParamDesc d = media::param::makeFloat("intensidade", "Intensidade", 100.0f, 0.0f, 100.0f, "%.0f %%", "Imagem");
    d.tooltip = "Multiplica o brilho de todos os slices acesos";
    return d;
}

std::vector<media::ParamDesc> paramsAvanco(const char* grupo)
{
    std::vector<media::ParamDesc> d;
    d.push_back(media::param::makeEnum("avanco", "Avanço",
                                       {{"velocidade", "Velocidade"}, {"bpm", "BPM"}, {"fixo", "Fixo"}}, "velocidade",
                                       grupo));
    d.push_back(media::param::makeFloat("velocidade", "Velocidade", 4.0f, 0.1f, 60.0f, "%.1f passos/s", grupo));
    d.back().enabledIf = "avanco=velocidade";
    d.push_back(media::param::makeFloat("batidas", "Batidas por passo", 1.0f, 0.0625f, 8.0f, "%.3f", grupo));
    d.back().enabledIf = "avanco=bpm";
    d.push_back(media::param::makeInt("passoFixo", "Passo", 1, 1, seq::kMaxPassos, "%d", grupo));
    d.back().enabledIf = "avanco=fixo";
    return d;
}

std::vector<media::ParamDesc> paramsCurva()
{
    std::vector<media::ParamDesc> d;
    d.push_back(media::param::makeEnum("curva", "Curva do passo",
                                       {{"sustentacao", "Sustentação e liberação"}, {"envelope", "Envelope"}},
                                       "sustentacao", "Curva"));
    d.push_back(media::param::makeFloat("sustentacao", "Sustentação", 0.2f, 0.0f, 1.0f, "%.2f", "Curva"));
    d.back().enabledIf = "curva=sustentacao";
    d.push_back(media::param::makeFloat("liberacao", "Liberação", 0.8f, 0.0f, 1.0f, "%.2f", "Curva"));
    d.back().enabledIf = "curva=sustentacao";
    d.push_back(paramEnvelope());
    return d;
}

EscalaSeq lerEscala(const media::ParamSet& params)
{
    const std::string v = params.s("escala");
    if (v == "preencher")
        return EscalaSeq::Preencher;
    if (v == "esticar")
        return EscalaSeq::Esticar;
    return EscalaSeq::Mascara;
}

FundoSeq lerFundo(const media::ParamSet& params)
{
    return params.s("fundo") == "transparente" ? FundoSeq::Transparente : FundoSeq::Preto;
}

}
