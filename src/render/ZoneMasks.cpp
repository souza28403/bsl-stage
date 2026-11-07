#include "render/ZoneMasks.h"

#include <algorithm>

namespace bsl {

ZoneMasks& ZoneMasks::instance()
{
    static ZoneMasks inst;
    return inst;
}

void ZoneMasks::beginFrame()
{
    std::lock_guard lock(mutex_);
    for (Entry& e : entries_)
        e.fresh = false;
}

void ZoneMasks::publish(const void* owner, int source, const ZoneMask& mask)
{
    std::lock_guard lock(mutex_);
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.owner == owner; });
    if (it == entries_.end()) {
        entries_.emplace_back();
        it = entries_.end() - 1;
        it->owner = owner;
    }
    it->source = source;
    it->mask = mask;
    it->fresh = true;
    it->seq = ++seq_;
}

void ZoneMasks::retract(const void* owner)
{
    std::lock_guard lock(mutex_);
    std::erase_if(entries_, [&](const Entry& e) { return e.owner == owner; });
}

bool ZoneMasks::find(int source, ZoneMask& out) const
{
    std::lock_guard lock(mutex_);
    const Entry* best = nullptr;
    for (const Entry& e : entries_)
        if (e.fresh && e.source == source && e.mask.texture != 0 && (!best || e.seq > best->seq))
            best = &e;
    if (!best)
        return false;
    out = best->mask;
    return true;
}

bool ZoneMasks::available(int source) const
{
    ZoneMask m;
    return find(source, m);
}

}
