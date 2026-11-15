#include "control/ActionRegistry.h"
#include "control/KeyBinding.h"
#include "control/KeyDispatcher.h"
#include "control/KeyService.h"
#include "control/SystemHotkeys.h"
#include "core/Composition.h"
#include "core/Generators.h"
#include "ui/ViewState.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace bsl;
using namespace bsl::control;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        ++g_checks;                                                                        \
        if (!(cond)) {                                                                     \
            ++g_failures;                                                                  \
            std::printf("  FALHOU (linha %d): %s\n", __LINE__, #cond);                     \
        }                                                                                  \
    } while (0)

void section(const char* name)
{
    std::printf("[%s]\n", name);
}

Composition makeComp()
{
    Composition comp = Composition::createDefault(3, 8);
    comp.width = 64;
    comp.height = 64;
    for (const auto& [l, c] : {std::pair{0, 0}, {0, 1}, {1, 0}, {1, 1}, {1, 2}, {2, 0}})
        comp.setClip(l, c, Clip::generator(kSolidColorId));
    return comp;
}

struct Rig
{
    Composition comp = makeComp();
    KeyService svc;
    KeyBindingSet app = defaultAppKeys();
    KeyBindingSet project;
    SelectionRef sel;
    KeyGate gate;
    std::set<Key> down;

    std::vector<std::string> calls;
    bool identify = false;
    std::vector<std::string> hiddenPanels;
    bool fullscreen = false;
    bool locked = false;

    Rig()
    {
        project = comp.keys;
        svc.hooks.project = [this](int what) { calls.push_back("project:" + std::to_string(what)); };
        svc.hooks.setMode = [this](int mode) { calls.push_back("mode:" + std::to_string(mode)); };
        svc.hooks.identify = [this] { return identify; };
        svc.hooks.setIdentify = [this](bool on) { identify = on; };
        svc.hooks.layoutPreset = [this](int i) {
            calls.push_back("preset:" + std::to_string(i));
            return i < 2;
        };
        svc.hooks.clearClipSelection = [this] { calls.push_back("clearsel"); };
        svc.hooks.panelVisible = [this](const char* key) {
            if (!ui::view::findPanel(key))
                return -1;
            return std::find(hiddenPanels.begin(), hiddenPanels.end(), key) == hiddenPanels.end() ? 1 : 0;
        };
        svc.hooks.setPanelVisible = [this](const char* key, bool on) {
            calls.push_back(std::string("panel:") + key + (on ? ":on" : ":off"));
            std::erase(hiddenPanels, std::string(key));
            if (!on)
                hiddenPanels.push_back(key);
        };
        svc.hooks.fullscreen = [this] { return fullscreen; };
        svc.hooks.setFullscreen = [this](bool on) { fullscreen = on; };
        svc.hooks.layoutLocked = [this] { return locked; };
        svc.hooks.setLayoutLocked = [this](bool on) { locked = on; };
        svc.hooks.showAllPanels = [this] {
            calls.push_back("showAll");
            hiddenPanels.clear();
        };
        svc.hooks.resetLayout = [this] { calls.push_back("resetLayout"); };
    }

    ActionContext ctx() { return svc.context(comp, sel); }

    std::optional<Fired> press(Key key, std::uint8_t mods = ModNone)
    {
        down.insert(key);
        ActionContext c = ctx();
        auto f = svc.dispatcher.press(Chord{key, mods}, app, project, gate, c);
        if (f)
            svc.note(*f);
        return f;
    }

    void release(Key key)
    {
        down.erase(key);
        ActionContext c = ctx();
        for (const Fired& f : svc.dispatcher.releaseUp([this](const KeyBinding& b) { return down.count(b.chord.key) > 0; }, c))
            svc.note(f);
    }

    bool played(int layer, int col) const
    {
        const Layer& l = comp.layers[static_cast<size_t>(layer)];
        return l.activeDeck == comp.currentDeck && l.activeColumn == col;
    }
};

KeyBinding bind(const char* action, Key key, std::uint8_t mods = ModNone, Mode mode = Mode::Press)
{
    KeyBinding b;
    b.action = action;
    b.chord = {key, mods};
    b.mode = mode;
    return b;
}

void testKeys()
{
    section("teclas e combinacoes");
    std::set<std::string> ids;
    bool allParse = true;
    for (int i = 1; i < static_cast<int>(Key::Count); ++i) {
        const Key k = static_cast<Key>(i);
        const std::string id = keyId(k);
        allParse = allParse && !id.empty() && keyFromId(id) == k && ids.insert(id).second && std::string(keyLabel(k)).size() > 0;
    }
    CHECK(allParse);
    CHECK(ids.size() == static_cast<size_t>(Key::Count) - 1);
    CHECK(std::string(keyId(Key::Count)).empty());
    CHECK(keyFromId("Q") == Key::Q && keyFromId("1") == Key::D1 && keyFromId("Space") == Key::Space
          && keyFromId("NumAdd") == Key::NumAdd && keyFromId("nada") == Key::None);
    CHECK(keyVirtualCode(Key::Q) == 'Q' && keyVirtualCode(Key::F5) == 0x74 && keyVirtualCode(Key::NumEnter) == 0);

    Chord c;
    CHECK(chordFromId("Ctrl+Shift+S", c) && c.key == Key::S && c.mods == (ModCtrl | ModShift));
    CHECK(chordId(c) == "Ctrl+Shift+S");
    CHECK(chordFromId("Alt+3", c) && c.key == Key::D3 && c.mods == ModAlt && chordLabel(c) == "Alt+3");
    CHECK(chordFromId("Ctrl+Alt+Shift+F12", c) && chordId(c) == "Ctrl+Alt+Shift+F12");
    CHECK(chordFromId("Ctrl+=", c) && c.key == Key::Equal && chordFromId("Space", c) && chordLabel(c) == "Espa\xC3\xA7o");
    CHECK(!chordFromId("Ctrl+", c) && !chordFromId("Foo+Q", c) && !chordFromId("", c));
    CHECK(keyIsReservedForCapture(Key::Escape) && !keyIsReservedForCapture(Key::Q));
}

