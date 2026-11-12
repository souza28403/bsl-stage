#include "seq/SeqView.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace bsl::seq {

namespace {

constexpr float kEspacoBorda = 1.0f;

float finito(float v, float padrao)
{
    return std::isfinite(v) ? v : padrao;
}

}

RetF normalizarRet(float ax, float ay, float bx, float by)
{
    return {std::min(ax, bx), std::min(ay, by), std::max(ax, bx), std::max(ay, by)};
}

bool retContem(const RetF& r, float x, float y)
{
    return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1;
}

bool retInterseta(const RetF& a, const RetF& b)
{
    return a.x0 <= b.x1 && a.x1 >= b.x0 && a.y0 <= b.y1 && a.y1 >= b.y0;
}

float retArea(const RetF& r)
{
    return std::max(0.0f, r.x1 - r.x0) * std::max(0.0f, r.y1 - r.y0);
}

PontoF paraTela(const VistaCanvas& vista, PontoF origem, float x, float y)
{
    return {origem.x + vista.panX + x * vista.zoom, origem.y + vista.panY + y * vista.zoom};
}

PontoF paraConteudo(const VistaCanvas& vista, PontoF origem, float tx, float ty)
{
    const float z = std::max(vista.zoom, kZoomMinimo);
    return {(tx - origem.x - vista.panX) / z, (ty - origem.y - vista.panY) / z};
}

VistaCanvas ajustarVista(float larguraConteudo, float alturaConteudo, float larguraVista, float alturaVista, float margem)
{
    VistaCanvas v;
    const float cw = std::max(larguraConteudo, 1.0f);
    const float ch = std::max(alturaConteudo, 1.0f);
    const float aw = std::max(larguraVista - margem * 2.0f, 1.0f);
    const float ah = std::max(alturaVista - margem * 2.0f, 1.0f);
    v.zoom = std::clamp(std::min(aw / cw, ah / ch), kZoomMinimo, kZoomMaximo);
    v.panX = (larguraVista - cw * v.zoom) * 0.5f;
    v.panY = (alturaVista - ch * v.zoom) * 0.5f;
    return v;
}

VistaCanvas zoomPorFator(const VistaCanvas& vista, PontoF origem, float tx, float ty, float fator)
{
    if (!std::isfinite(fator) || fator <= 0.0f)
        return vista;
    const PontoF ancora = paraConteudo(vista, origem, tx, ty);
    VistaCanvas nova = vista;
    nova.zoom = std::clamp(vista.zoom * fator, kZoomMinimo, kZoomMaximo);
    nova.panX = tx - origem.x - ancora.x * nova.zoom;
    nova.panY = ty - origem.y - ancora.y * nova.zoom;
    return nova;
}

VistaCanvas zoomNoPonto(const VistaCanvas& vista, PontoF origem, float tx, float ty, float passosRoda)
{
    return zoomPorFator(vista, origem, tx, ty, std::pow(1.12f, std::clamp(finito(passosRoda, 0.0f), -200.0f, 200.0f)));
}

int acharSliceEm(const std::vector<SliceCanvas>& slices, float x, float y)
{
    int melhor = -1;
    float areaMelhor = 0.0f;
    for (size_t i = 0; i < slices.size(); ++i) {
        if (!retContem(slices[i].r, x, y))
            continue;
        const float area = retArea(slices[i].r);
        if (melhor < 0 || area <= areaMelhor) {
            melhor = static_cast<int>(i);
            areaMelhor = area;
        }
    }
    return melhor;
}

std::vector<int> slicesNoRetangulo(const std::vector<SliceCanvas>& slices, const RetF& area)
{
    std::vector<int> uids;
    for (const SliceCanvas& s : slices)
        if (s.uid > 0 && retInterseta(s.r, area))
            uids.push_back(s.uid);
    return uids;
}

std::vector<int> GestoPincel::tocar(const std::vector<SliceCanvas>& slices, float x, float y)
{
    std::vector<int> novos;
    const int i = acharSliceEm(slices, x, y);
    if (i < 0)
        return novos;
    const int uid = slices[static_cast<size_t>(i)].uid;
    if (uid > 0 && tocados_.insert(uid).second)
        novos.push_back(uid);
    return novos;
}

std::vector<int> GestoPincel::tocarSegmento(const std::vector<SliceCanvas>& slices, float ax, float ay, float bx, float by,
                                            float passo)
{
    std::vector<int> novos;
    const float comprimento = std::hypot(bx - ax, by - ay);
    const float p = std::max(passo, 0.25f);
    const int n = std::clamp(static_cast<int>(std::ceil(comprimento / p)), 1, 4096);
    for (int i = 0; i <= n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        for (int uid : tocar(slices, ax + (bx - ax) * t, ay + (by - ay) * t))
            novos.push_back(uid);
    }
    return novos;
}

