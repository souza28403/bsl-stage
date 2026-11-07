#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace bsl::media {

using Json = nlohmann::json;

enum class ParamType { Float, Int, Bool, Color, Text, TextMultiline, Enum, Font, File, Custom };

struct ParamOption
{
    std::string value;
    std::string label;
};

struct Color
{
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

struct ParamDesc
{
    std::string id;
    std::string label;
    ParamType type = ParamType::Float;
    std::string group;
    double min = 0.0;
    double max = 1.0;
    Json def;
    std::vector<ParamOption> options;
    std::string format;
    std::string tooltip;
    std::string enabledIf;
    std::string custom;
    std::string fileFilter;
    int maxLength = 0;
};

namespace param {

inline Json floatJson(float v)
{
    if (!std::isfinite(v))
        return Json(0.0);
    char buf[48];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v);
    return Json(std::strtod(std::string(buf, r.ptr).c_str(), nullptr));
}

inline Json colorJson(Color c)
{
    return Json::array({floatJson(c.r), floatJson(c.g), floatJson(c.b), floatJson(c.a)});
}

inline ParamDesc makeFloat(std::string id, std::string label, float def, float lo, float hi,
                           std::string format = "%.2f", std::string group = {})
{
    ParamDesc d;
    d.id = std::move(id);
    d.label = std::move(label);
    d.type = ParamType::Float;
    d.min = lo;
    d.max = hi;
    d.def = floatJson(def);
    d.format = std::move(format);
    d.group = std::move(group);
    return d;
}

inline ParamDesc makeInt(std::string id, std::string label, int def, int lo, int hi, std::string format = "%d",
                         std::string group = {})
{
    ParamDesc d;
    d.id = std::move(id);
    d.label = std::move(label);
    d.type = ParamType::Int;
    d.min = lo;
    d.max = hi;
    d.def = def;
    d.format = std::move(format);
    d.group = std::move(group);
    return d;
}

inline ParamDesc makeBool(std::string id, std::string label, bool def, std::string group = {})
{
    ParamDesc d;
    d.id = std::move(id);
    d.label = std::move(label);
    d.type = ParamType::Bool;
    d.def = def;
    d.group = std::move(group);
    return d;
}

inline ParamDesc makeColor(std::string id, std::string label, Color def, std::string group = {})
{
    ParamDesc d;
    d.id = std::move(id);
    d.label = std::move(label);
    d.type = ParamType::Color;
    d.def = colorJson(def);
    d.group = std::move(group);
    return d;
}

inline ParamDesc makeText(std::string id, std::string label, std::string def, bool multiline = false,
                          std::string group = {})
{
    ParamDesc d;
    d.id = std::move(id);
    d.label = std::move(label);
    d.type = multiline ? ParamType::TextMultiline : ParamType::Text;
    d.def = std::move(def);
    d.group = std::move(group);
    return d;
}

inline ParamDesc makeEnum(std::string id, std::string label, std::vector<ParamOption> options, std::string def,
                          std::string group = {})
{
    ParamDesc d;
    d.id = std::move(id);
    d.label = std::move(label);
    d.type = ParamType::Enum;
    d.options = std::move(options);
    d.def = std::move(def);
    d.group = std::move(group);
    return d;
}

inline ParamDesc makeFont(std::string id, std::string label, std::string def, std::string group = {})
{
    ParamDesc d;
    d.id = std::move(id);
    d.label = std::move(label);
    d.type = ParamType::Font;
    d.def = std::move(def);
    d.group = std::move(group);
    return d;
}

inline ParamDesc makeFile(std::string id, std::string label, std::string filter = {}, std::string group = {})
{
    ParamDesc d;
    d.id = std::move(id);
    d.label = std::move(label);
    d.type = ParamType::File;
    d.def = std::string();
    d.fileFilter = std::move(filter);
    d.group = std::move(group);
    return d;
}

inline ParamDesc makeCustom(std::string id, std::string label, std::string editor, Json def, std::string group = {})
{
    ParamDesc d;
    d.id = std::move(id);
    d.label = std::move(label);
    d.type = ParamType::Custom;
    d.custom = std::move(editor);
    d.def = std::move(def);
    d.group = std::move(group);
    return d;
}

}