void testDefaults()
{
    section("padroes e resolucao");
    const KeyBindingSet app = defaultAppKeys();
    const KeyBindingSet project = defaultProjectKeys();
    CHECK(app.items.size() == 14);
    CHECK(project.items.size() == 23);

    for (int i = 0; i < 10; ++i) {
        const Key k = i < 9 ? static_cast<Key>(static_cast<int>(Key::D1) + i) : Key::D0;
        const auto m = resolveKey({k, ModNone}, app, project);
        CHECK(m && m->scope == Scope::Project && m->binding->action == "column.trigger" && m->binding->args.column == i);
    }
    const Key cells[] = {Key::Q, Key::W, Key::E, Key::R, Key::T, Key::Y, Key::U, Key::I, Key::O, Key::P};
    for (int i = 0; i < 10; ++i) {
        const auto m = resolveKey({cells[i], ModNone}, app, project);
        CHECK(m && m->binding->action == "clip.trigger" && m->binding->args.selected && m->binding->args.column == i);
    }
    auto space = resolveKey({Key::Space, ModNone}, app, project);
    CHECK(space && space->binding->action == "transport.toggle");
    auto del = resolveKey({Key::Delete, ModNone}, app, project);
    CHECK(del && del->binding->action == "layer.clear" && del->binding->args.selected);
    auto b = resolveKey({Key::B, ModNone}, app, project);
    CHECK(b && b->binding->action == "master.blackout" && b->binding->mode == Mode::Hold);
    auto save = resolveKey({Key::S, ModCtrl}, app, project);
    CHECK(save && save->scope == Scope::App && save->binding->action == "project.save");
    CHECK(resolveKey({Key::S, ModCtrl | ModShift}, app, project)->binding->action == "project.saveAs");
    CHECK(resolveKey({Key::O, ModCtrl}, app, project)->binding->action == "project.open");
    CHECK(resolveKey({Key::N, ModCtrl}, app, project)->binding->action == "project.new");
    auto fs = resolveKey({Key::F11, ModNone}, app, project);
    CHECK(fs && fs->scope == Scope::App && fs->binding->action == "view.fullscreen");
    auto preset = resolveKey({Key::D3, ModAlt}, app, project);
    CHECK(preset && preset->binding->action == "ui.layoutPreset" && preset->binding->args.index == 2);
    CHECK(!resolveKey({Key::Z, ModNone}, app, project) && !resolveKey({Key::None, ModNone}, app, project));
    CHECK(!resolveKey({Key::Q, ModShift}, app, project));
    bool valid = true;
    for (const KeyBindingSet* set : {&app, &project})
        for (const KeyBinding& k : set->items)
            valid = valid && findAction(k.action) && (k.mode == Mode::Press || supportsHold(k.action));
    CHECK(valid);
    CHECK(Composition::createDefault().keys == defaultProjectKeys() && Composition{}.keys == defaultProjectKeys());
}

void testRegistry()
{
    section("registro de acoes");
    std::set<std::string> ids;
    bool ok = true;
    for (const ActionDef& a : allActions()) {
        ok = ok && ids.insert(a.id).second && a.label && a.label[0] && a.group && a.group[0] && (a.isToggle() || a.run);
    }
    CHECK(ok);
    for (const char* id : {"clip.trigger", "column.trigger", "layer.clear", "all.clear", "layer.bypass", "layer.solo",
                           "layer.opacity.toggle", "layer.opacity.set", "layer.power", "master.opacity.toggle",
                           "master.opacity.set", "master.bypass", "master.blackout", "deck.next", "deck.prev", "deck.goto",
                           "bpm.tap", "bpm.half", "bpm.double", "bpm.resync", "transport.play", "transport.pause",
                           "transport.stop", "doc.next", "doc.prev", "doc.first", "doc.last", "preview.live", "effect.bypass",
                           "ui.layoutPreset", "mode.composition", "mode.mapping", "mode.pixelmap", "mode.sequences", "output.identify",
                           "project.save"})
        CHECK(findAction(id) != nullptr);
    CHECK(findAction("nao.existe") == nullptr);
    CHECK(supportsHold("master.blackout") && supportsHold("layer.power") && !supportsHold("clip.trigger")
          && !supportsHold("project.save") && !supportsHold("nao.existe"));
    CHECK(findAction("clip.trigger")->show && !findAction("project.save")->show && !findAction("ui.layoutPreset")->show);
    CHECK(defaultScopeFor("clip.trigger") == Scope::Project && defaultScopeFor("mode.mapping") == Scope::App);

    Composition comp = Composition::createDefault(3, 8);
    comp.layers[1].name = "Overlay";
    ActionArgs a;
    a.layer = 1;
    a.column = 2;
    CHECK(describeAction(comp, *findAction("clip.trigger"), a) == "Disparar clip - camada 2 (Overlay), coluna 3");
    a.selected = true;
    a.column = -1;
    CHECK(describeAction(comp, *findAction("clip.trigger"), a) == "Disparar clip - camada selecionada, clip selecionado");
    CHECK(describeAction(comp, *findAction("master.blackout"), {}) == "Blackout");
    std::string id;
    ActionArgs args;
    CHECK(actionForTarget(Target::cell(0, 1, 2), id, args) && id == "clip.trigger" && args.layer == 1 && args.column == 2
          && args.deck == 0);
    CHECK(actionForTarget(Target::layerOf(2), id, args) && id == "layer.power" && args.layer == 2);
    CHECK(actionForTarget(Target::columnOf(0, 4), id, args) && id == "column.trigger" && args.column == 4);
    CHECK(actionForTarget(Target::effectOf(2, -1, -1, -1, 1), id, args) && id == "effect.bypass" && args.fxScope == 2
          && args.effect == 1);
    CHECK(!actionForTarget(Target{}, id, args));
}

void testScopes()
{
    section("conflitos e escopos app x projeto");
    KeyBindingSet app, project;
    app.items.push_back(bind("clip.trigger", Key::D1));
    project.items.push_back(bind("column.trigger", Key::D1));
    app.items.push_back(bind("project.save", Key::F5));
    project.items.push_back(bind("column.trigger", Key::F5));
    app.items.push_back(bind("mode.mapping", Key::F6));
    project.items.push_back(bind("project.open", Key::F6));
    app.items.push_back(bind("mode.pixelmap", Key::F7));

    auto m1 = resolveKey({Key::D1, ModNone}, app, project);
    CHECK(m1 && m1->scope == Scope::Project && m1->binding->action == "column.trigger");
    auto m2 = resolveKey({Key::F5, ModNone}, app, project);
    CHECK(m2 && m2->scope == Scope::Project && m2->binding->action == "column.trigger");
    auto m3 = resolveKey({Key::F6, ModNone}, app, project);
    CHECK(m3 && m3->scope == Scope::App && m3->binding->action == "mode.mapping");
    auto m4 = resolveKey({Key::F7, ModNone}, app, project);
    CHECK(m4 && m4->scope == Scope::App);

    const auto c1 = findConflicts({Key::D1, ModNone}, app, project);
    CHECK(c1.size() == 2);
    CHECK(findConflicts({Key::D1, ModNone}, app, project, static_cast<int>(Scope::Project), 0).size() == 1);
    CHECK(findConflicts({Key::Z, ModNone}, app, project).empty());
    CHECK(findConflicts({Key::None, ModNone}, app, project).empty());
    project.items.push_back(bind("layer.clear", Key::D1));
    CHECK(findConflicts({Key::D1, ModNone}, app, project).size() == 3);
    ActionArgs col;
    col.column = 0;
    CHECK(project.find("column.trigger", col) == 0 && project.find("column.trigger", ActionArgs{}) == 0);
    col.column = 5;
    CHECK(project.find("column.trigger", col) == -1);

    project.items.erase(project.items.begin());
    auto m5 = resolveKey({Key::D1, ModNone}, app, project);
    CHECK(m5 && m5->binding->action == "layer.clear");
    project.items.pop_back();
    auto m6 = resolveKey({Key::D1, ModNone}, app, project);
    CHECK(m6 && m6->scope == Scope::App && m6->binding->action == "clip.trigger");
}

