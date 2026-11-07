#include "dmx/DmxMap.h"

#include "core/Layer.h"
#include "control/ActionRegistry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>

namespace bsl::dmx {

namespace {

struct KindInfo
{
    ParamKind kind;
    const char* key;
    const char* label;
};
constexpr KindInfo kKinds[] = {
    {ParamKind::LayerOpacity, "layerOpacity", "Opacidade da camada"},
    {ParamKind::LayerSpeed, "layerSpeed", "Velocidade do clip"},
    {ParamKind::LayerBlend, "layerBlend", "Blend da camada"},
    {ParamKind::LayerPosX, "layerPosX", "Posição X da camada"},
    {ParamKind::LayerPosY, "layerPosY", "Posição Y da camada"},
    {ParamKind::LayerScale, "layerScale", "Escala da camada"},
    {ParamKind::LayerRotation, "layerRotation", "Rotação da camada"},
    {ParamKind::MasterOpacity, "masterOpacity", "Opacidade do master"},
    {ParamKind::EffectParam, "effect", "Parâmetro de efeito"},
};

int clampInt(const nlohmann::json& j, const char* key, int def, int lo, int hi)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_number())
        return def;
    const double v = j[key].get<double>();
    if (!std::isfinite(v))
        return def;
    return static_cast<int>(std::clamp(v, static_cast<double>(lo), static_cast<double>(hi)));
}

bool boolField(const nlohmann::json& j, const char* key, bool def)
{
    return (j.is_object() && j.contains(key) && j[key].is_boolean()) ? j[key].get<bool>() : def;
}

std::string strField(const nlohmann::json& j, const char* key, const std::string& def = {})
{
    return (j.is_object() && j.contains(key) && j[key].is_string()) ? j[key].get<std::string>() : def;
}

}

const char* paramKindKey(ParamKind k)
{
    for (const KindInfo& i : kKinds)
        if (i.kind == k)
            return i.key;
    return "layerOpacity";
}

bool paramKindFromKey(const std::string& key, ParamKind& out)
{
    for (const KindInfo& i : kKinds)
        if (key == i.key) {
            out = i.kind;
            return true;
        }
    return false;
}

const char* paramKindLabel(ParamKind k)
{
    for (const KindInfo& i : kKinds)
        if (i.kind == k)
            return i.label;
    return "";
}

const char* layerModeKey(LayerMode m)
{
    switch (m) {
    case LayerMode::Basic: return "basic";
    case LayerMode::Standard: return "standard";
    case LayerMode::Full: return "full";
    case LayerMode::Full16: return "full16";
    }
    return "standard";
}

const char* layerModeLabel(LayerMode m)
{
    switch (m) {
    case LayerMode::Basic: return "Básico (2 canais)";
    case LayerMode::Standard: return "Padrão (4 canais)";
    case LayerMode::Full: return "Completo (8 canais)";
    case LayerMode::Full16: return "Completo 16 bits (12 canais)";
    }
    return "";
}

bool layerModeFromKey(const std::string& key, LayerMode& out)
{
    for (int i = 0; i < kLayerModeCount; ++i)
        if (key == layerModeKey(static_cast<LayerMode>(i))) {
            out = static_cast<LayerMode>(i);
            return true;
        }
    return false;
}

double dmxNormalized(int raw, int max)
{
    return max > 0 ? std::clamp(static_cast<double>(raw) / max, 0.0, 1.0) : 0.0;
}

double dmxCentered(int raw, int max)
{
    const double c = (static_cast<double>(max) + 1.0) / 2.0;
    const double r = std::clamp(static_cast<double>(raw), 0.0, static_cast<double>(max));
    return r <= c ? (r - c) / c : (r - c) / (static_cast<double>(max) - c);
}

double dmxScaleOf(int raw, int max)
{
    const double c = (static_cast<double>(max) + 1.0) / 2.0;
    const double r = std::clamp(static_cast<double>(raw), 0.0, static_cast<double>(max));
    return r <= c ? r / c : 1.0 + 3.0 * (r - c) / (static_cast<double>(max) - c);
}

