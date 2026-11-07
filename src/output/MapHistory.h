#pragma once

#include "output/OutputSetup.h"

#include <cstddef>
#include <deque>

namespace bsl {

class OutputHistory
{
public:
    explicit OutputHistory(size_t limit = 100) : limit_(limit) {}

    void reset(const OutputSetup& state);

    bool observe(const OutputSetup& live, bool gestureActive);

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
    OutputSetup current_;
    std::deque<OutputSetup> undo_;
    std::deque<OutputSetup> redo_;
};

}
