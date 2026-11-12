#include "seqtest.h"

#include "seq/Envelope.h"
#include "seq/SeqTiming.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace seqtest {

using namespace bsl::seq;

namespace {

bool perto(float a, float b, float tol = 1e-4f)
{
    return std::fabs(a - b) <= tol;
}

float valorDe(const std::vector<ItemIntensidade>& v, int uid)
{
    for (const ItemIntensidade& i : v)
        if (i.uid == uid)
            return i.valor;
    return 0.0f;
}

Sequencia sequenciaDeQuatro()
{
    Sequencia s;
    s.passos = {Passo{1}, Passo{2}, Passo{3}, Passo{4}};
    return s;
}

Relogio em(double segundos, double batidas = 0.0)
{
    Relogio r;
    r.segundos = segundos;
    r.batidas = batidas;
    return r;
}

}

void testarTempo()
{
    secao("tempo");

    VERIFICAR(perto(limitarBatidasPorPasso(0.0f), 1.0f / 16.0f) && perto(limitarBatidasPorPasso(100.0f), 8.0f));
    VERIFICAR(limitarEcos(-3) == 0 && limitarEcos(99) == 16);
    VERIFICAR(limiteAleatorios(0, 5) == 0 && limiteAleatorios(10, 100) == 5 && limiteAleatorios(1, 4) == 1);
    VERIFICAR(limiteAleatorios(500, 100) == 64 && limiteAleatorios(500, 0) == 1);

    ParametrosTempo vel;
    vel.avanco = ModoAvanco::Velocidade;
    vel.passosPorSegundo = 2.0f;
    PosicaoPasso p = posicaoNoTempo(vel, em(1.25));
    VERIFICAR(p.contador == 2 && perto(p.fase, 0.5f));

    ParametrosTempo bpm;
    bpm.avanco = ModoAvanco::Bpm;
    bpm.batidasPorPasso = 0.5f;
    p = posicaoNoTempo(bpm, em(0.0, 3.0));
    VERIFICAR(p.contador == 6 && perto(p.fase, 0.0f));
    bpm.batidasPorPasso = 2.0f;
    p = posicaoNoTempo(bpm, em(0.0, 3.0));
    VERIFICAR(p.contador == 1 && perto(p.fase, 0.5f));

    ParametrosTempo fixo;
    fixo.avanco = ModoAvanco::Fixo;
    fixo.passoFixo = 3;
    p = posicaoNoTempo(fixo, em(99.0, 99.0));
    VERIFICAR(p.contador == 3 && perto(p.fase, 0.0f));
    fixo.deslocamento = 2;
    VERIFICAR(posicaoNoTempo(fixo, em(0.0)).contador == 5);

    VERIFICAR(indiceDoPasso(0, 4, DirecaoSeq::Frente, 1) == 0 && indiceDoPasso(5, 4, DirecaoSeq::Frente, 1) == 1);
    VERIFICAR(indiceDoPasso(-1, 4, DirecaoSeq::Frente, 1) == 3);
    VERIFICAR(indiceDoPasso(0, 4, DirecaoSeq::Tras, 1) == 3 && indiceDoPasso(1, 4, DirecaoSeq::Tras, 1) == 2);
    const int esperado[] = {0, 1, 2, 3, 2, 1, 0, 1};
    for (int i = 0; i < 8; ++i)
        VERIFICAR(indiceDoPasso(i, 4, DirecaoSeq::VaiEVolta, 1) == esperado[i]);
    VERIFICAR(indiceDoPasso(7, 1, DirecaoSeq::VaiEVolta, 1) == 0);
    VERIFICAR(indiceDoPasso(7, 1, DirecaoSeq::Aleatorio, 1) == 0);

    for (int total : {2, 3, 4, 7}) {
        bool semRepeticao = true;
        bool faixa = true;
        for (std::int64_t c = -20; c < 200; ++c) {
            const int a = indiceDoPasso(c, total, DirecaoSeq::Aleatorio, 5);
            const int b = indiceDoPasso(c + 1, total, DirecaoSeq::Aleatorio, 5);
            semRepeticao = semRepeticao && a != b;
            faixa = faixa && a >= 0 && a < total && b >= 0 && b < total;
        }
        VERIFICAR(semRepeticao);
        VERIFICAR(faixa);
    }
    {
        std::set<int> rodada;
        for (int c = 0; c < 7; ++c)
            rodada.insert(indiceDoPasso(c, 7, DirecaoSeq::Aleatorio, 9));
        VERIFICAR(rodada.size() == 7);
        bool igual = true;
        bool diferente = false;
        for (int c = 0; c < 40; ++c) {
            igual = igual && indiceDoPasso(c, 7, DirecaoSeq::Aleatorio, 9) == indiceDoPasso(c, 7, DirecaoSeq::Aleatorio, 9);
            diferente = diferente || indiceDoPasso(c, 7, DirecaoSeq::Aleatorio, 9) != indiceDoPasso(c, 7, DirecaoSeq::Aleatorio, 10);
        }
        VERIFICAR(igual && diferente);
    }

    ParametrosTempo curva;
    curva.sustentacao = 0.2f;
    curva.liberacao = 0.8f;
    VERIFICAR(perto(valorDaCurva(curva, nullptr, 0.0f), 1.0f));
    VERIFICAR(perto(valorDaCurva(curva, nullptr, 0.2f), 1.0f));
    VERIFICAR(perto(valorDaCurva(curva, nullptr, 0.6f), 0.5f));
    VERIFICAR(perto(valorDaCurva(curva, nullptr, 1.0f), 0.0f));
    VERIFICAR(perto(valorDaCurva(curva, nullptr, 3.0f), 0.0f));
    curva.curva = ModoCurva::Envelope;
    const Envelope linear = envelopeLinear(1);
    VERIFICAR(perto(valorDaCurva(curva, &linear, 0.25f), 0.25f, 1e-2f));
    VERIFICAR(perto(valorDaCurva(curva, nullptr, 0.6f), 0.5f));

    const Sequencia seq = sequenciaDeQuatro();
    std::vector<ItemIntensidade> r;
    ParametrosTempo t;
    t.avanco = ModoAvanco::Fixo;
    t.passoFixo = 1;
    calcularIntensidades(seq, t, nullptr, em(0.0), nullptr, r);
    VERIFICAR(r.size() == 1 && r[0].uid == 2 && perto(r[0].valor, 1.0f));

    t.ecos = 3;
    calcularIntensidades(seq, t, nullptr, em(0.0), nullptr, r);
    VERIFICAR(r.size() == 4);
    VERIFICAR(perto(valorDe(r, 2), 1.0f) && perto(valorDe(r, 1), 0.9375f));
    VERIFICAR(perto(valorDe(r, 4), 0.625f) && perto(valorDe(r, 3), 0.3125f));

    t.avanco = ModoAvanco::Velocidade;
    t.passosPorSegundo = 1.0f;
    t.ecos = 0;
    t.sustentacao = 0.5f;
    t.liberacao = 0.5f;
    calcularIntensidades(seq, t, nullptr, em(2.5), nullptr, r);
    VERIFICAR(r.size() == 1 && r[0].uid == 3 && perto(r[0].valor, 1.0f));
    calcularIntensidades(seq, t, nullptr, em(2.75), nullptr, r);
    VERIFICAR(r.size() == 1 && perto(r[0].valor, 0.5f));
    calcularIntensidades(seq, t, nullptr, em(2.99), nullptr, r);
    VERIFICAR(r.size() == 1 && r[0].valor < 0.05f);

    t.direcao = DirecaoSeq::Tras;
    calcularIntensidades(seq, t, nullptr, em(0.0), nullptr, r);
    VERIFICAR(r.size() == 1 && r[0].uid == 4);

    t.direcao = DirecaoSeq::Frente;
    t.deslocamento = 1;
    calcularIntensidades(seq, t, nullptr, em(0.0), nullptr, r);
    VERIFICAR(r.size() == 1 && r[0].uid == 2);

    t.deslocamento = 0;
    t.avanco = ModoAvanco::Bpm;
    t.batidasPorPasso = 1.0f;
    calcularIntensidades(seq, t, nullptr, em(0.0, 5.0), nullptr, r);
    VERIFICAR(r.size() == 1 && r[0].uid == 2);

    Sequencia multi;
    multi.passos = {Passo{1, 2}, Passo{2, 3}};
    ParametrosTempo m;
    m.avanco = ModoAvanco::Fixo;
    m.passoFixo = 1;
    m.ecos = 1;
    m.sustentacao = 0.4f;
    m.liberacao = 0.6f;
    calcularIntensidades(multi, m, nullptr, em(0.0), nullptr, r);
    VERIFICAR(r.size() == 3 && perto(valorDe(r, 2), 1.0f) && perto(valorDe(r, 3), 1.0f) && perto(valorDe(r, 1), 1.0f - (0.5f - 0.4f) / 0.6f));
    const std::vector<int> visao = {2, 3};
    calcularIntensidades(multi, m, nullptr, em(0.0), &visao, r);
    VERIFICAR(r.size() == 2 && valorDe(r, 1) == 0.0f);

    Sequencia vazia;
    calcularIntensidades(vazia, m, nullptr, em(0.0), nullptr, r);
    VERIFICAR(r.empty());

    ParametrosVarredura v;
    v.avanco = ModoAvanco::Velocidade;
    v.passosPorSegundo = 1.0f;
    std::vector<ItemVarredura> vr;
    calcularVarredura(seq, v, nullptr, em(1.5), nullptr, vr);
    VERIFICAR(vr.size() == 4);
    VERIFICAR(vr[0].uid == 1 && !vr[0].ativo);
    VERIFICAR(vr[1].uid == 2 && vr[1].ativo && perto(vr[1].progresso, 0.5f));
    VERIFICAR(!vr[2].ativo && !vr[3].ativo);

    v.inverter = true;
    calcularVarredura(seq, v, nullptr, em(1.25), nullptr, vr);
    VERIFICAR(vr[1].invertido && perto(vr[1].progresso, 0.75f));
    v.inverter = false;
    v.alternarParesImpares = true;
    calcularVarredura(seq, v, nullptr, em(1.25), nullptr, vr);
    VERIFICAR(vr[1].invertido && !vr[0].invertido && !vr[2].invertido);

    v.alternarParesImpares = false;
    v.sobreposicao = 1.0f;
    calcularVarredura(seq, v, nullptr, em(1.0), nullptr, vr);
    VERIFICAR(vr[0].ativo && perto(vr[0].progresso, 0.5f) && vr[1].ativo && perto(vr[1].progresso, 0.0f));
    calcularVarredura(seq, v, nullptr, em(4.5), nullptr, vr);
    VERIFICAR(vr[3].ativo && perto(vr[3].progresso, 0.75f) && !vr[0].ativo);
    calcularVarredura(seq, v, nullptr, em(5.5), nullptr, vr);
    VERIFICAR(vr[0].ativo && perto(vr[0].progresso, 0.25f));

    v.sobreposicao = 0.0f;
    v.voltas = 2;
    calcularVarredura(seq, v, nullptr, em(0.25), nullptr, vr);
    VERIFICAR(vr[0].ativo && perto(vr[0].progresso, 0.5f));
    v.voltas = 1;
    v.usarEnvelope = true;
    const Envelope s = envelopeCurvaS(2);
    calcularVarredura(seq, v, &s, em(0.5), nullptr, vr);
    VERIFICAR(perto(vr[0].progresso, 0.5f, 2e-2f));
    calcularVarredura(seq, v, &s, em(0.1), nullptr, vr);
    VERIFICAR(vr[0].progresso < 0.1f);
    v.avanco = ModoAvanco::Fixo;
    v.passoFixo = 2;
    v.usarEnvelope = false;
    calcularVarredura(seq, v, nullptr, em(0.0), &visao, vr);
    VERIFICAR(vr.size() == 2 && vr[0].uid == 2 && vr[1].uid == 3 && vr[1].ativo);

    MaquinaDisparo d;
    ParametrosDisparo pd;
    pd.velocidadeApagar = 1.0f;
    pd.sustentacao = 0.5f;
    pd.liberacao = 0.5f;
    std::vector<ItemIntensidade> di;
    d.calcular(seq, pd, nullptr, 0.0, nullptr, di);
    VERIFICAR(di.empty() && d.passoAtual() == -1);
    d.proximo(1.0, 4);
    VERIFICAR(d.passoAtual() == 0);
    d.calcular(seq, pd, nullptr, 1.0, nullptr, di);
    VERIFICAR(di.size() == 1 && di[0].uid == 1 && perto(di[0].valor, 1.0f));
    d.calcular(seq, pd, nullptr, 1.75, nullptr, di);
    VERIFICAR(di.size() == 1 && perto(di[0].valor, 0.5f));
    d.proximo(1.5, 4);
    d.calcular(seq, pd, nullptr, 1.75, nullptr, di);
    VERIFICAR(di.size() == 2 && perto(valorDe(di, 2), 1.0f) && perto(valorDe(di, 1), 0.5f));
    d.atualizar(2.25, pd, 4);
    VERIFICAR(d.disparosAtivos() == 1);
    d.atualizar(3.0, pd, 4);
    VERIFICAR(d.disparosAtivos() == 0 && d.passoAtual() == 1);
    d.anterior(3.0, 4);
    d.anterior(3.0, 4);
    VERIFICAR(d.passoAtual() == 3);
    d.reiniciar();
    VERIFICAR(d.passoAtual() == -1 && d.disparosAtivos() == 0);
    d.anterior(4.0, 4);
    VERIFICAR(d.passoAtual() == 3);
    d.reiniciar();

    pd.resetAutomatico = true;
    pd.atrasoReset = 2.0f;
    d.proximo(10.0, 4);
    d.atualizar(11.0, pd, 4);
    VERIFICAR(d.passoAtual() == 0);
    d.atualizar(12.5, pd, 4);
    VERIFICAR(d.passoAtual() == -1);
    d.proximo(13.0, 4);
    VERIFICAR(d.passoAtual() == 0);

    d.reiniciar();
    pd.resetAutomatico = false;
    pd.velocidadeRajada = 4.0f;
    pd.velocidadeApagar = 0.25f;
    d.rajada(true, 20.0);
    d.atualizar(20.0, pd, 4);
    VERIFICAR(d.passoAtual() == 0 && d.rajadaAtiva());
    d.atualizar(20.6, pd, 4);
    VERIFICAR(d.passoAtual() == 2);
    d.atualizar(21.3, pd, 4);
    VERIFICAR(d.passoAtual() == 1);
    d.rajada(false, 21.3);
    d.atualizar(30.0, pd, 4);
    VERIFICAR(d.passoAtual() == 1 && !d.rajadaAtiva());
    d.atualizar(50.0, pd, 2);
    VERIFICAR(d.passoAtual() == 1);
    d.atualizar(51.0, pd, 1);
    VERIFICAR(d.passoAtual() == -1);

    SorteioSlices sorteio;
    ParametrosAleatorios pa;
    pa.maximo = 3;
    pa.passosPorSegundo = 1.0f;
    pa.sustentacao = 1.0f;
    pa.liberacao = 1.0f;
    std::vector<int> candidatos;
    for (int u = 1; u <= 20; ++u)
        candidatos.push_back(u);
    sorteio.atualizar(pa, candidatos, em(0.0));
    VERIFICAR(sorteio.sorteioAtual().size() == 3);
    std::vector<ItemIntensidade> ai;
    sorteio.calcular(pa, nullptr, ai);
    VERIFICAR(ai.size() == 3);
    for (const ItemIntensidade& i : ai)
        VERIFICAR(i.uid >= 1 && i.uid <= 20 && perto(i.valor, 1.0f));

    bool semRep = true;
    bool mudou = false;
    std::vector<int> anterior = sorteio.sorteioAtual();
    for (int passo = 1; passo <= 60; ++passo) {
        sorteio.atualizar(pa, candidatos, em(static_cast<double>(passo)));
        const std::vector<int>& agora = sorteio.sorteioAtual();
        for (int u : agora)
            if (std::find(anterior.begin(), anterior.end(), u) != anterior.end())
                semRep = false;
        mudou = mudou || agora != anterior;
        VERIFICAR(agora.size() == 3);
        anterior = agora;
    }
    VERIFICAR(semRep && mudou);

    SorteioSlices a;
    SorteioSlices b;
    pa.semente = 77;
    a.atualizar(pa, candidatos, em(5.0));
    b.atualizar(pa, candidatos, em(5.0));
    VERIFICAR(a.sorteioAtual() == b.sorteioAtual());
    pa.semente = 78;
    b.atualizar(pa, candidatos, em(5.0));
    VERIFICAR(a.sorteioAtual() != b.sorteioAtual());

    SorteioSlices e;
    pa.semente = 3;
    pa.ecos = 2;
    pa.sustentacao = 0.0f;
    pa.liberacao = 1.0f;
    e.atualizar(pa, candidatos, em(10.5));
    e.calcular(pa, nullptr, ai);
    VERIFICAR(ai.size() >= 3 && ai.size() <= 9);
    float maior = 0.0f;
    for (const ItemIntensidade& i : ai)
        maior = std::max(maior, i.valor);
    VERIFICAR(maior <= 1.0f && maior > 0.0f);
    e.atualizar(pa, candidatos, em(1000.0));
    VERIFICAR(e.contador() == 1000);
    e.calcular(pa, nullptr, ai);
    VERIFICAR(!ai.empty());

    SorteioSlices pequeno;
    std::vector<int> poucos = {5, 6};
    pa.maximo = 10;
    pa.ecos = 0;
    pequeno.atualizar(pa, poucos, em(0.0));
    VERIFICAR(pequeno.sorteioAtual().size() == 1);
    pequeno.atualizar(pa, {}, em(1.0));
    VERIFICAR(pequeno.sorteioAtual().empty());
}

}
