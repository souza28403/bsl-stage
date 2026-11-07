#include "output/MapHistory.h"

namespace bsl {

void OutputHistory::reset(const OutputSetup& state)
{
    current_ = state;
    hasBase_ = true;
    undo_.clear();
    redo_.clear();
}

bool OutputHistory::observe(const OutputSetup& live, bool gestureActive)
{
    if (!hasBase_) {
        reset(live);
        return false;
    }
    if (gestureActive || live == current_)
        return false;
    undo_.push_back(std::move(current_));
    while (undo_.size() > limit_)
        undo_.pop_front();
    redo_.clear();
    current_ = live;
    return true;
}

bool OutputHistory::undo(OutputSetup& live)
{
    observe(live, false);
    if (undo_.empty())
        return false;
    redo_.push_back(std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    live = current_;
    return true;
}

bool OutputHistory::redo(OutputSetup& live)
{
    observe(live, false);
    if (redo_.empty())
        return false;
    undo_.push_back(std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    live = current_;
    return true;
}

}
