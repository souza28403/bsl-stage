#pragma once

#include "control/ActionRegistry.h"
#include "control/KeyBinding.h"

#include <functional>
#include <optional>
#include <vector>

namespace bsl::control {

struct KeyGate
{
    bool textInput = false;
    bool mapEditor = false;
    bool popupOpen = false;
    bool busy = false;
    bool capturing = false;
    bool navKeys = false;
};

bool gateAllows(const KeyBinding& binding, const KeyGate& gate);

struct Fired
{
    Match match;
    ActionResult result;
    bool hold = false;
};

class KeyDispatcher
{
public:
    std::optional<Fired> press(const Chord& chord, const KeyBindingSet& app, const KeyBindingSet& project,
                               const KeyGate& gate, ActionContext& ctx);
    Fired fire(const Match& match, ActionContext& ctx);
    std::vector<Fired> release(Key key, ActionContext& ctx);
    std::vector<Fired> releaseUp(const std::function<bool(const KeyBinding&)>& isDown, ActionContext& ctx);

    Fired fireToken(int token, const KeyBinding& binding, ActionContext& ctx);
    std::vector<Fired> releaseToken(int token, ActionContext& ctx);
    bool tokenHeld(int token) const;

    std::vector<Fired> tick(ActionContext& ctx);

    bool isHeld(Key key) const;
    size_t heldCount() const { return held_.size(); }

private:
    struct Held
    {
        Match match;
        KeyBinding binding;
        int previous = -1;
        int token = 0;
        bool repeat = false;
        double next = 0.0;
        int rate = 4;
    };
    Fired begin(const Match& match, const KeyBinding& binding, int token, ActionContext& ctx);
    std::vector<Held> held_;
};

}