void testGate()
{
    section("foco: campo de texto, editor de Mapeamento, popup");
    const KeyBinding show = bind("clip.trigger", Key::Q);
    const KeyBinding showCtrl = bind("clip.trigger", Key::Q, ModCtrl);
    const KeyBinding appPlain = bind("mode.mapping", Key::F6);
    const KeyBinding appCtrl = bind("project.save", Key::S, ModCtrl);
    const KeyBinding space = bind("transport.toggle", Key::Space);
    KeyGate g;
    CHECK(gateAllows(show, g) && gateAllows(appPlain, g) && gateAllows(appCtrl, g));
    g.textInput = true;
    CHECK(!gateAllows(show, g) && !gateAllows(showCtrl, g) && !gateAllows(appPlain, g) && gateAllows(appCtrl, g));
    g = {};
    g.mapEditor = true;
    CHECK(!gateAllows(show, g) && !gateAllows(showCtrl, g) && !gateAllows(appPlain, g) && gateAllows(appCtrl, g));
    g = {};
    g.popupOpen = true;
    CHECK(!gateAllows(show, g) && gateAllows(appPlain, g) && gateAllows(appCtrl, g));
    g = {};
    g.busy = true;
    CHECK(!gateAllows(show, g) && !gateAllows(appCtrl, g));
    g = {};
    g.capturing = true;
    CHECK(!gateAllows(show, g) && !gateAllows(appCtrl, g));
    g = {};
    g.navKeys = true;
    CHECK(!gateAllows(space, g) && gateAllows(show, g));
    CHECK(!gateAllows(bind("x.desconhecida", Key::Q), KeyGate{}));
}