double dmxRotationOf(int raw, int max)
{
    return dmxCentered(raw, max) * 180.0;
}

int dmxBlendOf(int raw, int max)
{
    const int raw8 = max == 255 ? raw : static_cast<int>(std::lround(dmxNormalized(raw, max) * 255.0));
    return std::clamp(raw8 / 28, 0, kBlendModeCount - 1);
}

int dmxClipRange(int raw8)
{
    return std::clamp(raw8, 0, 255) / kClipRangeWidth;
}

int dmxRawFromNormalized(double n, int max)
{
    return static_cast<int>(std::lround(std::clamp(n, 0.0, 1.0) * max));
}

int dmxDefaultRaw(Func f, int max)
{
    switch (f) {
    case Func::Speed:
    case Func::PosX:
    case Func::PosY:
    case Func::Scale:
    case Func::Rotation: return (max + 1) / 2;
    case Func::MasterOpacity: return max;
    default: return 0;
    }
}

Func dmxFuncOf(ParamKind k)
{
    switch (k) {
    case ParamKind::LayerOpacity: return Func::Opacity;
    case ParamKind::LayerSpeed: return Func::Speed;
    case ParamKind::LayerBlend: return Func::Blend;
    case ParamKind::LayerPosX: return Func::PosX;
    case ParamKind::LayerPosY: return Func::PosY;
    case ParamKind::LayerScale: return Func::Scale;
    case ParamKind::LayerRotation: return Func::Rotation;
    case ParamKind::MasterOpacity: return Func::MasterOpacity;
    default: return Func::Fx;
    }
}

int layerChannelCount(LayerMode mode, int fxCount)
{
    int base = 2;
    switch (mode) {
    case LayerMode::Basic: base = 2; break;
    case LayerMode::Standard: base = 4; break;
    case LayerMode::Full: base = 8; break;
    case LayerMode::Full16: base = 12; break;
    }
    return base + std::clamp(fxCount, 0, kMaxFxSlots);
}

std::string personalityName(LayerMode mode, int fxCount)
{
    const char* base = mode == LayerMode::Basic ? "Basic" : mode == LayerMode::Standard ? "Standard"
                       : mode == LayerMode::Full ? "Full" : "Full 16bit";
    std::string name = base;
    if (fxCount > 0)
        name += " FX" + std::to_string(std::clamp(fxCount, 0, kMaxFxSlots));
    return name;
}

std::vector<ClipRange> clipRanges(int columns)
{
    std::vector<ClipRange> out;
    out.push_back({0, kClipRangeWidth - 1, "Nenhum", "None"});
    out.push_back({kClipRangeWidth, 2 * kClipRangeWidth - 1, "Limpar camada", "Clear"});
    for (int c = 1; c <= std::clamp(columns, 1, kClipMaxColumns); ++c) {
        const int from = (c + 1) * kClipRangeWidth;
        out.push_back({from, std::min(255, from + kClipRangeWidth - 1), "Coluna " + std::to_string(c), "Column " + std::to_string(c)});
    }
    return out;
}

