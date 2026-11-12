#pragma once

#include <string>
#include <vector>

namespace bsl {
struct OutputSetup;
}

namespace bsl::seq {

inline constexpr int kMaxPassos = 1024;
inline constexpr int kMaxSequencias = 256;
inline constexpr int kMaxVisoes = 256;
inline constexpr int kVisaoTudo = 0;

enum class DirecaoGerador { Horizontal = 0, Vertical, Linha, Coluna, Aleatorio };

struct ParametrosGerador
{
    DirecaoGerador direcao = DirecaoGerador::Horizontal;
    bool inverterX = false;
    bool inverterY = false;
    int bloco = 1;
    int modulo = 0;
    bool espelhoX = false;
    bool espelhoY = false;
    bool embaralhar = false;
    int semente = 1;
    float limiar = 0.5f;
    bool bordasManuais = false;
    std::vector<float> bordasX;
    std::vector<float> bordasY;

    bool operator==(const ParametrosGerador&) const = default;
};

using Passo = std::vector<int>;

struct Sequencia
{
    int id = 0;
    std::string nome;
    std::vector<Passo> passos;
    bool temGerador = false;
    ParametrosGerador gerador;

    bool operator==(const Sequencia&) const = default;
};

struct Visao
{
    int id = 0;
    std::string nome;
    bool previaVisivel = true;
    std::vector<int> ligados;

    bool operator==(const Visao&) const = default;
};

struct AncoraSlice
{
    int uid = 0;
    std::string tela;
    std::string nome;
    int indice = 0;

    bool operator==(const AncoraSlice&) const = default;
};

struct SequenceSet
{
    std::vector<Sequencia> sequencias;
    std::vector<Visao> visoes;
    std::vector<AncoraSlice> ancoras;
    int proximoId = 1;

    bool vazio() const { return sequencias.empty() && visoes.empty(); }
    bool operator==(const SequenceSet&) const = default;
};

std::string passoParaTexto(const Passo& passo);
bool textoParaPasso(const std::string& texto, Passo& passo);
void normalizarPasso(Passo& passo);

int criarSequencia(SequenceSet& conjunto, const std::string& nome, int passos = 8);
int criarVisao(SequenceSet& conjunto, const std::string& nome);
Sequencia* acharSequencia(SequenceSet& conjunto, int id);
const Sequencia* acharSequencia(const SequenceSet& conjunto, int id);
Visao* acharVisao(SequenceSet& conjunto, int id);
const Visao* acharVisao(const SequenceSet& conjunto, int id);
bool excluirSequencia(SequenceSet& conjunto, int id);
bool excluirVisao(SequenceSet& conjunto, int id);
int duplicarSequencia(SequenceSet& conjunto, int id);
int duplicarVisao(SequenceSet& conjunto, int id);
bool visaoContem(const SequenceSet& conjunto, int idVisao, int uid);
void definirTamanho(Sequencia& sequencia, int passos);

void normalizar(SequenceSet& conjunto);

void atualizarAncoras(SequenceSet& conjunto, const OutputSetup& saida);
std::vector<int> uidsOrfaos(const SequenceSet& conjunto, const OutputSetup& saida);
int religarSlices(SequenceSet& conjunto, const OutputSetup& saida);

}
