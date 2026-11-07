#include "ui/panels/EffectChainWidget.h"
#include "ui/panels/PluginWizard.h"

#include "app/Qa.h"
#include "ui/KeysUi.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/panels/DmxWindow.h"
#include "ui/panels/ParamWidgets.h"

#include "fx/EffectRegistry.h"
#include "fx/bsl/BslCommon.h"
#include "ui/panels/ZoneRouting.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <string>
#include <vector>

namespace bsl::ui {

namespace {

enum class ActionKind { None, Remove, Duplicate, Reset, Move, Insert };

struct Pending
{
    ActionKind kind = ActionKind::None;
    int index = 0;
    int to = 0;
    std::string typeId;
};

std::string itemId(const char* chain, int index, const char* part)
{
    return std::string("##") + chain + "_efeito_" + std::to_string(index + 1) + "_" + part;
}

std::string fitText(const std::string& text, float maxW)
{
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= maxW)
        return text;
    std::string cut = text;
    while (!cut.empty()) {
        cut.pop_back();
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80)
            cut.pop_back();
        if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, (cut + "...").c_str()).x <= maxW)
            return cut + "...";
    }
    return {};
}

void acceptDropsOnHeader(const fx::EffectChain& chain, int index, Pending& pending)
{
    if (!ImGui::BeginDragDropTarget())
        return;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kEffectMovePayload)) {
        EffectMoveRef ref{};
        std::memcpy(&ref, p->Data, sizeof(ref));
        if (ref.chain == &chain && ref.index != index)
            pending = {ActionKind::Move, ref.index, index, {}};
    } else if (const ImGuiPayload* q = ImGui::AcceptDragDropPayload(kEffectPayload)) {
        pending = {ActionKind::Insert, index, index, std::string(static_cast<const char*>(q->Data))};
    }
    ImGui::EndDragDropTarget();
}