ModoEdicao modoDosModificadores(bool shift, bool ctrl)
{
    if (ctrl)
        return ModoEdicao::Desligar;
    if (shift)
        return ModoEdicao::Ligar;
    return ModoEdicao::Inverter;
}

AlvoEdicao alvoDaEdicao(bool alt, int idVisao)
{
    if (!alt)
        return AlvoEdicao::Passo;
    return idVisao == kVisaoTudo ? AlvoEdicao::Nenhum : AlvoEdicao::Visao;
}

ArrastoPasso resolverArrasto(int origem, int destino, int total, bool ctrl)
{
    ArrastoPasso a;
    if (origem < 0 || destino < 0 || origem >= total || destino >= total || origem == destino)
        return a;
    a.origem = origem;
    a.destino = destino;
    if (ctrl && destino > origem) {
        a.tipo = TipoArrasto::CopiarACadaK;
        a.k = destino - origem;
    } else {
        a.tipo = TipoArrasto::Copiar;
    }
    return a;
}

int indiceDaCelula(float x, float inicioX, float larguraCelula, int total)
{
    if (larguraCelula <= 0.0f || total <= 0 || x < inicioX)
        return -1;
    const int i = static_cast<int>(std::floor((x - inicioX) / larguraCelula));
    return i >= 0 && i < total ? i : -1;
}

void faixaVisivel(float rolagemX, float larguraVista, float larguraCelula, int total, int& primeira, int& ultima)
{
    primeira = 0;
    ultima = -1;
    if (larguraCelula <= 0.0f || total <= 0)
        return;
    primeira = std::clamp(static_cast<int>(std::floor(rolagemX / larguraCelula)), 0, total - 1);
    ultima = std::clamp(static_cast<int>(std::floor((rolagemX + larguraVista) / larguraCelula)), primeira, total - 1);
}

int moverSelecao(int atual, int delta, int total)
{
    if (total <= 0)
        return 0;
    return std::clamp(atual + delta, 0, total - 1);
}

int acharBorda(const std::vector<float>& bordas, float posicao, float tolerancia)
{
    int melhor = -1;
    float distancia = tolerancia;
    for (size_t i = 0; i < bordas.size(); ++i) {
        const float d = std::fabs(bordas[i] - posicao);
        if (d <= distancia) {
            distancia = d;
            melhor = static_cast<int>(i);
        }
    }
    return melhor;
}

int inserirBorda(std::vector<float>& bordas, float posicao)
{
    if (!std::isfinite(posicao))
        return -1;
    for (float b : bordas)
        if (std::fabs(b - posicao) < kEspacoBorda)
            return -1;
    const auto it = std::lower_bound(bordas.begin(), bordas.end(), posicao);
    const auto onde = bordas.insert(it, posicao);
    return static_cast<int>(onde - bordas.begin());
}

bool removerBorda(std::vector<float>& bordas, int indice)
{
    if (indice < 0 || indice >= static_cast<int>(bordas.size()))
        return false;
    bordas.erase(bordas.begin() + indice);
    return true;
}

int moverBorda(std::vector<float>& bordas, int indice, float posicao)
{
    if (indice < 0 || indice >= static_cast<int>(bordas.size()) || !std::isfinite(posicao))
        return indice;
    const size_t i = static_cast<size_t>(indice);
    float minimo = -1e9f;
    float maximo = 1e9f;
    if (i > 0)
        minimo = bordas[i - 1] + kEspacoBorda;
    if (i + 1 < bordas.size())
        maximo = bordas[i + 1] - kEspacoBorda;
    bordas[i] = maximo < minimo ? minimo : std::clamp(posicao, minimo, maximo);
    return indice;
}

PontoF graficoParaTela(const AreaGrafico& area, float x, float y)
{
    return {area.x0 + x * (area.x1 - area.x0), area.y1 - y * (area.y1 - area.y0)};
}

PontoF telaParaGrafico(const AreaGrafico& area, float tx, float ty)
{
    const float w = area.x1 - area.x0;
    const float h = area.y1 - area.y0;
    return {w != 0.0f ? (tx - area.x0) / w : 0.0f, h != 0.0f ? (area.y1 - ty) / h : 0.0f};
}

int indiceDoSegmento(const Envelope& envelope, float x)
{
    const int n = static_cast<int>(envelope.ancoras.size());
    if (n < 2)
        return -1;
    int i = 0;
    while (i + 2 < n && x >= envelope.ancoras[static_cast<size_t>(i) + 1].x)
        ++i;
    return i;
}

