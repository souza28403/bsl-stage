#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/SliceInstancer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

namespace bsl::fx::bsllight {

using namespace media::param;

namespace {

constexpr double kIntervalo = 1.0 / 30.0;
constexpr int kMaxCamadas = 256;
constexpr int kTetoMb = 256;
constexpr int kMaxCamadasPrevia = 16;
constexpr float kQualidadePrevia = 25.0f;
constexpr double kOciosoSegundos = 2.0;

class EfeitoAtraso final : public EfeitoSeq
{
public:
    EfeitoAtraso() : EfeitoSeq(kAtrasoSlicesId, "Atraso por slice", describeAtrasoSlices())
    {
        instancias_.reserve(256);
        mapa_.reserve(256);
        temporario_.reserve(256);
    }

    ~EfeitoAtraso() override
    {
        liberar();
        if (leitura_)
            glDeleteFramebuffers(1, &leitura_);
        if (escrita_)
            glDeleteFramebuffers(1, &escrita_);
    }

    media::SourceStatus status() const override
    {
        const media::SourceStatus base = EfeitoSeq::status();
        if (base.level != media::SourceStatus::Level::Ok || !limitado_ || !arranjo_)
            return base;
        char texto[200];
        const double mb = static_cast<double>(camadas_) * static_cast<double>(lw_) * static_cast<double>(lh_) * 4.0 /
                          1048576.0;
        std::snprintf(texto, sizeof(texto),
                      "Memória limitada: histórico de %d quadros (%.0f MB), atraso máximo de %.2f s. O atraso pedido foi "
                      "reduzido.",
                      camadas_, mb, static_cast<double>(camadas_ - 1) * kIntervalo);
        return {media::SourceStatus::Level::Info, texto};
    }

protected:
    bool executar(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) override
    {
        const media::ParamSet& p = params();
        const auto snap = conjunto();
        const int idSequencia = lerInteiro(p, "sequencia");
        const seq::Sequencia* sequencia = snap->sequencia(idSequencia);
        if (!sequencia)
            return false;
        const double agora = ctx.time;
        const int total = static_cast<int>(sequencia->passos.size());
        const bool inverter = p.b("inverter");

        atualizarMapa(snap->versao, idSequencia, *sequencia);

        const double unidade = segundosDaUnidade(p, ctx);
        const int kMaximo = inverter ? total - 1 - kMinimo_ : kMaximo_;
        const double atrasoMaximo = unidade * static_cast<double>(std::max(kMaximo, 0));

        const float qualidade = ctx.preview ? kQualidadePrevia : std::clamp(p.f("qualidade"), 25.0f, 100.0f);
        int memoriaMb = std::clamp(p.i("memoria"), 1, kTetoMb);
        if (ctx.eco)
            memoriaMb = std::min(memoriaMb, 48);
        const int lw = std::clamp(static_cast<int>(std::lround(w * qualidade / 100.0f)), 1, std::max(w, 1));
        const int lh = std::clamp(static_cast<int>(std::lround(h * qualidade / 100.0f)), 1, std::max(h, 1));
        const std::size_t bytes = static_cast<std::size_t>(lw) * static_cast<std::size_t>(lh) * 4;
        const std::size_t orcamento = static_cast<std::size_t>(memoriaMb) * 1048576u;
        int maxCamadas = static_cast<int>(std::clamp<std::size_t>(orcamento / bytes, 2, kMaxCamadas));
        if (ctx.preview)
            maxCamadas = std::min(maxCamadas, kMaxCamadasPrevia);

        const int pedidas = static_cast<int>(std::ceil(atrasoMaximo / kIntervalo)) + 2;
        limitado_ = pedidas > maxCamadas;
        const int necessarias = std::clamp(pedidas, 2, maxCamadas);

        if (atrasoMaximo <= 0.0) {
            if (semAtrasoDesde_ < 0.0)
                semAtrasoDesde_ = agora;
            if (agora - semAtrasoDesde_ >= kOciosoSegundos)
                liberar();
        } else {
            semAtrasoDesde_ = -1.0;
            if (arranjo_ && (lw_ != lw || lh_ != lh || camadas_ > maxCamadas || camadas_ < necessarias))
                liberar();
            if (!arranjo_) {
                const int alvo = std::min(maxCamadas, std::max(necessarias, necessarias + necessarias / 2));
                if (!alocar(lw, lh, alvo)) {
                    avisar("Sem memória de vídeo para o histórico do atraso. A imagem passa direto.");
                    return false;
                }
            }
            if (contagem_ > 0 &&
                (agora < ultimoTempo_ - 0.001 || agora - ultimoTempo_ > static_cast<double>(p.i("resetPausa")) * 60.0))
                contagem_ = 0;
            if (contagem_ == 0 || agora - ultimoTempo_ >= kIntervalo - 1.0e-4)
                gravar(inputTex, w, h, agora);
        }

        int cw = 0, ch = 0;
        const std::vector<FeedSlice>& slices = slicesAtuais(cw, ch);
        const float geral = std::clamp(p.f("intensidade") / 100.0f, 0.0f, 1.0f);
        instancias_.clear();
        float indice = 0.0f;
        for (const FeedSlice& s : slices) {
            if (geral <= 0.0f)
                break;
            if (s.uid <= 0 || s.w <= 0.0f || s.h <= 0.0f)
                continue;
            InstanciaSlice inst = instanciaDe(s, cw, ch, geral, indice++);
            int k = procurarPasso(s.uid);
            if (k >= 0 && inverter)
                k = total - 1 - k;
            if (k > 0 && unidade > 0.0 && arranjo_ && contagem_ > 0) {
                inst.extra[0] = static_cast<float>(camadaPara(agora - unidade * static_cast<double>(k)));
                inst.extra[1] = 1.0f;
            }
            instancias_.push_back(inst);
        }

        ConfigDesenho config;
        config.escala = EscalaSeq::Mascara;
        config.fundo = lerFundo(p);
        config.topOrigin = ctx.topOrigin;
        config.arranjo = arranjo_;
        instanciador().desenhar(inputTex, w, h, output, config, instancias_);
        return true;
    }

private:
    static double segundosDaUnidade(const media::ParamSet& p, const EffectContext& ctx)
    {
        const double valor = static_cast<double>(p.f("atraso"));
        const std::string u = p.s("unidade");
        if (u == "segundos")
            return valor;
        if (u == "batidas")
            return valor * 60.0 / static_cast<double>(std::max(ctx.bpm, 1.0f));
        return valor * kIntervalo;
    }

