#pragma once

#include "seq/SeqModel.h"

#include <string>
#include <vector>

namespace bsl::seq {

enum class ModoEdicao { Inverter = 0, Ligar, Desligar };

bool passoContem(const Passo& passo, int uid);
void aplicarEdicao(Passo& passo, const std::vector<int>& uids, ModoEdicao modo);
void aplicarEdicaoVisao(Visao& visao, const std::vector<int>& uids, ModoEdicao modo);

bool passoValido(const Sequencia& sequencia, int indice);
bool copiarPasso(Sequencia& sequencia, int origem, int destino);
int copiarPassoACadaK(Sequencia& sequencia, int origem, int k);
bool trocarPassos(Sequencia& sequencia, int a, int b);
bool inserirPasso(Sequencia& sequencia, int posicao);
bool removerPasso(Sequencia& sequencia, int indice);
bool limparPasso(Sequencia& sequencia, int indice);
void limparSequencia(Sequencia& sequencia);
int repetirParaFrente(Sequencia& sequencia, int indice);
bool vaiEVolta(Sequencia& sequencia);

bool moverSequencia(SequenceSet& conjunto, int de, int para);
bool moverVisao(SequenceSet& conjunto, int de, int para);
bool renomearSequencia(SequenceSet& conjunto, int id, const std::string& nome);
bool renomearVisao(SequenceSet& conjunto, int id, const std::string& nome);
bool passosDiferentes(const SequenceSet& conjunto);

}
