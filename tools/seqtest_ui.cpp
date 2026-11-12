#include "seqtest.h"

#include "seq/SeqView.h"

#include <algorithm>
#include <cmath>

namespace seqtest {

using namespace bsl::seq;

namespace {

bool perto(float a, float b, float tol = 1e-3f)
{
    return std::fabs(a - b) <= tol;
}

std::vector<SliceCanvas> grade3()
{
    return {{1, {0, 0, 100, 100}}, {2, {100, 0, 200, 100}}, {3, {0, 100, 200, 200}}, {4, {20, 20, 60, 60}}};
}

}

void testarInterface()
{
    secao("interface: vista do canvas");
    {
        const PontoF origem{50.0f, 80.0f};
        VistaCanvas v;
        v.zoom = 2.0f;
        v.panX = 10.0f;
        v.panY = -20.0f;
        const PontoF t = paraTela(v, origem, 30.0f, 40.0f);
        VERIFICAR(perto(t.x, 120.0f) && perto(t.y, 140.0f));
        const PontoF c = paraConteudo(v, origem, t.x, t.y);
        VERIFICAR(perto(c.x, 30.0f) && perto(c.y, 40.0f));

        const VistaCanvas z = zoomNoPonto(v, origem, 200.0f, 150.0f, 3.0f);
        const PontoF antes = paraConteudo(v, origem, 200.0f, 150.0f);
        const PontoF depois = paraConteudo(z, origem, 200.0f, 150.0f);
        VERIFICAR(z.zoom > v.zoom && perto(antes.x, depois.x, 0.01f) && perto(antes.y, depois.y, 0.01f));
        VERIFICAR(zoomNoPonto(v, origem, 0.0f, 0.0f, 1000.0f).zoom == kZoomMaximo);
        VERIFICAR(zoomNoPonto(v, origem, 0.0f, 0.0f, -1000.0f).zoom == kZoomMinimo);
        VERIFICAR(zoomPorFator(v, origem, 0.0f, 0.0f, -1.0f).zoom == v.zoom);
        VERIFICAR(zoomNoPonto(v, origem, 0.0f, 0.0f, NAN).zoom == v.zoom);

        const VistaCanvas aj = ajustarVista(1920.0f, 1080.0f, 960.0f, 600.0f, 20.0f);
        VERIFICAR(perto(aj.zoom, 920.0f / 1920.0f, 1e-4f));
        VERIFICAR(perto(aj.panX, (960.0f - 1920.0f * aj.zoom) * 0.5f) && aj.panY > 0.0f);
        VERIFICAR(ajustarVista(0.0f, 0.0f, 0.0f, 0.0f, 50.0f).zoom >= kZoomMinimo);
    }

    secao("interface: acerto de clique nos slices");
    {
        const std::vector<SliceCanvas> s = grade3();
        VERIFICAR(acharSliceEm(s, 10.0f, 10.0f) == 0);
        VERIFICAR(acharSliceEm(s, 150.0f, 50.0f) == 1);
        VERIFICAR(acharSliceEm(s, 100.0f, 150.0f) == 2);
        VERIFICAR(acharSliceEm(s, 300.0f, 10.0f) == -1);
        VERIFICAR(acharSliceEm({}, 1.0f, 1.0f) == -1);
        VERIFICAR(acharSliceEm(s, 40.0f, 40.0f) == 3);
        VERIFICAR(acharSliceEm(s, 80.0f, 10.0f) == 0);
    }

    secao("interface: retangulo de selecao");
    {
        const std::vector<SliceCanvas> s = grade3();
        const RetF r = normalizarRet(150.0f, 150.0f, 90.0f, 40.0f);
        VERIFICAR(perto(r.x0, 90.0f) && perto(r.y0, 40.0f) && perto(r.x1, 150.0f) && perto(r.y1, 150.0f));
        const std::vector<int> uids = slicesNoRetangulo(s, r);
        VERIFICAR((uids == std::vector<int>{1, 2, 3}));
        VERIFICAR((slicesNoRetangulo(s, normalizarRet(300.0f, 300.0f, 400.0f, 400.0f)).empty()));
        VERIFICAR((slicesNoRetangulo(s, normalizarRet(25.0f, 25.0f, 30.0f, 30.0f)) == std::vector<int>{1, 4}));
        VERIFICAR(retContem(r, 100.0f, 100.0f) && !retContem(r, 10.0f, 100.0f));
        VERIFICAR(retInterseta(r, {140.0f, 140.0f, 500.0f, 500.0f}) && !retInterseta(r, {151.0f, 0.0f, 500.0f, 500.0f}));
        VERIFICAR(perto(retArea({0, 0, 10, 20}), 200.0f) && retArea({10, 10, 0, 0}) == 0.0f);
    }

    secao("interface: pincel inverte cada slice uma vez por gesto");
    {
        const std::vector<SliceCanvas> s = grade3();
        GestoPincel p;
        VERIFICAR((p.tocar(s, 10.0f, 10.0f) == std::vector<int>{1}));
        VERIFICAR(p.tocar(s, 20.0f, 10.0f).empty());
        VERIFICAR(p.tocar(s, 500.0f, 500.0f).empty());
        VERIFICAR((p.tocarSegmento(s, 10.0f, 10.0f, 190.0f, 10.0f, 5.0f) == std::vector<int>{2}));
        VERIFICAR((p.tocarSegmento(s, 150.0f, 50.0f, 150.0f, 190.0f, 5.0f) == std::vector<int>{3}));
        VERIFICAR(p.tocados().size() == 3);
        p.reiniciar();
        VERIFICAR(p.tocados().empty() && (p.tocar(s, 10.0f, 10.0f) == std::vector<int>{1}));
        Passo passo;
        for (int rodada = 0; rodada < 3; ++rodada)
            for (int uid : p.tocar(s, 150.0f, 50.0f))
                aplicarEdicao(passo, {uid}, ModoEdicao::Inverter);
        VERIFICAR((passo == Passo{2}));
        VERIFICAR(p.tocarSegmento(s, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f).empty());
    }

    secao("interface: modificadores do canvas");
    {
        VERIFICAR(modoDosModificadores(false, false) == ModoEdicao::Inverter);
        VERIFICAR(modoDosModificadores(true, false) == ModoEdicao::Ligar);
        VERIFICAR(modoDosModificadores(false, true) == ModoEdicao::Desligar);
        VERIFICAR(modoDosModificadores(true, true) == ModoEdicao::Desligar);
        VERIFICAR(alvoDaEdicao(false, kVisaoTudo) == AlvoEdicao::Passo);
        VERIFICAR(alvoDaEdicao(false, 7) == AlvoEdicao::Passo);
        VERIFICAR(alvoDaEdicao(true, 7) == AlvoEdicao::Visao);
        VERIFICAR(alvoDaEdicao(true, kVisaoTudo) == AlvoEdicao::Nenhum);
    }

    secao("interface: barra de passos");
    {
        VERIFICAR(indiceDaCelula(105.0f, 100.0f, 30.0f, 10) == 0);
        VERIFICAR(indiceDaCelula(130.0f, 100.0f, 30.0f, 10) == 1);
        VERIFICAR(indiceDaCelula(399.0f, 100.0f, 30.0f, 10) == 9);
        VERIFICAR(indiceDaCelula(400.0f, 100.0f, 30.0f, 10) == -1);
        VERIFICAR(indiceDaCelula(99.0f, 100.0f, 30.0f, 10) == -1);
        VERIFICAR(indiceDaCelula(120.0f, 100.0f, 0.0f, 10) == -1 && indiceDaCelula(120.0f, 100.0f, 30.0f, 0) == -1);
        int a = 0, b = 0;
        faixaVisivel(0.0f, 100.0f, 30.0f, 1024, a, b);
        VERIFICAR(a == 0 && b == 3);
        faixaVisivel(300.0f, 100.0f, 30.0f, 1024, a, b);
        VERIFICAR(a == 10 && b == 13);
        faixaVisivel(1e9f, 100.0f, 30.0f, 1024, a, b);
        VERIFICAR(a == 1023 && b == 1023);
        faixaVisivel(0.0f, 100.0f, 30.0f, 0, a, b);
        VERIFICAR(b < a);
        VERIFICAR(moverSelecao(0, -1, 8) == 0 && moverSelecao(7, 1, 8) == 7 && moverSelecao(3, 2, 8) == 5 && moverSelecao(3, 1, 0) == 0);

        ArrastoPasso r = resolverArrasto(2, 5, 8, false);
        VERIFICAR(r.tipo == TipoArrasto::Copiar && r.origem == 2 && r.destino == 5);
        r = resolverArrasto(2, 5, 8, true);
        VERIFICAR(r.tipo == TipoArrasto::CopiarACadaK && r.k == 3);
        r = resolverArrasto(5, 2, 8, true);
        VERIFICAR(r.tipo == TipoArrasto::Copiar);
        VERIFICAR(resolverArrasto(2, 2, 8, false).tipo == TipoArrasto::Nada);
        VERIFICAR(resolverArrasto(2, 9, 8, false).tipo == TipoArrasto::Nada);
        VERIFICAR(resolverArrasto(-1, 3, 8, true).tipo == TipoArrasto::Nada);
    }

    secao("interface: bordas da grade");
    {
        std::vector<float> b = {100.0f, 300.0f, 500.0f};
        VERIFICAR(acharBorda(b, 305.0f, 8.0f) == 1 && acharBorda(b, 350.0f, 8.0f) == -1 && acharBorda({}, 1.0f, 8.0f) == -1);
        VERIFICAR(inserirBorda(b, 200.0f) == 1 && b.size() == 4 && perto(b[1], 200.0f));
        VERIFICAR(inserirBorda(b, 200.4f) == -1 && inserirBorda(b, NAN) == -1);
        VERIFICAR(inserirBorda(b, 50.0f) == 0 && inserirBorda(b, 900.0f) == 5);
        VERIFICAR(std::is_sorted(b.begin(), b.end()));
        VERIFICAR(moverBorda(b, 2, 250.0f) == 2 && perto(b[2], 250.0f));
        moverBorda(b, 2, 1000.0f);
        VERIFICAR(perto(b[2], b[3] - 1.0f));
        moverBorda(b, 2, -1000.0f);
        VERIFICAR(perto(b[2], b[1] + 1.0f));
        VERIFICAR(std::is_sorted(b.begin(), b.end()));
        VERIFICAR(removerBorda(b, 0) && b.size() == 5 && !removerBorda(b, 9) && !removerBorda(b, -1));
        VERIFICAR(moverBorda(b, 9, 1.0f) == 9);
    }

    secao("interface: editor de curva");
    {
        const AreaGrafico area{100.0f, 50.0f, 500.0f, 450.0f};
        const PontoF t = graficoParaTela(area, 0.25f, 0.75f);
        VERIFICAR(perto(t.x, 200.0f) && perto(t.y, 150.0f));
        const PontoF g = telaParaGrafico(area, t.x, t.y);
        VERIFICAR(perto(g.x, 0.25f) && perto(g.y, 0.75f));
        const PontoF zero = telaParaGrafico({0, 0, 0, 0}, 5.0f, 5.0f);
        VERIFICAR(zero.x == 0.0f && zero.y == 0.0f);

        Envelope e = envelopeLinear(1, "L");
        VERIFICAR(indiceDoSegmento(e, 0.5f) == 0 && indiceDoSegmento(e, 1.0f) == 0);
        const int novo = inserirAncora(e, 0.5f);
        VERIFICAR(novo == 1 && e.ancoras.size() == 3);
        VERIFICAR(indiceDoSegmento(e, 0.25f) == 0 && indiceDoSegmento(e, 0.5f) == 1 && indiceDoSegmento(e, 0.9f) == 1);
        VERIFICAR(indiceDoSegmento(Envelope{}, 0.5f) == -1);

        const PontoF a0 = graficoParaTela(area, e.ancoras[0].x, e.ancoras[0].y);
        const PontoF a1 = graficoParaTela(area, e.ancoras[1].x, e.ancoras[1].y);
        AlvoCurva alvo = acharAlvoCurva(e, area, a0.x + 3.0f, a0.y - 2.0f, 8.0f, {});
        VERIFICAR(alvo.tipo == TipoAlvo::Ancora && alvo.indice == 0);
        alvo = acharAlvoCurva(e, area, a1.x, a1.y, 8.0f, {});
        VERIFICAR(alvo.tipo == TipoAlvo::Ancora && alvo.indice == 1);
        const PontoF meio = graficoParaTela(area, 0.25f, e.avaliar(0.25f));
        alvo = acharAlvoCurva(e, area, meio.x, meio.y + 2.0f, 8.0f, {});
        VERIFICAR(alvo.tipo == TipoAlvo::Segmento && alvo.indice == 0);
        alvo = acharAlvoCurva(e, area, meio.x, meio.y - 120.0f, 8.0f, {});
        VERIFICAR(alvo.tipo == TipoAlvo::Nada);
        alvo = acharAlvoCurva(e, area, 20.0f, 20.0f, 8.0f, {});
        VERIFICAR(alvo.tipo == TipoAlvo::Nada);

        Envelope s = envelopeCurvaS(2, "S");
        definirAlca(s, 0, true, 0.2f, 0.0f);
        const PontoF alca = graficoParaTela(area, s.ancoras[0].x + s.ancoras[0].saidaX, s.ancoras[0].y + s.ancoras[0].saidaY);
        alvo = acharAlvoCurva(s, area, alca.x, alca.y, 6.0f, {0});
        VERIFICAR(alvo.tipo == TipoAlvo::AlcaSaida && alvo.indice == 0);
        alvo = acharAlvoCurva(s, area, alca.x, alca.y, 6.0f, {});
        VERIFICAR(alvo.tipo != TipoAlvo::AlcaSaida);
        const PontoF ent = graficoParaTela(area, s.ancoras[1].x + s.ancoras[1].entradaX, s.ancoras[1].y + s.ancoras[1].entradaY);
        alvo = acharAlvoCurva(s, area, ent.x, ent.y, 6.0f, {1});
        VERIFICAR(alvo.tipo == TipoAlvo::AlcaEntrada && alvo.indice == 1);
        definirReto(s, 0, true);
        alvo = acharAlvoCurva(s, area, alca.x, alca.y, 6.0f, {0});
        VERIFICAR(alvo.tipo != TipoAlvo::AlcaSaida);

        VERIFICAR(perto(imaNaGrade(0.51f, 20, 0.02f), 0.5f) && perto(imaNaGrade(0.53f, 20, 0.01f), 0.53f));
        VERIFICAR(perto(imaNaGrade(0.99f, 4, 0.05f), 1.0f) && imaNaGrade(0.3f, 0, 0.1f) == 0.3f);
        float x = 0.8f, y = 0.5f;
        travarEixo(0.5f, 0.5f, x, y);
        VERIFICAR(perto(x, 0.8f) && perto(y, 0.5f));
        x = 0.52f;
        y = 0.9f;
        travarEixo(0.5f, 0.5f, x, y);
        VERIFICAR(perto(x, 0.5f) && perto(y, 0.9f));

        Envelope m = envelopeLinear(3, "M");
        inserirAncora(m, 0.3f);
        inserirAncora(m, 0.6f);
        const std::vector<int> dentro = ancorasNoRetangulo(m, normalizarRet(0.2f, 0.0f, 0.7f, 1.0f));
        VERIFICAR((dentro.size() == 2 && dentro[0] == 1 && dentro[1] == 2));
        VERIFICAR(ancorasNoRetangulo(m, normalizarRet(2.0f, 2.0f, 3.0f, 3.0f)).empty());
    }

    secao("interface: mover ancoras sem cruzar vizinhas");
    {
        Envelope e = envelopeLinear(1, "M");
        inserirAncora(e, 0.25f);
        inserirAncora(e, 0.5f);
        inserirAncora(e, 0.75f);
        VERIFICAR(e.ancoras.size() == 5);
        VERIFICAR(limitarXAncora(e, 0, 0.5f) == 0.0f && limitarXAncora(e, 4, 0.5f) == 1.0f);
        VERIFICAR(limitarXAncora(e, 2, 0.9f) < e.ancoras[3].x && limitarXAncora(e, 2, 0.1f) > e.ancoras[1].x);
        VERIFICAR(perto(limitarXAncora(e, 2, 0.55f), 0.55f) && limitarXAncora(e, 9, 0.3f) == 0.3f);
        const std::vector<int> idx = {1, 2, 3};
        std::vector<PontoF> ini;
        for (int i : idx)
            ini.push_back({e.ancoras[static_cast<size_t>(i)].x, e.ancoras[static_cast<size_t>(i)].y});
        moverAncorasPor(e, idx, ini, 0.5f, 0.2f);
        VERIFICAR(e.ancoras.size() == 5);
        bool ordenado = true;
        for (size_t i = 1; i < e.ancoras.size(); ++i)
            ordenado = ordenado && e.ancoras[i].x > e.ancoras[i - 1].x;
        VERIFICAR(ordenado && e.ancoras.front().x == 0.0f && e.ancoras.back().x == 1.0f);
        VERIFICAR(e.ancoras[3].x < 1.0f && perto(e.ancoras[1].y, 0.45f, 0.01f));
        moverAncorasPor(e, idx, ini, -0.9f, -0.9f);
        VERIFICAR(e.ancoras[1].x > 0.0f && e.ancoras[1].y == 0.0f && e.ancoras.size() == 5);
        moverAncorasPor(e, {7}, {{0.1f, 0.1f}}, 0.1f, 0.1f);
        VERIFICAR(e.ancoras.size() == 5);
    }

    secao("interface: desfazer proprio do editor de curva");
    {
        HistoricoCurva h(3);
        Envelope e = envelopeCurvaS(1, "S");
        const Envelope original = e;
        VERIFICAR(!h.podeDesfazer() && !h.desfazer(e) && !h.refazer(e));
        Envelope antes = e;
        inserirAncora(e, 0.5f);
        VERIFICAR(h.confirmar(antes, e) && h.podeDesfazer() && !h.podeRefazer());
        VERIFICAR(!h.confirmar(e, e));
        const Envelope com = e;
        VERIFICAR(h.desfazer(e) && e == original && h.podeRefazer());
        VERIFICAR(h.refazer(e) && e == com && !h.podeRefazer());
        antes = e;
        moverAncora(e, 1, 0.5f, 0.9f);
        h.confirmar(antes, e);
        VERIFICAR(h.desfazer(e));
        h.registrar(e);
        VERIFICAR(!h.podeRefazer());
        for (int i = 0; i < 10; ++i)
            h.registrar(e);
        VERIFICAR(h.quantidade() == 3);
        h.limpar();
        VERIFICAR(h.quantidade() == 0);
    }
}

}
