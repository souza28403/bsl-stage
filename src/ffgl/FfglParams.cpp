#include "ffgl/FfglParams.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <set>

namespace bsl::ffgl {

namespace {

using media::Json;
using media::ParamDesc;
using media::ParamType;

std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string uniqueId(const std::string& name, int index, std::set<std::string>& used)
{
    std::string base;
    for (const char ch : lower(name)) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c))
            base += ch;
        else if (!base.empty() && base.back() != '_')
            base += '_';
    }
    while (!base.empty() && base.back() == '_')
        base.pop_back();
    if (base.empty())
        base = "p" + std::to_string(index + 1);
    std::string id = base;
    for (int n = 2; used.count(id); ++n)
        id = base + "_" + std::to_string(n);
    used.insert(id);
    return id;
}

std::string stripWord(const std::string& name, const char* const* words, size_t n, const std::string& fallback)
{
    std::string out = name;
    const std::string low = lower(name);
    for (size_t i = 0; i < n; ++i) {
        const std::string w = words[i];
        const size_t pos = low.rfind(w);
        if (pos == std::string::npos)
            continue;
        const bool atEnd = pos + w.size() == low.size();
        const bool atStart = pos == 0;
        const bool boundaryBefore = pos == 0 || !std::isalnum(static_cast<unsigned char>(low[pos - 1]));
        const bool boundaryAfter = pos + w.size() == low.size() || !std::isalnum(static_cast<unsigned char>(low[pos + w.size()]));
        if ((atEnd || atStart) && boundaryBefore && boundaryAfter) {
            out.erase(pos, w.size());
            break;
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '_' || out.back() == '-' || out.back() == '.'))
        out.pop_back();
    size_t start = 0;
    while (start < out.size() && (out[start] == ' ' || out[start] == '_' || out[start] == '-' || out[start] == '.'))
        ++start;
    out = out.substr(start);
    if (out.empty())
        return fallback;
    if (std::all_of(out.begin(), out.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == ' '; }))
        return fallback + " " + out;
    return out;
}

std::string floatFormat(float lo, float hi)
{
    const float span = hi - lo;
    if (span >= 1000.0f)
        return "%.0f";
    if (span >= 20.0f)
        return "%.1f";
    return "%.2f";
}

bool isType(const std::vector<ParamInfo>& ps, size_t i, unsigned type)
{
    return i < ps.size() && ps[i].type == type;
}

ParamDesc baseDesc(const std::string& id, const std::string& label, const std::string& group)
{
    ParamDesc d;
    d.id = id;
    d.label = label;
    d.group = group;
    return d;
}

}

std::string optionKey(float value)
{
    if (!std::isfinite(value))
        return "0";
    if (value == 0.0f)
        return "0";
    char buf[48];
    const auto r = std::to_chars(buf, buf + sizeof(buf), value);
    return std::string(buf, r.ptr);
}

float optionValue(const std::string& key)
{
    return static_cast<float>(std::strtod(key.c_str(), nullptr));
}

void rgbToHsb(float r, float g, float b, float& h, float& s, float& v)
{
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    const float d = mx - mn;
    v = mx;
    s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 0.0f) {
        h = 0.0f;
        return;
    }
    float hh;
    if (mx == r)
        hh = std::fmod((g - b) / d, 6.0f);
    else if (mx == g)
        hh = (b - r) / d + 2.0f;
    else
        hh = (r - g) / d + 4.0f;
    hh /= 6.0f;
    if (hh < 0.0f)
        hh += 1.0f;
    h = hh;
}

void hsbToRgb(float h, float s, float v, float& r, float& g, float& b)
{
    h = h - std::floor(h);
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float rr = 0, gg = 0, bb = 0;
    switch (static_cast<int>(h * 6.0f) % 6) {
    case 0: rr = c; gg = x; break;
    case 1: rr = x; gg = c; break;
    case 2: gg = c; bb = x; break;
    case 3: gg = x; bb = c; break;
    case 4: rr = x; bb = c; break;
    default: rr = c; bb = x; break;
    }
    r = rr + m;
    g = gg + m;
    b = bb + m;
}