void testActions()
{
    section("acoes mudam a Composition");
    Rig r;

    auto f = r.press(Key::D1);
    CHECK(f && f->result.hit == 3 && f->match.binding->action == "column.trigger");
    CHECK(r.played(0, 0) && r.played(1, 0) && r.played(2, 0));
    CHECK(f->result.target == Target::columnOf(0, 0));
    f = r.press(Key::D2);
    CHECK(f && f->result.hit == 2 && r.played(0, 1) && r.played(1, 1) && r.played(2, 0));
    f = r.press(Key::D8);
    CHECK(f && f->result.hit == 0 && r.played(0, 1) && r.played(1, 1));
    f = r.press(Key::D9);
    CHECK(f && f->result.hit == 0);

    r.comp.stopAll();
    f = r.press(Key::Q);
    CHECK(f && f->result.hit == 0 && !r.played(0, 0) && !r.played(1, 0));
    r.sel = {SelectionRef::Kind::Layer, 1, -1};
    f = r.press(Key::W);
    CHECK(f && f->result.hit == 1 && r.played(1, 1) && !r.played(0, 1));
    CHECK(f->result.target == Target::cell(0, 1, 1));
    f = r.press(Key::E);
    CHECK(f && f->result.hit == 1 && r.played(1, 2));
    r.sel = {SelectionRef::Kind::Clip, 0, 0};
    f = r.press(Key::Q);
    CHECK(f && f->result.hit == 1 && r.played(0, 0));
    r.sel = {SelectionRef::Kind::Layer, 2, -1};
    f = r.press(Key::R);
    CHECK(f && f->result.hit == 0);

    r.sel = {SelectionRef::Kind::Layer, 1, -1};
    CHECK(r.played(1, 2) && r.played(0, 0));
    f = r.press(Key::Delete);
    CHECK(f && f->result.hit == 1 && r.comp.layers[1].activeColumn == -1 && r.played(0, 0));
    CHECK(f->result.target == Target::layerOf(1));
    r.sel = {};
    f = r.press(Key::Delete);
    CHECK(f && f->result.hit == 0 && r.played(0, 0));

    r.project.items.push_back(bind("all.clear", Key::F9));
    f = r.press(Key::F9);
    CHECK(f && f->result.hit >= 1 && !r.played(0, 0));

    r.comp.trigger(0, 0);
    CHECK(!r.comp.paused);
    f = r.press(Key::Space);
    CHECK(f && r.comp.paused);
    f = r.press(Key::Space);
    CHECK(f && !r.comp.paused);

    {
        KeyBinding b = bind("layer.power", Key::F1);
        b.args.layer = 2;
        r.project.items.push_back(b);
        CHECK(!r.comp.layers[2].bypass);
        f = r.press(Key::F1);
        CHECK(f && f->result.hit == 1 && r.comp.layers[2].bypass && f->result.target == Target::layerOf(2));
        f = r.press(Key::F1);
        CHECK(f && !r.comp.layers[2].bypass);
        r.project.items.back().args.layer = 9;
        f = r.press(Key::F1);
        CHECK(f && f->result.hit == 0 && !r.comp.layers[2].bypass);
        r.project.items.back().args.layer = 2;
    }
    {
        Rig q;
        const int idA = q.comp.layers[0].id, idB = q.comp.layers[1].id;
        KeyBinding b = bind("layer.bypass", Key::F5);
        b.args.layer = 1;
        q.project.items.push_back(b);
        CHECK(stampLayerIds(q.project, q.comp) == 1 && q.project.items.back().args.layerId == idB && q.project.items.back().args.layer == 0);
        CHECK(stampLayerIds(q.project, q.comp) == 0);
        std::swap(q.comp.layers[0], q.comp.layers[1]);
        auto fq = q.press(Key::F5);
        CHECK(fq && fq->result.hit == 1 && q.comp.layers[0].bypass && !q.comp.layers[1].bypass && fq->result.target == Target::layerOf(0));
        CHECK(q.comp.layers[0].id == idB && q.comp.layers[1].id == idA);
        const ActionDef* def = findAction("layer.bypass");
        CHECK(def && describeAction(q.comp, *def, q.project.items.back().args).find("camada 1") != std::string::npos);
        q.comp.layers.erase(q.comp.layers.begin());
        for (Deck& d : q.comp.decks)
            d.clips.erase(d.clips.begin());
        q.comp.layers[0].bypass = false;
        fq = q.press(Key::F5);
        CHECK(fq && fq->result.hit == 0 && !q.comp.layers[0].bypass);
        CHECK(describeAction(q.comp, *def, q.project.items.back().args).find("removida") != std::string::npos);
        const nlohmann::json j = bindingsToJson(q.project);
        const std::string text = j.dump();
        CHECK(text.find("\"layerId\"") != std::string::npos && text.find("\"layer\":") == std::string::npos);
        CHECK(bindingsFromJson(j) == q.project);
    }
    {
        Rig q;
        q.comp.width = 160;
        q.comp.height = 90;
        q.comp.layers[1].transform = LayerTransform::rect(10, 10, 50, 50);
        KeyBinding reset = bind("layer.transform.reset", Key::F6);
        reset.args.layer = 1;
        KeyBinding preset = bind("layer.transform.preset", Key::F7);
        preset.args.layer = 1;
        preset.args.index = static_cast<int>(TransformPreset::Quad4);
        KeyBinding grid = bind("layer.transform.preset", Key::F8);
        grid.args.index = static_cast<int>(TransformPreset::Grid2x2);
        q.project.items.push_back(reset);
        q.project.items.push_back(preset);
        q.project.items.push_back(grid);
        stampLayerIds(q.project, q.comp);
        auto fq = q.press(Key::F6);
        CHECK(fq && fq->result.hit == 1 && q.comp.layers[1].transform.isDefault() && fq->result.target == Target::layerOf(1));
        fq = q.press(Key::F7);
        CHECK(fq && fq->result.hit == 1 && q.comp.layers[1].transform.x == 80 && q.comp.layers[1].transform.y == 45);
        fq = q.press(Key::F8);
        CHECK(fq && fq->result.hit == 3 && !q.comp.layers[0].transform.isDefault() && q.comp.layers[2].transform.y == 45);
        const ActionDef* def = findAction("layer.transform.preset");
        CHECK(def && describeAction(q.comp, *def, preset.args).find("Quadrante 4") != std::string::npos);
    }
    r.sel = {SelectionRef::Kind::Layer, 0, -1};
    {
        KeyBinding b = bind("layer.bypass", Key::F2);
        b.args.selected = true;
        r.project.items.push_back(b);
        KeyBinding s = bind("layer.solo", Key::F3);
        s.args.selected = true;
        r.project.items.push_back(s);
        KeyBinding o = bind("layer.opacity.toggle", Key::F4);
        o.args.selected = true;
        r.project.items.push_back(o);
        KeyBinding v = bind("layer.opacity.set", Key::F8);
        v.args.selected = true;
        v.args.value = 0.35f;
        r.project.items.push_back(v);
        r.press(Key::F2);
        CHECK(r.comp.layers[0].bypass);
        r.press(Key::F2);
        CHECK(!r.comp.layers[0].bypass);
        r.press(Key::F3);
        CHECK(r.comp.layers[0].solo && !r.comp.layers[1].solo);
        r.press(Key::F3);
        CHECK(!r.comp.layers[0].solo);
        CHECK(r.comp.layers[0].opacity == 1.0f);
        r.press(Key::F4);
        CHECK(r.comp.layers[0].opacity == 0.0f);
        r.press(Key::F4);
        CHECK(r.comp.layers[0].opacity == 1.0f);
        r.press(Key::F8);
        CHECK(std::fabs(r.comp.layers[0].opacity - 0.35f) < 1e-6f);
        r.sel = {SelectionRef::Kind::Layer, 1, -1};
        r.press(Key::F2);
        CHECK(r.comp.layers[1].bypass && r.comp.layers[0].opacity > 0.3f && !r.comp.layers[0].bypass);
    }

    {
        r.project.items.push_back(bind("master.opacity.toggle", Key::F10));
        KeyBinding mv = bind("master.opacity.set", Key::PageUp);
        mv.args.value = 0.5f;
        r.project.items.push_back(mv);
        r.project.items.push_back(bind("master.bypass", Key::F12));
        r.press(Key::F10);
        CHECK(r.comp.masterOpacity == 0.0f);
        r.press(Key::F10);
        CHECK(r.comp.masterOpacity == 1.0f);
        r.press(Key::PageUp);
        CHECK(r.comp.masterOpacity == 0.5f);
        r.press(Key::F12);
        CHECK(r.comp.masterBypass && r.comp.outputOff());
        r.press(Key::F12);
        CHECK(!r.comp.masterBypass && !r.comp.outputOff());
    }

    {
        r.comp.addDeck();
        r.comp.addDeck();
        r.project.items.push_back(bind("deck.next", Key::Right));
        r.project.items.push_back(bind("deck.prev", Key::Left));
        KeyBinding g = bind("deck.goto", Key::Up);
        g.args.index = 2;
        r.project.items.push_back(g);
        r.calls.clear();
        r.press(Key::Right);
        CHECK(r.comp.currentDeck == 1 && r.calls == std::vector<std::string>{"clearsel"});
        r.press(Key::Right);
        r.press(Key::Right);
        CHECK(r.comp.currentDeck == 0);
        r.press(Key::Left);
        CHECK(r.comp.currentDeck == 2);
        r.press(Key::Left);
        CHECK(r.comp.currentDeck == 1);
        r.press(Key::Up);
        CHECK(r.comp.currentDeck == 2);
        g.args.index = 7;
        r.project.items.back() = g;
        r.press(Key::Up);
        CHECK(r.comp.currentDeck == 2);
        r.comp.currentDeck = 0;
    }

    {
        r.comp.setClip(0, 0, Clip::generator(kSolidColorId));
        r.comp.currentDeck = 1;
        r.comp.setClip(1, 3, Clip::generator(kSolidColorId));
        r.comp.currentDeck = 0;
        KeyBinding b = bind("clip.trigger", Key::F5);
        b.args.deck = 1;
        b.args.layer = 1;
        b.args.column = 3;
        r.project.items.push_back(b);
        r.comp.stopAll();
        f = r.press(Key::F5);
        CHECK(f && f->result.hit == 1 && r.comp.currentDeck == 0);
        CHECK(r.comp.layers[1].activeDeck == 1 && r.comp.layers[1].activeColumn == 3);
        CHECK(f->result.target == Target::cell(1, 1, 3));
        r.comp.stopAll();
    }

    {
        r.project.items.push_back(bind("bpm.tap", Key::T, ModCtrl));
        r.project.items.push_back(bind("bpm.half", Key::H, ModCtrl));
        r.project.items.push_back(bind("bpm.double", Key::D, ModCtrl));
        r.project.items.push_back(bind("bpm.resync", Key::R, ModCtrl));
        r.comp.bpm = 120.0f;
        r.press(Key::H, ModCtrl);
        CHECK(r.comp.bpm == 60.0f);
        r.press(Key::D, ModCtrl);
        r.press(Key::D, ModCtrl);
        CHECK(r.comp.bpm == 240.0f);
        r.press(Key::D, ModCtrl);
        CHECK(r.comp.bpm == 300.0f);
        r.comp.beat = 3.7;
        r.press(Key::R, ModCtrl);
        CHECK(r.comp.beat == 0.0);
        for (int i = 0; i < 4; ++i) {
            ActionContext c = r.ctx();
            c.now = 10.0 + 0.5 * i;
            runAction(c, "bpm.tap", {});
        }
        CHECK(std::fabs(r.comp.bpm - 120.0f) < 0.1f);
        ActionContext c = r.ctx();
        c.now = 100.0;
        runAction(c, "bpm.tap", {});
        CHECK(std::fabs(r.comp.bpm - 120.0f) < 0.1f);
    }

    {
        r.comp.trigger(0, 0);
        ActionContext c = r.ctx();
        CHECK(runAction(c, "transport.pause", {}).hit == 1 && r.comp.paused);
        CHECK(runAction(c, "transport.play", {}).hit == 1 && !r.comp.paused);
        r.comp.beat = 2.0;
        CHECK(runAction(c, "transport.stop", {}).hit == 1 && r.comp.beat == 0.0 && !r.played(0, 0));
    }

    {
        r.comp.layers[0].effects.add("native:blur");
        r.comp.layers[0].effects.add("native:invert");
        KeyBinding b = bind("effect.bypass", Key::F6);
        b.args.fxScope = 1;
        b.args.layer = 0;
        b.args.effect = 1;
        r.project.items.push_back(b);
        CHECK(!r.comp.layers[0].effects.items[1].bypass);
        f = r.press(Key::F6);
        CHECK(f && f->result.hit == 1 && r.comp.layers[0].effects.items[1].bypass && !r.comp.layers[0].effects.items[0].bypass);
        CHECK(f->result.target == Target::effectOf(1, -1, 0, -1, 1));
        f = r.press(Key::F6);
        CHECK(f && !r.comp.layers[0].effects.items[1].bypass);
        r.comp.masterEffects.add("native:vignette");
        ActionArgs m;
        m.fxScope = 2;
        ActionContext c = r.ctx();
        CHECK(runAction(c, "effect.bypass", m).hit == 1 && r.comp.masterEffects.items[0].bypass);
        r.comp.clip(1, 0).effects.add("native:blur");
        ActionArgs cl;
        cl.fxScope = 0;
        cl.layer = 1;
        cl.column = 0;
        CHECK(runAction(c, "effect.bypass", cl).hit == 1 && r.comp.clip(1, 0).effects.items[0].bypass);
        cl.effect = 5;
        CHECK(runAction(c, "effect.bypass", cl).hit == 0);
    }

    {
        ActionContext c = r.ctx();
        CHECK(runAction(c, "doc.next", {}).hit == 0);
        r.sel = {SelectionRef::Kind::Clip, 1, 2};
        r.comp.stopAll();
        CHECK(!r.comp.clip(1, 2).isOpen());
        ActionContext c2 = r.ctx();
        CHECK(runAction(c2, "preview.live", {}).hit == 1 && r.comp.clip(1, 2).isOpen() && r.played(1, 2));
        CHECK(runAction(c2, "preview.live", {}).hit == 0);
        r.sel = {SelectionRef::Kind::Clip, 2, 5};
        ActionContext c3 = r.ctx();
        CHECK(runAction(c3, "preview.live", {}).hit == 0);
    }

    {
        r.calls.clear();
        f = r.press(Key::S, ModCtrl);
        CHECK(f && f->result.hit == 1 && r.calls == std::vector<std::string>{"project:2"});
        r.press(Key::S, ModCtrl | ModShift);
        r.press(Key::O, ModCtrl);
        r.press(Key::N, ModCtrl);
        CHECK(r.calls == (std::vector<std::string>{"project:2", "project:3", "project:1", "project:0"}));
        r.calls.clear();
        r.press(Key::D1, ModAlt);
        r.press(Key::D3, ModAlt);
        CHECK(r.calls == (std::vector<std::string>{"preset:0", "preset:2"}));
        r.app.items.push_back(bind("mode.mapping", Key::F1, ModAlt));
        r.app.items.push_back(bind("mode.pixelmap", Key::F2, ModAlt));
        r.app.items.push_back(bind("mode.composition", Key::F3, ModAlt));
        r.app.items.push_back(bind("output.identify", Key::F4, ModAlt));
        r.calls.clear();
        r.press(Key::F1, ModAlt);
        r.press(Key::F2, ModAlt);
        r.press(Key::F3, ModAlt);
        CHECK(r.calls == (std::vector<std::string>{"mode:1", "mode:2", "mode:0"}));
        CHECK(!r.identify);
        r.press(Key::F4, ModAlt);
        CHECK(r.identify);
        r.press(Key::F4, ModAlt);
        CHECK(!r.identify);
        Rig bare;
        bare.svc.hooks = AppHooks{};
        auto none = bare.press(Key::S, ModCtrl);
        CHECK(none && none->result.hit == 0);
    }
}

