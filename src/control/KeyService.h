#pragma once

#include "control/ActionRegistry.h"
#include "control/KeyDispatcher.h"
#include "control/SystemHotkeys.h"

#include <string>
#include <utility>
#include <vector>

namespace bsl::control {

class KeyService
{
public:
    AppHooks hooks;
    TapTempo tap;
    KeyDispatcher dispatcher;
    SystemHotkeys system;

    double now() const;

    ActionContext context(Composition& comp, const SelectionRef& selection);

    void note(const Fired& fired);

    float flash(const Target& target) const;
    static constexpr double kFlashSeconds = 0.4;

    std::vector<Fired> pollSystem(Composition& comp, const SelectionRef& selection, const KeyBindingSet& app,
                                  const KeyBindingSet& project, bool windowFocused, bool busy = false);
    std::string systemError(Scope scope, int index) const;

    struct Info
    {
        int firedCount = 0;
        std::string lastAction;
        std::string lastKey;
        Target lastTarget;
        int lastHit = 0;
    };
    const Info& info() const { return info_; }

private:
    struct FlashEntry
    {
        Target target;
        double time = 0.0;
    };
    std::vector<FlashEntry> flashes_;
    Info info_;
};

inline int systemTag(Scope scope, int index)
{
    return (scope == Scope::Project ? 1000000 : 0) + index;
}

}