    void atualizarMapa(std::uint64_t versao, int idSequencia, const seq::Sequencia& sequencia)
    {
        if (temMapa_ && versao == mapaVersao_ && idSequencia == mapaSequencia_)
            return;
        temMapa_ = true;
        mapaVersao_ = versao;
        mapaSequencia_ = idSequencia;
        temporario_.clear();
        for (int i = 0; i < static_cast<int>(sequencia.passos.size()); ++i)
            for (int uid : sequencia.passos[static_cast<std::size_t>(i)])
                temporario_.emplace_back(uid, i);
        std::sort(temporario_.begin(), temporario_.end());
        temporario_.erase(std::unique(temporario_.begin(), temporario_.end(),
                                      [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                                          return a.first == b.first;
                                      }),
                          temporario_.end());
        mapa_.swap(temporario_);
        kMinimo_ = 0;
        kMaximo_ = 0;
        if (!mapa_.empty()) {
            kMinimo_ = kMaximo_ = mapa_.front().second;
            for (const auto& par : mapa_) {
                kMinimo_ = std::min(kMinimo_, par.second);
                kMaximo_ = std::max(kMaximo_, par.second);
            }
        }
    }

    int procurarPasso(int uid) const
    {
        const auto it = std::lower_bound(mapa_.begin(), mapa_.end(), uid,
                                         [](const std::pair<int, int>& a, int u) { return a.first < u; });
        return (it != mapa_.end() && it->first == uid) ? it->second : -1;
    }

    int fisico(int i) const { return (proxima_ + camadas_ - contagem_ + i) % camadas_; }