ParamMap buildParamMap(const PluginInfo& info)
{
    ParamMap map;
    map.paramCount = static_cast<int>(info.params.size());
    std::set<std::string> used;
    const std::vector<ParamInfo>& ps = info.params;

    auto add = [&](ParamDesc d, Binding b) {
        map.descs.push_back(std::move(d));
        map.bindings.push_back(std::move(b));
    };

    for (size_t i = 0; i < ps.size();) {
        const ParamInfo& p = ps[i];
        const int idx = p.index;

        if (p.type == kPRed && isType(ps, i + 1, kPGreen) && isType(ps, i + 2, kPBlue)) {
            const bool alpha = isType(ps, i + 3, kPAlpha);
            static const char* const words[] = {"red", "vermelho", "r"};
            const std::string label = stripWord(p.name, words, 3, "Cor");
            Binding b;
            b.kind = BindKind::Rgb;
            b.count = alpha ? 4 : 3;
            for (int k = 0; k < b.count; ++k)
                b.params[k] = ps[i + static_cast<size_t>(k)].index;
            ParamDesc d = baseDesc(uniqueId(label, idx, used), label, p.group);
            d.type = ParamType::Color;
            d.def = media::param::colorJson({ps[i].defFloat, ps[i + 1].defFloat, ps[i + 2].defFloat,
                                             alpha ? ps[i + 3].defFloat : 1.0f});
            add(std::move(d), std::move(b));
            i += alpha ? 4 : 3;
            continue;
        }
        if (p.type == kPHue && isType(ps, i + 1, kPSaturation) && isType(ps, i + 2, kPBrightness)) {
            const bool alpha = isType(ps, i + 3, kPAlpha);
            static const char* const words[] = {"hue", "matiz"};
            const std::string label = stripWord(p.name, words, 2, "Cor");
            Binding b;
            b.kind = BindKind::Hsb;
            b.count = alpha ? 4 : 3;
            for (int k = 0; k < b.count; ++k)
                b.params[k] = ps[i + static_cast<size_t>(k)].index;
            float r, g, bl;
            hsbToRgb(ps[i].defFloat, ps[i + 1].defFloat, ps[i + 2].defFloat, r, g, bl);
            ParamDesc d = baseDesc(uniqueId(label, idx, used), label, p.group);
            d.type = ParamType::Color;
            d.def = media::param::colorJson({r, g, bl, alpha ? ps[i + 3].defFloat : 1.0f});
            add(std::move(d), std::move(b));
            i += alpha ? 4 : 3;
            continue;
        }
        if (p.type == kPXPos && isType(ps, i + 1, kPYPos)) {
            static const char* const words[] = {"x", "xpos"};
            const std::string label = stripWord(p.name, words, 2, "Posição");
            Binding b;
            b.kind = BindKind::Point;
            b.count = 2;
            b.params[0] = ps[i].index;
            b.params[1] = ps[i + 1].index;
            ParamDesc d = baseDesc(uniqueId(label, idx, used), label, p.group);
            d.type = ParamType::Custom;
            d.custom = "point2d";
            d.min = 0.0;
            d.max = 1.0;
            d.def = Json::array({media::param::floatJson(ps[i].defFloat), media::param::floatJson(ps[i + 1].defFloat)});
            add(std::move(d), std::move(b));
            i += 2;
            continue;
        }

        Binding b;
        b.params[0] = idx;
        b.count = 1;
        ParamDesc d = baseDesc(uniqueId(p.name, idx, used), p.name, p.group);
        switch (p.type) {
        case kPBoolean:
            b.kind = BindKind::Bool;
            d.type = ParamType::Bool;
            d.def = p.defFloat >= 0.5f;
            break;
        case kPEvent:
            b.kind = BindKind::Event;
            d.type = ParamType::Custom;
            d.custom = "event";
            d.def = 0;
            break;
        case kPInteger:
            b.kind = BindKind::Int;
            b.min = p.min;
            b.max = p.max;
            d.type = ParamType::Int;
            d.min = std::round(p.min);
            d.max = std::round(p.max);
            if (d.max <= d.min)
                d.max = d.min + 1.0;
            d.def = static_cast<int>(std::lround(std::clamp<double>(p.defFloat, d.min, d.max)));
            d.format = "%d";
            break;
        case kPOption:
            if (p.options.empty()) {
                b.kind = BindKind::Float;
                b.min = p.min;
                b.max = p.max;
                d.type = ParamType::Float;
                d.min = p.min;
                d.max = p.max;
                d.def = media::param::floatJson(std::clamp(p.defFloat, p.min, p.max));
                d.format = floatFormat(p.min, p.max);
                break;
            }
            b.kind = BindKind::Option;
            b.options = p.options;
            d.type = ParamType::Enum;
            for (const OptionInfo& o : p.options)
                d.options.push_back({optionKey(o.value), o.name});
            d.def = d.options.front().value;
            for (const OptionInfo& o : p.options)
                if (std::fabs(o.value - p.defFloat) < 1e-4f)
                    d.def = optionKey(o.value);
            break;
        case kPText:
            b.kind = BindKind::Text;
            d.type = ParamType::Text;
            d.def = p.defText;
            break;
        case kPFile: {
            b.kind = BindKind::File;
            d.type = ParamType::File;
            d.def = p.defText;
            if (!p.extensions.empty()) {
                std::string spec;
                for (const std::string& e : p.extensions)
                    spec += (spec.empty() ? "" : ";") + std::string("*.") + e;
                d.fileFilter = "Arquivos (" + spec + ")|" + spec;
            }
            break;
        }
        case kPBuffer:
            ++i;
            continue;
        case kPStandard:
        default:
            b.kind = BindKind::Float;
            b.min = p.min;
            b.max = p.max;
            d.type = ParamType::Float;
            d.min = p.min;
            d.max = p.max;
            d.def = media::param::floatJson(std::clamp(p.defFloat, p.min, p.max));
            d.format = floatFormat(p.min, p.max);
            break;
        }
        add(std::move(d), std::move(b));
        ++i;
    }
    return map;
}

