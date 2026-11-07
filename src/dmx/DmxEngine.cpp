#include "dmx/DmxEngine.h"

#include "control/ActionRegistry.h"
#include "core/Composition.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

namespace bsl::dmx {

namespace {

constexpr int kLearnMinStep = 4;
constexpr int kThresholdHysteresis = 6;

int rawAt(const UniverseView& uv, int channel, int span)
{
    if (channel < 1 || channel + span - 1 > kChannels)
        return -1;
    const int hi = uv.data[static_cast<size_t>(channel - 1)];
    return span == 2 ? hi * 256 + uv.data[static_cast<size_t>(channel)] : hi;
}

void noteResult(control::KeyService& keys, const control::ActionResult& res)
{
    control::Fired f;
    f.result = res;
    keys.note(f);
}

LayerTransform* transformOf(Composition& comp, int layerIndex)
{
    return comp.transformRef(layerIndex);
}

std::string percentText(double v)
{
    return std::to_string(static_cast<int>(std::lround(v * 100.0))) + "%";
}

}

bool setEffectParamNormalized(fx::EffectInstance& inst, const std::string& paramId, double n)
{
    n = std::clamp(n, 0.0, 1.0);
    for (const media::ParamDesc& d : inst.paramDescs()) {
        if (d.id != paramId)
            continue;
        switch (d.type) {
        case media::ParamType::Float:
            inst.setParam(paramId, media::param::floatJson(static_cast<float>(d.min + n * (d.max - d.min))));
            return true;
        case media::ParamType::Int:
            inst.setParam(paramId, media::Json(static_cast<int>(std::lround(d.min + n * (d.max - d.min)))));
            return true;
        case media::ParamType::Bool:
            inst.setParam(paramId, media::Json(n >= 0.5));
            return true;
        case media::ParamType::Enum:
            if (d.options.empty())
                return false;
            inst.setParam(paramId, media::Json(d.options[static_cast<size_t>(std::min<double>(
                                                    std::floor(n * static_cast<double>(d.options.size())),
                                                    static_cast<double>(d.options.size() - 1)))].value));
            return true;
        default:
            return false;
        }
    }
    return false;
}

bool applyParam(Composition& comp, const ParamRef& ref, int raw, int max, std::string* what)
{
    auto say = [&](const std::string& s) {
        if (what)
            *what = s;
    };
    const double n = dmxNormalized(raw, max);

    if (ref.kind == ParamKind::MasterOpacity) {
        comp.masterOpacity = static_cast<float>(n);
        say("Master: opacidade " + percentText(n));
        return true;
    }

    if (ref.kind == ParamKind::EffectParam) {
        fx::EffectChain* chain = nullptr;
        if (ref.fxScope == 2) {
            chain = &comp.masterEffects;
        } else {
            const int l = comp.findLayerById(ref.layerId);
            if (l < 0)
                return false;
            if (ref.fxScope == 1) {
                chain = &comp.layers[static_cast<size_t>(l)].effects;
            } else {
                const int d = ref.deck < 0 ? comp.currentDeck : ref.deck;
                if (!comp.validCell(d, l, ref.column))
                    return false;
                chain = &comp.clipAt(d, l, ref.column).effects;
            }
        }
        if (!chain->valid(ref.effect))
            return false;
        if (!setEffectParamNormalized(chain->items[static_cast<size_t>(ref.effect)], ref.paramId, n))
            return false;
        say("Efeito " + std::to_string(ref.effect + 1) + ": " + ref.paramId + " " + percentText(n));
        return true;
    }

    const int l = comp.findLayerById(ref.layerId);
    if (l < 0)
        return false;
    Layer& layer = comp.layers[static_cast<size_t>(l)];
    const std::string who = "Camada " + std::to_string(l + 1);
    const float cw = static_cast<float>(comp.width), ch = static_cast<float>(comp.height);

    switch (ref.kind) {
    case ParamKind::LayerOpacity:
        layer.opacity = static_cast<float>(n);
        say(who + ": opacidade " + percentText(n));
        return true;
    case ParamKind::LayerSpeed: {
        Clip* clip = comp.activeClip(l);
        if (!clip)
            return true;
        clip->setSpeed(static_cast<float>(std::clamp(dmxSpeedOf(raw, max), 0.0, 4.0)));
        say(who + ": velocidade");
        return true;
    }
    case ParamKind::LayerBlend:
        layer.blendMode = static_cast<BlendMode>(dmxBlendOf(raw, max));
        say(who + ": blend " + blendModeName(layer.blendMode));
        return true;
    case ParamKind::LayerPosX:
    case ParamKind::LayerPosY: {
        LayerTransform* t = transformOf(comp, l);
        if (!t)
            return false;
        const float offset = static_cast<float>(dmxCentered(raw, max));
        if (ref.kind == ParamKind::LayerPosX)
            t->x = (cw - t->width(comp.width)) * 0.5f + offset * cw;
        else
            t->y = (ch - t->height(comp.height)) * 0.5f + offset * ch;
        say(who + (ref.kind == ParamKind::LayerPosX ? ": posição X" : ": posição Y"));
        return true;
    }
    case ParamKind::LayerScale: {
        LayerTransform* t = transformOf(comp, l);
        if (!t)
            return false;
        const float s = static_cast<float>(dmxScaleOf(raw, max));
        const float oldW = t->width(comp.width), oldH = t->height(comp.height);
        const float cx = t->x + oldW * 0.5f, cy = t->y + oldH * 0.5f;
        const float newW = cw * s, newH = ch * s;
        if (std::abs(s - 1.0f) < 1.0e-4f) {
            t->w = 0.0f;
            t->h = 0.0f;
        } else {
            t->w = std::max(1.0f, newW);
            t->h = std::max(1.0f, newH);
        }
        t->x = cx - t->width(comp.width) * 0.5f;
        t->y = cy - t->height(comp.height) * 0.5f;
        say(who + ": escala " + percentText(s));
        return true;
    }
    case ParamKind::LayerRotation: {
        LayerTransform* t = transformOf(comp, l);
        if (!t)
            return false;
        t->rotation = static_cast<float>(dmxRotationOf(raw, max));
        say(who + ": rotação");
        return true;
    }
    default:
        return false;
    }
}

void Engine::reset()
{
    blocks_.clear();
    maps_.clear();
}

int Engine::applyBlock(Composition& comp, control::KeyService& keys, control::ActionContext& ctx, const Block& b, const Snapshot& snap)
{
    const UniverseView* uv = snap.find(b.universe);
    BlockState& st = blocks_[b.uid];
    st.live = false;
    if (!uv || !uv->everSeen)
        return 0;
    st.live = uv->active;

    const int fxCount = static_cast<int>(b.fx.size());
    if (st.mode != static_cast<int>(b.mode) + (b.kind == BlockKind::Master ? 100 : 0) || st.fxCount != fxCount ||
        st.universe != b.universe || st.channel != b.channel) {
        st.mode = static_cast<int>(b.mode) + (b.kind == BlockKind::Master ? 100 : 0);
        st.fxCount = fxCount;
        st.universe = b.universe;
        st.channel = b.channel;
        st.rows = b.kind == BlockKind::Master ? masterRows() : layerRows(b.mode, fxCount);
        st.last.fill(-1);
        st.seen.fill(false);
    }

    const bool defaults = uv->released;
    const int layerIndex = b.kind == BlockKind::Layer ? comp.findLayerById(b.layerId) : -1;
    if (b.kind == BlockKind::Layer && layerIndex < 0)
        return 0;

    int changes = 0;
    for (size_t i = 0; i < st.rows.size() && i < st.last.size(); ++i) {
        const ChannelRow& row = st.rows[i];
        const int max = row.span == 2 ? 65535 : 255;
        const bool isTrigger = row.func == Func::Clip || row.func == Func::Column;
        if (isTrigger && defaults)
            continue;
        const int raw = defaults ? row.defaultRaw : rawAt(*uv, b.channel + row.offset - 1, row.span);
        if (raw < 0)
            continue;
        const bool first = !st.seen[i];
        const int before = st.last[i];
        if (!first && raw == before)
            continue;
        st.last[i] = raw;
        st.seen[i] = true;

        std::string what;
        switch (row.func) {
        case Func::Opacity:
            if (applyParam(comp, {ParamKind::LayerOpacity, b.layerId}, raw, max, &what)) {
                ++changes;
                info_.lastEvent = what;
            }
            break;
        case Func::Speed: {
            st.speed = std::clamp(dmxSpeedOf(raw, max), 0.0, 4.0);
            st.speedSet = true;
            st.speedClipDeck = st.speedClipColumn = -2;
            ++changes;
            break;
        }
        case Func::Blend:
            if (applyParam(comp, {ParamKind::LayerBlend, b.layerId}, raw, max, &what)) {
                ++changes;
                info_.lastEvent = what;
            }
            break;
        case Func::PosX:
        case Func::PosY:
        case Func::Scale:
        case Func::Rotation: {
            const ParamKind kind = row.func == Func::PosX ? ParamKind::LayerPosX
                                   : row.func == Func::PosY ? ParamKind::LayerPosY
                                   : row.func == Func::Scale ? ParamKind::LayerScale
                                                             : ParamKind::LayerRotation;
            if (applyParam(comp, {kind, b.layerId}, raw, max, &what)) {
                ++changes;
                info_.lastEvent = what;
            }
            break;
        }
        case Func::Fx: {
            if (row.fxIndex < 0 || row.fxIndex >= fxCount)
                break;
            const FxSlot& slot = b.fx[static_cast<size_t>(row.fxIndex)];
            if (slot.param.empty())
                break;
            ParamRef ref{ParamKind::EffectParam, b.layerId, 1, -1, 0, slot.effect, slot.param};
            if (applyParam(comp, ref, raw, max, &what)) {
                ++changes;
                info_.lastEvent = "Camada " + std::to_string(layerIndex + 1) + " - " + what;
            }
            break;
        }
        case Func::MasterOpacity:
            if (applyParam(comp, {ParamKind::MasterOpacity}, raw, max, &what)) {
                ++changes;
                info_.lastEvent = what;
            }
            break;
        case Func::Blackout: {
            const bool on = raw >= 128;
            if (comp.blackout != on) {
                comp.blackout = on;
                ++changes;
                info_.lastEvent = on ? "Master: blackout ligado" : "Master: blackout desligado";
            }
            break;
        }
        case Func::Clip:
        case Func::Column: {
            if (first)
                break;
            const int range = dmxClipRange(raw);
            if (range == dmxClipRange(before) || range == 0)
                break;
            control::ActionArgs args;
            std::string action;
            if (range == 1) {
                action = row.func == Func::Clip ? "layer.clear" : "all.clear";
                args.layerId = b.layerId;
            } else {
                args.column = range - 2;
                if (row.func == Func::Clip) {
                    action = "clip.trigger";
                    args.layerId = b.layerId;
                } else {
                    action = "column.trigger";
                }
            }
            const control::ActionResult res = control::runAction(ctx, action, args);
            noteResult(keys, res);
            ++info_.triggers;
            ++changes;
            info_.lastEvent = (row.func == Func::Clip ? "Camada " + std::to_string(layerIndex + 1) + ": " : std::string("Master: ")) +
                              (range == 1 ? "limpar" : "coluna " + std::to_string(range - 1));
            break;
        }
        }
    }

    if (b.kind == BlockKind::Layer && st.speedSet) {
        const Layer& layer = comp.layers[static_cast<size_t>(layerIndex)];
        if (layer.activeDeck != st.speedClipDeck || layer.activeColumn != st.speedClipColumn) {
            if (Clip* clip = comp.activeClip(layerIndex)) {
                clip->setSpeed(static_cast<float>(st.speed));
                st.speedClipDeck = layer.activeDeck;
                st.speedClipColumn = layer.activeColumn;
            }
        }
    }
    return changes;
}

int Engine::applyMap(Composition& comp, control::KeyService& keys, control::ActionContext& ctx, const Map& m, const Snapshot& snap)
{
    MapState& st = maps_[m.uid];
    st.live = false;
    const UniverseView* uv = snap.find(m.universe);
    if (!uv || !uv->everSeen)
        return 0;
    st.live = uv->active;
    if (uv->released)
        return 0;
    const int span = m.channelSpan();
    if (st.universe != m.universe || st.channel != m.channel || st.span != span) {
        st.universe = m.universe;
        st.channel = m.channel;
        st.span = span;
        st.last = -1;
        st.high = false;
    }
    int raw = rawAt(*uv, m.channel, span);
    if (raw < 0)
        return 0;
    const int max = span == 2 ? 65535 : 255;
    if (m.invert)
        raw = max - raw;

    if (st.last < 0) {
        st.last = raw;
        if (m.type == Map::Type::Action) {
            const int raw8 = span == 2 ? raw / 257 : raw;
            st.high = raw8 >= m.threshold;
        }
        return 0;
    }
    if (raw == st.last)
        return 0;
    st.last = raw;

    if (m.type == Map::Type::Param) {
        ParamRef ref{m.param, m.layerId, m.fxScope, m.deck, m.column, m.effect, m.paramId};
        std::string what;
        if (!applyParam(comp, ref, raw, max, &what))
            return 0;
        info_.lastEvent = what;
        return 1;
    }

    const int raw8 = span == 2 ? raw / 257 : raw;
    int changes = 0;
    if (!st.high && raw8 >= m.threshold) {
        st.high = true;
        const control::Fired fired = keys.dispatcher.fireToken(m.uid, m.binding, ctx);
        keys.note(fired);
        ++info_.triggers;
        changes = 1;
        const control::ActionDef* def = control::findAction(m.binding.action);
        info_.lastEvent = def ? def->label : m.binding.action;
    } else if (st.high && raw8 <= m.threshold - kThresholdHysteresis) {
        st.high = false;
        for (const control::Fired& f : keys.dispatcher.releaseToken(m.uid, ctx))
            keys.note(f);
    }
    return changes;
}

void Engine::learnBegin(LearnTarget target, const Snapshot& snap)
{
    learning_ = true;
    learnTarget_ = std::move(target);
    learnBase_.clear();
    for (const UniverseView& u : snap.universes)
        learnBase_[u.universe] = u.data;
    learnedUid_ = 0;
}

void Engine::learnCancel()
{
    learning_ = false;
    learnBase_.clear();
}

int Engine::takeLearned(std::string* message)
{
    const int uid = learnedUid_;
    if (message)
        *message = learnedMessage_;
    learnedUid_ = 0;
    learnedMessage_.clear();
    return uid;
}

bool Engine::learnStep(Composition& comp, const Snapshot& snap)
{
    for (const UniverseView& u : snap.universes) {
        auto base = learnBase_.find(u.universe);
        if (base == learnBase_.end()) {
            learnBase_[u.universe] = u.data;
            continue;
        }
        for (int c = 0; c < kChannels; ++c) {
            if (std::abs(static_cast<int>(u.data[static_cast<size_t>(c)]) - static_cast<int>(base->second[static_cast<size_t>(c)])) < kLearnMinStep)
                continue;
            Map m = learnTarget_.prototype;
            m.universe = u.universe;
            m.channel = c + 1;
            m.bit16 = false;
            m.enabled = true;
            std::erase_if(comp.dmx.maps, [&](const Map& o) {
                return sameTarget(o, m) || (o.universe == m.universe && o.channel == m.channel && o.type == m.type &&
                                         o.channelSpan() == 1);
            });
            Map& added = comp.dmx.addMap(m);
            MapState st;
            st.universe = added.universe;
            st.channel = added.channel;
            st.span = 1;
            st.last = u.data[static_cast<size_t>(c)];
            st.high = added.type == Map::Type::Action && st.last >= added.threshold;
            maps_[added.uid] = st;
            learnedUid_ = added.uid;
            learnedMessage_ = describeMap(comp, added) + " <- " + universeChannelLabel(added.universe, added.channel);
            learning_ = false;
            learnBase_.clear();
            return true;
        }
    }
    return false;
}

int Engine::apply(Composition& comp, control::KeyService& keys, const control::SelectionRef& selection, const Snapshot& snap)
{
    Config& cfg = comp.dmx;
    cfg.assignUids();

    if (learning_) {
        learnStep(comp, snap);
        return 0;
    }

    control::ActionContext ctx = keys.context(comp, selection);

    std::set<int> alive;
    for (const Map& m : cfg.maps)
        alive.insert(m.uid);
    for (auto it = maps_.begin(); it != maps_.end();) {
        if (!alive.count(it->first)) {
            for (const control::Fired& f : keys.dispatcher.releaseToken(it->first, ctx))
                keys.note(f);
            it = maps_.erase(it);
        } else {
            ++it;
        }
    }
    std::set<int> aliveBlocks;
    for (const Block& b : cfg.blocks)
        aliveBlocks.insert(b.uid);
    for (auto it = blocks_.begin(); it != blocks_.end();)
        it = aliveBlocks.count(it->first) ? std::next(it) : blocks_.erase(it);

    int changes = 0;
    info_.blocksLive = info_.mapsLive = 0;
    for (const Block& b : cfg.blocks) {
        if (!b.enabled)
            continue;
        changes += applyBlock(comp, keys, ctx, b, snap);
        if (blocks_[b.uid].live)
            ++info_.blocksLive;
    }
    for (const Map& m : cfg.maps) {
        if (!m.enabled) {
            for (const control::Fired& f : keys.dispatcher.releaseToken(m.uid, ctx))
                keys.note(f);
            maps_.erase(m.uid);
            continue;
        }
        changes += applyMap(comp, keys, ctx, m, snap);
        if (maps_[m.uid].live)
            ++info_.mapsLive;
    }
    info_.applied += static_cast<std::uint64_t>(changes);
    return changes;
}

std::string universeChannelLabel(int universe, int channel)
{
    return "U" + std::to_string(universe) + "." + std::to_string(channel);
}

std::string layerLabel(const Composition& comp, int layerId)
{
    const int i = comp.findLayerById(layerId);
    if (i < 0)
        return "camada removida";
    const std::string& name = comp.layers[static_cast<size_t>(i)].name;
    std::string text = "Camada " + std::to_string(i + 1);
    if (!name.empty() && name != "Camada " + std::to_string(i + 1))
        text += " (" + name + ")";
    return text;
}

std::string describeMap(const Composition& comp, const Map& m)
{
    if (m.type == Map::Type::Action) {
        const control::ActionDef* def = control::findAction(m.binding.action);
        return def ? control::describeAction(comp, *def, m.binding.args) : m.binding.action;
    }
    if (m.param == ParamKind::MasterOpacity)
        return "Master: opacidade";
    if (m.param == ParamKind::EffectParam) {
        const char* scope = m.fxScope == 2 ? "Master" : m.fxScope == 0 ? "Clip" : nullptr;
        std::string head = scope ? scope : layerLabel(comp, m.layerId);
        if (m.fxScope == 0)
            head += " " + layerLabel(comp, m.layerId) + ", coluna " + std::to_string(m.column + 1);
        return head + ": efeito " + std::to_string(m.effect + 1) + " - " + m.paramId;
    }
    return layerLabel(comp, m.layerId) + ": " + paramKindLabel(m.param);
}

std::string describeBlock(const Composition& comp, const Block& b)
{
    if (b.kind == BlockKind::Master)
        return "Master";
    return layerLabel(comp, b.layerId);
}

std::vector<std::string> channelOwnersFor(const Composition& comp, int universe)
{
    return channelOwners(
        comp.dmx, universe,
        [](int layerId, void* user) { return layerLabel(*static_cast<const Composition*>(user), layerId); },
        const_cast<Composition*>(&comp));
}

}
