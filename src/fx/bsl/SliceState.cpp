#include "fx/bsl/SliceState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bsl::fx::bsllight {

using media::Json;

const char* const kSliceEffectIds[kSliceEffects] = {"noise", "spiral", "radial", "zebra",  "disco",  "chaser",
                                                    "dashes", "rainbow", "sparkle", "glitch", "pulse", "scanner"};
const char* const kSliceEffectLabels[kSliceEffects] = {"Manchas", "Espiral",   "Ondas",  "Zebra", "Disco",  "Corrida",
                                                       "Tracejado", "Arco-íris", "Brilhos", "Glitch", "Pulsar", "Scanner"};

namespace {

float clampf(double v, double lo, double hi)
{
    if (!std::isfinite(v))
        return static_cast<float>(lo);
    return static_cast<float>(std::min(hi, std::max(lo, v)));
}

const Json* member(const Json* o, const char* key)
{
    if (!o || !o->is_object())
        return nullptr;
    const auto it = o->find(key);
    return it == o->end() ? nullptr : &*it;
}

float getNum(const Json* o, const char* key, float def, double lo, double hi)
{
    const Json* v = member(o, key);
    if (!v)
        return def;
    if (v->is_number())
        return clampf(v->get<double>(), lo, hi);
    if (v->is_boolean())
        return clampf(v->get<bool>() ? 1.0 : 0.0, lo, hi);
    return def;
}

bool getBool(const Json* o, const char* key, bool def)
{
    const Json* v = member(o, key);
    if (!v)
        return def;
    if (v->is_boolean())
        return v->get<bool>();
    if (v->is_number())
        return v->get<double>() != 0.0;
    return def;
}

std::string getStr(const Json* o, const char* key, const std::string& def)
{
    const Json* v = member(o, key);
    return v && v->is_string() ? v->get<std::string>() : def;
}

const Json* getObj(const Json* o, const char* key)
{
    const Json* v = member(o, key);
    return v && v->is_object() ? v : nullptr;
}

SliceMode parseMode(const std::string& s, SliceMode def)
{
    if (s == "off")
        return SliceMode::Off;
    if (s == "speed")
        return SliceMode::Speed;
    if (s == "bpm")
        return SliceMode::Bpm;
    return def;
}

const char* modeName(SliceMode m)
{
    switch (m) {
    case SliceMode::Speed: return "speed";
    case SliceMode::Bpm: return "bpm";
    default: return "off";
    }
}

std::string limitBytes(std::string s, size_t maxLen)
{
    if (s.size() > maxLen) {
        s.resize(maxLen);
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80)
            s.pop_back();
        if (!s.empty() && static_cast<unsigned char>(s.back()) >= 0xC0)
            s.pop_back();
    }
    return s;
}