std::vector<ChannelRow> layerRows(LayerMode mode, int fxCount)
{
    std::vector<ChannelRow> rows;
    int at = 1;
    auto add = [&](Func f, int span, const char* name, const char* key, const std::string& range, int defaultRaw,
                   const char* note) {
        ChannelRow r;
        r.func = f;
        r.offset = at;
        r.span = span;
        r.name = name;
        r.key = key;
        r.range = range;
        r.defaultRaw = defaultRaw;
        r.note = note;
        rows.push_back(std::move(r));
        at += span;
    };
    const bool std8 = mode != LayerMode::Basic;
    const bool xf = mode == LayerMode::Full || mode == LayerMode::Full16;
    const int span = mode == LayerMode::Full16 ? 2 : 1;
    const int mx = span == 2 ? 65535 : 255;

    add(Func::Opacity, 1, "Opacidade", "Dimmer", "0-255 = 0-100%", 0, "Opacidade da camada");
    add(Func::Clip, 1, "Clip / coluna", "BSL_Clip",
        "0-9 nada | 10-19 limpar camada | 20-29 coluna 1 | 30-39 coluna 2 | ... (faixas de 10, até a coluna 24)", 0,
        "Dispara ao entrar na faixa; para disparar o mesmo clip de novo, passe pela faixa 0-9");
    if (std8) {
        add(Func::Speed, 1, "Velocidade", "BSL_Speed", "0 = parado | 128 = 1x | 255 = 4x", 128, "Velocidade do clip que toca na camada");
        std::string blend = "faixas de 28: ";
        for (int i = 0; i < kBlendModeCount; ++i)
            blend += std::string(i ? " | " : "") + std::to_string(i * 28) + "+ " + blendModeName(static_cast<BlendMode>(i));
        add(Func::Blend, 1, "Blend", "BSL_Blend", blend, 0, "Modo de mistura da camada");
    }
    if (xf) {
        add(Func::PosX, span, "Posição X", "BSL_PosX", "metade = centro | 0 = canvas inteiro para a esquerda | máximo = para a direita",
            dmxDefaultRaw(Func::PosX, mx), "Desloca a camada, em fração da largura do canvas (-100% a +100%)");
        add(Func::PosY, span, "Posição Y", "BSL_PosY", "metade = centro | 0 = para cima | máximo = para baixo",
            dmxDefaultRaw(Func::PosY, mx), "Desloca a camada, em fração da altura do canvas (-100% a +100%)");
        add(Func::Scale, span, "Escala", "BSL_Scale", "0 = 0% | metade = 100% | máximo = 400%", dmxDefaultRaw(Func::Scale, mx),
            "Tamanho da camada, uniforme");
        add(Func::Rotation, span, "Rotação", "BSL_Rotation", "metade = 0° | 0 = -180° | máximo = +180°",
            dmxDefaultRaw(Func::Rotation, mx), "Gira em volta da âncora");
    }
    const int fx = std::clamp(fxCount, 0, kMaxFxSlots);
    for (int i = 0; i < fx; ++i) {
        add(Func::Fx, 1, "", "", "0-255 = mínimo-máximo do parâmetro", 0, "Parâmetro de efeito da camada, escolhido no patch");
        rows.back().fxIndex = i;
        rows.back().name = "Efeito " + std::to_string(i + 1);
        rows.back().key = "BSL_FX" + std::to_string(i + 1);
    }
    return rows;
}

std::vector<ChannelRow> masterRows()
{
    std::vector<ChannelRow> rows;
    auto add = [&](Func f, int offset, const char* name, const char* key, const char* range, int def, const char* note) {
        ChannelRow r;
        r.func = f;
        r.offset = offset;
        r.name = name;
        r.key = key;
        r.range = range;
        r.defaultRaw = def;
        r.note = note;
        rows.push_back(std::move(r));
    };
    add(Func::MasterOpacity, 1, "Opacidade master", "Dimmer", "0-255 = 0-100%", 255, "Fade da saída final");
    add(Func::Blackout, 2, "Blackout", "BSL_Blackout", "0-127 normal | 128-255 blackout", 0, "Apaga a saída enquanto estiver alto");
    add(Func::Column, 3, "Coluna (global)", "BSL_Column",
        "0-9 nada | 10-19 limpar tudo | 20-29 coluna 1 | 30-39 coluna 2 | ... (faixas de 10)", 0,
        "Dispara a coluna em todas as camadas ao entrar na faixa");
    return rows;
}

int Block::channelCount() const
{
    return kind == BlockKind::Master ? masterChannelCount() : layerChannelCount(mode, static_cast<int>(fx.size()));
}

