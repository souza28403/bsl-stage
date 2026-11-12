#include "seqtest.h"

namespace seqtest {

int falhas = 0;
int verificacoes = 0;

}

int main()
{
    seqtest::testarModelo();
    seqtest::testarEnvelope();
    seqtest::testarFabrica();
    seqtest::testarEdicao();
    seqtest::testarTempo();
    seqtest::testarInterface();
    std::printf("%d verificacoes, %d falhas\n", seqtest::verificacoes, seqtest::falhas);
    return seqtest::falhas == 0 ? 0 : 1;
}
