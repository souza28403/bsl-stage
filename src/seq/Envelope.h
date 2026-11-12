#pragma once

#include <string>
#include <vector>

namespace bsl::seq {

inline constexpr int kTamanhoTabela = 256;
inline constexpr int kMaxEnvelopes = 512;
inline constexpr int kMaxAncoras = 128;
inline constexpr float kEspacoMinimo = 1e-4f;

struct Ancora
{
    float x = 0.0f;
    float y = 0.0f;
    float entradaX = 0.0f;
    float entradaY = 0.0f;
    float saidaX = 0.0f;
    float saidaY = 0.0f;
    bool alcasLigadas = true;
    bool reto = false;

    bool operator==(const Ancora&) const = default;
};

struct Envelope
{
    int id = 0;
    std::string nome;
    std::vector<Ancora> ancoras;
    std::vector<float> tabela;

    bool mesmoConteudo(const Envelope& o) const { return nome == o.nome && ancoras == o.ancoras; }
    bool operator==(const Envelope& o) const { return id == o.id && mesmoConteudo(o); }

    float avaliar(float x) const;
    float amostrar(float x) const;
};

struct BibliotecaEnvelopes
{
    std::vector<Envelope> lista;
    int proximoId = 1;
};

Envelope envelopeCurvaS(int id = 0, const std::string& nome = "Curva S");
Envelope envelopeLinear(int id = 0, const std::string& nome = "Linear");

void normalizarEnvelope(Envelope& envelope);
void reconstruirTabela(Envelope& envelope);

int inserirAncora(Envelope& envelope, float x);
bool removerAncora(Envelope& envelope, int indice);
void moverAncora(Envelope& envelope, int indice, float x, float y);
void definirAlca(Envelope& envelope, int indice, bool saida, float dx, float dy);
void definirReto(Envelope& envelope, int indice, bool reto);
void definirAlcasLigadas(Envelope& envelope, int indice, bool ligadas);

void garantirBiblioteca(BibliotecaEnvelopes& biblioteca);
int criarEnvelope(BibliotecaEnvelopes& biblioteca, const std::string& nome);
int duplicarEnvelope(BibliotecaEnvelopes& biblioteca, int id);
bool excluirEnvelope(BibliotecaEnvelopes& biblioteca, int id);
bool renomearEnvelope(BibliotecaEnvelopes& biblioteca, int id, const std::string& nome);
Envelope* acharEnvelope(BibliotecaEnvelopes& biblioteca, int id);
const Envelope* acharEnvelope(const BibliotecaEnvelopes& biblioteca, int id);

}