bool sameTarget(const Map& a, const Map& b)
{
    if (a.type != b.type)
        return false;
    if (a.type == Map::Type::Action)
        return a.binding.action == b.binding.action && a.binding.args == b.binding.args;
    return a.param == b.param && a.layerId == b.layerId && a.fxScope == b.fxScope && a.effect == b.effect &&
           a.paramId == b.paramId && (a.fxScope != 0 || (a.deck == b.deck && a.column == b.column));
}

void Config::assignUids()
{
    std::set<int> used;
    for (const Block& b : blocks)
        if (b.uid > 0)
            used.insert(b.uid);
    for (const Map& m : maps)
        if (m.uid > 0)
            used.insert(m.uid);
    for (int u : used)
        nextUid = std::max(nextUid, u + 1);
    std::set<int> seen;
    for (Block& b : blocks)
        if (b.uid <= 0 || !seen.insert(b.uid).second)
            b.uid = nextUid++;
    for (Map& m : maps)
        if (m.uid <= 0 || !seen.insert(m.uid).second)
            m.uid = nextUid++;
}

Block& Config::addBlock(Block b)
{
    b.uid = nextUid++;
    blocks.push_back(std::move(b));
    return blocks.back();
}

Map& Config::addMap(Map m)
{
    m.uid = nextUid++;
    maps.push_back(std::move(m));
    return maps.back();
}

std::vector<int> Config::universesUsed() const
{
    std::set<int> u;
    for (const Block& b : blocks)
        if (b.enabled)
            u.insert(b.universe);
    for (const Map& m : maps)
        if (m.enabled)
            u.insert(m.universe);
    return std::vector<int>(u.begin(), u.end());
}

bool Config::sameContent(const Config& o) const
{
    if (blocks.size() != o.blocks.size() || maps.size() != o.maps.size())
        return false;
    for (size_t i = 0; i < blocks.size(); ++i) {
        Block a = blocks[i], b = o.blocks[i];
        a.uid = b.uid = 0;
        if (!(a == b))
            return false;
    }
    for (size_t i = 0; i < maps.size(); ++i) {
        Map a = maps[i], b = o.maps[i];
        a.uid = b.uid = 0;
        if (!(a == b))
            return false;
    }
    return true;
}

std::vector<std::string> channelOwners(const Config& cfg, int universe, LayerNamer namer, void* user)
{
    std::vector<std::string> out(kChannels);
    auto name = [&](int layerId) {
        return namer ? namer(layerId, user) : std::string("Camada ") + std::to_string(layerId);
    };
    auto put = [&](int channel, int span, const std::string& label) {
        for (int i = 0; i < span; ++i) {
            const int idx = channel - 1 + i;
            if (idx < 0 || idx >= kChannels)
                continue;
            out[static_cast<size_t>(idx)] += (out[static_cast<size_t>(idx)].empty() ? "" : " + ") + label;
        }
    };
    for (const Block& b : cfg.blocks) {
        if (!b.enabled || b.universe != universe)
            continue;
        const std::string who = b.kind == BlockKind::Master ? "Master" : name(b.layerId);
        const std::vector<ChannelRow> rows = b.kind == BlockKind::Master ? masterRows() : layerRows(b.mode, static_cast<int>(b.fx.size()));
        for (const ChannelRow& r : rows)
            put(b.channel + r.offset - 1, r.span, who + " - " + r.name);
    }
    for (const Map& m : cfg.maps) {
        if (!m.enabled || m.universe != universe)
            continue;
        std::string label;
        if (m.type == Map::Type::Action) {
            const control::ActionDef* def = control::findAction(m.binding.action);
            label = def ? def->label : m.binding.action;
        } else if (m.param == ParamKind::EffectParam) {
            label = "Efeito " + std::to_string(m.effect + 1) + ": " + m.paramId;
        } else {
            label = paramKindLabel(m.param);
        }
        put(m.channel, m.channelSpan(), "Aprendido: " + label);
    }
    return out;
}

