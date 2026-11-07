#pragma once

#include "core/Composition.h"

#include <cstddef>
#include <deque>
#include <vector>

namespace bsl {

struct TransformSnapshot
{
    struct LayerEntry
    {
        int layerId = 0;
        LayerTransform transform;
        bool operator==(const LayerEntry&) const = default;
    };
    struct ClipEntry
    {
        int deck = 0;
        int layerId = 0;
        int column = 0;
        bool own = false;
        LayerTransform transform;
        bool operator==(const ClipEntry&) const = default;
    };

    std::vector<LayerEntry> layers;
    std::vector<ClipEntry> clips;

    bool operator==(const TransformSnapshot&) const = default;

    static TransformSnapshot capture(const Composition& comp);
    void apply(Composition& comp) const;
};

class TransformHistory
{
public:
    explicit TransformHistory(size_t limit = 100) : limit_(limit) {}

    void reset(const Composition& comp);

    bool observe(const Composition& comp, bool gestureActive);

    bool undo(Composition& comp);
    bool redo(Composition& comp);

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    size_t undoCount() const { return undo_.size(); }
    size_t redoCount() const { return redo_.size(); }

private:
    size_t limit_;
    bool hasBase_ = false;
    TransformSnapshot current_;
    std::deque<TransformSnapshot> undo_;
    std::deque<TransformSnapshot> redo_;
};

}
