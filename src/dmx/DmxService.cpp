#include "dmx/DmxService.h"

#include "core/Composition.h"

namespace bsl::dmx {

void Service::update(const NetConfig& cfg, Composition& comp, control::KeyService& keys, const control::SelectionRef& selection)
{
    const double now = Receiver::clock();
    if (!cfg.enabled) {
        if (receiver_.running() || haveStarted_) {
            receiver_.stop();
            haveStarted_ = false;
            snap_ = Snapshot{};
            engine_.reset();
        }
        error_.clear();
        return;
    }

    const std::vector<int> used = comp.dmx.universesUsed();
    const bool changed = !haveStarted_ || !(started_ == cfg) || startedUniverses_ != used || restart_;
    if (changed && now >= nextRetry_) {
        restart_ = false;
        receiver_.stop();
        std::string error;
        started_ = cfg;
        startedUniverses_ = used;
        haveStarted_ = true;
        if (receiver_.start(cfg, used, &error)) {
            error_.clear();
        } else {
            error_ = error;
            haveStarted_ = false;
            nextRetry_ = now + 5.0;
            snap_ = Snapshot{};
            snap_.status = error;
        }
    }

    if (!receiver_.running())
        return;
    receiver_.poll(snap_);
    engine_.apply(comp, keys, selection, snap_);
}

void Service::shutdown()
{
    receiver_.stop();
    haveStarted_ = false;
}

Service::State Service::state() const
{
    if (!error_.empty())
        return State::Error;
    if (!receiver_.running())
        return State::Off;
    bool any = false, receiving = false, lost = false;
    for (const UniverseView& u : snap_.universes) {
        any = any || u.everSeen;
        receiving = receiving || u.active;
        lost = lost || u.lost;
    }
    if (receiving)
        return State::Receiving;
    return (any && lost) ? State::Lost : State::Waiting;
}

}
