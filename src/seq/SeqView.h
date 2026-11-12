#pragma once

#include "seq/Envelope.h"
#include "seq/SeqEdit.h"

#include <cstddef>
#include <set>
#include <vector>

namespace bsl::seq {

struct PontoF
{
    float x = 0.0f;
    float y = 0.0f;
};

struct RetF
{
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

inline constexpr float kZoomMinimo = 0.02f;
inline constexpr float kZoomMaximo = 64.0f;

RetF normalizarRet(float ax, float ay, float bx, float by);
bool retContem(const RetF& r, float x, float y);
bool retInterseta(const RetF& a, const RetF& b);
float retArea(const RetF& r);

struct VistaCanvas
{
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
};

PontoF paraTela(const VistaCanvas& vista, PontoF origem, float x, float y);
PontoF paraConteudo(const VistaCanvas& vista, PontoF origem, float tx, float ty);
VistaCanvas ajustarVista(float larguraConteudo, float alturaConteudo, float larguraVista, float alturaVista, float margem);
VistaCanvas zoomNoPonto(const VistaCanvas& vista, PontoF origem, float tx, float ty, float passosRoda);
VistaCanvas zoomPorFator(const VistaCanvas& vista, PontoF origem, float tx, float ty, float fator);

struct SliceCanvas
{
    int uid = 0;
    RetF r;
};

int acharSliceEm(const std::vector<SliceCanvas>& slices, float x, float y);
std::vector<int> slicesNoRetangulo(const std::vector<SliceCanvas>& slices, const RetF& area);

class GestoPincel
{
public:
    void reiniciar() { tocados_.clear(); }
    std::vector<int> tocar(const std::vector<SliceCanvas>& slices, float x, float y);
    std::vector<int> tocarSegmento(const std::vector<SliceCanvas>& slices, float ax, float ay, float bx, float by, float passo);
    const std::set<int>& tocados() const { return tocados_; }

private:
    std::set<int> tocados_;
};

ModoEdicao modoDosModificadores(bool shift, bool ctrl);

enum class AlvoEdicao { Passo, Visao, Nenhum };
AlvoEdicao alvoDaEdicao(bool alt, int idVisao);

enum class TipoArrasto { Nada, Copiar, CopiarACadaK };

struct ArrastoPasso
{
    TipoArrasto tipo = TipoArrasto::Nada;
    int origem = -1;
    int destino = -1;
    int k = 0;
};

ArrastoPasso resolverArrasto(int origem, int destino, int total, bool ctrl);
int indiceDaCelula(float x, float inicioX, float larguraCelula, int total);
void faixaVisivel(float rolagemX, float larguraVista, float larguraCelula, int total, int& primeira, int& ultima);
int moverSelecao(int atual, int delta, int total);

int acharBorda(const std::vector<float>& bordas, float posicao, float tolerancia);
int inserirBorda(std::vector<float>& bordas, float posicao);
bool removerBorda(std::vector<float>& bordas, int indice);
int moverBorda(std::vector<float>& bordas, int indice, float posicao);

struct AreaGrafico
{
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 1.0f;
    float y1 = 1.0f;
};

PontoF graficoParaTela(const AreaGrafico& area, float x, float y);
PontoF telaParaGrafico(const AreaGrafico& area, float tx, float ty);

enum class TipoAlvo { Nada, Ancora, AlcaEntrada, AlcaSaida, Segmento };

struct AlvoCurva
{
    TipoAlvo tipo = TipoAlvo::Nada;
    int indice = -1;

    bool operator==(const AlvoCurva&) const = default;
};

int indiceDoSegmento(const Envelope& envelope, float x);
AlvoCurva acharAlvoCurva(const Envelope& envelope, const AreaGrafico& area, float tx, float ty, float raio,
                         const std::vector<int>& comAlcas);
float imaNaGrade(float valor, int divisoes, float limiar);
void travarEixo(float xInicial, float yInicial, float& x, float& y);
std::vector<int> ancorasNoRetangulo(const Envelope& envelope, const RetF& area);
float limitarXAncora(const Envelope& envelope, int indice, float x);
void moverAncorasPor(Envelope& envelope, const std::vector<int>& indices, const std::vector<PontoF>& inicio, float dx, float dy);

class HistoricoCurva
{
public:
    explicit HistoricoCurva(std::size_t limite = 64) : limite_(limite) {}

    void registrar(const Envelope& antes);
    bool confirmar(const Envelope& antes, const Envelope& depois);
    bool desfazer(Envelope& atual);
    bool refazer(Envelope& atual);
    void limpar();

    bool podeDesfazer() const { return !desfeitos_.empty(); }
    bool podeRefazer() const { return !refeitos_.empty(); }
    std::size_t quantidade() const { return desfeitos_.size() + refeitos_.size(); }

private:
    std::vector<Envelope> desfeitos_;
    std::vector<Envelope> refeitos_;
    std::size_t limite_;
};

}
