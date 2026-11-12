#pragma once

#include "seq/SeqModel.h"

#include <cstddef>
#include <deque>

namespace bsl::seq {

inline constexpr std::size_t kTetoHistoricoBytes = static_cast<std::size_t>(32) * 1024 * 1024;

std::size_t estimarBytes(const SequenceSet& conjunto);

class SeqHistory
{
public:
    explicit SeqHistory(std::size_t teto = kTetoHistoricoBytes) : teto_(teto) {}

    void registrar(const SequenceSet& antes);
    bool confirmar(const SequenceSet& antes, const SequenceSet& depois);
    bool desfazer(SequenceSet& atual);
    bool refazer(SequenceSet& atual);
    void limpar();

    bool podeDesfazer() const { return !desfeitos_.empty(); }
    bool podeRefazer() const { return !refeitos_.empty(); }
    std::size_t bytes() const { return bytes_; }
    std::size_t quantidade() const { return desfeitos_.size() + refeitos_.size(); }

private:
    struct Entrada
    {
        SequenceSet estado;
        std::size_t bytes = 0;
    };

    void aparar();

    std::deque<Entrada> desfeitos_;
    std::deque<Entrada> refeitos_;
    std::size_t bytes_ = 0;
    std::size_t teto_;
};

}