AlvoCurva acharAlvoCurva(const Envelope& envelope, const AreaGrafico& area, float tx, float ty, float raio,
                         const std::vector<int>& comAlcas)
{
    const int n = static_cast<int>(envelope.ancoras.size());
    auto perto = [&](PontoF p) { return std::hypot(p.x - tx, p.y - ty) <= raio; };

    for (int i : comAlcas) {
        if (i < 0 || i >= n)
            continue;
        const Ancora& a = envelope.ancoras[static_cast<size_t>(i)];
        if (i + 1 < n && !a.reto && perto(graficoParaTela(area, a.x + a.saidaX, a.y + a.saidaY)))
            return {TipoAlvo::AlcaSaida, i};
        if (i > 0 && !envelope.ancoras[static_cast<size_t>(i) - 1].reto
            && perto(graficoParaTela(area, a.x + a.entradaX, a.y + a.entradaY)))
            return {TipoAlvo::AlcaEntrada, i};
    }

    int melhor = -1;
    float distancia = raio;
    for (int i = 0; i < n; ++i) {
        const Ancora& a = envelope.ancoras[static_cast<size_t>(i)];
        const PontoF p = graficoParaTela(area, a.x, a.y);
        const float d = std::hypot(p.x - tx, p.y - ty);
        if (d <= distancia) {
            distancia = d;
            melhor = i;
        }
    }
    if (melhor >= 0)
        return {TipoAlvo::Ancora, melhor};

    const PontoF g = telaParaGrafico(area, tx, ty);
    if (g.x >= 0.0f && g.x <= 1.0f) {
        const PontoF sobre = graficoParaTela(area, g.x, envelope.avaliar(g.x));
        if (std::fabs(sobre.y - ty) <= raio * 0.8f)
            return {TipoAlvo::Segmento, indiceDoSegmento(envelope, g.x)};
    }
    return {};
}

float imaNaGrade(float valor, int divisoes, float limiar)
{
    if (divisoes < 1 || !std::isfinite(valor))
        return valor;
    const float d = static_cast<float>(divisoes);
    const float alvo = std::round(valor * d) / d;
    return std::fabs(alvo - valor) <= limiar ? alvo : valor;
}

void travarEixo(float xInicial, float yInicial, float& x, float& y)
{
    if (std::fabs(x - xInicial) >= std::fabs(y - yInicial))
        y = yInicial;
    else
        x = xInicial;
}

std::vector<int> ancorasNoRetangulo(const Envelope& envelope, const RetF& area)
{
    std::vector<int> dentro;
    for (size_t i = 0; i < envelope.ancoras.size(); ++i)
        if (retContem(area, envelope.ancoras[i].x, envelope.ancoras[i].y))
            dentro.push_back(static_cast<int>(i));
    return dentro;
}

float limitarXAncora(const Envelope& envelope, int indice, float x)
{
    const int n = static_cast<int>(envelope.ancoras.size());
    if (indice < 0 || indice >= n)
        return x;
    const size_t i = static_cast<size_t>(indice);
    if (indice == 0 || indice == n - 1)
        return envelope.ancoras[i].x;
    const float minimo = envelope.ancoras[i - 1].x + kEspacoMinimo * 2.0f;
    const float maximo = envelope.ancoras[i + 1].x - kEspacoMinimo * 2.0f;
    if (maximo < minimo)
        return envelope.ancoras[i].x;
    return std::clamp(finito(x, envelope.ancoras[i].x), minimo, maximo);
}

void moverAncorasPor(Envelope& envelope, const std::vector<int>& indices, const std::vector<PontoF>& inicio, float dx, float dy)
{
    std::vector<size_t> ordem;
    for (size_t k = 0; k < indices.size() && k < inicio.size(); ++k)
        ordem.push_back(k);
    std::sort(ordem.begin(), ordem.end(), [&](size_t a, size_t b) { return indices[a] < indices[b]; });
    if (dx > 0.0f)
        std::reverse(ordem.begin(), ordem.end());
    for (size_t k : ordem) {
        const int i = indices[k];
        if (i < 0 || i >= static_cast<int>(envelope.ancoras.size()))
            continue;
        const float x = limitarXAncora(envelope, i, inicio[k].x + dx);
        const float y = std::clamp(finito(inicio[k].y + dy, inicio[k].y), 0.0f, 1.0f);
        moverAncora(envelope, i, x, y);
    }
}

void HistoricoCurva::registrar(const Envelope& antes)
{
    refeitos_.clear();
    desfeitos_.push_back(antes);
    if (desfeitos_.size() > limite_)
        desfeitos_.erase(desfeitos_.begin());
}

bool HistoricoCurva::confirmar(const Envelope& antes, const Envelope& depois)
{
    if (antes == depois)
        return false;
    registrar(antes);
    return true;
}

bool HistoricoCurva::desfazer(Envelope& atual)
{
    if (desfeitos_.empty())
        return false;
    refeitos_.push_back(std::move(atual));
    atual = std::move(desfeitos_.back());
    desfeitos_.pop_back();
    return true;
}

bool HistoricoCurva::refazer(Envelope& atual)
{
    if (refeitos_.empty())
        return false;
    desfeitos_.push_back(std::move(atual));
    atual = std::move(refeitos_.back());
    refeitos_.pop_back();
    return true;
}

void HistoricoCurva::limpar()
{
    desfeitos_.clear();
    refeitos_.clear();
}

}