void groupFromJson(const Json& gv, SliceGroup& g)
{
    g.name = limitBytes(getStr(&gv, "name", g.name), 64);
    g.enabled = getBool(&gv, "enabled", g.enabled);
    {
        const std::string f = getStr(&gv, "fill", g.fillVideo ? "video" : "none");
        if (f == "video")
            g.fillVideo = true;
        else if (f == "none")
            g.fillVideo = false;
    }
    g.masterSize = getNum(&gv, "masterSize", g.masterSize, 0.0, 1.0);
    if (const Json* c = member(&gv, "corners"); c && c->is_array())
        for (size_t i = 0; i < 4 && i < c->size(); ++i)
            if ((*c)[i].is_number())
                g.corners[i] = clampf((*c)[i].get<double>(), 0.0, 1.0);
    g.equalCorners = getBool(&gv, "equalCorners", g.equalCorners);
    g.offset = getNum(&gv, "offset", g.offset, -200.0, 200.0);
    g.merge = getBool(&gv, "merge", g.merge);
    g.mergeSmooth = getNum(&gv, "mergeSmooth", g.mergeSmooth, 0.0, 100.0);
    g.softEdge = getNum(&gv, "softEdge", g.softEdge, 0.0, 1.0);

    if (const Json* st = getObj(&gv, "stroke")) {
        g.strokeWidth = getNum(st, "width", g.strokeWidth, 0.0, 100.0);
        {
            const std::string m = getStr(st, "mode", g.strokeEffectMode ? "effect" : "stroke");
            if (m == "effect")
                g.strokeEffectMode = true;
            else if (m == "stroke")
                g.strokeEffectMode = false;
        }
        {
            float rgb[3];
            if (const Json* c = member(st, "color"); c && c->is_string() && parseHexColor(c->get<std::string>(), rgb))
                std::copy(rgb, rgb + 3, g.color);
            if (const Json* c = member(st, "colorB"); c && c->is_string() && parseHexColor(c->get<std::string>(), rgb))
                std::copy(rgb, rgb + 3, g.colorB);
        }
        {
            const std::string e = getStr(st, "effect", kSliceEffectIds[g.effect]);
            for (int i = 0; i < kSliceEffects; ++i)
                if (e == kSliceEffectIds[i])
                    g.effect = i;
        }
        g.speed = getNum(st, "speed", g.speed, 0.0, 2.0);
        g.variation = getNum(st, "variation", g.variation, 0.0, 1.0);
        g.smooth = getBool(st, "smooth", g.smooth);
        g.symX = getBool(st, "symX", g.symX);
        g.symY = getBool(st, "symY", g.symY);
        g.glow = getNum(st, "glow", g.glow, 0.0, 1.0);
        if (const Json* d = getObj(st, "dual")) {
            g.dualOn = getBool(d, "on", g.dualOn);
            g.dualWidth = getNum(d, "width", g.dualWidth, 0.0, 100.0);
            g.dualGap = getNum(d, "gap", g.dualGap, 0.0, 100.0);
        }
    }

    if (const Json* b = getObj(&gv, "blinker")) {
        g.blinkMode = parseMode(getStr(b, "mode", modeName(g.blinkMode)), g.blinkMode);
        g.blinkRate = getNum(b, "rate", g.blinkRate, 0.05, 32.0);
    }
    if (const Json* b = getObj(&gv, "bounce")) {
        g.bounceMode = parseMode(getStr(b, "mode", modeName(g.bounceMode)), g.bounceMode);
        g.bounceSpeed = getNum(b, "speed", g.bounceSpeed, 0.0, 16.0);
        g.bounceBeats = getNum(b, "beats", g.bounceBeats, 0.125, 64.0);
        g.bounceMin = getNum(b, "min", g.bounceMin, 0.0, 1.0);
        g.bounceMulti = getBool(b, "multi", g.bounceMulti);
        g.bounceSmooth = getBool(b, "smooth", g.bounceSmooth);
    }
    if (const Json* r = getObj(&gv, "colorRotate")) {
        g.rotOn = getBool(r, "on", g.rotOn);
        g.rotSpeed = getNum(r, "speed", g.rotSpeed, 0.0, 4.0);
        g.rotBars = getNum(r, "bars", g.rotBars, 0.0, 64.0);
        g.rotBars = std::floor(g.rotBars + 0.5f);
    }
}

Json num(float v)
{
    return media::param::floatJson(v);
}

}

bool parseHexColor(const std::string& s, float rgb[3])
{
    std::string h = s;
    if (!h.empty() && h[0] == '#')
        h.erase(0, 1);
    auto hexv = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    if (h.size() == 3) {
        for (int i = 0; i < 3; ++i) {
            const int v = hexv(h[static_cast<size_t>(i)]);
            if (v < 0)
                return false;
            rgb[i] = static_cast<float>(v * 17) / 255.0f;
        }
        return true;
    }
    if (h.size() == 6 || h.size() == 8) {
        for (int i = 0; i < 3; ++i) {
            const int a = hexv(h[static_cast<size_t>(i) * 2]), b = hexv(h[static_cast<size_t>(i) * 2 + 1]);
            if (a < 0 || b < 0)
                return false;
            rgb[i] = static_cast<float>(a * 16 + b) / 255.0f;
        }
        return true;
    }
    return false;
}