void testHold()
{
    section("pressionar x segurar");
    Rig r;
    CHECK(!r.comp.blackout);
    auto f = r.press(Key::B);
    CHECK(f && f->hold && r.comp.blackout && r.comp.outputOff() && r.svc.dispatcher.isHeld(Key::B));
    CHECK(f->result.target == Target::master());
    auto again = r.press(Key::B);
    CHECK(again && !again->hold && again->result.hit == 0 && r.comp.blackout && r.svc.dispatcher.heldCount() == 1);
    r.release(Key::Z);
    CHECK(r.comp.blackout);
    r.release(Key::B);
    CHECK(!r.comp.blackout && r.svc.dispatcher.heldCount() == 0);
    r.project.items.back().mode = Mode::Press;
    r.press(Key::B);
    r.release(Key::B);
    CHECK(r.comp.blackout && r.svc.dispatcher.heldCount() == 0);
    r.press(Key::B);
    r.release(Key::B);
    CHECK(!r.comp.blackout);
    r.project.items.back().mode = Mode::Hold;

    KeyBinding b = bind("layer.power", Key::F, ModNone, Mode::Hold);
    b.args.layer = 1;
    r.project.items.push_back(b);
    r.press(Key::F);
    CHECK(r.comp.layers[1].bypass);
    r.release(Key::F);
    CHECK(!r.comp.layers[1].bypass);
    r.comp.layers[1].bypass = true;
    r.press(Key::F);
    CHECK(!r.comp.layers[1].bypass);
    r.release(Key::F);
    CHECK(r.comp.layers[1].bypass);
    r.comp.layers[1].bypass = false;

    r.press(Key::B);
    r.down.clear();
    r.release(Key::Q);
    CHECK(!r.comp.blackout);

    r.project.items.push_back(bind("clip.trigger", Key::G, ModNone, Mode::Hold));
    r.project.items.back().args.layer = 0;
    r.project.items.back().args.column = 0;
    f = r.press(Key::G);
    CHECK(f && !f->hold && r.played(0, 0) && r.svc.dispatcher.heldCount() == 0);
    nlohmann::json j = bindingsToJson(r.project);
    KeyBindingSet back = bindingsFromJson(j);
    bool clipHold = false;
    for (const KeyBinding& k : back.items)
        clipHold = clipHold || (k.action == "clip.trigger" && k.mode == Mode::Hold);
    CHECK(!clipHold);
}

