#include "control/KeyService.h"

#include <algorithm>
#include <chrono>

namespace bsl::control {

double KeyService::now() const
{
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

ActionContext KeyService::context(Composition& comp, const SelectionRef& selection)
{
    return ActionContext{comp, selection, now(), &tap, &hooks};
}

void KeyService::note(const Fired& fired)
{
    const double t = now();
    ++info_.firedCount;
    info_.lastHit = fired.result.hit;
    if (fired.match.binding) {
        info_.lastAction = fired.match.binding->action;
        info_.lastKey = chordId(fired.match.binding->chord);
    }
    info_.lastTarget = fired.result.target;
    if (fired.result.target.valid() && fired.result.hit > 0)
        flashes_.push_back({fired.result.target, t});
    std::erase_if(flashes_, [t](const FlashEntry& f) { return t - f.time > kFlashSeconds; });
}

float KeyService::flash(const Target& target) const
{
    const double t = now();
    float best = 0.0f;
    for (const FlashEntry& f : flashes_) {
        if (!(f.target == target))
            continue;
        const double age = t - f.time;
        if (age < 0.0 || age > kFlashSeconds)
            continue;
        best = std::max(best, static_cast<float>(1.0 - age / kFlashSeconds));
    }
    return best;
}

std::vector<Fired> KeyService::pollSystem(Composition& comp, const SelectionRef& selection, const KeyBindingSet& app,
                                          const KeyBindingSet& project, bool windowFocused, bool busy)
{
    std::vector<Fired> out;

    std::vector<SystemHotkeys::Entry> wanted;
    if (!windowFocused) {
        auto collect = [&](const KeyBindingSet& set, Scope scope) {
            for (size_t i = 0; i < set.items.size(); ++i)
                if (set.items[i].global && set.items[i].chord.valid())
                    wanted.push_back({set.items[i].chord, systemTag(scope, static_cast<int>(i))});
        };
        collect(project, Scope::Project);
        collect(app, Scope::App);
    }
    system.sync(wanted);

    ActionContext ctx = context(comp, selection);
    for (const int tag : system.poll()) {
        if (busy)
            continue;
        const Scope scope = tag >= 1000000 ? Scope::Project : Scope::App;
        const int index = tag >= 1000000 ? tag - 1000000 : tag;
        const KeyBindingSet& set = scope == Scope::Project ? project : app;
        if (index < 0 || index >= static_cast<int>(set.items.size()))
            continue;
        const KeyBinding& b = set.items[static_cast<size_t>(index)];
        const ActionDef* def = findAction(b.action);
        if (!def)
            continue;
        out.push_back(dispatcher.fire(Match{scope, index, &b}, ctx));
        note(out.back());
    }
    for (const Fired& f : dispatcher.releaseUp(
             [](const KeyBinding& b) { return !b.global || SystemHotkeys::keyDown(b.chord.key); }, ctx)) {
        out.push_back(f);
        note(f);
    }
    for (const Fired& f : dispatcher.tick(ctx)) {
        out.push_back(f);
        note(f);
    }
    return out;
}

std::string KeyService::systemError(Scope scope, int index) const
{
    return system.error(systemTag(scope, index));
}

}
