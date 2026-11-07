#include "ui/ViewState.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace bsl::ui::view {

using nlohmann::json;

namespace {

bool contains(const std::vector<std::string>& v, std::string_view key)
{
    return std::find(v.begin(), v.end(), key) != v.end();
}

bool setMember(std::vector<std::string>& v, std::string_view key, bool on)
{
    const auto it = std::find(v.begin(), v.end(), key);
    if (on && it == v.end()) {
        v.emplace_back(key);
        return true;
    }
    if (!on && it != v.end()) {
        v.erase(it);
        return true;
    }
    return false;
}

std::vector<std::string> readKeys(const json& j, const char* name)
{
    std::vector<std::string> out;
    if (!j.contains(name) || !j[name].is_array())
        return out;
    for (const json& item : j[name])
        if (item.is_string())
            out.push_back(item.get<std::string>());
    return sanitizeKeys(out);
}

}

const PanelDef* findPanel(std::string_view key)
{
    for (const PanelDef& p : kPanels)
        if (key == p.key)
            return &p;
    return nullptr;
}

const PanelDef* findPanelByWindow(std::string_view window)
{
    for (const PanelDef& p : kPanels)
        if (window == p.window)
            return &p;
    return nullptr;
}

int panelIndex(std::string_view key)
{
    for (int i = 0; i < kPanelCount; ++i)
        if (key == kPanels[i].key)
            return i;
    return -1;
}

bool State::isHidden(std::string_view key) const
{
    return contains(hidden, key);
}

bool State::isDetached(std::string_view key) const
{
    return contains(detached, key);
}

bool State::setHidden(std::string_view key, bool hide)
{
    if (!findPanel(key))
        return false;
    return setMember(hidden, key, hide);
}

bool State::setDetached(std::string_view key, bool on)
{
    if (!findPanel(key))
        return false;
    return setMember(detached, key, on);
}

bool State::setDockId(std::string_view key, unsigned id)
{
    if (!findPanel(key) || id == 0)
        return false;
    unsigned& slot = dockIds[std::string(key)];
    if (slot == id)
        return false;
    slot = id;
    return true;
}

bool State::setDockRect(std::string_view key, const std::array<float, 4>& rect)
{
    if (!findPanel(key))
        return false;
    for (const float v : rect)
        if (!(v > -0.5f && v < 2.0f))
            return false;
    if (rect[2] <= 0.01f || rect[3] <= 0.01f)
        return false;
    std::array<float, 4>& slot = dockRects[std::string(key)];
    bool changed = slot[2] <= 0.0f;
    for (int i = 0; i < 4; ++i)
        changed = changed || std::abs(slot[static_cast<size_t>(i)] - rect[static_cast<size_t>(i)]) > 0.003f;
    if (changed)
        slot = rect;
    return changed;
}

bool State::showAll(PanelMode mode)
{
    bool changed = false;
    for (const PanelDef& p : kPanels)
        if (p.mode == mode || p.mode == PanelMode::Any)
            changed = setMember(hidden, p.key, false) || changed;
    return changed;
}

std::vector<std::string> sanitizeKeys(const std::vector<std::string>& keys)
{
    std::vector<std::string> out;
    for (const std::string& k : keys)
        if (findPanel(k) && !contains(out, k))
            out.push_back(k);
    return out;
}

json toJson(const State& s)
{
    json j;
    j["hidden"] = s.hidden;
    j["detached"] = s.detached;
    j["locked"] = s.locked;
    json docks = json::object();
    for (const auto& [key, id] : s.dockIds)
        docks[key] = id;
    j["dockIds"] = docks;
    json rects = json::object();
    for (const auto& [key, r] : s.dockRects)
        rects[key] = {r[0], r[1], r[2], r[3]};
    j["dockRects"] = rects;
    return j;
}

State fromJson(const json& j)
{
    State s;
    if (!j.is_object())
        return s;
    s.hidden = readKeys(j, "hidden");
    s.detached = readKeys(j, "detached");
    s.locked = j.contains("locked") && j["locked"].is_boolean() && j["locked"].get<bool>();
    if (j.contains("dockIds") && j["dockIds"].is_object()) {
        for (const auto& [key, value] : j["dockIds"].items())
            if (findPanel(key) && value.is_number_unsigned() && value.get<unsigned long long>() <= 0xFFFFFFFFull
                && value.get<unsigned long long>() != 0)
                s.dockIds[key] = static_cast<unsigned>(value.get<unsigned long long>());
    }
    if (j.contains("dockRects") && j["dockRects"].is_object()) {
        for (const auto& [key, value] : j["dockRects"].items()) {
            if (!findPanel(key) || !value.is_array() || value.size() != 4)
                continue;
            std::array<float, 4> r{};
            bool ok = true;
            for (size_t i = 0; i < 4; ++i) {
                ok = ok && value[i].is_number();
                if (ok)
                    r[i] = value[i].get<float>();
            }
            State probe;
            if (ok && probe.setDockRect(key, r))
                s.dockRects[key] = r;
        }
    }
    return s;
}

}