    int camadaPara(double alvo) const
    {
        if (tempos_[static_cast<std::size_t>(fisico(0))] > alvo)
            return fisico(0);
        int lo = 0;
        int hi = contagem_ - 1;
        while (lo < hi) {
            const int meio = (lo + hi + 1) / 2;
            if (tempos_[static_cast<std::size_t>(fisico(meio))] <= alvo)
                lo = meio;
            else
                hi = meio - 1;
        }
        return fisico(lo);
    }

    bool alocar(int lw, int lh, int camadas)
    {
        glGetError();
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D_ARRAY, t);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, lw, lh, camadas, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        if (glGetError() != GL_NO_ERROR) {
            glDeleteTextures(1, &t);
            return false;
        }
        if (!leitura_)
            glGenFramebuffers(1, &leitura_);
        if (!escrita_)
            glGenFramebuffers(1, &escrita_);
        arranjo_ = t;
        lw_ = lw;
        lh_ = lh;
        camadas_ = camadas;
        proxima_ = 0;
        contagem_ = 0;
        return true;
    }

    void liberar()
    {
        if (arranjo_)
            glDeleteTextures(1, &arranjo_);
        arranjo_ = 0;
        camadas_ = 0;
        contagem_ = 0;
        proxima_ = 0;
        lw_ = lh_ = 0;
    }

    void gravar(GLuint entrada, int w, int h, double agora)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, leitura_);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, entrada, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, escrita_);
        glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, arranjo_, 0, proxima_);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glBlitFramebuffer(0, 0, w, h, 0, 0, lw_, lh_, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        tempos_[static_cast<std::size_t>(proxima_)] = agora;
        proxima_ = (proxima_ + 1) % camadas_;
        contagem_ = std::min(contagem_ + 1, camadas_);
        ultimoTempo_ = agora;
    }

    GLuint arranjo_ = 0;
    GLuint leitura_ = 0;
    GLuint escrita_ = 0;
    int lw_ = 0;
    int lh_ = 0;
    int camadas_ = 0;
    int proxima_ = 0;
    int contagem_ = 0;
    double ultimoTempo_ = 0.0;
    double semAtrasoDesde_ = -1.0;
    bool limitado_ = false;
    std::array<double, kMaxCamadas> tempos_{};
    std::vector<InstanciaSlice> instancias_;
    std::vector<std::pair<int, int>> mapa_;
    std::vector<std::pair<int, int>> temporario_;
    bool temMapa_ = false;
    std::uint64_t mapaVersao_ = 0;
    int mapaSequencia_ = -1;
    int kMinimo_ = 0;
    int kMaximo_ = 0;
};

}

std::vector<media::ParamDesc> describeAtrasoSlices()
{
    std::vector<media::ParamDesc> d;
    d.push_back(paramSequencia());
    d.back().tooltip = "O passo k da sequência atrasa k vezes a unidade. Slices fora da sequência não têm atraso";
    d.push_back(makeFloat("atraso", "Atraso", 1.0f, 0.0f, 60.0f, "%.2f", "Atraso"));
    d.push_back(makeEnum("unidade", "Unidade", {{"quadros", "Quadros"}, {"segundos", "Segundos"}, {"batidas", "Batidas"}},
                         "quadros", "Atraso"));
    d.back().tooltip = "Os quadros contam 30 por segundo, a taxa em que o histórico é gravado";
    d.push_back(makeBool("inverter", "Inverter ordem", false, "Atraso"));
    d.push_back(makeFloat("qualidade", "Qualidade", 50.0f, 25.0f, 100.0f, "%.0f %%", "Memória"));
    d.back().tooltip = "Resolução do histórico guardado. Menos qualidade permite atrasos mais longos";
    d.push_back(makeInt("memoria", "Memória máxima", 96, 16, kTetoMb, "%d MB", "Memória"));
    d.back().tooltip = "No perfil Eco o limite cai para 48 MB";
    d.push_back(makeInt("resetPausa", "Limpar após pausa", 5, 1, 60, "%d min", "Memória"));
    d.push_back(paramFundo());
    d.push_back(paramIntensidade());
    return d;
}

std::unique_ptr<IEffect> makeAtrasoSlices()
{
    return std::make_unique<EfeitoAtraso>();
}

}
