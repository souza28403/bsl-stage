#include "seq/SeqTiming.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace bsl::seq {

namespace {

std::uint64_t misturar(std::uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

struct Gerador
{
    std::uint64_t estado;

    explicit Gerador(std::uint64_t semente) : estado(misturar(semente)) {}

    std::uint64_t proximo()
    {
        estado = misturar(estado);
        return estado;
    }

    int ate(int limite) { return static_cast<int>(proximo() % static_cast<std::uint64_t>(limite)); }
};

std::int64_t moduloPositivo(std::int64_t a, std::int64_t n)
{
    const std::int64_t r = a % n;
    return r < 0 ? r + n : r;
}

std::uint64_t chaveRodada(int semente, std::int64_t rodada)
{
    return misturar(static_cast<std::uint64_t>(static_cast<std::int64_t>(semente)) * 0x2545F4914F6CDD1Dull) ^
           static_cast<std::uint64_t>(rodada);
}

std::vector<int> permutacaoBruta(int total, int semente, std::int64_t rodada)
{
    std::vector<int> v(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i)
        v[static_cast<size_t>(i)] = i;
    Gerador g(chaveRodada(semente, rodada));
    for (int i = total - 1; i > 0; --i)
        std::swap(v[static_cast<size_t>(i)], v[static_cast<size_t>(g.ate(i + 1))]);
    return v;
}

int indiceAleatorio(std::int64_t contador, int total, int semente)
{
    if (total <= 1)
        return 0;
    if (total == 2)
        return static_cast<int>(moduloPositivo(contador, 2));
    const std::int64_t t = total;
    const std::int64_t rodada = (contador - moduloPositivo(contador, t)) / t;
    const int posicao = static_cast<int>(moduloPositivo(contador, t));
    std::vector<int> atual = permutacaoBruta(total, semente, rodada);
    if (posicao <= 1) {
        const std::vector<int> anterior = permutacaoBruta(total, semente, rodada - 1);
        if (atual[0] == anterior.back())
            std::swap(atual[0], atual[1]);
    }
    return atual[static_cast<size_t>(posicao)];
}

float curvaGeral(ModoCurva modo, float sustentacao, float liberacao, const Envelope* envelope, float idade)
{
    idade = std::clamp(idade, 0.0f, 1.0f);
    if (modo == ModoCurva::Envelope && envelope)
        return std::clamp(envelope->amostrar(idade), 0.0f, 1.0f);
    const float s = std::clamp(sustentacao, 0.0f, 1.0f);
    const float r = std::max(liberacao, 1e-4f);
    if (idade <= s)
        return 1.0f;
    return std::max(0.0f, 1.0f - (idade - s) / r);
}

double posicaoBruta(ModoAvanco avanco, float passosPorSegundo, float batidasPorPasso, int passoFixo, int deslocamento,
                    const Relogio& relogio)
{
    double pos = 0.0;
    switch (avanco) {
    case ModoAvanco::Velocidade:
        pos = relogio.segundos * static_cast<double>(std::max(passosPorSegundo, 0.0f));
        break;
    case ModoAvanco::Bpm:
        pos = relogio.batidas / static_cast<double>(limitarBatidasPorPasso(batidasPorPasso));
        break;
    case ModoAvanco::Fixo:
        pos = static_cast<double>(passoFixo);
        break;
    }
    return pos + static_cast<double>(deslocamento);
}

PosicaoPasso dividirPosicao(double pos, bool fixo)
{
    PosicaoPasso p;
    const double base = std::floor(pos);
    p.contador = static_cast<std::int64_t>(base);
    p.fase = fixo ? 0.0f : static_cast<float>(pos - base);
    return p;
}

void reduzir(std::vector<ItemIntensidade>& itens)
{
    std::sort(itens.begin(), itens.end(), [](const ItemIntensidade& a, const ItemIntensidade& b) {
        return a.uid != b.uid ? a.uid < b.uid : a.valor > b.valor;
    });
    itens.erase(std::unique(itens.begin(), itens.end(),
                            [](const ItemIntensidade& a, const ItemIntensidade& b) { return a.uid == b.uid; }),
                itens.end());
}

bool permitido(const std::vector<int>* permitidos, int uid)
{
    return !permitidos || std::binary_search(permitidos->begin(), permitidos->end(), uid);
}

void acumularPasso(const Passo& passo, float valor, const std::vector<int>* permitidos,
                   std::vector<ItemIntensidade>& saida)
{
    if (valor <= 0.0f)
        return;
    for (int uid : passo)
        if (permitido(permitidos, uid))
            saida.push_back({uid, valor});
}

}

float limitarBatidasPorPasso(float valor)
{
    return std::clamp(valor, 1.0f / 16.0f, 8.0f);
}

int limitarEcos(int ecos)
{
    return std::clamp(ecos, 0, kMaxEcos);
}

int limiteAleatorios(int disponiveis, int maximo)
{
    if (disponiveis <= 0)
        return 0;
    const int pedido = std::clamp(maximo, 1, kMaxAleatorios);
    return std::min(pedido, std::max(1, disponiveis / 2));
}

PosicaoPasso posicaoNoTempo(const ParametrosTempo& p, const Relogio& relogio)
{
    return dividirPosicao(posicaoBruta(p.avanco, p.passosPorSegundo, p.batidasPorPasso, p.passoFixo, p.deslocamento,
                                       relogio),
                          p.avanco == ModoAvanco::Fixo);
}

int indiceDoPasso(std::int64_t contador, int total, DirecaoSeq direcao, int semente)
{
    if (total <= 1)
        return 0;
    const std::int64_t t = total;
    switch (direcao) {
    case DirecaoSeq::Frente:
        return static_cast<int>(moduloPositivo(contador, t));
    case DirecaoSeq::Tras:
        return total - 1 - static_cast<int>(moduloPositivo(contador, t));
    case DirecaoSeq::VaiEVolta: {
        const std::int64_t periodo = 2 * t - 2;
        const std::int64_t m = moduloPositivo(contador, periodo);
        return static_cast<int>(m < t ? m : periodo - m);
    }
    case DirecaoSeq::Aleatorio:
        return indiceAleatorio(contador, total, semente);
    }
    return 0;
}

float valorDaCurva(const ParametrosTempo& p, const Envelope* envelope, float idade)
{
    return curvaGeral(p.curva, p.sustentacao, p.liberacao, envelope, idade);
}

void calcularIntensidades(const Sequencia& sequencia, const ParametrosTempo& parametros, const Envelope* envelope,
                          const Relogio& relogio, const std::vector<int>* permitidos,
                          std::vector<ItemIntensidade>& saida)
{
    saida.clear();
    const int total = static_cast<int>(sequencia.passos.size());
    if (total <= 0)
        return;
    const PosicaoPasso pos = posicaoNoTempo(parametros, relogio);
    const int ecos = limitarEcos(parametros.ecos);
    for (int k = 0; k <= ecos; ++k) {
        const int indice = indiceDoPasso(pos.contador - k, total, parametros.direcao, parametros.semente);
        const float idade = (static_cast<float>(k) + pos.fase) / static_cast<float>(ecos + 1);
        acumularPasso(sequencia.passos[static_cast<size_t>(indice)], valorDaCurva(parametros, envelope, idade),
                      permitidos, saida);
    }
    reduzir(saida);
}

void calcularVarredura(const Sequencia& sequencia, const ParametrosVarredura& p, const Envelope* envelope,
                       const Relogio& relogio, const std::vector<int>* permitidos, std::vector<ItemVarredura>& saida)
{
    saida.clear();
    const int total = static_cast<int>(sequencia.passos.size());
    if (total <= 0)
        return;
    const double sobreposicao = std::clamp(static_cast<double>(p.sobreposicao), 0.0, 16.0);
    const double ciclo = static_cast<double>(total) + sobreposicao;
    double pos = posicaoBruta(p.avanco, p.passosPorSegundo, p.batidasPorPasso, p.passoFixo, p.deslocamento, relogio);
    pos -= std::floor(pos / ciclo) * ciclo;
    const int voltas = std::clamp(p.voltas, 1, kMaxVoltas);

    std::vector<std::pair<int, int>> primeiros;
    for (int i = 0; i < total; ++i)
        for (int uid : sequencia.passos[static_cast<size_t>(i)])
            if (permitido(permitidos, uid))
                primeiros.emplace_back(uid, i);
    std::sort(primeiros.begin(), primeiros.end());
    primeiros.erase(std::unique(primeiros.begin(), primeiros.end(),
                                [](const auto& a, const auto& b) { return a.first == b.first; }),
                    primeiros.end());

    for (const auto& [uid, indice] : primeiros) {
        ItemVarredura item;
        item.uid = uid;
        item.invertido = p.inverter != (p.alternarParesImpares && (indice % 2 == 1));
        const double x = (pos - static_cast<double>(indice)) / (1.0 + sobreposicao);
        if (x >= 0.0 && x <= 1.0) {
            item.ativo = true;
            double q = x * static_cast<double>(voltas);
            if (x < 1.0)
                q -= std::floor(q);
            else
                q = 1.0;
            float progresso = static_cast<float>(q);
            if (item.invertido)
                progresso = 1.0f - progresso;
            if (p.usarEnvelope && envelope)
                progresso = std::clamp(envelope->amostrar(progresso), 0.0f, 1.0f);
            item.progresso = progresso;
        }
        saida.push_back(item);
    }
}

void MaquinaDisparo::disparar(int indice, double agora)
{
    disparos_.push_back({indice, agora});
    while (disparos_.size() > 64)
        disparos_.pop_front();
    atual_ = indice;
    ultimo_ = agora;
}

void MaquinaDisparo::proximo(double agora, int total)
{
    if (total <= 0)
        return;
    disparar(atual_ < 0 ? 0 : (atual_ + 1) % total, agora);
}

void MaquinaDisparo::anterior(double agora, int total)
{
    if (total <= 0)
        return;
    disparar(atual_ < 0 ? total - 1 : (atual_ - 1 + total) % total, agora);
}

void MaquinaDisparo::rajada(bool ativa, double agora)
{
    if (ativa && !rajada_)
        proximaRajada_ = agora;
    rajada_ = ativa;
}

void MaquinaDisparo::reiniciar()
{
    disparos_.clear();
    atual_ = -1;
    rajada_ = false;
    ultimo_ = 0.0;
    proximaRajada_ = 0.0;
}

void MaquinaDisparo::atualizar(double agora, const ParametrosDisparo& p, int total)
{
    const double apagar = static_cast<double>(std::max(p.velocidadeApagar, 0.01f));
    while (!disparos_.empty() && (agora - disparos_.front().inicio) * apagar >= 1.0)
        disparos_.pop_front();
    if (total <= 0) {
        atual_ = -1;
        disparos_.clear();
        return;
    }
    if (atual_ >= total)
        atual_ = -1;
    if (rajada_) {
        const double intervalo = 1.0 / static_cast<double>(std::max(p.velocidadeRajada, 0.1f));
        int guarda = 0;
        while (agora >= proximaRajada_ && guarda < 64) {
            disparar(atual_ < 0 ? 0 : (atual_ + 1) % total, proximaRajada_);
            proximaRajada_ += intervalo;
            ++guarda;
        }
        if (guarda >= 64)
            proximaRajada_ = agora + intervalo;
    } else if (p.resetAutomatico && atual_ >= 0 && agora - ultimo_ >= static_cast<double>(std::max(p.atrasoReset, 0.0f))) {
        atual_ = -1;
    }
}

void MaquinaDisparo::calcular(const Sequencia& sequencia, const ParametrosDisparo& p, const Envelope* envelope,
                              double agora, const std::vector<int>* permitidos,
                              std::vector<ItemIntensidade>& saida) const
{
    saida.clear();
    const double apagar = static_cast<double>(std::max(p.velocidadeApagar, 0.01f));
    const int total = static_cast<int>(sequencia.passos.size());
    for (const Disparo& d : disparos_) {
        if (d.indice < 0 || d.indice >= total)
            continue;
        const float idade = static_cast<float>(std::clamp((agora - d.inicio) * apagar, 0.0, 1.0));
        acumularPasso(sequencia.passos[static_cast<size_t>(d.indice)],
                      curvaGeral(p.curva, p.sustentacao, p.liberacao, envelope, idade), permitidos, saida);
    }
    reduzir(saida);
}

void SorteioSlices::reiniciar()
{
    historico_.clear();
    contador_ = 0;
    fase_ = 0.0f;
    assinatura_ = 0;
    iniciado_ = false;
}

std::vector<int> SorteioSlices::sortear(const ParametrosAleatorios& p, const std::vector<int>& candidatos,
                                        std::int64_t contador, const std::vector<int>* anterior) const
{
    const int quantidade = limiteAleatorios(static_cast<int>(candidatos.size()), p.maximo);
    if (quantidade <= 0)
        return {};
    std::vector<int> pool;
    std::vector<int> reserva;
    for (int uid : candidatos) {
        if (p.semRepetir && anterior && std::binary_search(anterior->begin(), anterior->end(), uid))
            reserva.push_back(uid);
        else
            pool.push_back(uid);
    }
    Gerador g(chaveRodada(p.semente, contador));
    auto embaralhar = [&g](std::vector<int>& v) {
        for (int i = static_cast<int>(v.size()) - 1; i > 0; --i)
            std::swap(v[static_cast<size_t>(i)], v[static_cast<size_t>(g.ate(i + 1))]);
    };
    embaralhar(pool);
    embaralhar(reserva);
    pool.insert(pool.end(), reserva.begin(), reserva.end());
    pool.resize(static_cast<size_t>(quantidade));
    std::sort(pool.begin(), pool.end());
    return pool;
}

void SorteioSlices::atualizar(const ParametrosAleatorios& p, const std::vector<int>& candidatos,
                              const Relogio& relogio)
{
    std::uint64_t assinatura = misturar(static_cast<std::uint64_t>(p.maximo) * 31 + (p.semRepetir ? 7 : 3));
    assinatura = misturar(assinatura ^ static_cast<std::uint64_t>(static_cast<std::int64_t>(p.semente)));
    for (int uid : candidatos)
        assinatura = misturar(assinatura ^ static_cast<std::uint64_t>(uid));

    ParametrosTempo t;
    t.avanco = p.avanco;
    t.passosPorSegundo = p.passosPorSegundo;
    t.batidasPorPasso = p.batidasPorPasso;
    const PosicaoPasso pos = posicaoNoTempo(t, relogio);
    const int ecos = limitarEcos(p.ecos);

    if (!iniciado_ || assinatura != assinatura_ || pos.contador < contador_ || pos.contador - contador_ > kMaxEcos + 1) {
        historico_.clear();
        iniciado_ = true;
        assinatura_ = assinatura;
        contador_ = pos.contador - ecos - 1;
    }
    for (std::int64_t c = contador_ + 1; c <= pos.contador; ++c) {
        const std::vector<int>* anterior = historico_.empty() ? nullptr : &historico_.back().uids;
        Rodada r;
        r.contador = c;
        r.uids = sortear(p, candidatos, c, anterior);
        historico_.push_back(std::move(r));
        while (historico_.size() > static_cast<size_t>(kMaxEcos) + 2)
            historico_.pop_front();
    }
    contador_ = pos.contador;
    fase_ = pos.fase;
}

void SorteioSlices::calcular(const ParametrosAleatorios& p, const Envelope* envelope,
                             std::vector<ItemIntensidade>& saida) const
{
    saida.clear();
    const int ecos = limitarEcos(p.ecos);
    for (const Rodada& r : historico_) {
        const std::int64_t k = contador_ - r.contador;
        if (k < 0 || k > ecos)
            continue;
        const float idade = (static_cast<float>(k) + fase_) / static_cast<float>(ecos + 1);
        for (int uid : r.uids) {
            const float v = curvaGeral(p.curva, p.sustentacao, p.liberacao, envelope, idade);
            if (v > 0.0f)
                saida.push_back({uid, v});
        }
    }
    reduzir(saida);
}

const std::vector<int>& SorteioSlices::sorteioAtual() const
{
    static const std::vector<int> vazio;
    return historico_.empty() ? vazio : historico_.back().uids;
}

}