nlohmann::json configToJson(const Config& cfg)
{
    nlohmann::json root = nlohmann::json::object();
    root["version"] = 1;
    nlohmann::json blocks = nlohmann::json::array();
    for (const Block& b : cfg.blocks) {
        nlohmann::json j = nlohmann::json::object();
        j["kind"] = b.kind == BlockKind::Master ? "master" : "layer";
        if (b.kind == BlockKind::Layer) {
            j["layerId"] = b.layerId;
            j["mode"] = layerModeKey(b.mode);
        }
        j["universe"] = b.universe;
        j["channel"] = b.channel;
        if (!b.enabled)
            j["enabled"] = false;
        if (!b.fx.empty()) {
            nlohmann::json fx = nlohmann::json::array();
            for (const FxSlot& s : b.fx)
                fx.push_back({{"effect", s.effect}, {"param", s.param}});
            j["fx"] = std::move(fx);
        }
        blocks.push_back(std::move(j));
    }
    root["blocks"] = std::move(blocks);

    nlohmann::json maps = nlohmann::json::array();
    for (const Map& m : cfg.maps) {
        nlohmann::json j = nlohmann::json::object();
        j["type"] = m.type == Map::Type::Action ? "action" : "param";
        j["universe"] = m.universe;
        j["channel"] = m.channel;
        if (m.bit16)
            j["bit16"] = true;
        if (!m.enabled)
            j["enabled"] = false;
        if (m.type == Map::Type::Param) {
            j["param"] = paramKindKey(m.param);
            if (m.param != ParamKind::MasterOpacity && !(m.param == ParamKind::EffectParam && m.fxScope == 2))
                j["layerId"] = m.layerId;
            if (m.param == ParamKind::EffectParam) {
                j["fxScope"] = m.fxScope;
                j["effect"] = m.effect;
                j["paramId"] = m.paramId;
                if (m.fxScope == 0) {
                    j["deck"] = m.deck;
                    j["column"] = m.column;
                }
            }
            if (m.invert)
                j["invert"] = true;
        } else {
            control::KeyBindingSet one;
            one.items.push_back(m.binding);
            j["binding"] = control::bindingsToJson(one).at(0);
            j["threshold"] = m.threshold;
        }
        maps.push_back(std::move(j));
    }
    root["maps"] = std::move(maps);
    return root;
}

