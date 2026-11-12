#include "seq/SeqHistory.h"

#include <utility>

namespace bsl::seq {

std::size_t estimarBytes(const SequenceSet& conjunto)
{
    std::size_t total = sizeof(SequenceSet);
    for (const Sequencia& s : conjunto.sequencias) {
        total += sizeof(Sequencia) + s.nome.capacity();
        total += s.gerador.bordasX.capacity() * sizeof(float) + s.gerador.bordasY.capacity() * sizeof(float);
        for (const Passo& p : s.passos)
            total += sizeof(Passo) + p.capacity() * sizeof(int);
    }
    for (const Visao& v : conjunto.visoes)
        total += sizeof(Visao) + v.nome.capacity() + v.ligados.capacity() * sizeof(int);
    for (const AncoraSlice& a : conjunto.ancoras)
        total += sizeof(AncoraSlice) + a.tela.capacity() + a.nome.capacity();
    return total;
}

void SeqHistory::aparar()
{
    while (bytes_ > teto_ && (desfeitos_.size() + refeitos_.size()) > 1) {
        if (!desfeitos_.empty()) {
            bytes_ -= desfeitos_.front().bytes;
            desfeitos_.pop_front();
        } else {
            bytes_ -= refeitos_.front().bytes;
            refeitos_.pop_front();
        }
    }
}

void SeqHistory::registrar(const SequenceSet& antes)
{
    for (const Entrada& e : refeitos_)
        bytes_ -= e.bytes;
    refeitos_.clear();
    Entrada nova;
    nova.estado = antes;
    nova.bytes = estimarBytes(antes);
    bytes_ += nova.bytes;
    desfeitos_.push_back(std::move(nova));
    aparar();
}

bool SeqHistory::confirmar(const SequenceSet& antes, const SequenceSet& depois)
{
    if (antes == depois)
        return false;
    registrar(antes);
    return true;
}

bool SeqHistory::desfazer(SequenceSet& atual)
{
    if (desfeitos_.empty())
        return false;
    Entrada alvo = std::move(desfeitos_.back());
    desfeitos_.pop_back();
    bytes_ -= alvo.bytes;
    Entrada inverso;
    inverso.estado = std::move(atual);
    inverso.bytes = estimarBytes(inverso.estado);
    bytes_ += inverso.bytes;
    refeitos_.push_back(std::move(inverso));
    atual = std::move(alvo.estado);
    aparar();
    return true;
}

bool SeqHistory::refazer(SequenceSet& atual)
{
    if (refeitos_.empty())
        return false;
    Entrada alvo = std::move(refeitos_.back());
    refeitos_.pop_back();
    bytes_ -= alvo.bytes;
    Entrada inverso;
    inverso.estado = std::move(atual);
    inverso.bytes = estimarBytes(inverso.estado);
    bytes_ += inverso.bytes;
    desfeitos_.push_back(std::move(inverso));
    atual = std::move(alvo.estado);
    aparar();
    return true;
}

void SeqHistory::limpar()
{
    desfeitos_.clear();
    refeitos_.clear();
    bytes_ = 0;
}

}