class ParamValues
{
public:
    ParamValues() : j_(Json::object()) {}
    explicit ParamValues(const Json& j) : j_(j.is_object() ? j : Json::object()) {}

    bool empty() const { return j_.empty(); }
    bool has(const std::string& id) const { return j_.contains(id); }
    const Json* find(const std::string& id) const
    {
        const auto it = j_.find(id);
        return it == j_.end() ? nullptr : &*it;
    }
    void set(const std::string& id, Json value) { j_[id] = std::move(value); }
    void erase(const std::string& id) { j_.erase(id); }
    const Json& json() const { return j_; }

    void fillDefaults(const std::vector<ParamDesc>& descs)
    {
        for (const ParamDesc& d : descs)
            if (!d.def.is_null() && !j_.contains(d.id))
                j_[d.id] = d.def;
    }

    bool operator==(const ParamValues& o) const { return j_ == o.j_; }
    bool operator!=(const ParamValues& o) const { return !(*this == o); }

private:
    Json j_;
};

class ParamSet
{
public:
    ParamSet(std::vector<ParamDesc> descs, const ParamValues& values) : descs_(std::move(descs)), values_(values) {}

    const ParamDesc* desc(const std::string& id) const
    {
        for (const ParamDesc& d : descs_)
            if (d.id == id)
                return &d;
        return nullptr;
    }

    float f(const std::string& id) const
    {
        const ParamDesc* d = desc(id);
        const Json* v = values_.find(id);
        double x = (v && v->is_number()) ? v->get<double>() : (d && d->def.is_number() ? d->def.get<double>() : 0.0);
        if (!std::isfinite(x))
            x = 0.0;
        if (d && d->max > d->min)
            x = std::clamp(x, d->min, d->max);
        return static_cast<float>(x);
    }

    int i(const std::string& id) const { return static_cast<int>(std::lround(f(id))); }

    bool b(const std::string& id) const
    {
        const ParamDesc* d = desc(id);
        const Json* v = values_.find(id);
        if (v && v->is_boolean())
            return v->get<bool>();
        return d && d->def.is_boolean() && d->def.get<bool>();
    }

    Color c(const std::string& id) const
    {
        const ParamDesc* d = desc(id);
        const Json* v = values_.find(id);
        if (!(v && isColor(*v)))
            v = d ? &d->def : nullptr;
        Color out;
        if (v && isColor(*v)) {
            out.r = std::clamp((*v)[0].get<float>(), 0.0f, 1.0f);
            out.g = std::clamp((*v)[1].get<float>(), 0.0f, 1.0f);
            out.b = std::clamp((*v)[2].get<float>(), 0.0f, 1.0f);
            out.a = std::clamp((*v)[3].get<float>(), 0.0f, 1.0f);
        }
        return out;
    }

    std::string s(const std::string& id) const
    {
        const ParamDesc* d = desc(id);
        const Json* v = values_.find(id);
        if (v && v->is_string())
            return v->get<std::string>();
        return (d && d->def.is_string()) ? d->def.get<std::string>() : std::string();
    }

    Json raw(const std::string& id) const
    {
        const ParamDesc* d = desc(id);
        const Json* v = values_.find(id);
        if (v)
            return *v;
        return d ? d->def : Json();
    }

    const std::vector<ParamDesc>& descs() const { return descs_; }
    const ParamValues& values() const { return values_; }

private:
    static bool isColor(const Json& v)
    {
        if (!v.is_array() || v.size() != 4)
            return false;
        for (const Json& e : v)
            if (!e.is_number())
                return false;
        return true;
    }

    std::vector<ParamDesc> descs_;
    ParamValues values_;
};

struct SourceStatus
{
    enum class Level { Ok, Info, Warning, Error };
    Level level = Level::Ok;
    std::string text;
};

}
