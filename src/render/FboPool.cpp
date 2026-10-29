#include "render/FboPool.h"

#include <algorithm>

namespace bsl {

FboPool::Lease FboPool::acquire(int width, int height)
{
    width = std::max(1, width);
    height = std::max(1, height);
    for (Entry& e : entries_) {
        if (!e.used && e.fbo->width() == width && e.fbo->height() == height) {
            e.used = true;
            e.idle = 0;
            return Lease(this, e.fbo.get());
        }
    }
    Entry e;
    e.fbo = std::make_unique<Fbo>(width, height);
    e.used = true;
    ++created_;
    entries_.push_back(std::move(e));
    return Lease(this, entries_.back().fbo.get());
}

void FboPool::release(Fbo* fbo)
{
    for (Entry& e : entries_)
        if (e.fbo.get() == fbo) {
            e.used = false;
            e.idle = 0;
            return;
        }
}

void FboPool::endFrame(int maxIdleFrames)
{
    for (Entry& e : entries_)
        if (!e.used)
            ++e.idle;
    std::erase_if(entries_, [&](const Entry& e) { return !e.used && e.idle > maxIdleFrames; });
}

void FboPool::clear()
{
    entries_.clear();
}

size_t FboPool::inUse() const
{
    return static_cast<size_t>(std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.used; }));
}

} // namespace bsl