std::string formatHexColor(const float rgb[3])
{
    char buf[16];
    auto q = [](float v) { return static_cast<int>(std::min(1.0f, std::max(0.0f, v)) * 255.0f + 0.5f); };
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", q(rgb[0]), q(rgb[1]), q(rgb[2]));
    return buf;
}

void setDefaultSliceGroup(SliceGroup& g, int index)
{
    g = SliceGroup();
    g.name = "Grupo " + std::to_string(index + 1);
}

SliceState::SliceState()
{
    for (int i = 0; i < kSliceGroups; ++i)
        setDefaultSliceGroup(groups[i], i);
}

bool sliceStateFromJson(const Json& v, SliceState& out, std::string* error)
{
    if (!v.is_object()) {
        if (error)
            *error = "o estado precisa ser um objeto JSON";
        return false;
    }
    SliceState s;
    if (const Json* a = getObj(&v, "assign")) {
        for (auto it = a->begin(); it != a->end() && s.assign.size() < 256; ++it) {
            if (!it.value().is_number() || it.key().empty() || it.key().size() > 256)
                continue;
            const int gi = static_cast<int>(std::floor(it.value().get<double>() + 0.5));
            if (gi < 0 || gi >= kSliceGroups)
                continue;
            s.assign[it.key()] = gi;
        }
    }
    if (const Json* gs = member(&v, "groups"); gs && gs->is_array())
        for (size_t i = 0; i < static_cast<size_t>(kSliceGroups) && i < gs->size(); ++i)
            if ((*gs)[i].is_object())
                groupFromJson((*gs)[i], s.groups[i]);
    out = std::move(s);
    return true;
}

Json sliceStateToJson(const SliceState& s)
{
    Json root = Json::object();
    root["v"] = 1;
    Json assign = Json::object();
    for (const auto& [id, g] : s.assign)
        assign[id] = g;
    root["assign"] = std::move(assign);

    Json groups = Json::array();
    for (int i = 0; i < kSliceGroups; ++i) {
        const SliceGroup& g = s.groups[i];
        Json o = Json::object();
        o["name"] = g.name;
        o["enabled"] = g.enabled;
        o["fill"] = g.fillVideo ? "video" : "none";
        o["masterSize"] = num(g.masterSize);
        o["corners"] = Json::array({num(g.corners[0]), num(g.corners[1]), num(g.corners[2]), num(g.corners[3])});
        o["equalCorners"] = g.equalCorners;
        o["offset"] = num(g.offset);
        o["merge"] = g.merge;
        o["mergeSmooth"] = num(g.mergeSmooth);
        o["softEdge"] = num(g.softEdge);

        Json st = Json::object();
        st["width"] = num(g.strokeWidth);
        st["mode"] = g.strokeEffectMode ? "effect" : "stroke";
        st["color"] = formatHexColor(g.color);
        st["colorB"] = formatHexColor(g.colorB);
        st["effect"] = kSliceEffectIds[g.effect];
        st["speed"] = num(g.speed);
        st["variation"] = num(g.variation);
        st["smooth"] = g.smooth;
        st["symX"] = g.symX;
        st["symY"] = g.symY;
        st["glow"] = num(g.glow);
        st["dual"] = Json{{"on", g.dualOn}, {"width", num(g.dualWidth)}, {"gap", num(g.dualGap)}};
        o["stroke"] = std::move(st);

        o["blinker"] = Json{{"mode", modeName(g.blinkMode)}, {"rate", num(g.blinkRate)}};
        o["bounce"] = Json{{"mode", modeName(g.bounceMode)}, {"speed", num(g.bounceSpeed)}, {"beats", num(g.bounceBeats)},
                           {"min", num(g.bounceMin)},        {"multi", g.bounceMulti},      {"smooth", g.bounceSmooth}};
        o["colorRotate"] = Json{{"on", g.rotOn}, {"speed", num(g.rotSpeed)}, {"bars", num(g.rotBars)}};
        groups.push_back(std::move(o));
    }
    root["groups"] = std::move(groups);
    return root;
}

Json defaultSliceStateJson()
{
    return sliceStateToJson(SliceState());
}

}
