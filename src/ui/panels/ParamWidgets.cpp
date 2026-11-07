#include "ui/panels/ParamWidgets.h"

#include "io/FileDialogs.h"
#include "media/FontCatalog.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/panels/DmxWindow.h"
#include "ui/panels/SliceEditor.h"

#include <imgui.h>

#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstring>

namespace bsl::ui {

namespace {

using media::Json;
using media::ParamDesc;
using media::ParamSet;
using media::ParamType;

struct StringBuffer
{
    std::string* str;
    int maxLength;
};

int stringResizeCallback(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* sb = static_cast<StringBuffer*>(data->UserData);
        sb->str->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = sb->str->data();
    }
    return 0;
}

bool inputString(const char* id, std::string& value, int maxLength)
{
    StringBuffer sb{&value, maxLength};
    value.reserve(static_cast<size_t>(std::min(maxLength, 4096)));
    return ImGui::InputText(id, value.data(), static_cast<size_t>(value.capacity()) + 1,
                            ImGuiInputTextFlags_CallbackResize, stringResizeCallback, &sb);
}

bool inputMultiline(const char* id, std::string& value, int maxLength, ImVec2 size)
{
    StringBuffer sb{&value, maxLength};
    value.reserve(static_cast<size_t>(std::min(maxLength, 4096)));
    return ImGui::InputTextMultiline(id, value.data(), static_cast<size_t>(value.capacity()) + 1, size,
                                     ImGuiInputTextFlags_CallbackResize, stringResizeCallback, &sb);
}

int comboWithFilter(const char* id, const std::string& preview, const std::vector<std::string>& labels, int selected)
{
    int chosen = -1;
    if (beginCombo(id, preview.c_str())) {
        static ImGuiTextFilter filter;
        const bool search = labels.size() > 20;
        if (ImGui::IsWindowAppearing())
            filter.Clear();
        if (search) {
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            filter.Draw("##busca", -FLT_MIN);
        }
        for (size_t i = 0; i < labels.size(); ++i) {
            if (search && !filter.PassFilter(labels[i].c_str()))
                continue;
            const bool isSelected = static_cast<int>(i) == selected;
            if (ImGui::Selectable(labels[i].c_str(), isSelected))
                chosen = static_cast<int>(i);
            if (isSelected && ImGui::IsWindowAppearing())
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return chosen;
}

float labelWidth()
{
    return std::clamp(ImGui::GetContentRegionAvail().x * 0.36f, dp(100.0f), dp(160.0f));
}

}

bool paramConditionMet(const std::string& condition, const ParamSet& set)
{
    if (condition.empty())
        return true;
    const size_t ne = condition.find("!=");
    if (ne != std::string::npos)
        return set.s(condition.substr(0, ne)) != condition.substr(ne + 2);
    const size_t eq = condition.find('=');
    if (eq != std::string::npos)
        return set.s(condition.substr(0, eq)) == condition.substr(eq + 1);
    if (condition[0] == '!')
        return !set.b(condition.substr(1));
    return set.b(condition);
}

std::vector<ParamEdit> drawParams(const std::vector<ParamDesc>& descs, const media::ParamValues& values,
                                  const CustomParamEditor& custom)
{
    std::vector<ParamEdit> edits;
    const ParamSet set(descs, values);
    std::string lastGroup;

    for (const ParamDesc& d : descs) {
        if (!d.group.empty() && d.group != lastGroup) {
            ImGui::Spacing();
            ImGui::SeparatorText(d.group.c_str());
            lastGroup = d.group;
        }

        ImGui::PushID(d.id.c_str());
        if (d.type == ParamType::Custom && d.custom == "bsl-slices") {
            Json out;
            if (drawSliceEditor(set, d.id, out))
                edits.push_back({d.id, std::move(out)});
            ImGui::PopID();
            continue;
        }
        const bool enabled = paramConditionMet(d.enabledIf, set);
        ImGui::BeginDisabled(!enabled);

        const float x = ImGui::GetCursorPosX();
        const bool multiline = d.type == ParamType::TextMultiline;
        if (multiline) {
            ImGui::AlignTextToFramePadding();
            dimLabel(d.label.c_str());
        } else {
            ImGui::AlignTextToFramePadding();
            dimLabel(d.label.c_str());
            if (!d.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", d.tooltip.c_str());
            ImGui::SameLine();
            ImGui::SetCursorPosX(x + labelWidth());
        }
        ImGui::SetNextItemWidth(-FLT_MIN);

        switch (d.type) {
        case ParamType::Float: {
            float v = set.f(d.id);
            std::string numberFormat, unit;
            splitValueFormat(d.format.empty() ? "%.2f" : d.format.c_str(), numberFormat, unit);
            const float lo = static_cast<float>(d.min), hi = static_cast<float>(d.max);
            const float def = d.def.is_number() ? d.def.get<float>() : NAN;
            if (dmxParamOwnerValid())
                hotDmx(dmxEffectRef(d.id));
            if (hotNumber("##v", v, lo, hi, (hi - lo) / 200.0f, numberFormat.c_str(), unit.c_str(), def))
                edits.push_back({d.id, media::param::floatJson(v)});
            break;
        }
        case ParamType::Int: {
            int v = set.i(d.id);
            std::string numberFormat, unit;
            splitValueFormat(d.format.empty() ? "%d" : d.format.c_str(), numberFormat, unit);
            const int lo = static_cast<int>(d.min), hi = static_cast<int>(d.max);
            const int def = d.def.is_number() ? d.def.get<int>() : INT_MIN;
            if (dmxParamOwnerValid())
                hotDmx(dmxEffectRef(d.id));
            if (hotInt("##v", v, lo, hi, std::max(0.05f, static_cast<float>(hi - lo) / 200.0f), numberFormat.c_str(),
                       unit.c_str(), def))
                edits.push_back({d.id, Json(v)});
            break;
        }
        case ParamType::Bool: {
            bool v = set.b(d.id);
            if (ImGui::Checkbox("##v", &v))
                edits.push_back({d.id, Json(v)});
            if (dmxParamOwnerValid() && ImGui::BeginPopupContextItem("##dmx_ctx")) {
                dmxParamMenu(dmxEffectRef(d.id));
                ImGui::EndPopup();
            }
            break;
        }
        case ParamType::Color: {
            const media::Color c = set.c(d.id);
            float col[4] = {c.r, c.g, c.b, c.a};
            if (ImGui::ColorEdit4("##v", col,
                                  ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar |
                                      ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_DisplayHex |
                                      ImGuiColorEditFlags_InputRGB))
                edits.push_back({d.id, media::param::colorJson({col[0], col[1], col[2], col[3]})});
            break;
        }
        case ParamType::Text: {
            std::string v = set.s(d.id);
            if (inputString("##v", v, d.maxLength > 0 ? d.maxLength : 4096))
                edits.push_back({d.id, Json(v)});
            break;
        }
        case ParamType::TextMultiline: {
            std::string v = set.s(d.id);
            if (inputMultiline("##v", v, d.maxLength > 0 ? d.maxLength : 20000,
                               ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 5.5f)))
                edits.push_back({d.id, Json(v)});
            break;
        }
        case ParamType::Enum: {
            const std::string current = set.s(d.id);
            std::vector<std::string> labels;
            int selected = -1;
            for (size_t i = 0; i < d.options.size(); ++i) {
                labels.push_back(d.options[i].label);
                if (d.options[i].value == current)
                    selected = static_cast<int>(i);
            }
            const std::string preview = selected >= 0 ? labels[static_cast<size_t>(selected)] : current;
            const int chosen = comboWithFilter("##v", preview, labels, selected);
            if (chosen >= 0)
                edits.push_back({d.id, Json(d.options[static_cast<size_t>(chosen)].value)});
            if (dmxParamOwnerValid() && ImGui::BeginPopupContextItem("##dmx_ctx")) {
                dmxParamMenu(dmxEffectRef(d.id));
                ImGui::EndPopup();
            }
            break;
        }
        case ParamType::Font: {
            media::FontCatalog& fonts = media::FontCatalog::instance();
            fonts.ensureScanned();
            const std::string current = set.s(d.id);
            if (!fonts.ready()) {
                ImGui::BeginDisabled();
                beginCombo("##v", "Carregando fontes…");
                ImGui::EndDisabled();
                break;
            }
            const std::vector<media::FontEntry> entries = fonts.entries();
            std::vector<std::string> labels;
            int selected = -1;
            for (size_t i = 0; i < entries.size(); ++i) {
                labels.push_back(entries[i].label);
                if (entries[i].file == current)
                    selected = static_cast<int>(i);
            }
            const std::string preview = selected >= 0 ? labels[static_cast<size_t>(selected)] : current + " (não encontrada)";
            const int chosen = comboWithFilter("##v", preview, labels, selected);
            if (chosen >= 0)
                edits.push_back({d.id, Json(entries[static_cast<size_t>(chosen)].file)});
            break;
        }
        case ParamType::File: {
            std::string v = set.s(d.id);
            const float btn = dp(30.0f);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btn - ImGui::GetStyle().ItemSpacing.x);
            if (inputString("##v", v, 1024))
                edits.push_back({d.id, Json(v)});
            ImGui::SameLine();
            if (ImGui::Button("…", ImVec2(btn, 0.0f))) {
                std::wstring spec;
                std::wstring name = L"Arquivos";
                if (!d.fileFilter.empty()) {
                    const size_t bar = d.fileFilter.find('|');
                    if (bar != std::string::npos) {
                        const std::string n = d.fileFilter.substr(0, bar), s = d.fileFilter.substr(bar + 1);
                        name.assign(n.begin(), n.end());
                        spec.assign(s.begin(), s.end());
                    }
                }
                if (auto file = dialogs::openFile(GetActiveWindow(), name, spec))
                    edits.push_back({d.id, Json(std::string(reinterpret_cast<const char*>(file->u8string().c_str())))});
            }
            break;
        }
        case ParamType::Custom: {
            if (d.custom == "point2d") {
                const Json cur = set.raw(d.id);
                const Json def = d.def;
                auto comp = [](const Json& j, size_t i) {
                    return (j.is_array() && j.size() > i && j[i].is_number()) ? j[i].get<float>() : 0.0f;
                };
                float vx = comp(cur, 0), vy = comp(cur, 1);
                const float lo = static_cast<float>(d.min), hi = static_cast<float>(d.max);
                const float step = std::max(0.01f, (hi - lo) / 400.0f);
                const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                const char* pointFormat = (hi - lo) <= 20.0f ? "%.3f" : "%.1f";
                bool changed = hotNumber("##x", vx, lo, hi, step, pointFormat, "x", comp(def, 0), half / uiScale());
                ImGui::SameLine();
                changed = hotNumber("##y", vy, lo, hi, step, pointFormat, "y", comp(def, 1), -1.0f) || changed;
                if (changed)
                    edits.push_back({d.id, Json::array({media::param::floatJson(vx), media::param::floatJson(vy)})});
                break;
            }
            if (d.custom == "event") {
                if (ImGui::Button("Disparar", ImVec2(-FLT_MIN, 0.0f))) {
                    const Json cur = set.raw(d.id);
                    edits.push_back({d.id, Json((cur.is_number() ? cur.get<int>() : 0) + 1)});
                }
                break;
            }
            Json out;
            if (custom && custom(d, set.raw(d.id), out))
                edits.push_back({d.id, std::move(out)});
            break;
        }
        }

        if (multiline && !d.tooltip.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", d.tooltip.c_str());
        else if (!multiline && !d.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", d.tooltip.c_str());

        ImGui::EndDisabled();
        ImGui::PopID();
    }
    return edits;
}

}
