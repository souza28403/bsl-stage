#pragma once

#include <cstdio>
#include <string>

namespace seqtest {

extern int falhas;
extern int verificacoes;

inline void secao(const char* nome)
{
    std::printf("[%s]\n", nome);
}

inline void verificar(bool ok, const std::string& o_que)
{
    ++verificacoes;
    if (!ok) {
        ++falhas;
        std::printf("  FALHOU: %s\n", o_que.c_str());
    }
}

void testarModelo();
void testarEnvelope();
void testarFabrica();
void testarEdicao();
void testarTempo();
void testarInterface();

}

#define VERIFICAR(cond) ::seqtest::verificar((cond), std::string(#cond) + " (linha " + std::to_string(__LINE__) + ")")
