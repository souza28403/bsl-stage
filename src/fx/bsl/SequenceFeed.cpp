#include "fx/bsl/SequenceFeed.h"

#include <utility>

namespace bsl::fx::bsllight {

const seq::Envelope* SequenceSnapshot::envelope(int id) const
{
    for (const seq::Envelope& e : envelopes)
        if (e.id == id)
            return &e;
    return nullptr;
}

std::string SequenceSnapshot::nomeDaSequencia(int id) const
{
    const seq::Sequencia* s = sequencia(id);
    return s ? s->nome : std::string();
}

std::string SequenceSnapshot::nomeDaVisao(int id) const
{
    if (id == seq::kVisaoTudo)
        return "Tudo";
    const seq::Visao* v = visao(id);
    return v ? v->nome : std::string();
}

std::string SequenceSnapshot::nomeDoEnvelope(int id) const
{
    const seq::Envelope* e = envelope(id);
    return e ? e->nome : std::string();
}

SequenceFeed& SequenceFeed::instance()
{
    static SequenceFeed feed;
    return feed;
}

bool SequenceFeed::set(const seq::SequenceSet& conjunto, const std::vector<seq::Envelope>& envelopes)
{
    std::lock_guard lk(mutex_);
    if (atual_ && atual_->conjunto == conjunto && atual_->envelopes == envelopes)
        return false;
    auto novo = std::make_shared<SequenceSnapshot>();
    novo->conjunto = conjunto;
    novo->envelopes = envelopes;
    novo->versao = ++versao_;
    atual_ = std::move(novo);
    return true;
}

std::shared_ptr<const SequenceSnapshot> SequenceFeed::snapshot() const
{
    std::lock_guard lk(mutex_);
    if (atual_)
        return atual_;
    static const std::shared_ptr<const SequenceSnapshot> vazio = std::make_shared<SequenceSnapshot>();
    return vazio;
}

std::uint64_t SequenceFeed::version() const
{
    std::lock_guard lk(mutex_);
    return versao_;
}

void SequenceFeed::setPreferida(int id)
{
    preferida_ = id;
}

int SequenceFeed::sequenciaPadrao() const
{
    const auto snap = snapshot();
    if (snap->sequencia(preferida_))
        return preferida_;
    return snap->conjunto.sequencias.empty() ? 0 : snap->conjunto.sequencias.front().id;
}

}