void drawHeader(fx::EffectChain& chain, int index, const char* chainId, Pending& pending, const EffectOwner& owner)
{
    fx::EffectInstance& inst = chain.items[static_cast<size_t>(index)];
    const std::optional<fx::EffectTypeInfo> info = fx::EffectRegistry::instance().find(inst.typeId);
    const bool known = info && info->error.empty();

    const float h = ImGui::GetFrameHeight();
    const float gripW = dp(20.0f);
    const float full = ImGui::GetContentRegionAvail().x;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + full, p0.y + h), ImGui::GetColorU32(inst.bypass ? theme::kPanel : theme::kPanelRaised),
                      rounding());
    if (inst.bypass)
        dl->AddRect(p0, ImVec2(p0.x + full, p0.y + h), ImGui::GetColorU32(theme::kBorder), rounding());

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));

    ImGui::InvisibleButton(itemId(chainId, index, "alca").c_str(), ImVec2(gripW, h));
    const bool gripHovered = ImGui::IsItemHovered();
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        const EffectMoveRef ref{&chain, index};
        ImGui::SetDragDropPayload(kEffectMovePayload, &ref, sizeof(ref));
        ImGui::TextUnformatted(inst.displayName().c_str());
        ImGui::EndDragDropSource();
    }
    if (gripHovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        ImGui::SetTooltip("Arraste para reordenar");
    drawIcon(dl, ICON_GRIP, ImVec2(p0.x + gripW * 0.5f, p0.y + h * 0.5f),
             ImGui::GetColorU32(gripHovered ? theme::kText : theme::kTextDim));

    ImGui::SameLine(0.0f, 0.0f);
    const float bodyW = std::max(dp(20.0f), full - gripW - 2.0f * h);
    ImGui::InvisibleButton(itemId(chainId, index, "cabecalho").c_str(), ImVec2(bodyW, h));
    const bool headHovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        inst.uiOpen = !inst.uiOpen;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##efeito_menu");
    acceptDropsOnHeader(chain, index, pending);
    if (headHovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        ImGui::SetTooltip("%s\nClique para %s  |  botão direito: menu",
                          info && !info->description.empty() ? info->description.c_str() : inst.displayName().c_str(),
                          inst.uiOpen ? "recolher" : "expandir");

    const float cy = p0.y + h * 0.5f;
    const ImU32 fg = ImGui::GetColorU32(inst.bypass ? theme::kTextDim : (known ? theme::kText : theme::kWarning));
    float x = p0.x + gripW;
    drawIcon(dl, inst.uiOpen ? ICON_CHEVRON_DOWN : ICON_CHEVRON_RIGHT, ImVec2(x + dp(8.0f), cy), fg, 0.9f);
    x += dp(20.0f);
    drawIcon(dl, info && info->icon && info->icon[0] ? info->icon : ICON_SPARKLES, ImVec2(x + dp(8.0f), cy), fg);
    x += dp(24.0f);
    const control::Target keyTarget = owner.scope >= 0
                                          ? control::Target::effectOf(owner.scope, owner.deck, owner.layer, owner.column, index)
                                          : control::Target{};
    const std::string hint = keyTarget.valid() ? keyHint(keyTarget) : std::string();
    const float hintW = hint.empty() ? 0.0f : keyHintWidth(hint) + dp(3.0f);
    const std::string title = fitText(inst.displayName(), p0.x + gripW + bodyW - x - dp(4.0f) - hintW);
    dl->AddText(ImVec2(x, cy - ImGui::GetFontSize() * 0.5f), fg, title.c_str());
    if (!hint.empty())
        drawKeyHint(dl, ImVec2(p0.x + gripW + bodyW - hintW + dp(1.0f), p0.y + dp(3.0f)), hint);

    ImGui::SameLine(0.0f, 0.0f);
    iconButton(inst.bypass ? ICON_EYE_OFF : ICON_EYE, itemId(chainId, index, "bypass").c_str(),
               inst.bypass ? "Efeito em bypass (ignorado). Clique para ativar" : "Efeito ativo. Clique para pôr em bypass",
               ImVec2(h, h), IconButton_Flat, ImGui::GetColorU32(inst.bypass ? theme::kTextDim : theme::kText));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        inst.bypass = !inst.bypass;
    if (keyTarget.valid() && ImGui::BeginPopupContextItem("##efeito_bypass_ctx")) {
        keyAssignMenu(keyTarget, false);
        dmxActionMenu(keyTarget);
        ImGui::EndPopup();
    }
    if (keyTarget.valid())
        drawKeyFlash(dl, p0, ImVec2(p0.x + full, p0.y + h), keyFlash(keyTarget), rounding());
    ImGui::SameLine(0.0f, 0.0f);
    if (iconButton(ICON_ELLIPSIS, itemId(chainId, index, "menu").c_str(), "Menu do efeito", ImVec2(h, h), IconButton_Flat))
        ImGui::OpenPopup("##efeito_menu");

    ImGui::PopStyleVar();

    if (ImGui::BeginPopup("##efeito_menu")) {
        if (ImGui::MenuItem(ICON_COPY "  Duplicar##efeito_duplicar"))
            pending = {ActionKind::Duplicate, index, index, {}};
        if (ImGui::MenuItem(ICON_UNDO "  Restaurar padrões##efeito_restaurar"))
            pending = {ActionKind::Reset, index, index, {}};
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_TRASH "  Remover##efeito_remover"))
            pending = {ActionKind::Remove, index, index, {}};
        ImGui::EndPopup();
    }
}

}

std::string foldSearchText(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const unsigned char d = static_cast<unsigned char>(s[++i]);
            char base = '?';
            if ((d >= 0x80 && d <= 0x85) || (d >= 0xA0 && d <= 0xA5)) base = 'a';
            else if (d == 0x87 || d == 0xA7) base = 'c';
            else if ((d >= 0x88 && d <= 0x8B) || (d >= 0xA8 && d <= 0xAB)) base = 'e';
            else if ((d >= 0x8C && d <= 0x8F) || (d >= 0xAC && d <= 0xAF)) base = 'i';
            else if ((d >= 0x92 && d <= 0x96) || (d >= 0xB2 && d <= 0xB6)) base = 'o';
            else if ((d >= 0x99 && d <= 0x9C) || (d >= 0xB9 && d <= 0xBC)) base = 'u';
            out += base;
        } else {
            out += static_cast<char>(std::tolower(c));
        }
    }
    return out;
}

