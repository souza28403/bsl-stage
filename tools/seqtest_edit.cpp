#include "seqtest.h"

#include "seq/SeqEdit.h"
#include "seq/SeqHistory.h"

namespace seqtest {

using namespace bsl::seq;

void testarEdicao()
{
    secao("edicao");

    Passo p;
    aplicarEdicao(p, {3, 1, 2}, ModoEdicao::Inverter);
    VERIFICAR((p == Passo{1, 2, 3}));
    aplicarEdicao(p, {2, 5}, ModoEdicao::Inverter);
    VERIFICAR((p == Passo{1, 3, 5}));
    aplicarEdicao(p, {3, 3, 7}, ModoEdicao::Ligar);
    VERIFICAR((p == Passo{1, 3, 5, 7}));
    aplicarEdicao(p, {1, 9}, ModoEdicao::Desligar);
    VERIFICAR((p == Passo{3, 5, 7}));
    aplicarEdicao(p, {0, -4}, ModoEdicao::Ligar);
    VERIFICAR((p == Passo{3, 5, 7}));
    VERIFICAR(passoContem(p, 5) && !passoContem(p, 4));

    SequenceSet c;
    const int id = criarSequencia(c, "A", 6);
    Sequencia& s = *acharSequencia(c, id);
    aplicarEdicao(s.passos[0], {1, 2}, ModoEdicao::Ligar);
    aplicarEdicao(s.passos[1], {3}, ModoEdicao::Ligar);
    VERIFICAR(copiarPasso(s, 0, 4));
    VERIFICAR((s.passos[4] == Passo{1, 2}));
    VERIFICAR(!copiarPasso(s, 0, 6) && !copiarPasso(s, -1, 0));
    VERIFICAR(copiarPassoACadaK(s, 0, 2) == 2);
    VERIFICAR((s.passos[2] == Passo{1, 2}) && (s.passos[4] == Passo{1, 2}) && s.passos[3].empty());
    VERIFICAR(copiarPassoACadaK(s, 0, 0) == 0);
    VERIFICAR(trocarPassos(s, 0, 1));
    VERIFICAR((s.passos[0] == Passo{3}) && (s.passos[1] == Passo{1, 2}));
    VERIFICAR(inserirPasso(s, 0) && s.passos.size() == 7 && s.passos[0].empty());
    VERIFICAR(removerPasso(s, 0) && s.passos.size() == 6);
    VERIFICAR(limparPasso(s, 0) && s.passos[0].empty());
    VERIFICAR(repetirParaFrente(s, 1) == 4);
    VERIFICAR((s.passos[5] == s.passos[1]));
    limparSequencia(s);
    for (const Passo& q : s.passos)
        VERIFICAR(q.empty());

    Sequencia curta;
    curta.passos.assign(1, Passo{});
    VERIFICAR(!removerPasso(curta, 0) && !vaiEVolta(curta));

    Sequencia vv;
    vv.passos = {Passo{1}, Passo{2}, Passo{3}, Passo{4}};
    VERIFICAR(vaiEVolta(vv) && vv.passos.size() == 6);
    VERIFICAR((vv.passos[4] == Passo{3}) && (vv.passos[5] == Passo{2}));

    Sequencia cheia;
    cheia.passos.assign(static_cast<size_t>(kMaxPassos), Passo{});
    VERIFICAR(!inserirPasso(cheia, 0) && !vaiEVolta(cheia));

    SequenceSet d;
    const int a = criarSequencia(d, "A", 4);
    const int b = criarSequencia(d, "B", 4);
    const int cc = criarSequencia(d, "C", 4);
    VERIFICAR(!passosDiferentes(d));
    VERIFICAR(moverSequencia(d, 0, 2));
    VERIFICAR(d.sequencias[0].id == b && d.sequencias[2].id == a);
    VERIFICAR(!moverSequencia(d, 0, 3));
    definirTamanho(*acharSequencia(d, cc), 7);
    VERIFICAR(passosDiferentes(d));
    VERIFICAR(renomearSequencia(d, a, "Nova") && acharSequencia(d, a)->nome == "Nova");
    VERIFICAR(!renomearSequencia(d, a, "") && !renomearSequencia(d, 999, "x"));
    const int v1 = criarVisao(d, "V1");
    const int v2 = criarVisao(d, "V2");
    VERIFICAR(moverVisao(d, 0, 1) && d.visoes[0].id == v2 && d.visoes[1].id == v1);
    VERIFICAR(renomearVisao(d, v1, "X"));
    aplicarEdicaoVisao(*acharVisao(d, v1), {4, 2, 4}, ModoEdicao::Ligar);
    VERIFICAR((acharVisao(d, v1)->ligados == std::vector<int>{2, 4}));

    SequenceSet estado;
    criarSequencia(estado, "H", 2);
    SeqHistory h;
    VERIFICAR(!h.podeDesfazer() && !h.podeRefazer());
    SequenceSet e0 = estado;
    aplicarEdicao(estado.sequencias[0].passos[0], {1}, ModoEdicao::Ligar);
    VERIFICAR(h.confirmar(e0, estado));
    VERIFICAR(!h.confirmar(estado, estado));
    SequenceSet e1 = estado;
    aplicarEdicao(estado.sequencias[0].passos[1], {2}, ModoEdicao::Ligar);
    h.registrar(e1);
    SequenceSet e2 = estado;
    VERIFICAR(h.podeDesfazer() && !h.podeRefazer() && h.bytes() > 0);
    VERIFICAR(h.desfazer(estado) && estado == e1);
    VERIFICAR(h.desfazer(estado) && estado == e0);
    VERIFICAR(!h.desfazer(estado));
    VERIFICAR(h.refazer(estado) && estado == e1);
    VERIFICAR(h.refazer(estado) && estado == e2);
    VERIFICAR(!h.refazer(estado));
    VERIFICAR(h.desfazer(estado) && estado == e1);
    h.registrar(estado);
    VERIFICAR(!h.podeRefazer());
    h.limpar();
    VERIFICAR(!h.podeDesfazer() && h.bytes() == 0);

    SequenceSet grande;
    const int gid = criarSequencia(grande, "G", kMaxPassos);
    for (Passo& q : acharSequencia(grande, gid)->passos)
        for (int u = 1; u <= 200; ++u)
            q.push_back(u);
    const size_t unitario = estimarBytes(grande);
    VERIFICAR(unitario > 800000);
    SeqHistory limitado;
    for (int i = 0; i < 100; ++i)
        limitado.registrar(grande);
    VERIFICAR(limitado.bytes() <= kTetoHistoricoBytes);
    VERIFICAR(limitado.quantidade() >= 1 && limitado.quantidade() < 100);
    SeqHistory minimo(1);
    minimo.registrar(grande);
    minimo.registrar(grande);
    VERIFICAR(minimo.quantidade() == 1);
}

}
