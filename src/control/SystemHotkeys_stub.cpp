#include "control/SystemHotkeys.h"

#include "platform/Unavailable.h"

namespace bsl::control {

SystemHotkeys::~SystemHotkeys() = default;

void SystemHotkeys::unregisterAll()
{
    regs_.clear();
}

size_t SystemHotkeys::registeredCount() const
{
    return 0;
}

std::string SystemHotkeys::error(int tag) const
{
    for (const Reg& r : regs_)
        if (r.tag == tag)
            return r.error;
    return {};
}

void SystemHotkeys::sync(const std::vector<Entry>& wanted)
{
    regs_.clear();
    for (const Entry& e : wanted) {
        if (!e.chord.valid())
            continue;
        Reg r;
        r.chord = e.chord;
        r.tag = e.tag;
        r.error = bslplat::unavailableText("Atalhos globais");
        regs_.push_back(std::move(r));
    }
}

std::vector<int> SystemHotkeys::poll()
{
    return {};
}

bool SystemHotkeys::keyDown(Key)
{
    return false;
}

std::string SystemHotkeys::probe(Chord)
{
    return bslplat::unavailableText("Atalhos globais");
}

}
