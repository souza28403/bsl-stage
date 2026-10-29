#include "ffgl/FfglInfo.h"

namespace bsl::ffgl {

namespace {

template <class T>
T val(const Json& j, const char* key, T fallback)
{
    const auto it = j.find(key);
    if (it == j.end() || it->is_null())
        return fallback;
    try {
        return it->get<T>();
    } catch (const Json::exception&) {
        return fallback;
    }
}

Json paramToJson(const ParamInfo& p)
{
    Json j = {{"index", p.index}, {"name", p.name}, {"type", p.type}, {"defFloat", p.defFloat},
              {"min", p.min},     {"max", p.max},   {"usage", p.usage}, {"visible", p.visible}};
    if (!p.defText.empty())
        j["defText"] = p.defText;
    if (!p.group.empty())
        j["group"] = p.group;
    if (!p.options.empty()) {
        Json arr = Json::array();
        for (const OptionInfo& o : p.options)
            arr.push_back({{"name", o.name}, {"value", o.value}});
        j["options"] = std::move(arr);
    }
    if (!p.extensions.empty())
        j["extensions"] = p.extensions;
    return j;
}

ParamInfo paramFromJson(const Json& j)
{
    ParamInfo p;
    p.index = val(j, "index", 0);
    p.name = val<std::string>(j, "name", {});
    p.type = val<unsigned>(j, "type", kPStandard);
    p.defFloat = val(j, "defFloat", 0.0f);
    p.defText = val<std::string>(j, "defText", {});
    p.min = val(j, "min", 0.0f);
    p.max = val(j, "max", 1.0f);
    p.group = val<std::string>(j, "group", {});
    p.usage = val<unsigned>(j, "usage", 0);
    p.visible = val(j, "visible", true);
    if (const auto it = j.find("options"); it != j.end() && it->is_array()) {
        for (const Json& o : *it)
            if (o.is_object())
                p.options.push_back({val<std::string>(o, "name", {}), val(o, "value", 0.0f)});
    }
    if (const auto it = j.find("extensions"); it != j.end() && it->is_array()) {
        for (const Json& e : *it)
            if (e.is_string())
                p.extensions.push_back(e.get<std::string>());
    }
    return p;
}

} // namespace

Json PluginInfo::toJson() const
{
    Json j = {{"path", path},
              {"fileName", fileName},
              {"size", size},
              {"mtime", mtime},
              {"pluginId", pluginId},
              {"name", name},
              {"type", type},
              {"apiMajor", apiMajor},
              {"apiMinor", apiMinor},
              {"versionMajor", versionMajor},
              {"versionMinor", versionMinor},
              {"description", description},
              {"about", about},
              {"supportsTime", supportsTime},
              {"supportsTopLeft", supportsTopLeft},
              {"minInputs", minInputs},
              {"maxInputs", maxInputs}};
    Json arr = Json::array();
    for (const ParamInfo& p : params)
        arr.push_back(paramToJson(p));
    j["params"] = std::move(arr);
    return j;
}

std::optional<PluginInfo> PluginInfo::fromJson(const Json& j)
{
    if (!j.is_object() || !j.contains("pluginId") || !j.contains("name"))
        return std::nullopt;
    PluginInfo i;
    i.path = val<std::string>(j, "path", {});
    i.fileName = val<std::string>(j, "fileName", {});
    i.size = val<std::uint64_t>(j, "size", 0);
    i.mtime = val<std::int64_t>(j, "mtime", 0);
    i.pluginId = val<std::string>(j, "pluginId", {});
    i.name = val<std::string>(j, "name", {});
    i.type = val<unsigned>(j, "type", kTypeEffect);
    i.apiMajor = val<unsigned>(j, "apiMajor", 0);
    i.apiMinor = val<unsigned>(j, "apiMinor", 0);
    i.versionMajor = val<unsigned>(j, "versionMajor", 0);
    i.versionMinor = val<unsigned>(j, "versionMinor", 0);
    i.description = val<std::string>(j, "description", {});
    i.about = val<std::string>(j, "about", {});
    i.supportsTime = val(j, "supportsTime", false);
    i.supportsTopLeft = val(j, "supportsTopLeft", false);
    i.minInputs = val(j, "minInputs", -1);
    i.maxInputs = val(j, "maxInputs", -1);
    if (const auto it = j.find("params"); it != j.end() && it->is_array())
        for (const Json& p : *it)
            if (p.is_object())
                i.params.push_back(paramFromJson(p));
    return i;
}

Json ScanResult::toJson() const
{
    Json j = {{"ok", ok}};
    if (!error.empty())
        j["error"] = error;
    if (ok)
        j["plugin"] = info.toJson();
    return j;
}

ScanResult ScanResult::fromJson(const Json& j)
{
    ScanResult r;
    if (!j.is_object()) {
        r.error = "resposta inválida da varredura";
        return r;
    }
    r.error = val<std::string>(j, "error", {});
    if (val(j, "ok", false)) {
        if (const auto it = j.find("plugin"); it != j.end()) {
            if (auto info = PluginInfo::fromJson(*it)) {
                r.info = std::move(*info);
                r.ok = true;
            }
        }
        if (!r.ok && r.error.empty())
            r.error = "descrição do plugin inválida";
    } else if (r.error.empty()) {
        r.error = "varredura sem resultado";
    }
    return r;
}

std::string makeTypeId(const std::string& pluginId, const std::string& fileName)
{
    std::string id = pluginId;
    id.resize(4, ' ');
    for (char& c : id) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E)
            c = '?';
    }
    return "ffgl:" + id + ":" + fileName;
}

bool splitTypeId(const std::string& typeId, std::string& pluginId, std::string& fileName)
{
    // "ffgl:" + 4 caracteres + ":" + nome
    if (typeId.size() < 5 + 4 + 1 || typeId.compare(0, 5, "ffgl:") != 0 || typeId[9] != ':')
        return false;
    pluginId = typeId.substr(5, 4);
    fileName = typeId.substr(10);
    return true;
}

} // namespace bsl::ffgl
