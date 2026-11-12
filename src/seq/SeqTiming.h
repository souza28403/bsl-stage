#pragma once

#include "seq/Envelope.h"
#include "seq/SeqModel.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace bsl::seq {

inline constexpr int kMaxEcos = 16;
inline constexpr int kMaxAleatorios = 64;
inline constexpr int kMaxVoltas = 8;

enum class ModoAvanco { Velocidade = 0, Bpm, Fixo };
enum class DirecaoSeq { Frente = 0, Tras, VaiEVolta, Aleatorio };
enum class ModoCurva { Sustentacao = 0, Envelope };

struct Relogio
{
    double segundos = 0.0;
    double batidas = 0.0;
};

struct ParametrosTempo
{
    ModoAvanco avanco = ModoAvanco::Velocidade;
    float passosPorSegundo = 4.0f;
    float batidasPorPasso = 1.0f;
    int passoFixo = 0;
    DirecaoSeq direcao = DirecaoSeq::Frente;
    int deslocamento = 0;
    int ecos = 0;
    float sustentacao = 0.2f;
    float liberacao = 0.8f;
    ModoCurva curva = ModoCurva::Sustentacao;
    int semente = 1;
};

struct PosicaoPasso
{
    std::int64_t contador = 0;
    float fase = 0.0f;
};

struct ItemIntensidade
{
    int uid = 0;
    float valor = 0.0f;
};

struct ItemVarredura
{
    int uid = 0;
    float progresso = 0.0f;
    bool ativo = false;
    bool invertido = false;
};

struct ParametrosVarredura
{
    ModoAvanco avanco = ModoAvanco::Velocidade;
    float passosPorSegundo = 4.0f;
    float batidasPorPasso = 1.0f;
    int passoFixo = 0;
    int deslocamento = 0;
    float sobreposicao = 0.0f;
    int voltas = 1;
    bool inverter = false;
    bool alternarParesImpares = false;
    bool usarEnvelope = false;
};

float limitarBatidasPorPasso(float valor);
int limitarEcos(int ecos);
int limiteAleatorios(int disponiveis, int maximo);

PosicaoPasso posicaoNoTempo(const ParametrosTempo& parametros, const Relogio& relogio);
int indiceDoPasso(std::int64_t contador, int total, DirecaoSeq direcao, int semente);
float valorDaCurva(const ParametrosTempo& parametros, const Envelope* envelope, float idade);

void calcularIntensidades(const Sequencia& sequencia, const ParametrosTempo& parametros, const Envelope* envelope,
                          const Relogio& relogio, const std::vector<int>* permitidos,
                          std::vector<ItemIntensidade>& saida);

void calcularVarredura(const Sequencia& sequencia, const ParametrosVarredura& parametros, const Envelope* envelope,
                       const Relogio& relogio, const std::vector<int>* permitidos, std::vector<ItemVarredura>& saida);

struct ParametrosDisparo
{
    float velocidadeRajada = 8.0f;
    bool resetAutomatico = false;
    float atrasoReset = 2.0f;
    float velocidadeApagar = 2.0f;
    float sustentacao = 0.2f;
    float liberacao = 0.8f;
    ModoCurva curva = ModoCurva::Sustentacao;
};

class MaquinaDisparo
{
public:
    void proximo(double agora, int total);
    void anterior(double agora, int total);
    void rajada(bool ativa, double agora);
    void reiniciar();
    void atualizar(double agora, const ParametrosDisparo& parametros, int total);

    int passoAtual() const { return atual_; }
    bool rajadaAtiva() const { return rajada_; }
    int disparosAtivos() const { return static_cast<int>(disparos_.size()); }

    void calcular(const Sequencia& sequencia, const ParametrosDisparo& parametros, const Envelope* envelope,
                  double agora, const std::vector<int>* permitidos, std::vector<ItemIntensidade>& saida) const;

private:
    struct Disparo
    {
        int indice = 0;
        double inicio = 0.0;
    };

    void disparar(int indice, double agora);

    std::deque<Disparo> disparos_;
    int atual_ = -1;
    bool rajada_ = false;
    double ultimo_ = 0.0;
    double proximaRajada_ = 0.0;
};

struct ParametrosAleatorios
{
    int maximo = 4;
    float passosPorSegundo = 2.0f;
    ModoAvanco avanco = ModoAvanco::Velocidade;
    float batidasPorPasso = 1.0f;
    int ecos = 0;
    float sustentacao = 0.2f;
    float liberacao = 0.8f;
    ModoCurva curva = ModoCurva::Sustentacao;
    int semente = 1;
    bool semRepetir = true;
};

class SorteioSlices
{
public:
    void reiniciar();
    void atualizar(const ParametrosAleatorios& parametros, const std::vector<int>& candidatos, const Relogio& relogio);
    void calcular(const ParametrosAleatorios& parametros, const Envelope* envelope,
                  std::vector<ItemIntensidade>& saida) const;

    const std::vector<int>& sorteioAtual() const;
    std::int64_t contador() const { return contador_; }

private:
    struct Rodada
    {
        std::int64_t contador = 0;
        std::vector<int> uids;
    };

    std::vector<int> sortear(const ParametrosAleatorios& parametros, const std::vector<int>& candidatos,
                             std::int64_t contador, const std::vector<int>* anterior) const;

    std::deque<Rodada> historico_;
    std::int64_t contador_ = 0;
    float fase_ = 0.0f;
    std::uint64_t assinatura_ = 0;
    bool iniciado_ = false;
};

}