void testDispatchGate()
{
    section("despacho respeita o foco");
    Rig r;
    r.sel = {SelectionRef::Kind::Layer, 1, -1};
    r.gate.textInput = true;
    CHECK(!r.press(Key::Q) && !r.press(Key::D1) && !r.press(Key::Space) && !r.press(Key::B) && !r.press(Key::Delete));
    CHECK(!r.played(1, 0) && !r.played(0, 0) && !r.comp.paused && !r.comp.blackout);
    r.calls.clear();
    CHECK(r.press(Key::S, ModCtrl));
    CHECK(r.calls == std::vector<std::string>{"project:2"});
    r.gate = {};
    r.gate.mapEditor = true;
    CHECK(!r.press(Key::Q) && !r.press(Key::D2) && !r.press(Key::B));
    CHECK(!r.played(1, 0) && !r.comp.blackout);
    r.gate = {};
    r.gate.popupOpen = true;
    CHECK(!r.press(Key::Q));
    r.gate = {};
    r.gate.capturing = true;
    CHECK(!r.press(Key::Q) && !r.press(Key::S, ModCtrl));
    r.gate = {};
    CHECK(r.press(Key::Q) && r.played(1, 0));
    r.press(Key::B);
    CHECK(r.comp.blackout);
    r.gate.textInput = true;
    r.release(Key::B);
    CHECK(!r.comp.blackout);
}

void testFlash()
{
    section("flash e resumo");
    Rig r;
    r.sel = {SelectionRef::Kind::Layer, 0, -1};
    r.press(Key::Q);
    CHECK(r.svc.info().firedCount == 1 && r.svc.info().lastAction == "clip.trigger" && r.svc.info().lastKey == "Q");
    const Target cell = Target::cell(0, 0, 0);
    CHECK(r.svc.flash(cell) > 0.5f && r.svc.flash(Target::cell(0, 0, 1)) == 0.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    CHECK(r.svc.flash(cell) == 0.0f);
    r.press(Key::D1);
    CHECK(r.svc.flash(Target::columnOf(0, 0)) > 0.5f);
    r.press(Key::D9);
    CHECK(r.svc.flash(Target::columnOf(0, 8)) == 0.0f);
}

void testFiles()
{
    section("JSON e .bslkeys");
    KeyBindingSet set;
    {
        KeyBinding a = bind("clip.trigger", Key::J);
        a.args.deck = 1;
        a.args.layer = 2;
        a.args.column = 5;
        set.items.push_back(a);
        KeyBinding b = bind("master.blackout", Key::B, ModCtrl | ModShift, Mode::Hold);
        b.global = true;
        set.items.push_back(b);
        KeyBinding c = bind("layer.opacity.set", Key::F8);
        c.args.selected = true;
        c.args.value = 0.25f;
        set.items.push_back(c);
        KeyBinding d = bind("effect.bypass", Key::K);
        d.args.fxScope = 0;
        d.args.deck = 0;
        d.args.layer = 1;
        d.args.column = 3;
        d.args.effect = 2;
        set.items.push_back(d);
        KeyBinding e = bind("ui.layoutPreset", Key::D4, ModAlt);
        e.args.index = 3;
        set.items.push_back(e);
        set.items.push_back(bind("transport.toggle", Key::None));
    }
    CHECK(bindingsFromJson(bindingsToJson(set)) == set);
    CHECK(bindingsFromJson(bindingsToJson(defaultAppKeys())) == defaultAppKeys());
    CHECK(bindingsFromJson(bindingsToJson(defaultProjectKeys())) == defaultProjectKeys());
    const nlohmann::json j = bindingsToJson(defaultProjectKeys());
    CHECK(j.is_array() && j[0]["action"] == "column.trigger" && !j[0].contains("args") && !j[0].contains("global"));
    CHECK(j[1]["args"] == nlohmann::json({{"column", 1}}));
    CHECK(j[j.size() - 1]["mode"] == "hold" && !j[j.size() - 1].contains("args"));

    nlohmann::json bad = nlohmann::json::array();
    bad.push_back({{"action", "clip.trigger"}, {"key", "Q"}});
    bad.push_back({{"action", "acao.futura"}, {"key", "W"}});
    bad.push_back({{"action", "clip.trigger"}, {"key", "Ctrl+Teclado9"}});
    bad.push_back({{"nada", 1}});
    bad.push_back(5);
    bad.push_back({{"action", "clip.trigger"}, {"key", "E"}, {"mode", "hold"}, {"args", {{"layer", -3}, {"column", "x"}}}});
    bad.push_back({{"action", "master.blackout"}, {"key", "F"}, {"mode", "hold"}, {"args", {{"value", 7}}}});
    std::vector<std::string> warnings;
    const KeyBindingSet loaded = bindingsFromJson(bad, &warnings);
    CHECK(loaded.items.size() == 3 && warnings.size() == 2);
    CHECK(loaded.items[1].mode == Mode::Press && loaded.items[1].args.layer == 0 && loaded.items[1].args.column == 0);
    CHECK(loaded.items[2].mode == Mode::Hold && loaded.items[2].args.value == 1.0f);
    CHECK(bindingsFromJson(nlohmann::json::object()).items.empty() && bindingsFromJson(nlohmann::json(3)).items.empty());

    const KeyBindingSet app = defaultAppKeys();
    const std::string text = keysFileText(app, set);
    KeyBindingSet app2, project2;
    std::string error;
    CHECK(parseKeysFile(text, app2, project2, &error) && app2 == app && project2 == set);
    CHECK(text.find("\"format\": \"bslkeys\"") != std::string::npos);
    CHECK(parseKeysFile("\xEF\xBB\xBF" + text, app2, project2, &error));
    CHECK(!parseKeysFile("{nao e json", app2, project2, &error) && !error.empty());
    CHECK(!parseKeysFile("{\"format\":\"outro\"}", app2, project2, &error) && error.find("bslkeys") != std::string::npos);
    CHECK(!parseKeysFile("[1,2]", app2, project2, &error));
    warnings.clear();
    CHECK(parseKeysFile("{\"format\":\"bslkeys\",\"version\":99,\"app\":[],\"project\":[]}", app2, project2, &error, &warnings)
          && warnings.size() == 1 && app2.items.empty() && project2.items.empty());

    const fs::path file = fs::temp_directory_path() / "bslstage_keystest.bslkeys";
    CHECK(saveKeysFile(file, app, set, &error));
    KeyBindingSet app3, project3;
    CHECK(loadKeysFile(file, app3, project3, &error) && app3 == app && project3 == set);
    std::error_code ec;
    fs::remove(file, ec);
    CHECK(!loadKeysFile(file, app3, project3, &error));
    CHECK(!saveKeysFile(fs::temp_directory_path() / "pasta-que-nao-existe" / "x.bslkeys", app, set, &error));
}

#if BSL_WITH_HOTKEYS
void testSystemHotkeys()
{
    section("atalhos globais do sistema");
    const Chord chord{Key::F11, ModCtrl | ModAlt | ModShift};
    CHECK(SystemHotkeys::probe(chord).empty());
    {
        SystemHotkeys hk;
        hk.sync({{chord, 7}});
        CHECK(hk.registeredCount() == 1 && hk.error(7).empty());
        CHECK(!SystemHotkeys::probe(chord).empty());
        hk.sync({{chord, 7}});
        CHECK(hk.registeredCount() == 1);
        hk.sync({{chord, 7}, {chord, 8}});
        CHECK(hk.registeredCount() == 1 && !hk.error(8).empty());
        hk.sync({});
        CHECK(hk.registeredCount() == 0);
        hk.sync({{Chord{Key::NumEnter, ModCtrl}, 9}});
        CHECK(hk.registeredCount() == 0 && !hk.error(9).empty());
        hk.sync({{chord, 7}});
        CHECK(hk.poll().empty());
    }
    CHECK(SystemHotkeys::probe(chord).empty());

    Rig r;
    KeyBinding g = bind("master.blackout", Key::F11, ModCtrl | ModAlt | ModShift);
    g.global = true;
    r.project.items.push_back(g);
    r.svc.pollSystem(r.comp, r.sel, r.app, r.project, true);
    CHECK(r.svc.system.registeredCount() == 0);
    r.svc.pollSystem(r.comp, r.sel, r.app, r.project, false);
    CHECK(r.svc.system.registeredCount() == 1 && r.svc.systemError(Scope::Project, static_cast<int>(r.project.items.size()) - 1).empty());
    r.svc.pollSystem(r.comp, r.sel, r.app, r.project, true);
    CHECK(r.svc.system.registeredCount() == 0);
    r.project.items.back().global = false;
    r.svc.pollSystem(r.comp, r.sel, r.app, r.project, false);
    CHECK(r.svc.system.registeredCount() == 0);
}
#endif

}

