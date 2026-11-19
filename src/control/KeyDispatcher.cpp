#include "control/KeyDispatcher.h"

#include "core/Composition.h"

#include <algorithm>

namespace bsl::control {

bool gateAllows(const KeyBinding& binding, const KeyGate& gate)
{
    if (gate.capturing || gate.busy)
        return false;
    const ActionDef* def = findAction(binding.action);
    if (!def)
        return false;
    const bool modded = (binding.chord.mods & (ModCtrl | ModAlt)) != 0;
    if (gate.textInput || gate.mapEditor)
        return !def->show && modded;
    if (gate.popupOpen)
        return !def->show;
    const Key key = binding.chord.key;
    if (gate.navKeys && binding.chord.mods == ModNone && (key == Key::Space || key == Key::Enter))
        return false;
    return true;
}

std::optional<Fired> KeyDispatcher::press(const Chord& chord, const KeyBindingSet& app, const KeyBindingSet& project,
                                          const KeyGate& gate, ActionContext& ctx)
{
    const std::optional<Match> match = resolveKey(chord, app, project);
    if (!match || !gateAllows(*match->binding, gate))
        return std::nullopt;
    return fire(*match, ctx);
}

Fired KeyDispatcher::fire(const Match& match, ActionContext& ctx)
{
    Fired out;
    out.match = match;
    const KeyBinding& b = *match.binding;
    if (b.mode == Mode::Repeat && supportsRepeat(b.action)) {
        if (isHeld(b.chord.key)) {
            out.result = ActionResult{};
            return out;
        }
        return begin(match, b, 0, ctx);
    }
    if (b.mode == Mode::Hold && supportsHold(b.action)) {
        if (isHeld(b.chord.key)) {
            out.result = ActionResult{};
            return out;
        }
        int previous = -1;
        out.result = runAction(ctx, b.action, b.args, &previous);
        if (previous >= 0) {
            out.hold = true;
            held_.push_back(Held{match, b, previous});
            held_.back().match.binding = nullptr;
        }
        return out;
    }
    out.result = runAction(ctx, b.action, b.args);
    return out;
}

namespace {

double repeatInterval(const ActionContext& ctx, int rate)
{
    const double bpm = std::max<double>(ctx.comp.bpm, 1.0);
    return 60.0 / bpm / std::max<int>(rate, 1);
}

}

Fired KeyDispatcher::begin(const Match& match, const KeyBinding& binding, int token, ActionContext& ctx)
{
    Fired out;
    out.match = match;
    int previous = -1;
    out.result = runAction(ctx, binding.action, binding.args, &previous);
    const ActionDef* def = findAction(binding.action);
    if (def && def->isToggle() && previous < 0)
        return out;
    out.hold = true;
    Held h{match, binding, previous, token};
    h.match.binding = nullptr;
    h.repeat = true;
    h.rate = snapRate(binding.rate);
    h.next = ctx.now + repeatInterval(ctx, h.rate);
    held_.push_back(std::move(h));
    return out;
}

std::vector<Fired> KeyDispatcher::tick(ActionContext& ctx)
{
    std::vector<Fired> out;
    for (Held& h : held_) {
        if (!h.repeat || h.next > ctx.now)
            continue;
        Fired f;
        f.match = h.match;
        f.hold = true;
        f.result = runAction(ctx, h.binding.action, h.binding.args);
        out.push_back(f);
        const double interval = repeatInterval(ctx, h.rate);
        h.next += interval;
        if (h.next <= ctx.now)
            h.next = ctx.now + interval;
    }
    return out;
}

Fired KeyDispatcher::fireToken(int token, const KeyBinding& binding, ActionContext& ctx)
{
    Fired out;
    out.match = Match{Scope::Project, -1, &binding};
    if (binding.mode == Mode::Repeat && supportsRepeat(binding.action)) {
        if (tokenHeld(token))
            return out;
        return begin(out.match, binding, token, ctx);
    }
    if (binding.mode == Mode::Hold && supportsHold(binding.action)) {
        if (tokenHeld(token))
            return out;
        int previous = -1;
        out.result = runAction(ctx, binding.action, binding.args, &previous);
        if (previous >= 0) {
            out.hold = true;
            Held h{out.match, binding, previous, token};
            h.match.binding = nullptr;
            held_.push_back(std::move(h));
        }
        return out;
    }
    out.result = runAction(ctx, binding.action, binding.args);
    return out;
}

std::vector<Fired> KeyDispatcher::releaseToken(int token, ActionContext& ctx)
{
    std::vector<Fired> out;
    for (size_t i = 0; i < held_.size();) {
        if (held_[i].token != token) {
            ++i;
            continue;
        }
        Fired f;
        f.match = held_[i].match;
        f.hold = true;
        f.result = setActionState(ctx, held_[i].binding.action, held_[i].binding.args, held_[i].previous == 1);
        if (f.result.ran || !held_[i].repeat)
            out.push_back(f);
        held_.erase(held_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return out;
}

bool KeyDispatcher::tokenHeld(int token) const
{
    for (const Held& h : held_)
        if (h.token == token)
            return true;
    return false;
}

std::vector<Fired> KeyDispatcher::release(Key key, ActionContext& ctx)
{
    std::vector<Fired> out;
    for (size_t i = 0; i < held_.size();) {
        if (held_[i].token != 0 || held_[i].binding.chord.key != key) {
            ++i;
            continue;
        }
        Fired f;
        f.match = held_[i].match;
        f.hold = true;
        f.result = setActionState(ctx, held_[i].binding.action, held_[i].binding.args, held_[i].previous == 1);
        if (f.result.ran || !held_[i].repeat)
            out.push_back(f);
        held_.erase(held_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return out;
}

std::vector<Fired> KeyDispatcher::releaseUp(const std::function<bool(const KeyBinding&)>& isDown, ActionContext& ctx)
{
    std::vector<Fired> out;
    for (size_t i = 0; i < held_.size();) {
        if (held_[i].token != 0 || isDown(held_[i].binding)) {
            ++i;
            continue;
        }
        Fired f;
        f.match = held_[i].match;
        f.hold = true;
        f.result = setActionState(ctx, held_[i].binding.action, held_[i].binding.args, held_[i].previous == 1);
        if (f.result.ran || !held_[i].repeat)
            out.push_back(f);
        held_.erase(held_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return out;
}

bool KeyDispatcher::isHeld(Key key) const
{
    for (const Held& h : held_)
        if (h.token == 0 && h.binding.chord.key == key)
            return true;
    return false;
}

}
