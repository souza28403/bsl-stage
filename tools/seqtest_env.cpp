#include "seqtest.h"

#include "seq/Envelope.h"
#include "seq/EnvelopeStore.h"

#include <cmath>
#include <filesystem>
#include <fstream>

namespace seqtest {

using namespace bsl::seq;

namespace {

bool proximo(float a, float b, float tol = 1e-4f)
{
    return std::fabs(a - b) <= tol;
}

}

void testarEnvelope()
{
    secao("envelope");

    Envelope s = envelopeCurvaS(1);
    VERIFICAR(s.ancoras.size() == 2 && s.tabela.size() == static_cast<size_t>(kTamanhoTabela));
    VERIFICAR(proximo(s.avaliar(0.0f), 0.0f) && proximo(s.avaliar(1.0f), 1.0f) && proximo(s.avaliar(0.5f), 0.5f));
    VERIFICAR(s.avaliar(0.1f) < 0.1f && s.avaliar(0.9f) > 0.9f);
    VERIFICAR(proximo(s.avaliar(-5.0f), 0.0f) && proximo(s.avaliar(7.0f), 1.0f));
    bool monotona = true;
    float anterior = -1.0f;
    for (int i = 0; i <= 1000; ++i) {
        const float v = s.avaliar(static_cast<float>(i) / 1000.0f);
        monotona = monotona && v >= anterior - 1e-6f;
        anterior = v;
    }
    VERIFICAR(monotona);
    VERIFICAR(proximo(s.amostrar(0.37f), s.avaliar(0.37f), 2e-3f));
    VERIFICAR(proximo(s.tabela.front(), 0.0f) && proximo(s.tabela.back(), 1.0f));

    Envelope l = envelopeLinear(2);
    VERIFICAR(proximo(l.avaliar(0.25f), 0.25f) && proximo(l.avaliar(0.8f), 0.8f));

    Envelope e = envelopeCurvaS(3);
    const float antes = e.avaliar(0.3f);
    const int novo = inserirAncora(e, 0.3f);
    VERIFICAR(novo == 1 && e.ancoras.size() == 3);
    VERIFICAR(proximo(e.ancoras[1].x, 0.3f, 1e-3f));
    VERIFICAR(proximo(e.avaliar(0.3f), antes, 2e-3f));
    VERIFICAR(proximo(e.avaliar(0.15f), s.avaliar(0.15f), 2e-3f));
    VERIFICAR(proximo(e.avaliar(0.7f), s.avaliar(0.7f), 2e-3f));
    VERIFICAR(inserirAncora(e, 0.3f) == -1);
    VERIFICAR(!removerAncora(e, 0) && !removerAncora(e, 2) && !removerAncora(e, 9));
    VERIFICAR(removerAncora(e, 1) && e.ancoras.size() == 2);

    moverAncora(e, 0, 0.5f, 0.2f);
    VERIFICAR(e.ancoras[0].x == 0.0f && proximo(e.ancoras[0].y, 0.2f));
    moverAncora(e, 1, 0.5f, 0.9f);
    VERIFICAR(e.ancoras[1].x == 1.0f && proximo(e.ancoras[1].y, 0.9f));

    Envelope m = envelopeLinear(4);
    inserirAncora(m, 0.5f);
    moverAncora(m, 1, 5.0f, -3.0f);
    VERIFICAR(m.ancoras[1].x < 1.0f && m.ancoras[1].x > 0.99f && m.ancoras[1].y == 0.0f);
    moverAncora(m, 1, -2.0f, 0.5f);
    VERIFICAR(m.ancoras[1].x > 0.0f && m.ancoras[1].x < 0.001f);

    Envelope a = envelopeCurvaS(5);
    inserirAncora(a, 0.5f);
    definirAlca(a, 1, true, 5.0f, 5.0f);
    const Ancora& mid = a.ancoras[1];
    VERIFICAR(mid.saidaX <= a.ancoras[2].x - mid.x + 1e-6f);
    VERIFICAR(mid.saidaY + mid.y <= 1.0f + 1e-6f);
    bool voltaNoTempo = false;
    float ant = -1.0f;
    for (int i = 0; i <= 500; ++i) {
        const float v = a.avaliar(static_cast<float>(i) / 500.0f);
        if (!std::isfinite(v) || v < 0.0f || v > 1.0f)
            voltaNoTempo = true;
        ant = v;
    }
    VERIFICAR(!voltaNoTempo && std::isfinite(ant));

    Envelope lig = envelopeCurvaS(6);
    inserirAncora(lig, 0.5f);
    definirAlca(lig, 1, true, 0.1f, 0.05f);
    const float razaoLigada = lig.ancoras[1].entradaY / lig.ancoras[1].entradaX;
    VERIFICAR(proximo(razaoLigada, 0.5f, 5e-2f));
    definirAlcasLigadas(lig, 1, false);
    const float entradaAntes = lig.ancoras[1].entradaY;
    definirAlca(lig, 1, true, 0.1f, -0.05f);
    VERIFICAR(proximo(lig.ancoras[1].entradaY, entradaAntes));

    Envelope r = envelopeCurvaS(7);
    definirReto(r, 0, true);
    VERIFICAR(proximo(r.avaliar(0.25f), 0.25f));
    definirReto(r, 1, true);
    VERIFICAR(!r.ancoras.back().reto);

    Envelope lixo;
    lixo.id = 8;
    lixo.ancoras = {Ancora{0.6f, 2.0f}, Ancora{-1.0f, -1.0f}, Ancora{0.6f, 0.5f}, Ancora{std::nanf(""), 0.3f}};
    normalizarEnvelope(lixo);
    VERIFICAR(lixo.ancoras.front().x == 0.0f && lixo.ancoras.back().x == 1.0f);
    bool crescente = true;
    for (size_t i = 1; i < lixo.ancoras.size(); ++i)
        crescente = crescente && lixo.ancoras[i].x > lixo.ancoras[i - 1].x;
    VERIFICAR(crescente);
    Envelope vazio;
    normalizarEnvelope(vazio);
    VERIFICAR(vazio.ancoras.size() == 2);

    Envelope muitas = envelopeLinear(9);
    int inseridas = 0;
    for (int i = 1; i < 400; ++i)
        if (inserirAncora(muitas, static_cast<float>(i) / 400.0f) >= 0)
            ++inseridas;
    VERIFICAR(static_cast<int>(muitas.ancoras.size()) == kMaxAncoras && inseridas == kMaxAncoras - 2);

    BibliotecaEnvelopes b;
    garantirBiblioteca(b);
    VERIFICAR(b.lista.size() == 1 && b.lista[0].id == 1 && b.proximoId == 2);
    const int c1 = criarEnvelope(b, "Dois");
    const int c2 = duplicarEnvelope(b, c1);
    VERIFICAR(c1 == 2 && c2 == 3 && b.lista.size() == 3);
    VERIFICAR(renomearEnvelope(b, c2, "Tres") && acharEnvelope(b, c2)->nome == "Tres");
    VERIFICAR(!renomearEnvelope(b, 99, "x") && duplicarEnvelope(b, 99) == 0);
    VERIFICAR(excluirEnvelope(b, 1) && excluirEnvelope(b, 2) && excluirEnvelope(b, 3));
    VERIFICAR(b.lista.size() == 1 && b.lista[0].ancoras.size() == 2 && b.lista[0].id == 4);
    VERIFICAR(!excluirEnvelope(b, 99));
    for (int i = 0; i < kMaxEnvelopes + 5; ++i)
        criarEnvelope(b, "x");
    VERIFICAR(static_cast<int>(b.lista.size()) == kMaxEnvelopes);
    VERIFICAR(criarEnvelope(b, "y") == 0);

    BibliotecaEnvelopes p;
    garantirBiblioteca(p);
    const int pid = criarEnvelope(p, "Rampa");
    inserirAncora(*acharEnvelope(p, pid), 0.4f);
    moverAncora(*acharEnvelope(p, pid), 1, 0.4f, 0.9f);

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path pasta = fs::temp_directory_path() / "bslstage_seqtest_env";
    fs::remove_all(pasta, ec);
    const fs::path arquivo = pasta / "envelopes.json";
    VERIFICAR(salvarBiblioteca(p, arquivo));
    VERIFICAR(!fs::exists(fs::path(arquivo).concat(".tmp"), ec));
    const BibliotecaEnvelopes lida = carregarBiblioteca(arquivo);
    VERIFICAR(lida.lista.size() == p.lista.size() && lida.proximoId == p.proximoId);
    bool iguais = lida.lista.size() == p.lista.size();
    for (size_t i = 0; iguais && i < p.lista.size(); ++i)
        iguais = lida.lista[i] == p.lista[i];
    VERIFICAR(iguais);
    VERIFICAR(proximo(acharEnvelope(lida, pid)->avaliar(0.6f), acharEnvelope(p, pid)->avaliar(0.6f), 1e-5f));
    VERIFICAR(salvarBiblioteca(lida, arquivo));
    VERIFICAR(bibliotecaParaJson(carregarBiblioteca(arquivo)) == bibliotecaParaJson(lida));

    const BibliotecaEnvelopes ausente = carregarBiblioteca(pasta / "nada.json");
    VERIFICAR(ausente.lista.size() == 1);
    {
        std::ofstream quebrado(pasta / "quebrado.json", std::ios::binary);
        quebrado << "{ nao e json";
    }
    VERIFICAR(carregarBiblioteca(pasta / "quebrado.json").lista.size() == 1);
    VERIFICAR(envelopeDeJson(nlohmann::json("texto")).ancoras.size() == 2);
    fs::remove_all(pasta, ec);

    BibliotecaEnvelopes destino;
    garantirBiblioteca(destino);
    Envelope igual = destino.lista[0];
    Envelope colide = envelopeLinear(1, "Outra");
    Envelope novoId = envelopeLinear(40, "Nova");
    const std::map<int, int> mapa = mesclarNaBiblioteca(destino, {igual, colide, novoId});
    VERIFICAR(mapa.at(1) != 1 && mapa.at(40) == 40);
    VERIFICAR(destino.lista.size() == 3);
    const std::map<int, int> mapa2 = mesclarNaBiblioteca(destino, {novoId});
    VERIFICAR(destino.lista.size() == 3 && mapa2.at(40) == 40);
    VERIFICAR(acharEnvelope(destino, 40) != nullptr && destino.proximoId > 40);
    bool idsUnicos = true;
    for (size_t i = 0; i < destino.lista.size(); ++i)
        for (size_t k = i + 1; k < destino.lista.size(); ++k)
            idsUnicos = idsUnicos && destino.lista[i].id != destino.lista[k].id;
    VERIFICAR(idsUnicos);
}

}