Config configFromJson(const nlohmann::json& j, std::vector<std::string>* warnings)
{
    Config cfg;
    if (!j.is_object())
        return cfg;
    auto warn = [&](const std::string& msg) {
        if (warnings)
            warnings->push_back("DMX: " + msg);
    };
    if (clampInt(j, "version", 1, 0, 1 << 20) > 1)
        warn("este projeto foi salvo por uma versão mais nova; parte do patch DMX pode ser ignorada.");

    if (j.contains("blocks") && j["blocks"].is_array()) {
        for (const nlohmann::json& e : j["blocks"]) {
            if (static_cast<int>(cfg.blocks.size()) >= kMaxBlocks || !e.is_object())
                continue;
            Block b;
            const std::string kind = strField(e, "kind", "layer");
            b.kind = kind == "master" ? BlockKind::Master : BlockKind::Layer;
            if (b.kind == BlockKind::Layer) {
                b.layerId = clampInt(e, "layerId", 0, 0, 1 << 30);
                b.layerIndex = clampInt(e, "layer", -1, -1, 4096);
                if (!layerModeFromKey(strField(e, "mode", "standard"), b.mode))
                    warn("modo de camada desconhecido, usando Padrão.");
            }
            b.universe = clampInt(e, "universe", 1, 1, kMaxUniverse);
            b.channel = clampInt(e, "channel", 1, 1, kChannels);
            b.enabled = boolField(e, "enabled", true);
            if (e.contains("fx") && e["fx"].is_array() && b.kind == BlockKind::Layer) {
                for (const nlohmann::json& f : e["fx"]) {
                    if (static_cast<int>(b.fx.size()) >= kMaxFxSlots)
                        break;
                    FxSlot s;
                    s.effect = clampInt(f, "effect", 0, 0, 4096);
                    s.param = strField(f, "param");
                    b.fx.push_back(std::move(s));
                }
            }
            if (b.lastChannel() > kChannels)
                warn("um bloco passa do canal 512 (o excesso é ignorado).");
            cfg.blocks.push_back(std::move(b));
        }
    }

    if (j.contains("maps") && j["maps"].is_array()) {
        for (const nlohmann::json& e : j["maps"]) {
            if (static_cast<int>(cfg.maps.size()) >= kMaxMaps || !e.is_object())
                continue;
            Map m;
            m.universe = clampInt(e, "universe", 1, 1, kMaxUniverse);
            m.channel = clampInt(e, "channel", 1, 1, kChannels);
            m.bit16 = boolField(e, "bit16", false);
            m.enabled = boolField(e, "enabled", true);
            if (strField(e, "type", "param") == "action") {
                m.type = Map::Type::Action;
                m.threshold = clampInt(e, "threshold", 128, 1, 255);
                std::vector<std::string> kw;
                control::KeyBindingSet one = control::bindingsFromJson(nlohmann::json::array({e.value("binding", nlohmann::json::object())}), &kw);
                if (one.items.empty()) {
                    warn(kw.empty() ? "ação ignorada." : kw.front());
                    continue;
                }
                const control::ActionDef* def = control::findAction(one.items.front().action);
                if (!def || !def->show) {
                    warn("a ação \"" + one.items.front().action + "\" não pode ser disparada por DMX.");
                    continue;
                }
                m.binding = one.items.front();
                m.binding.chord = control::Chord{};
                m.binding.global = false;
                m.layerIndex = -1;
            } else {
                m.type = Map::Type::Param;
                if (!paramKindFromKey(strField(e, "param"), m.param)) {
                    warn("parâmetro desconhecido ignorado.");
                    continue;
                }
                m.layerId = clampInt(e, "layerId", 0, 0, 1 << 30);
                m.layerIndex = clampInt(e, "layer", -1, -1, 4096);
                m.fxScope = clampInt(e, "fxScope", 1, 0, 2);
                m.effect = clampInt(e, "effect", 0, 0, 4096);
                m.paramId = strField(e, "paramId");
                m.deck = clampInt(e, "deck", -1, -1, 4096);
                m.column = clampInt(e, "column", 0, 0, 4096);
                m.invert = boolField(e, "invert", false);
            }
            cfg.maps.push_back(std::move(m));
        }
    }
    cfg.assignUids();
    return cfg;
}

int stampLayerIds(Config& cfg, const std::vector<int>& idsByIndex)
{
    int n = 0;
    auto stamp = [&](int& layerId, int& layerIndex) {
        if (layerId > 0 || layerIndex < 0)
            return;
        if (layerIndex < static_cast<int>(idsByIndex.size()) && idsByIndex[static_cast<size_t>(layerIndex)] > 0) {
            layerId = idsByIndex[static_cast<size_t>(layerIndex)];
            layerIndex = -1;
            ++n;
        }
    };
    for (Block& b : cfg.blocks)
        if (b.kind == BlockKind::Layer)
            stamp(b.layerId, b.layerIndex);
    for (Map& m : cfg.maps) {
        if (m.type == Map::Type::Param) {
            stamp(m.layerId, m.layerIndex);
            continue;
        }
        const control::ActionDef* def = control::findAction(m.binding.action);
        control::ActionArgs& a = m.binding.args;
        if (!def || (def->args & control::ArgLayer) == 0 || a.selected || a.layerId > 0)
            continue;
        if ((def->args & control::ArgEffect) != 0 && a.fxScope == 2)
            continue;
        if (a.layer >= 0 && a.layer < static_cast<int>(idsByIndex.size()) && idsByIndex[static_cast<size_t>(a.layer)] > 0) {
            a.layerId = idsByIndex[static_cast<size_t>(a.layer)];
            a.layer = 0;
            ++n;
        }
    }
    return n;
}

}