void testView()
{
    section("menu Exibir: painéis, tela cheia e trava");
    bool all = true;
    for (int i = 0; i < ui::view::kPanelCount; ++i) {
        const std::string id = std::string("view.panel.") + ui::view::kPanels[i].key;
        const ActionDef* def = findAction(id);
        all = all && def && def->isToggle() && !def->show && std::string(def->group) == "Exibir";
        bool hasDefault = false;
        for (const KeyBinding& b : defaultAppKeys().items)
            hasDefault = hasDefault || b.action == id;
        all = all && !hasDefault;
    }
    CHECK(all);
    for (const char* id : {"view.fullscreen", "view.lock", "view.showAll", "view.resetLayout"})
        CHECK(findAction(id) != nullptr);
    CHECK(supportsHold("view.fullscreen") && !supportsHold("view.showAll"));
    CHECK(defaultScopeFor("view.panel.saida") == Scope::App && defaultScopeFor("view.fullscreen") == Scope::App);

    Rig r;
    r.app.items.push_back(bind("view.panel.previa", Key::F2));
    r.app.items.push_back(bind("view.panel.status", Key::F3));
    r.app.items.push_back(bind("view.lock", Key::F4));
    r.app.items.push_back(bind("view.showAll", Key::F6));
    r.app.items.push_back(bind("view.resetLayout", Key::F7));
    auto f = r.press(Key::F2);
    CHECK(f && f->result.hit == 1 && r.hiddenPanels == std::vector<std::string>{"previa"});
    f = r.press(Key::F2);
    CHECK(f && f->result.hit == 1 && r.hiddenPanels.empty());
    r.press(Key::F2);
    r.press(Key::F3);
    CHECK(r.hiddenPanels.size() == 2);
    r.press(Key::F6);
    CHECK(r.hiddenPanels.empty() && r.calls.back() == "showAll");
    f = r.press(Key::F11);
    CHECK(f && f->match.binding->action == "view.fullscreen" && r.fullscreen);
    r.press(Key::F11);
    CHECK(!r.fullscreen);
    r.press(Key::F4);
    CHECK(r.locked);
    r.press(Key::F4);
    CHECK(!r.locked);
    r.press(Key::F7);
    CHECK(r.calls.back() == "resetLayout");
    Rig bare;
    bare.svc.hooks = {};
    bare.app.items.push_back(bind("view.panel.clip", Key::F2));
    auto g = bare.press(Key::F2);
    CHECK(g && g->result.hit == 0);
    CHECK(describeAction(r.comp, *findAction("view.panel.efeitos"), {}).find("Efeitos") != std::string::npos);
    CHECK(r.svc.hooks.panelVisible("nao-existe") == -1);
    std::set<std::string> keys;
    bool lookups = true;
    for (const ui::view::PanelDef& p : ui::view::kPanels) {
        lookups = lookups && keys.insert(p.key).second && ui::view::findPanelByWindow(p.window) == ui::view::findPanel(p.key);
    }
    CHECK(lookups);
}

