#pragma once

#include "output/OutputSetup.h"

#include <cstddef>
#include <deque>

namespace bsl {

// Historico de desfazer/refazer do mapeamento: snapshots do OutputSetup inteiro, um por operacao.
//
// Uso: a cada frame, 'observe(live, gestureActive)'. Enquanto um gesto esta em andamento (mouse apertado, campo de
// texto em edicao) as mudancas ficam pendentes; quando ele termina, tudo que mudou vira UMA operacao.
class OutputHistory
{
public:
    explicit OutputHistory(size_t limit = 100) : limit_(limit) {}

    // Novo ponto de partida (projeto aberto): limpa as duas pilhas
    void reset(const OutputSetup& state);

    // true se registrou uma operacao nova
    bool observe(const OutputSetup& live, bool gestureActive);

    // Desfaz/refaz: gravam o estado de volta em 'live'. Mudancas pendentes entram antes como uma operacao.
    bool undo(OutputSetup& live);
    bool redo(OutputSetup& live);

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    size_t undoCount() const { return undo_.size(); }
    size_t redoCount() const { return redo_.size(); }
    size_t limit() const { return limit_; }

private:
    size_t limit_;
    bool hasBase_ = false;
    OutputSetup current_; // o ultimo estado registrado
    std::deque<OutputSetup> undo_;
    std::deque<OutputSetup> redo_;
};

} // namespace bsl
