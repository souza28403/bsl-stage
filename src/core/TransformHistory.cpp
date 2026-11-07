#include "core/TransformHistory.h"

namespace bsl {

TransformSnapshot TransformSnapshot::capture(const Composition& comp)
{
    TransformSnapshot s;
    for (const Layer& l : comp.layers)
        if (!l.transform.isDefault())
            s.layers.push_back({l.id, l.transform});
    for (int d = 0; d < comp.deckCount(); ++d)
        for (int li = 0; li < comp.layerCount(); ++li)
            for (int c = 0; c < comp.decks[static_cast<size_t>(d)].columnCount(); ++c) {
                const Clip& clip = comp.clipAt(d, li, c);
                if (clip.ownTransform || !clip.transform.isDefault())
                    s.clips.push_back({d, comp.layers[static_cast<size_t>(li)].id, c, clip.ownTransform, clip.transform});
            }
    return s;
}

void TransformSnapshot::apply(Composition& comp) const
{
    for (Layer& l : comp.layers)
        l.transform = LayerTransform{};
    for (const LayerEntry& e : layers)
        if (const int i = comp.findLayerById(e.layerId); i >= 0)
            comp.layers[static_cast<size_t>(i)].transform = e.transform;
    for (int d = 0; d < comp.deckCount(); ++d)
        for (int li = 0; li < comp.layerCount(); ++li)
            for (int c = 0; c < comp.decks[static_cast<size_t>(d)].columnCount(); ++c) {
                Clip& clip = comp.clipAt(d, li, c);
                clip.ownTransform = false;
                clip.transform = LayerTransform{};
            }
    for (const ClipEntry& e : clips) {
        const int li = comp.findLayerById(e.layerId);
        if (li < 0 || !comp.validCell(e.deck, li, e.column))
            continue;
        Clip& clip = comp.clipAt(e.deck, li, e.column);
        clip.ownTransform = e.own;
        clip.transform = e.transform;
    }
}

void TransformHistory::reset(const Composition& comp)
{
    current_ = TransformSnapshot::capture(comp);
    hasBase_ = true;
    undo_.clear();
    redo_.clear();
}

bool TransformHistory::observe(const Composition& comp, bool gestureActive)
{
    if (!hasBase_) {
        reset(comp);
        return false;
    }
    if (gestureActive)
        return false;
    TransformSnapshot now = TransformSnapshot::capture(comp);
    if (now == current_)
        return false;
    undo_.push_back(std::move(current_));
    while (undo_.size() > limit_)
        undo_.pop_front();
    redo_.clear();
    current_ = std::move(now);
    return true;
}

bool TransformHistory::undo(Composition& comp)
{
    observe(comp, false);
    if (undo_.empty())
        return false;
    redo_.push_back(std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    current_.apply(comp);
    return true;
}

bool TransformHistory::redo(Composition& comp)
{
    observe(comp, false);
    if (redo_.empty())
        return false;
    undo_.push_back(std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    current_.apply(comp);
    return true;
}

}
