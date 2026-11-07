#pragma once

#include "control/ActionArgs.h"
#include "control/KeyBinding.h"

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace bsl {
struct Composition;
}

namespace bsl::control {

struct SelectionRef
{
    enum class Kind { None, Layer, Clip };
    Kind kind = Kind::None;
    int layer = -1;
    int column = -1;

    bool operator==(const SelectionRef&) const = default;
};

class TapTempo
{
public:
    void tap(Composition& comp, double now);

private:
    std::vector<double> taps_;
};

struct AppHooks
{
    std::function<void(int)> project;
    std::function<void(int)> setMode;
    std::function<bool()> identify;
    std::function<void(bool)> setIdentify;
    std::function<bool(int)> layoutPreset;
    std::function<void()> clearClipSelection;
    std::function<int(const char*)> panelVisible;
    std::function<void(const char*, bool)> setPanelVisible;
    std::function<bool()> fullscreen;
    std::function<void(bool)> setFullscreen;
    std::function<bool()> layoutLocked;
    std::function<void(bool)> setLayoutLocked;
    std::function<void()> showAllPanels;
    std::function<void()> resetLayout;
};

struct ActionContext
{
    Composition& comp;
    SelectionRef selection;
    double now = 0.0;
    TapTempo* tap = nullptr;
    const AppHooks* app = nullptr;
};

enum ArgUse : unsigned {
    ArgNone = 0,
    ArgLayer = 1u << 0,
    ArgColumn = 1u << 1,
    ArgDeck = 1u << 2,
    ArgIndex = 1u << 3,
    ArgValue = 1u << 4,
    ArgEffect = 1u << 5,
};

struct ActionDef
{
    const char* id;
    const char* label;
    const char* group;
    unsigned args;
    bool show;
    Target::Kind targetKind;
    int (*run)(ActionContext&, const ActionArgs&);
    int (*get)(ActionContext&, const ActionArgs&);
    int (*set)(ActionContext&, const ActionArgs&, bool);

    bool isToggle() const { return get != nullptr && set != nullptr; }
};

std::span<const ActionDef> allActions();
const ActionDef* findAction(const std::string& id);

struct ActionResult
{
    int hit = 0;
    Target target;
    bool ran = false;
    explicit operator bool() const { return hit > 0; }
};

ActionResult runAction(ActionContext& ctx, const std::string& id, const ActionArgs& args, int* previousState = nullptr);
ActionResult setActionState(ActionContext& ctx, const std::string& id, const ActionArgs& args, bool on);

Target targetOf(const Composition& comp, const SelectionRef& selection, const ActionDef& def, const ActionArgs& args);

std::string describeAction(const Composition& comp, const ActionDef& def, const ActionArgs& args);

int stampLayerIds(KeyBindingSet& set, const Composition& comp);
bool stampLayerId(const ActionDef& def, ActionArgs& args, const Composition& comp);

bool actionForTarget(const Target& target, std::string& id, ActionArgs& args);

}