namespace {

std::string drawAddPopup(const char* chainId)
{
    static char filter[64] = {};
    std::string chosen;
    ImGui::SetNextWindowSizeConstraints(ImVec2(dp(240.0f), 0.0f), ImVec2(dp(360.0f), dp(420.0f)));
    if (!ImGui::BeginPopup("##efeito_novo"))
        return chosen;
    if (ImGui::IsWindowAppearing()) {
        filter[0] = '\0';
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(dp(240.0f));
    ImGui::InputTextWithHint((std::string("##") + chainId + "_efeito_novo_busca").c_str(), "Buscar efeitos…", filter,
                             sizeof(filter));
    const std::string needle = foldSearchText(filter);
    if (ImGui::BeginChild("##efeito_novo_lista", ImVec2(dp(240.0f), dp(280.0f)), ImGuiChildFlags_None)) {
        const std::vector<fx::EffectTypeInfo> all = fx::EffectRegistry::instance().types();
        for (const fx::EffectSection section :
             {fx::EffectSection::Bsl, fx::EffectSection::Native, fx::EffectSection::Isf, fx::EffectSection::Ffgl}) {
            std::string lastCategory;
            bool any = false;
            for (const fx::EffectTypeInfo& t : all) {
                if (t.section != section || t.generator || !t.error.empty())
                    continue;
                if (!needle.empty() && foldSearchText(t.name).find(needle) == std::string::npos
                    && foldSearchText(t.category).find(needle) == std::string::npos)
                    continue;
                if (!any) {
                    ImGui::SeparatorText(section == fx::EffectSection::Bsl      ? "BSL Light"
                                         : section == fx::EffectSection::Native ? "Nativos"
                                         : section == fx::EffectSection::Isf    ? "ISF"
                                                                                : "FFGL");
                    any = true;
                }
                if (t.category != lastCategory) {
                    ImGui::TextColored(theme::kTextDim, "%s", t.category.c_str());
                    lastCategory = t.category;
                }
                const std::string label = std::string(t.icon && t.icon[0] ? t.icon : ICON_SPARKLES) + "  " + t.name + "##add_" + t.id;
                if (ImGui::Selectable(label.c_str())) {
                    chosen = t.id;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::EndPopup();
    return chosen;
}

}

std::string acceptEffectDrop()
{
    std::string type;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kEffectPayload))
            type = std::string(static_cast<const char*>(p->Data));
        ImGui::EndDragDropTarget();
    }
    return type;
}

void drawEffectsBadge(ImDrawList* dl, ImVec2 center, const fx::EffectChain& chain)
{
    if (chain.empty())
        return;
    const float r = dp(9.0f);
    dl->AddCircleFilled(center, r, IM_COL32(0, 0, 0, 170));
    drawIcon(dl, ICON_SPARKLES, center, ImGui::GetColorU32(chain.hasActive() ? theme::kText : theme::kTextDim), 0.8f);
}

bool drawEffectChain(fx::EffectChain& chain, const char* id, PanelContext* ctx, int zoneSource, const EffectOwner& owner)
{
    bool changed = false;
    Pending pending;

    for (int i = 0; i < chain.size(); ++i) {
        fx::EffectInstance& inst = chain.items[static_cast<size_t>(i)];
        ImGui::PushID(inst.uid);
        drawHeader(chain, i, id, pending, owner);

        if (inst.uiOpen) {
            const media::SourceStatus st = inst.status();
            if (!st.text.empty()) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(st.level == media::SourceStatus::Level::Error     ? theme::kError
                                   : st.level == media::SourceStatus::Level::Warning ? theme::kWarning
                                                                                     : theme::kTextDim,
                                   "%s", st.text.c_str());
                ImGui::PopTextWrapPos();
            }
            if (const auto info = fx::EffectRegistry::instance().find(inst.typeId);
                info && info->section == fx::EffectSection::Isf && !info->sourceFile.empty()) {
                const std::filesystem::path file{std::u8string(info->sourceFile.begin(), info->sourceFile.end())};
                const std::string base = std::string(id) + "_efeito_" + std::to_string(i + 1);
                if (ImGui::SmallButton(("Abrir no editor##" + base + "_editar").c_str()))
                    openPluginInEditor(file);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Abre o arquivo .fs deste efeito. Ao salvar, ele recompila sozinho.");
                ImGui::SameLine();
                if (ImGui::SmallButton(("Mostrar na pasta##" + base + "_pasta").c_str()))
                    showPluginInFolder(file);
                ImGui::SameLine();
                if (ImGui::SmallButton(("Duplicar plugin##" + base + "_duplicar_plugin").c_str()))
                    duplicateUserPlugin(file);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Cria uma cópia do arquivo para você alterar (em Meus plugins, se este for um exemplo).");
            }
            const std::vector<media::ParamDesc> descs = inst.paramDescs();
            if (!descs.empty()) {
                ImGui::Indent(dp(8.0f));
                if (ctx && zoneSource != kNoZoneSource && inst.typeId == fx::bsllight::kZSliceId) {
                    drawZoneRoutingButton(*ctx, media::ParamSet(descs, inst.params), zoneSource,
                                          std::string(id) + "_efeito_" + std::to_string(i + 1));
                    ImGui::Spacing();
                }
                DmxParamOwner dmxOwner;
                dmxOwner.valid = owner.scope >= 0 && (owner.scope == 2 || owner.layer >= 0);
                dmxOwner.scope = owner.scope;
                dmxOwner.layerIndex = owner.layer;
                dmxOwner.deck = owner.deck;
                dmxOwner.column = std::max(0, owner.column);
                dmxOwner.effect = i;
                dmxSetParamOwner(dmxOwner);
                std::vector<ParamEdit> paramEdits = drawParams(descs, inst.params);
                dmxSetParamOwner({});
                for (ParamEdit& e : paramEdits) {
                    inst.setParam(e.id, std::move(e.value));
                    changed = true;
                }
                ImGui::Unindent(dp(8.0f));
            }
            ImGui::Spacing();
        }
        ImGui::PopID();
    }

    {
        const float h = chain.empty() ? dp(56.0f) : dp(28.0f);
        const float full = std::max(ImGui::GetContentRegionAvail().x, dp(60.0f));
        const float btnW = dp(112.0f);
        const float w = std::max(dp(40.0f), full - btnW - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InvisibleButton((std::string("##") + id + "_efeitos_soltar").c_str(), ImVec2(w, h));
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool dragging = ImGui::GetDragDropPayload() != nullptr;
        drawDashedRect(dl, a, b, ImGui::GetColorU32(dragging ? theme::kAccent : theme::kPanelActive));
        const char* text = chain.empty() ? "Arraste efeitos aqui" : "Arraste mais efeitos aqui";
        const float room = b.x - a.x - dp(12.0f);
        const std::string shown = fitText(text, room);
        const ImVec2 ts = ImGui::CalcTextSize(shown.c_str());
        dl->AddText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), ImGui::GetColorU32(theme::kTextDim),
                    shown.c_str());
        const std::string dropped = acceptEffectDrop();
        if (!dropped.empty())
            pending = {ActionKind::Insert, chain.size(), chain.size(), dropped};

        ImGui::SameLine();
        if (ImGui::Button((ICON_PLUS "  Efeito##" + std::string(id) + "_efeito_adicionar").c_str(), ImVec2(btnW, h)))
            ImGui::OpenPopup("##efeito_novo");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Escolher um efeito do catálogo (ou arraste um da aba Efeitos)");
        const std::string added = drawAddPopup(id);
        if (!added.empty())
            pending = {ActionKind::Insert, chain.size(), chain.size(), added};
    }

    switch (pending.kind) {
    case ActionKind::Remove:
        changed = chain.remove(pending.index) || changed;
        break;
    case ActionKind::Duplicate:
        changed = chain.duplicate(pending.index) || changed;
        break;
    case ActionKind::Reset:
        if (chain.valid(pending.index)) {
            chain.items[static_cast<size_t>(pending.index)].resetParams();
            changed = true;
        }
        break;
    case ActionKind::Move:
        changed = chain.move(pending.index, pending.to) || changed;
        break;
    case ActionKind::Insert:
        if (static_cast<size_t>(chain.size()) < fx::EffectChain::kMaxEffects) {
            chain.insert(pending.to, pending.typeId);
            changed = true;
        }
        break;
    case ActionKind::None:
        break;
    }
    return changed;
}

}