void toPlugin(const ParamMap& map, size_t bi, const media::ParamSet& set, std::vector<IndexedValue>& out)
{
    const Binding& b = map.bindings[bi];
    const std::string& id = map.descs[bi].id;
    auto push = [&](int slot, float f) {
        IndexedValue v;
        v.index = b.params[slot];
        v.value.f = f;
        out.push_back(std::move(v));
    };
    switch (b.kind) {
    case BindKind::Float:
        push(0, set.f(id));
        break;
    case BindKind::Int:
        push(0, static_cast<float>(set.i(id)));
        break;
    case BindKind::Bool:
        push(0, set.b(id) ? 1.0f : 0.0f);
        break;
    case BindKind::Event: {
        const Json raw = set.raw(id);
        push(0, raw.is_number() ? raw.get<float>() : 0.0f);
        break;
    }
    case BindKind::Option: {
        const std::string key = set.s(id);
        float v = b.options.front().value;
        bool found = false;
        for (const OptionInfo& o : b.options)
            if (optionKey(o.value) == key) {
                v = o.value;
                found = true;
            }
        if (!found && !key.empty())
            v = optionValue(key);
        push(0, v);
        break;
    }
    case BindKind::Text:
    case BindKind::File: {
        IndexedValue v;
        v.index = b.params[0];
        v.value.s = set.s(id);
        out.push_back(std::move(v));
        break;
    }
    case BindKind::Rgb: {
        const media::Color c = set.c(id);
        push(0, c.r);
        push(1, c.g);
        push(2, c.b);
        if (b.count > 3)
            push(3, c.a);
        break;
    }
    case BindKind::Hsb: {
        const media::Color c = set.c(id);
        float h, s, v;
        rgbToHsb(c.r, c.g, c.b, h, s, v);
        push(0, h);
        push(1, s);
        push(2, v);
        if (b.count > 3)
            push(3, c.a);
        break;
    }
    case BindKind::Point: {
        const Json raw = set.raw(id);
        auto comp = [&](size_t k) {
            return (raw.is_array() && raw.size() > k && raw[k].is_number()) ? raw[k].get<float>() : 0.0f;
        };
        push(0, comp(0));
        push(1, comp(1));
        break;
    }
    }
}

media::Json fromPlugin(const ParamMap& map, size_t bi, const std::vector<PluginValue>& plugin)
{
    const Binding& b = map.bindings[bi];
    auto val = [&](int slot) -> const PluginValue& {
        static const PluginValue none;
        const int idx = b.params[slot];
        return (idx >= 0 && static_cast<size_t>(idx) < plugin.size()) ? plugin[static_cast<size_t>(idx)] : none;
    };
    auto clamp01 = [](float v) { return std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f); };
    switch (b.kind) {
    case BindKind::Float: {
        float v = val(0).f;
        if (!std::isfinite(v))
            v = b.min;
        if (b.max > b.min)
            v = std::clamp(v, b.min, b.max);
        return media::param::floatJson(v);
    }
    case BindKind::Int: {
        float v = val(0).f;
        if (!std::isfinite(v))
            v = b.min;
        if (b.max > b.min)
            v = std::clamp(v, b.min, b.max);
        return Json(static_cast<int>(std::lround(v)));
    }
    case BindKind::Bool:
        return Json(val(0).f >= 0.5f);
    case BindKind::Event:
        return Json();
    case BindKind::Option: {
        const float v = val(0).f;
        const OptionInfo* best = &b.options.front();
        for (const OptionInfo& o : b.options)
            if (std::fabs(o.value - v) < std::fabs(best->value - v))
                best = &o;
        return Json(optionKey(best->value));
    }
    case BindKind::Text:
    case BindKind::File:
        return Json(val(0).s);
    case BindKind::Rgb:
        return media::param::colorJson({clamp01(val(0).f), clamp01(val(1).f), clamp01(val(2).f),
                                        b.count > 3 ? clamp01(val(3).f) : 1.0f});
    case BindKind::Hsb: {
        float r, g, bl;
        hsbToRgb(clamp01(val(0).f), clamp01(val(1).f), clamp01(val(2).f), r, g, bl);
        return media::param::colorJson({clamp01(r), clamp01(g), clamp01(bl), b.count > 3 ? clamp01(val(3).f) : 1.0f});
    }
    case BindKind::Point:
        return Json::array({media::param::floatJson(val(0).f), media::param::floatJson(val(1).f)});
    }
    return Json();
}

bool pluginValueDiffers(const PluginValue& a, const PluginValue& b)
{
    if (a.s != b.s)
        return true;
    const float tol = 1e-6f * std::max(1.0f, std::max(std::fabs(a.f), std::fabs(b.f)));
    return std::fabs(a.f - b.f) > tol;
}

}
