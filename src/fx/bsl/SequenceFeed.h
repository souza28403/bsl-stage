#pragma once

#include "seq/Envelope.h"
#include "seq/SeqModel.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace bsl::fx::bsllight {

struct SequenceSnapshot
{
    seq::SequenceSet conjunto;
    std::vector<seq::Envelope> envelopes;
    std::uint64_t versao = 0;

    const seq::Sequencia* sequencia(int id) const { return seq::acharSequencia(conjunto, id); }
    const seq::Visao* visao(int id) const { return seq::acharVisao(conjunto, id); }
    bool visaoExiste(int id) const { return id == seq::kVisaoTudo || visao(id) != nullptr; }
    const std::vector<int>* uidsDaVisao(int id) const
    {
        const seq::Visao* v = visao(id);
        return (id == seq::kVisaoTudo || !v) ? nullptr : &v->ligados;
    }
    const seq::Envelope* envelope(int id) const;
    std::string nomeDaSequencia(int id) const;
    std::string nomeDaVisao(int id) const;
    std::string nomeDoEnvelope(int id) const;
};

class SequenceFeed
{
public:
    static SequenceFeed& instance();

    bool set(const seq::SequenceSet& conjunto, const std::vector<seq::Envelope>& envelopes);
    std::shared_ptr<const SequenceSnapshot> snapshot() const;
    std::uint64_t version() const;
    void setPreferida(int id);
    int sequenciaPadrao() const;

private:
    std::atomic<int> preferida_{0};
    mutable std::mutex mutex_;
    std::shared_ptr<const SequenceSnapshot> atual_;
    std::uint64_t versao_ = 0;
};

}
