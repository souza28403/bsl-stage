#include "control/ActionRegistry.h"

#include "core/Composition.h"
#include "ui/ViewState.h"

#include <algorithm>
#include <array>
#include <deque>
#include <string>
#include <utility>

namespace bsl::control {

namespace {

constexpr float kBpmMin = 20.0f;
constexpr float kBpmMax = 300.0f;
constexpr double kTapResetSeconds = 2.0;
constexpr size_t kTapWindow = 4;

struct View
{
    const Composition& comp;
    const SelectionRef& sel;
};

int layerIndex(const View& v, const ActionArgs& a)
{
    int l = a.layer;
    if (a.selected)
        l = v.sel.kind != SelectionRef::Kind::None ? v.sel.layer : -1;
    else if (a.layerId > 0)
        l = v.comp.findLayerById(a.layerId);
    return (l >= 0 && l < v.comp.layerCount()) ? l : -1;
}

int deckIndex(const View& v, const ActionArgs& a)
{
    const int d = a.deck < 0 ? v.comp.currentDeck : a.deck;
    return v.comp.validDeck(d) ? d : -1;
}

int columnIndex(const View& v, const ActionArgs& a, bool followSelection = false)
{
    if (followSelection || (a.selected && a.column < 0))
        return v.sel.kind == SelectionRef::Kind::Clip ? v.sel.column : -1;
    return a.column;
}

class DeckScope
{
public:
    DeckScope(Composition& comp, int deck) : comp_(comp), saved_(comp.currentDeck) { comp_.currentDeck = deck; }
    ~DeckScope() { comp_.currentDeck = saved_; }
    DeckScope(const DeckScope&) = delete;
    DeckScope& operator=(const DeckScope&) = delete;

private:
    Composition& comp_;
    int saved_;
};

View viewOf(const ActionContext& c)
{
    return View{c.comp, c.selection};
}

int clipTrigger(ActionContext& c, const ActionArgs& a)
{
    const View v = viewOf(c);
    const int d = deckIndex(v, a), l = layerIndex(v, a), col = columnIndex(v, a);
    if (d < 0 || l < 0 || !c.comp.validCell(d, l, col))
        return 0;
    DeckScope scope(c.comp, d);
    return c.comp.trigger(l, col) ? 1 : 0;
}

int columnTrigger(ActionContext& c, const ActionArgs& a)
{
    const View v = viewOf(c);
    const int d = deckIndex(v, a);
    if (d < 0 || a.column < 0 || a.column >= c.comp.decks[static_cast<size_t>(d)].columnCount())
        return 0;
    int filled = 0;
    for (int l = 0; l < c.comp.layerCount(); ++l)
        filled += c.comp.clipAt(d, l, a.column).empty() ? 0 : 1;
    if (filled == 0)
        return 0;
    DeckScope scope(c.comp, d);
    c.comp.triggerColumn(a.column);
    return filled;
}

int layerClear(ActionContext& c, const ActionArgs& a)
{
    const int l = layerIndex(viewOf(c), a);
    if (l < 0)
        return 0;
    c.comp.stopLayer(l);
    return 1;
}

int allClear(ActionContext& c, const ActionArgs&)
{
    c.comp.stopAll();
    return std::max(1, c.comp.layerCount());
}

Layer* layerFor(ActionContext& c, const ActionArgs& a)
{
    const int l = layerIndex(viewOf(c), a);
    return l < 0 ? nullptr : &c.comp.layers[static_cast<size_t>(l)];
}

int layerBypassGet(ActionContext& c, const ActionArgs& a)
{
    const Layer* l = layerFor(c, a);
    return l ? (l->bypass ? 1 : 0) : -1;
}
int layerBypassSet(ActionContext& c, const ActionArgs& a, bool on)
{
    Layer* l = layerFor(c, a);
    if (!l)
        return 0;
    l->bypass = on;
    return 1;
}
int layerPowerGet(ActionContext& c, const ActionArgs& a)
{
    const Layer* l = layerFor(c, a);
    return l ? (l->bypass ? 0 : 1) : -1;
}
int layerPowerSet(ActionContext& c, const ActionArgs& a, bool on)
{
    return layerBypassSet(c, a, !on);
}
int layerSoloGet(ActionContext& c, const ActionArgs& a)
{
    const Layer* l = layerFor(c, a);
    return l ? (l->solo ? 1 : 0) : -1;
}
int layerSoloSet(ActionContext& c, const ActionArgs& a, bool on)
{
    Layer* l = layerFor(c, a);
    if (!l)
        return 0;
    l->solo = on;
    return 1;
}
int layerOpacityGet(ActionContext& c, const ActionArgs& a)
{
    const Layer* l = layerFor(c, a);
    return l ? (l->opacity > 0.5f ? 1 : 0) : -1;
}
int layerOpacitySetOnOff(ActionContext& c, const ActionArgs& a, bool on)
{
    Layer* l = layerFor(c, a);
    if (!l)
        return 0;
    l->opacity = on ? 1.0f : 0.0f;
    return 1;
}
int layerOpacityValue(ActionContext& c, const ActionArgs& a)
{
    Layer* l = layerFor(c, a);
    if (!l)
        return 0;
    l->opacity = std::clamp(a.value, 0.0f, 1.0f);
    return 1;
}

int layerTransformReset(ActionContext& c, const ActionArgs& a)
{
    const int l = layerIndex(viewOf(c), a);
    return (l >= 0 && c.comp.resetTransform(l)) ? 1 : 0;
}
int layerTransformPreset(ActionContext& c, const ActionArgs& a)
{
    const int l = layerIndex(viewOf(c), a);
    const bool grid = a.index == static_cast<int>(TransformPreset::Grid2x2) || a.index == static_cast<int>(TransformPreset::Grid3x3);
    if (l < 0 && !grid)
        return 0;
    return c.comp.applyTransformPreset(l, a.index);
}

int masterOpacityGet(ActionContext& c, const ActionArgs&)
{
    return c.comp.masterOpacity > 0.5f ? 1 : 0;
}
int masterOpacitySetOnOff(ActionContext& c, const ActionArgs&, bool on)
{
    c.comp.masterOpacity = on ? 1.0f : 0.0f;
    return 1;
}
int masterOpacityValue(ActionContext& c, const ActionArgs& a)
{
    c.comp.masterOpacity = std::clamp(a.value, 0.0f, 1.0f);
    return 1;
}
int masterBypassGet(ActionContext& c, const ActionArgs&)
{
    return c.comp.masterBypass ? 1 : 0;
}
int masterBypassSet(ActionContext& c, const ActionArgs&, bool on)
{
    c.comp.masterBypass = on;
    return 1;
}
int blackoutGet(ActionContext& c, const ActionArgs&)
{
    return c.comp.blackout ? 1 : 0;
}
int blackoutSet(ActionContext& c, const ActionArgs&, bool on)
{
    c.comp.blackout = on;
    return 1;
}

int gotoDeck(ActionContext& c, int deck)
{
    if (!c.comp.validDeck(deck))
        return 0;
    if (deck != c.comp.currentDeck) {
        c.comp.currentDeck = deck;
        if (c.app && c.app->clearClipSelection)
            c.app->clearClipSelection();
    }
    return 1;
}
int deckNext(ActionContext& c, const ActionArgs&)
{
    if (c.comp.deckCount() <= 1)
        return 0;
    return gotoDeck(c, (c.comp.currentDeck + 1) % c.comp.deckCount());
}
int deckPrev(ActionContext& c, const ActionArgs&)
{
    if (c.comp.deckCount() <= 1)
        return 0;
    return gotoDeck(c, (c.comp.currentDeck + c.comp.deckCount() - 1) % c.comp.deckCount());
}
int deckGoto(ActionContext& c, const ActionArgs& a)
{
    return gotoDeck(c, a.index);
}

float clampBpm(float v)
{
    return std::clamp(v, kBpmMin, kBpmMax);
}

int bpmTap(ActionContext& c, const ActionArgs&)
{
    static TapTempo fallback;
    (c.tap ? *c.tap : fallback).tap(c.comp, c.now);
    return 1;
}
int bpmHalf(ActionContext& c, const ActionArgs&)
{
    c.comp.bpm = clampBpm(c.comp.bpm * 0.5f);
    return 1;
}
int bpmDouble(ActionContext& c, const ActionArgs&)
{
    c.comp.bpm = clampBpm(c.comp.bpm * 2.0f);
    return 1;
}
int bpmResync(ActionContext& c, const ActionArgs&)
{
    c.comp.beat = 0.0;
    return 1;
}
int transportPlay(ActionContext& c, const ActionArgs&)
{
    c.comp.resumeAll();
    return 1;
}
int transportPause(ActionContext& c, const ActionArgs&)
{
    c.comp.pauseAll();
    return 1;
}
int transportStop(ActionContext& c, const ActionArgs&)
{
    c.comp.stopAll();
    c.comp.beat = 0.0;
    return 1;
}
int transportToggle(ActionContext& c, const ActionArgs&)
{
    if (c.comp.paused)
        c.comp.resumeAll();
    else
        c.comp.pauseAll();
    return 1;
}

int docNext(ActionContext& c, const ActionArgs&) { return c.comp.pageAction(Composition::PageAction::Next); }
int docPrev(ActionContext& c, const ActionArgs&) { return c.comp.pageAction(Composition::PageAction::Prev); }
int docFirst(ActionContext& c, const ActionArgs&) { return c.comp.pageAction(Composition::PageAction::First); }
int docLast(ActionContext& c, const ActionArgs&) { return c.comp.pageAction(Composition::PageAction::Last); }
int docReload(ActionContext& c, const ActionArgs&) { return c.comp.pageAction(Composition::PageAction::Reload); }

int previewLive(ActionContext& c, const ActionArgs&)
{
    const SelectionRef& s = c.selection;
    if (s.kind != SelectionRef::Kind::Clip || !c.comp.validCell(s.layer, s.column))
        return 0;
    const Clip& clip = c.comp.clip(s.layer, s.column);
    if (clip.empty() || clip.missing || clip.isOpen())
        return 0;
    return c.comp.trigger(s.layer, s.column) ? 1 : 0;
}

fx::EffectChain* chainFor(ActionContext& c, const ActionArgs& a)
{
    const View v = viewOf(c);
    switch (a.fxScope) {
    case 0: {
        const int d = deckIndex(v, a), l = layerIndex(v, a), col = columnIndex(v, a, a.selected);
        return (d >= 0 && l >= 0 && c.comp.validCell(d, l, col)) ? &c.comp.clipAt(d, l, col).effects : nullptr;
    }
    case 1: {
        Layer* l = layerFor(c, a);
        return l ? &l->effects : nullptr;
    }
    default:
        return &c.comp.masterEffects;
    }
}

int effectGet(ActionContext& c, const ActionArgs& a)
{
    const fx::EffectChain* chain = chainFor(c, a);
    return (chain && chain->valid(a.effect)) ? (chain->items[static_cast<size_t>(a.effect)].bypass ? 0 : 1) : -1;
}
int effectSet(ActionContext& c, const ActionArgs& a, bool on)
{
    fx::EffectChain* chain = chainFor(c, a);
    return (chain && chain->setBypass(a.effect, !on)) ? 1 : 0;
}

int modeComposition(ActionContext& c, const ActionArgs&)
{
    if (!c.app || !c.app->setMode)
        return 0;
    c.app->setMode(0);
    return 1;
}
int modeMapping(ActionContext& c, const ActionArgs&)
{
    if (!c.app || !c.app->setMode)
        return 0;
    c.app->setMode(1);
    return 1;
}
int modePixelMap(ActionContext& c, const ActionArgs&)
{
    if (!c.app || !c.app->setMode)
        return 0;
    c.app->setMode(2);
    return 1;
}
int identifyGet(ActionContext& c, const ActionArgs&)
{
    return (c.app && c.app->identify) ? (c.app->identify() ? 1 : 0) : -1;
}
int identifySet(ActionContext& c, const ActionArgs&, bool on)
{
    if (!c.app || !c.app->setIdentify)
        return 0;
    c.app->setIdentify(on);
    return 1;
}
int layoutPreset(ActionContext& c, const ActionArgs& a)
{
    return (c.app && c.app->layoutPreset && c.app->layoutPreset(a.index)) ? 1 : 0;
}

int fullscreenGet(ActionContext& c, const ActionArgs&)
{
    return (c.app && c.app->fullscreen) ? (c.app->fullscreen() ? 1 : 0) : -1;
}
int fullscreenSet(ActionContext& c, const ActionArgs&, bool on)
{
    if (!c.app || !c.app->setFullscreen)
        return 0;
    c.app->setFullscreen(on);
    return 1;
}
int lockGet(ActionContext& c, const ActionArgs&)
{
    return (c.app && c.app->layoutLocked) ? (c.app->layoutLocked() ? 1 : 0) : -1;
}
int lockSet(ActionContext& c, const ActionArgs&, bool on)
{
    if (!c.app || !c.app->setLayoutLocked)
        return 0;
    c.app->setLayoutLocked(on);
    return 1;
}
int showAllPanels(ActionContext& c, const ActionArgs&)
{
    if (!c.app || !c.app->showAllPanels)
        return 0;
    c.app->showAllPanels();
    return 1;
}
int resetLayout(ActionContext& c, const ActionArgs&)
{
    if (!c.app || !c.app->resetLayout)
        return 0;
    c.app->resetLayout();
    return 1;
}

template <size_t N>
int panelGet(ActionContext& c, const ActionArgs&)
{
    return (c.app && c.app->panelVisible) ? c.app->panelVisible(ui::view::kPanels[N].key) : -1;
}
template <size_t N>
int panelSet(ActionContext& c, const ActionArgs&, bool on)
{
    if (!c.app || !c.app->setPanelVisible)
        return 0;
    c.app->setPanelVisible(ui::view::kPanels[N].key, on);
    return 1;
}

using GetFn = int (*)(ActionContext&, const ActionArgs&);
using SetFn = int (*)(ActionContext&, const ActionArgs&, bool);

template <size_t... I>
std::array<std::pair<GetFn, SetFn>, sizeof...(I)> panelFunctions(std::index_sequence<I...>)
{
    return {{{&panelGet<I>, &panelSet<I>}...}};
}

int projectCommand(ActionContext& c, int what)
{
    if (!c.app || !c.app->project)
        return 0;
    c.app->project(what);
    return 1;
}
int projectNew(ActionContext& c, const ActionArgs&) { return projectCommand(c, 0); }
int projectOpen(ActionContext& c, const ActionArgs&) { return projectCommand(c, 1); }
int projectSave(ActionContext& c, const ActionArgs&) { return projectCommand(c, 2); }
int projectSaveAs(ActionContext& c, const ActionArgs&) { return projectCommand(c, 3); }

using K = Target::Kind;

#define BSL_ONCE(id, label, group, args, show, kind, fn) \
    ActionDef{id, label, group, args, show, K::kind, &fn, nullptr, nullptr}
#define BSL_TOGGLE(id, label, group, args, show, kind, getter, setter) \
    ActionDef{id, label, group, args, show, K::kind, nullptr, &getter, &setter}

const ActionDef kActions[] = {
    BSL_ONCE("clip.trigger", "Disparar clip", "Clips e colunas", ArgLayer | ArgColumn | ArgDeck, true, Cell, clipTrigger),
    BSL_ONCE("column.trigger", "Disparar coluna", "Clips e colunas", ArgColumn | ArgDeck, true, Column, columnTrigger),
    BSL_ONCE("layer.clear", "Limpar camada", "Camadas", ArgLayer, true, Layer, layerClear),
    BSL_ONCE("all.clear", "Limpar tudo", "Camadas", ArgNone, true, None, allClear),
    BSL_TOGGLE("layer.power", "Ligar/desligar camada", "Camadas", ArgLayer, true, Layer, layerPowerGet, layerPowerSet),
    BSL_TOGGLE("layer.bypass", "Camada: bypass", "Camadas", ArgLayer, true, Layer, layerBypassGet, layerBypassSet),
    BSL_TOGGLE("layer.solo", "Camada: solo", "Camadas", ArgLayer, true, Layer, layerSoloGet, layerSoloSet),
    BSL_TOGGLE("layer.opacity.toggle", "Camada: opacidade 0/100", "Camadas", ArgLayer, true, Layer, layerOpacityGet,
               layerOpacitySetOnOff),
    BSL_ONCE("layer.opacity.set", "Camada: definir opacidade", "Camadas", ArgLayer | ArgValue, true, Layer,
             layerOpacityValue),
    BSL_ONCE("layer.transform.reset", "Camada: resetar transform", "Camadas", ArgLayer, true, Layer, layerTransformReset),
    BSL_ONCE("layer.transform.preset", "Camada: preset de transform", "Camadas", ArgLayer | ArgIndex, true, Layer,
             layerTransformPreset),
    BSL_TOGGLE("master.opacity.toggle", "Master: opacidade 0/100", "Master", ArgNone, true, Master, masterOpacityGet,
               masterOpacitySetOnOff),
    BSL_ONCE("master.opacity.set", "Master: definir opacidade", "Master", ArgValue, true, Master, masterOpacityValue),
    BSL_TOGGLE("master.bypass", "Master: bypass", "Master", ArgNone, true, Master, masterBypassGet, masterBypassSet),
    BSL_TOGGLE("master.blackout", "Blackout", "Master", ArgNone, true, Master, blackoutGet, blackoutSet),
    BSL_ONCE("deck.next", "Próximo deck", "Decks", ArgNone, true, None, deckNext),
    BSL_ONCE("deck.prev", "Deck anterior", "Decks", ArgNone, true, None, deckPrev),
    BSL_ONCE("deck.goto", "Ir para o deck", "Decks", ArgIndex, true, None, deckGoto),
    BSL_ONCE("bpm.tap", "BPM: TAP", "BPM", ArgNone, true, None, bpmTap),
    BSL_ONCE("bpm.half", "BPM: ÷2", "BPM", ArgNone, true, None, bpmHalf),
    BSL_ONCE("bpm.double", "BPM: ×2", "BPM", ArgNone, true, None, bpmDouble),
    BSL_ONCE("bpm.resync", "BPM: resync", "BPM", ArgNone, true, None, bpmResync),
    BSL_ONCE("transport.toggle", "Tocar/pausar", "Transporte", ArgNone, true, None, transportToggle),
    BSL_ONCE("transport.play", "Tocar", "Transporte", ArgNone, true, None, transportPlay),
    BSL_ONCE("transport.pause", "Pausar", "Transporte", ArgNone, true, None, transportPause),
    BSL_ONCE("transport.stop", "Parar tudo", "Transporte", ArgNone, true, None, transportStop),
    BSL_ONCE("doc.next", "Próximo slide", "Documentos", ArgNone, true, None, docNext),
    BSL_ONCE("doc.prev", "Slide anterior", "Documentos", ArgNone, true, None, docPrev),
    BSL_ONCE("doc.first", "Voltar ao início (slide inicial)", "Documentos", ArgNone, true, None, docFirst),
    BSL_ONCE("doc.last", "Último slide", "Documentos", ArgNone, true, None, docLast),
    BSL_ONCE("doc.reload", "Recarregar documento", "Documentos", ArgNone, true, None, docReload),
    BSL_ONCE("preview.live", "Prévia: enviar Ao vivo", "Prévia", ArgNone, true, None, previewLive),
    BSL_TOGGLE("effect.bypass", "Ligar/desligar efeito (bypass)", "Efeitos", ArgLayer | ArgColumn | ArgDeck | ArgEffect,
               true, Effect, effectGet, effectSet),
    BSL_ONCE("ui.layoutPreset", "Layout: aplicar preset", "Aplicativo", ArgIndex, false, None, layoutPreset),
    BSL_TOGGLE("view.fullscreen", "Tela cheia", "Exibir", ArgNone, false, None, fullscreenGet, fullscreenSet),
    BSL_TOGGLE("view.lock", "Bloquear layout", "Exibir", ArgNone, false, None, lockGet, lockSet),
    BSL_ONCE("view.showAll", "Mostrar todos os painéis", "Exibir", ArgNone, false, None, showAllPanels),
    BSL_ONCE("view.resetLayout", "Restaurar layout padrão", "Exibir", ArgNone, false, None, resetLayout),
    BSL_ONCE("mode.composition", "Modo Composição", "Aplicativo", ArgNone, false, None, modeComposition),
    BSL_ONCE("mode.mapping", "Modo Mapeamento", "Aplicativo", ArgNone, false, None, modeMapping),
    BSL_ONCE("mode.pixelmap", "Modo Pixel Map", "Aplicativo", ArgNone, false, None, modePixelMap),
    BSL_TOGGLE("output.identify", "Identificar (saídas)", "Aplicativo", ArgNone, false, None, identifyGet, identifySet),
    BSL_ONCE("project.new", "Projeto: novo", "Aplicativo", ArgNone, false, None, projectNew),
    BSL_ONCE("project.open", "Projeto: abrir", "Aplicativo", ArgNone, false, None, projectOpen),
    BSL_ONCE("project.save", "Projeto: salvar", "Aplicativo", ArgNone, false, None, projectSave),
    BSL_ONCE("project.saveAs", "Projeto: salvar como", "Aplicativo", ArgNone, false, None, projectSaveAs),
};

#undef BSL_ONCE
#undef BSL_TOGGLE

}

void TapTempo::tap(Composition& comp, double now)
{
    if (!taps_.empty() && now - taps_.back() > kTapResetSeconds)
        taps_.clear();
    taps_.push_back(now);
    if (taps_.size() > kTapWindow)
        taps_.erase(taps_.begin());
    if (taps_.size() >= 2) {
        const double avg = (taps_.back() - taps_.front()) / static_cast<double>(taps_.size() - 1);
        if (avg > 0.0)
            comp.bpm = clampBpm(static_cast<float>(60.0 / avg));
    }
}

std::span<const ActionDef> allActions()
{
    static const std::vector<ActionDef> all = [] {
        static std::deque<std::string> texts;
        std::vector<ActionDef> list(std::begin(kActions), std::end(kActions));
        const auto fns = panelFunctions(std::make_index_sequence<static_cast<size_t>(ui::view::kPanelCount)>{});
        for (size_t i = 0; i < fns.size(); ++i) {
            const ui::view::PanelDef& p = ui::view::kPanels[i];
            const std::string& id = texts.emplace_back(std::string("view.panel.") + p.key);
            const std::string& label = texts.emplace_back(std::string("Alternar painel: ") + p.label);
            list.push_back(ActionDef{id.c_str(), label.c_str(), "Exibir", ArgNone, false, K::None, nullptr, fns[i].first,
                                     fns[i].second});
        }
        return list;
    }();
    return all;
}

const ActionDef* findAction(const std::string& id)
{
    for (const ActionDef& a : allActions())
        if (id == a.id)
            return &a;
    return nullptr;
}

Target targetOf(const Composition& comp, const SelectionRef& selection, const ActionDef& def, const ActionArgs& args)
{
    const View v{comp, selection};
    switch (def.targetKind) {
    case K::Cell: {
        const int d = deckIndex(v, args), l = layerIndex(v, args), col = columnIndex(v, args);
        return (d >= 0 && l >= 0 && comp.validCell(d, l, col)) ? Target::cell(d, l, col) : Target{};
    }
    case K::Column: {
        const int d = deckIndex(v, args);
        return (d >= 0 && args.column >= 0 && args.column < comp.decks[static_cast<size_t>(d)].columnCount())
                   ? Target::columnOf(d, args.column)
                   : Target{};
    }
    case K::Layer: {
        const int l = layerIndex(v, args);
        return l >= 0 ? Target::layerOf(l) : Target{};
    }
    case K::Master:
        return Target::master();
    case K::Effect: {
        if (args.fxScope == 2)
            return Target::effectOf(2, -1, -1, -1, args.effect);
        const int l = layerIndex(v, args);
        if (l < 0)
            return Target{};
        if (args.fxScope == 1)
            return Target::effectOf(1, -1, l, -1, args.effect);
        const int d = deckIndex(v, args), col = columnIndex(v, args, args.selected);
        return (d >= 0 && comp.validCell(d, l, col)) ? Target::effectOf(0, d, l, col, args.effect) : Target{};
    }
    default:
        return Target{};
    }
}

ActionResult runAction(ActionContext& ctx, const std::string& id, const ActionArgs& args, int* previousState)
{
    ActionResult res;
    if (previousState)
        *previousState = -1;
    const ActionDef* def = findAction(id);
    if (!def)
        return res;
    res.target = targetOf(ctx.comp, ctx.selection, *def, args);
    if (def->isToggle()) {
        const int cur = def->get(ctx, args);
        if (previousState)
            *previousState = cur;
        if (cur < 0)
            return res;
        res.hit = def->set(ctx, args, cur == 0);
    } else if (def->run) {
        res.hit = def->run(ctx, args);
    }
    res.ran = true;
    return res;
}

ActionResult setActionState(ActionContext& ctx, const std::string& id, const ActionArgs& args, bool on)
{
    ActionResult res;
    const ActionDef* def = findAction(id);
    if (!def || !def->isToggle())
        return res;
    res.target = targetOf(ctx.comp, ctx.selection, *def, args);
    res.hit = def->set(ctx, args, on);
    res.ran = true;
    return res;
}

namespace {

std::string layerText(const Composition& comp, const ActionArgs& a)
{
    if (a.selected)
        return "camada selecionada";
    if (a.layerId > 0) {
        const int idx = comp.findLayerById(a.layerId);
        if (idx < 0)
            return "camada removida";
        const std::string& name = comp.layers[static_cast<size_t>(idx)].name;
        std::string text = "camada " + std::to_string(idx + 1);
        if (!name.empty() && name != "Camada " + std::to_string(idx + 1))
            text += " (" + name + ")";
        return text;
    }
    if (a.layer >= 0 && a.layer < comp.layerCount()) {
        const std::string& name = comp.layers[static_cast<size_t>(a.layer)].name;
        std::string text = "camada " + std::to_string(a.layer + 1);
        if (!name.empty() && name != "Camada " + std::to_string(a.layer + 1))
            text += " (" + name + ")";
        return text;
    }
    return "camada " + std::to_string(a.layer + 1);
}

}

std::string describeAction(const Composition& comp, const ActionDef& def, const ActionArgs& args)
{
    std::vector<std::string> parts;
    if ((def.args & ArgEffect) != 0) {
        parts.push_back(args.fxScope == 0 ? "clip" : args.fxScope == 2 ? "master" : "camada");
    }
    const bool deckIrrelevant = (def.args & ArgEffect) != 0 && args.fxScope != 0;
    if ((def.args & ArgDeck) != 0 && args.deck >= 0 && !deckIrrelevant) {
        std::string d = "deck " + std::to_string(args.deck + 1);
        if (args.deck < comp.deckCount()) {
            const std::string& name = comp.decks[static_cast<size_t>(args.deck)].name;
            if (!name.empty() && name != "Deck " + std::to_string(args.deck + 1))
                d = "deck \"" + name + "\"";
        }
        parts.push_back(d);
    }
    const bool clipFollowsSelection = std::string(def.id) == "clip.trigger" && args.selected && args.column < 0;
    if ((def.args & ArgLayer) != 0 && !(args.fxScope == 2 && (def.args & ArgEffect) != 0))
        parts.push_back(layerText(comp, args));
    if ((def.args & ArgColumn) != 0) {
        if (clipFollowsSelection)
            parts.push_back("clip selecionado");
        else if ((def.args & ArgEffect) == 0 || args.fxScope == 0)
            parts.push_back(args.selected && (def.args & ArgEffect) != 0 ? "clip selecionado"
                                                                         : "coluna " + std::to_string(args.column + 1));
    }
    if ((def.args & ArgIndex) != 0)
    {
        const std::string id = def.id;
        if (id == "layer.transform.preset")
            parts.push_back(std::string("preset ") + std::to_string(args.index + 1) + " (" + transformPresetName(args.index) + ")");
        else
            parts.push_back(id == "ui.layoutPreset" ? "preset " + std::to_string(args.index + 1)
                                                    : "deck " + std::to_string(args.index + 1));
    }
    if ((def.args & ArgEffect) != 0)
        parts.push_back("efeito " + std::to_string(args.effect + 1));
    if ((def.args & ArgValue) != 0)
        parts.push_back(std::to_string(static_cast<int>(args.value * 100.0f + 0.5f)) + "%");

    std::string text = def.label;
    for (size_t i = 0; i < parts.size(); ++i)
        text += (i == 0 ? " - " : ", ") + parts[i];
    return text;
}

bool stampLayerId(const ActionDef& def, ActionArgs& args, const Composition& comp)
{
    if ((def.args & ArgLayer) == 0 || args.selected || args.layerId > 0)
        return false;
    if ((def.args & ArgEffect) != 0 && args.fxScope == 2)
        return false;
    if (args.layer < 0 || args.layer >= comp.layerCount())
        return false;
    args.layerId = comp.layers[static_cast<size_t>(args.layer)].id;
    if (args.layerId > 0)
        args.layer = 0;
    return args.layerId > 0;
}

int stampLayerIds(KeyBindingSet& set, const Composition& comp)
{
    int n = 0;
    for (KeyBinding& b : set.items) {
        const ActionDef* def = findAction(b.action);
        if (def && stampLayerId(*def, b.args, comp))
            ++n;
    }
    return n;
}

bool actionForTarget(const Target& target, std::string& id, ActionArgs& args)
{
    args = ActionArgs{};
    switch (target.kind) {
    case Target::Kind::Cell:
        id = "clip.trigger";
        args.deck = target.deck;
        args.layer = target.layer;
        args.column = target.column;
        return true;
    case Target::Kind::Column:
        id = "column.trigger";
        args.deck = target.deck;
        args.column = target.column;
        return true;
    case Target::Kind::Layer:
        id = "layer.power";
        args.layer = target.layer;
        return true;
    case Target::Kind::Master:
        id = "master.bypass";
        return true;
    case Target::Kind::Effect:
        id = "effect.bypass";
        args.fxScope = target.scope;
        args.deck = target.deck;
        args.layer = std::max(0, target.layer);
        args.column = std::max(0, target.column);
        args.effect = target.effect;
        return true;
    default:
        return false;
    }
}

}