void testSequenceKeys()
{
    section("modo Sequencias: atalhos do editor nao conflitam com os globais");
    CHECK(findAction("mode.sequences") != nullptr && defaultScopeFor("mode.sequences") == Scope::App);
    CHECK(!findAction("mode.sequences")->show && !supportsHold("mode.sequences"));
    Rig r;
    r.app.items.push_back(bind("mode.sequences", Key::F5, ModAlt));
    r.press(Key::F5, ModAlt);
    CHECK(!r.calls.empty() && r.calls.back() == "mode:3");

    struct Atalho
    {
        Key key;
        std::uint8_t mods;
    };
    const Atalho editor[] = {{Key::Z, ModCtrl},      {Key::Y, ModCtrl},        {Key::Z, ModCtrl | ModShift}, {Key::A, ModCtrl},
                             {Key::Equal, ModCtrl},  {Key::Minus, ModCtrl},    {Key::D0, ModCtrl},           {Key::Left, ModAlt},
                             {Key::Right, ModAlt},   {Key::Left, ModNone},     {Key::Right, ModNone},        {Key::Home, ModNone},
                             {Key::End, ModNone},    {Key::Delete, ModNone},   {Key::Backspace, ModNone},    {Key::Space, ModNone},
                             {Key::Escape, ModNone}};
    const KeyBindingSet app = defaultAppKeys();
    const KeyBindingSet projeto = defaultProjectKeys();
    KeyGate focado;
    focado.mapEditor = true;
    bool livre = true;
    bool semAtalhoComMod = true;
    for (const Atalho& a : editor) {
        const auto m = resolveKey({a.key, a.mods}, app, projeto);
        if (m && gateAllows(*m->binding, focado))
            livre = false;
        if ((a.mods & (ModCtrl | ModAlt)) != 0 && m)
            semAtalhoComMod = false;
    }
    CHECK(livre);
    CHECK(semAtalhoComMod);
    KeyGate texto;
    texto.textInput = true;
    CHECK(!gateAllows(*resolveKey({Key::Space, ModNone}, app, projeto)->binding, texto));
}

void testFxEvent()
{
    section("evento de efeito: acao fx.event, segurar e alvo inexistente");
    Rig r;
    CHECK(findAction("fx.event") != nullptr && findAction("fx.event")->show && supportsHold("fx.event"));
    r.comp.layers[0].effects.add("bsl:disparo");
    r.comp.layers[0].effects.add("bsl:borda-neon");
    auto counter = [&](int effect, const char* id) {
        const media::Json* v = r.comp.layers[0].effects.items[static_cast<size_t>(effect)].params.find(id);
        return (v && v->is_number_integer()) ? v->get<int>() : 0;
    };
    auto rajada = [&] {
        const media::Json* v = r.comp.layers[0].effects.items[0].params.find("rajada");
        return v && v->is_boolean() && v->get<bool>();
    };

    ActionArgs next;
    next.fxScope = 1;
    next.layer = 0;
    next.effect = 0;
    next.param = "proximo";
    ActionContext c = r.ctx();
    ActionResult res = runAction(c, "fx.event", next);
    CHECK(res.hit == 1 && counter(0, "proximo") == 1 && res.target == Target::effectOf(1, -1, 0, -1, 0));
    CHECK(runAction(c, "fx.event", next).hit == 1 && counter(0, "proximo") == 2);
    ActionArgs prev = next;
    prev.param = "anterior";
    CHECK(runAction(c, "fx.event", prev).hit == 1 && counter(0, "anterior") == 1 && counter(0, "proximo") == 2);
    ActionArgs reset = next;
    reset.param = "reiniciar";
    CHECK(runAction(c, "fx.event", reset).hit == 1 && counter(0, "reiniciar") == 1);
    ActionArgs flash = next;
    flash.effect = 1;
    flash.param = "flash";
    CHECK(runAction(c, "fx.event", flash).hit == 1 && counter(1, "flash") == 1);

    ActionArgs burst = next;
    burst.param = "rajada";
    r.project.items.push_back(bind("fx.event", Key::J, ModNone, Mode::Hold));
    r.project.items.back().args = burst;
    CHECK(!rajada());
    auto f = r.press(Key::J);
    CHECK(f && f->hold && rajada() && r.svc.dispatcher.isHeld(Key::J));
    auto again = r.press(Key::J);
    CHECK(again && again->result.hit == 0 && rajada() && r.svc.dispatcher.heldCount() == 1);
    r.release(Key::J);
    CHECK(!rajada() && r.svc.dispatcher.heldCount() == 0);

    r.project.items.push_back(bind("fx.event", Key::H, ModNone, Mode::Press));
    r.project.items.back().args = next;
    f = r.press(Key::H);
    CHECK(f && f->result.hit == 1 && !f->hold && counter(0, "proximo") == 3);
    r.release(Key::H);
    CHECK(counter(0, "proximo") == 3 && r.svc.dispatcher.heldCount() == 0);

    ActionArgs missing = next;
    missing.effect = 9;
    CHECK(runAction(c, "fx.event", missing).hit == 0);
    missing = next;
    missing.param = "nao_existe";
    CHECK(runAction(c, "fx.event", missing).hit == 0);
    missing.param.clear();
    CHECK(runAction(c, "fx.event", missing).hit == 0);
    missing = next;
    missing.param = "velocidadeRajada";
    CHECK(runAction(c, "fx.event", missing).hit == 0);
    missing = next;
    missing.layer = 50;
    CHECK(runAction(c, "fx.event", missing).hit == 0);
    missing = next;
    missing.fxScope = 2;
    CHECK(runAction(c, "fx.event", missing).hit == 0);
    missing.fxScope = 0;
    missing.column = 7;
    CHECK(runAction(c, "fx.event", missing).hit == 0);
    CHECK(setActionState(c, "fx.event", missing, true).hit == 0);

    KeyBindingSet set;
    set.items.push_back(r.project.items[r.project.items.size() - 2]);
    const nlohmann::json j = bindingsToJson(set);
    const KeyBindingSet back = bindingsFromJson(j);
    CHECK(back.items.size() == 1 && back.items[0].args.param == "rajada" && back.items[0].mode == Mode::Hold
          && back.items[0].args == set.items[0].args);
    CHECK(describeAction(r.comp, *findAction("fx.event"), burst).find("Rajada (segurar)") != std::string::npos);
    CHECK(describeAction(r.comp, *findAction("fx.event"), burst).find("rajada") == std::string::npos);
}

int main()
{
    if (!glfwInit())
        return 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "keystest", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "sem contexto GL 3.3\n");
        return 2;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress))
        return 2;

    testKeys();
    testDefaults();
    testRegistry();
    testScopes();
    testGate();
    testActions();
    testHold();
    testDispatchGate();
    testFlash();
    testFiles();
#if BSL_WITH_HOTKEYS
    testSystemHotkeys();
#endif
    testView();
    testSequenceKeys();
    testFxEvent();

    glfwDestroyWindow(win);
    glfwTerminate();
    std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    std::printf(g_failures == 0 ? "KEYSTEST OK\n" : "KEYSTEST FALHOU\n");
    return g_failures == 0 ? 0 : 1;
}
