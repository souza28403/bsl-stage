#include "ui/panels/CompositionPanel.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "app/ProjectSession.h"
#include "app/Qa.h"
#include "media/DocInfo.h"
#include "media/NetStream.h"
#include "output/OutputSetup.h"
#include "render/ThumbnailCache.h"
#include "ui/Theme.h"
#include "ui/KeysUi.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"
#include "ui/panels/CaptureTargetChooser.h"
#include "ui/panels/DmxWindow.h"
#include "ui/panels/EffectChainWidget.h"
#include "ui/panels/TransportBar.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace bsl::ui {

namespace {

float cellW() { return dp(104.0f); }
float thumbH() { return dp(58.0f); }
float labelH() { return dp(20.0f); }
float cellH() { return thumbH() + labelH(); }
float gap() { return dp(6.0f); }
float headerH() { return dp(24.0f); }
float controlsW() { return dp(240.0f); }
float nameH() { return dp(22.0f); }
float ctrlH() { return dp(23.0f); }
float deckBarH() { return dp(26.0f); }
float addLayerH() { return dp(26.0f); }

struct CellRef
{
    int layer;
    int column;
};

using Deferred = std::vector<std::function<void()>>;

bool contains(ImVec2 a, ImVec2 b, float x, float y)
{
    return x >= a.x && x < b.x && y >= a.y && y < b.y;
}

void drawEllipsized(ImDrawList* dl, ImVec2 pos, float maxW, ImU32 color, const std::string& text)
{
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= maxW) {
        dl->AddText(font, size, pos, color, text.c_str());
        return;
    }
    std::string cut = text;
    while (!cut.empty()) {
        cut.pop_back();
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80)
            cut.pop_back();
        const std::string candidate = cut + "...";
        if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, candidate.c_str()).x <= maxW) {
            dl->AddText(font, size, pos, color, candidate.c_str());
            return;
        }
    }
}

int g_renameLayer = -1;
int g_renameDeck = -1;
bool g_renameFocus = false;
char g_renameBuf[128] = {};
int g_deleteDeck = -1;
bool g_openDeleteConfirm = false;

void beginRename(int& target, int index, const std::string& current)
{
    target = index;
    g_renameFocus = true;
    std::snprintf(g_renameBuf, sizeof(g_renameBuf), "%s", current.c_str());
}

bool renameField(float width, std::string& name)
{
    if (g_renameFocus) {
        ImGui::SetKeyboardFocusHere();
        g_renameFocus = false;
    }
    ImGui::SetNextItemWidth(width);
    const bool entered = ImGui::InputText("##rename", g_renameBuf, sizeof(g_renameBuf),
                                          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
    const bool done = entered || ImGui::IsItemDeactivated();
    if (done || cancelled) {
        if (!cancelled && g_renameBuf[0] != '\0')
            name = g_renameBuf;
        return true;
    }
    return false;
}

void switchDeck(PanelContext& ctx, int deck)
{
    Composition& comp = ctx.composition;
    if (!comp.validDeck(deck) || deck == comp.currentDeck)
        return;
    comp.currentDeck = deck;
    if (ctx.selection.kind == Selection::Kind::Clip)
        ctx.selection = {};
}

void drawMissingCell(ImDrawList* dl, ImVec2 a, ImVec2 thumbMax, ImVec2 b, const std::string& name, const char* text)
{
    const ImU32 amber = ImGui::GetColorU32(theme::kWarning);
    dl->AddRectFilled(a, b, IM_COL32(18, 18, 19, 255), rounding());
    drawDashedRect(dl, ImVec2(a.x + dp(1.5f), a.y + dp(1.5f)), ImVec2(b.x - dp(1.5f), b.y - dp(1.5f)), amber, dp(5.0f),
                   dp(3.0f));

    ImFont* font = ImGui::GetFont();
    float size = ImGui::GetFontSize();
    const float maxW = (b.x - a.x) - dp(12.0f);
    const float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
    if (w > maxW)
        size *= maxW / w;
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    dl->AddText(font, size, ImVec2(a.x + ((b.x - a.x) - ts.x) * 0.5f, a.y + (thumbMax.y - a.y - ts.y) * 0.5f),
                amber, text);

    dl->PushClipRect(ImVec2(a.x + dp(2.0f), thumbMax.y - dp(4.0f)), ImVec2(b.x - dp(2.0f), b.y - dp(3.0f)), true);
    drawEllipsized(dl, ImVec2(a.x + dp(5.0f), thumbMax.y - dp(3.0f)), (b.x - a.x) - dp(10.0f),
                   ImGui::GetColorU32(theme::kTextDim), name);
    dl->PopClipRect();
}

void applyFileDrop(Composition& comp, Selection& selection, int layer, int column,
                   const std::vector<std::filesystem::path>& files)
{
    int col = column;
    bool first = true;
    for (const auto& file : files) {
        std::error_code ec;
        Clip clip;
        if (const std::string url = media::net::urlFromShortcut(file); !url.empty()) {
            clip = Clip::generator(kNetStreamId);
            clip.setParam("mode", "pull");
            clip.setParam("url", url);
        } else if (!std::filesystem::is_directory(file, ec)) {
            clip = Clip::fromFile(file);
        }
        if (clip.empty()) {
            log::warn("Arquivo ignorado (formato não suportado): " + paths::toUtf8(file.filename()));
            continue;
        }
        while (col >= comp.columnCount())
            comp.addColumn();
        comp.setClip(layer, col, std::move(clip));
        if (first) {
            selection = {Selection::Kind::Clip, layer, col};
            first = false;
        }
        ++col;
    }
}

void drawMasterHeader(PanelContext& ctx, ImVec2 pos, Deferred& deferred)
{
    Composition& comp = ctx.composition;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, dp(6.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, dp(4.0f, 4.0f));
    ImGui::PushID("master");

    ImGui::SetCursorScreenPos(pos);
    if (iconButton(ICON_X, "##master_limpar", "Limpar todas: para todas as camadas", ImVec2(dp(26.0f), headerH()), 0,
                   ImGui::GetColorU32(comp.anyActive() ? theme::kText : theme::kTextDim)))
        deferred.push_back([&comp] { comp.stopAll(); });

    ImGui::SameLine();
    toggleButton("B##master_bypass", comp.masterBypass, "Bypass master: apaga a saída", ImVec2(dp(26.0f), headerH()));

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    dimLabel("Master");
    ImGui::SameLine();
    const float room = pos.x + controlsW() - ImGui::GetCursorScreenPos().x - dp(2.0f);
    hotDmx({dmx::ParamKind::MasterOpacity});
    hotPercent("##master_opacidade", comp.masterOpacity, 1.0f, room / uiScale());

    ImGui::PopID();
    ImGui::PopStyleVar(2);
}

void drawLayerControls(PanelContext& ctx, int li, ImVec2 pos, Deferred& deferred)
{
    Composition& comp = ctx.composition;
    Layer& layer = comp.layers[static_cast<size_t>(li)];
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool selected = ctx.selection.kind != Selection::Kind::None && ctx.selection.layer == li;
    const bool playing = layer.activeColumn >= 0;
    const std::string tag = "camada " + std::to_string(li + 1);
    const std::string ids = "##camada_" + std::to_string(li + 1) + "_";

    ImGui::PushID(li);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, dp(6.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, dp(4.0f, 4.0f));

    const ImVec2 end(pos.x + controlsW(), pos.y + cellH());
    dl->AddRectFilled(pos, end, ImGui::GetColorU32(theme::kBackground), rounding());

    const ImVec2 nameEnd(pos.x + controlsW(), pos.y + nameH());
    dl->AddRectFilled(pos, nameEnd, ImGui::GetColorU32(selected ? theme::kAccent : theme::kPanelRaised), rounding(),
                      ImDrawFlags_RoundCornersTop);

    ImGui::SetCursorScreenPos(pos);
    if (g_renameLayer == li) {
        if (renameField(controlsW(), layer.name))
            g_renameLayer = -1;
        qa::note(tag + " renomear");
    } else {
        const ImU32 eyeCol = ImGui::GetColorU32(layer.bypass ? ImVec4(1, 1, 1, selected ? 0.55f : 0.35f)
                                                             : (selected ? theme::kOnAccent : theme::kText));
        if (iconButton(layer.bypass ? ICON_EYE_OFF : ICON_EYE, (ids + "visivel").c_str(),
                       layer.bypass ? "Camada em bypass (ignorada). Clique para ativar" : "Camada ativa. Clique para pôr em bypass",
                       ImVec2(nameH(), nameH()), IconButton_Flat, eyeCol))
            layer.bypass = !layer.bypass;
        ImGui::SameLine(0.0f, 0.0f);
        const float linkRight = nameEnd.x - dp(52.0f);
        ImGui::InvisibleButton("##nome", ImVec2(linkRight - nameH() - ImGui::GetCursorScreenPos().x, nameH()));
        qa::note(tag + " nome");
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            ctx.selection = {Selection::Kind::Layer, li, -1};
        if (const std::string dropped = acceptEffectDrop(); !dropped.empty()) {
            layer.effects.add(dropped);
            ctx.selection = {Selection::Kind::Layer, li, -1};
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            beginRename(g_renameLayer, li, layer.name);
        if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("##layerctx"))
            ImGui::SetTooltip("Clique para selecionar, duplo clique para renomear");
        if (ImGui::BeginPopupContextItem("##layerctx")) {
            keyAssignMenu(control::Target::layerOf(li), false);
            dmxActionMenu(control::Target::layerOf(li));
            ImGui::EndPopup();
        }

        const std::vector<std::string> linked = slicesLinkedToLayer(ctx.output, layer.id);
        const float sealW = layer.sliceOnly ? ImGui::CalcTextSize("SLICE").x + dp(10.0f) : 0.0f;
        std::string linkTip = layer.sliceOnly ? "Só nos slices vinculados: a camada não aparece no canvas principal. Clique para "
                                                "voltar a compor no canvas"
                                              : "Clique para usar esta camada só nos slices vinculados (ela sai do canvas "
                                                "principal). Vincule o slice no Mapeamento";
        if (linked.empty()) {
            linkTip += "\n\nNenhum slice vinculado a esta camada ainda.";
        } else {
            linkTip += "\n\nSlices vinculados:";
            for (const std::string& n : linked)
                linkTip += "\n  " + n;
        }
        ImGui::SameLine(0.0f, 0.0f);
        const bool hasLink = layer.sliceOnly || !linked.empty();
        const ImU32 linkCol = ImGui::GetColorU32(
            layer.sliceOnly ? (selected ? theme::kOnAccent : theme::kAccent)
                            : (hasLink ? (selected ? theme::kOnAccent : theme::kText)
                                       : ImVec4(1, 1, 1, selected ? 0.55f : 0.30f)));
        if (iconButton(ICON_LINK, (ids + "slice").c_str(), linkTip.c_str(), ImVec2(nameH(), nameH()), IconButton_Flat, linkCol))
            layer.sliceOnly = !layer.sliceOnly;
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("##nome_dir", ImVec2(nameEnd.x - ImGui::GetCursorScreenPos().x, nameH()));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            ctx.selection = {Selection::Kind::Layer, li, -1};

        const std::string layerHint = keyHint(control::Target::layerOf(li));
        const float hintW = layerHint.empty() ? 0.0f : keyHintWidth(layerHint) + dp(3.0f);
        dl->PushClipRect(pos, ImVec2(linkRight - nameH() - sealW - hintW, nameEnd.y), true);
        drawEllipsized(dl, ImVec2(pos.x + nameH() + dp(2.0f), pos.y + (nameH() - ImGui::GetFontSize()) * 0.5f),
                       linkRight - nameH() - sealW - hintW - (pos.x + nameH() + dp(2.0f)) - dp(2.0f),
                       ImGui::GetColorU32(selected ? theme::kOnAccent : theme::kText), layer.name);
        dl->PopClipRect();
        if (!layerHint.empty())
            drawKeyHint(dl, ImVec2(linkRight - nameH() - sealW - hintW + dp(1.0f), pos.y + dp(3.0f)), layerHint);
        if (layer.sliceOnly) {
            const ImVec2 ts = ImGui::CalcTextSize("SLICE");
            const ImVec2 a(linkRight - nameH() - sealW, pos.y + (nameH() - ts.y) * 0.5f - dp(1.0f));
            const ImVec2 b(a.x + sealW - dp(2.0f), a.y + ts.y + dp(2.0f));
            dl->AddRectFilled(a, b, ImGui::GetColorU32(selected ? theme::kOnAccent : theme::kAccent), dp(3.0f));
            dl->AddText(ImVec2(a.x + dp(4.0f), a.y + dp(1.0f)),
                        ImGui::GetColorU32(selected ? theme::kAccent : theme::kOnAccent), "SLICE");
        }
        if (playing)
            drawPlayTriangle(dl, ImVec2(nameEnd.x - dp(13.0f), pos.y + nameH() * 0.5f), dp(9.0f),
                             ImGui::GetColorU32(selected ? theme::kOnAccent : theme::kAccent));
        drawEffectsBadge(dl, ImVec2(nameEnd.x - dp(36.0f), pos.y + nameH() * 0.5f), layer.effects);
    }

    const float rowY = pos.y + nameH() + dp(4.0f);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + dp(4.0f), rowY));
    const std::string stopTip = "Parar " + layer.name;
    if (iconButton(ICON_X, (ids + "parar").c_str(), stopTip.c_str(), ImVec2(dp(26.0f), ctrlH()), 0,
                   ImGui::GetColorU32(playing ? theme::kText : theme::kTextDim)))
        deferred.push_back([&comp, li] { comp.stopLayer(li); });
    ImGui::SameLine();
    toggleButton(("B" + ids + "bypass").c_str(), layer.bypass, "Bypass: ignora a camada", ImVec2(dp(26.0f), ctrlH()));
    ImGui::SameLine();
    toggleButton(("S" + ids + "solo").c_str(), layer.solo, "Solo: mostra só as camadas em solo",
                 ImVec2(dp(26.0f), ctrlH()));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(end.x - dp(4.0f) - ImGui::GetCursorScreenPos().x);
    if (beginCombo("##blend", blendModeName(layer.blendMode))) {
        for (int m = 0; m < kBlendModeCount; ++m) {
            const auto mode = static_cast<BlendMode>(m);
            if (ImGui::Selectable(blendModeName(mode), mode == layer.blendMode))
                layer.blendMode = mode;
        }
        ImGui::EndCombo();
    }
    qa::note(tag + " blend");
    if (ImGui::BeginPopupContextItem("##blendctx")) {
        dmxParamMenu({dmx::ParamKind::LayerBlend, layer.id});
        ImGui::EndPopup();
    }
    drawKeyFlash(dl, pos, end, keyFlash(control::Target::layerOf(li)), rounding());

    ImGui::SetCursorScreenPos(ImVec2(pos.x + dp(8.0f), rowY + ctrlH() + dp(4.0f)));
    ImGui::AlignTextToFramePadding();
    dimLabel("Opacidade");
    ImGui::SameLine();
    const float roomX = end.x - dp(8.0f) - ImGui::GetCursorScreenPos().x;
    hotDmx({dmx::ParamKind::LayerOpacity, layer.id});
    hotPercent((ids + "opacidade").c_str(), layer.opacity, 1.0f, roomX / uiScale());

    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

void drawDeckBar(PanelContext& ctx, Deferred& deferred)
{
    Composition& comp = ctx.composition;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, dp(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, dp(8.0f, 4.0f));

    for (int d = 0; d < comp.deckCount(); ++d) {
        Deck& deck = comp.decks[static_cast<size_t>(d)];
        const bool current = d == comp.currentDeck;
        if (d > 0)
            ImGui::SameLine();
        ImGui::PushID(d);

        if (g_renameDeck == d) {
            if (renameField(dp(130.0f), deck.name))
                g_renameDeck = -1;
            qa::note("deck " + std::to_string(d + 1) + " renomear");
        } else {
            const float textW = ImGui::CalcTextSize(deck.name.c_str()).x;
            const float w = std::max(dp(72.0f), textW + dp(28.0f));
            if (ImGui::InvisibleButton("##deck", ImVec2(w, deckBarH())))
                deferred.push_back([&ctx, d] { switchDeck(ctx, d); });
            qa::note("deck " + std::to_string(d + 1));
            const bool hovered = ImGui::IsItemHovered();
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                beginRename(g_renameDeck, d, deck.name);

            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            const ImVec4 bg = current ? theme::kPanelActive : (hovered ? theme::kPanelHover : theme::kPanelRaised);
            dl->AddRectFilled(a, b, ImGui::GetColorU32(bg), rounding(), ImDrawFlags_RoundCornersTop);
            if (current)
                dl->AddRectFilled(ImVec2(a.x, b.y - dp(2.0f)), b, ImGui::GetColorU32(theme::kAccent));
            dl->AddText(ImVec2(a.x + (w - textW) * 0.5f, a.y + (deckBarH() - ImGui::GetFontSize()) * 0.5f),
                        ImGui::GetColorU32(current ? theme::kText : theme::kTextDim), deck.name.c_str());

            if (ImGui::BeginPopupContextItem("##deckctx", ImGuiPopupFlags_MouseButtonRight)) {
                if (ImGui::MenuItem("Renomear"))
                    beginRename(g_renameDeck, d, deck.name);
                if (ImGui::MenuItem("Excluir", nullptr, false, comp.deckCount() > 1)) {
                    if (deck.clipCount() > 0) {
                        g_deleteDeck = d;
                        g_openDeleteConfirm = true;
                    } else {
                        deferred.push_back([&comp, &ctx, d] {
                            comp.removeDeck(d);
                            if (ctx.selection.kind == Selection::Kind::Clip)
                                ctx.selection = {};
                        });
                    }
                }
                ImGui::EndPopup();
            }
            if (hovered && !ImGui::IsPopupOpen("##deckctx"))
                ImGui::SetTooltip("Clique: trocar de deck  |  duplo clique: renomear  |  botão direito: excluir");
        }
        ImGui::PopID();
    }

    ImGui::SameLine();
    if (iconButton(ICON_PLUS, "##deck_novo", "Novo deck", ImVec2(dp(30.0f), deckBarH())))
        deferred.push_back([&comp, &ctx] {
            switchDeck(ctx, comp.addDeck());
        });

    ImGui::PopStyleVar(2);

    if (g_openDeleteConfirm) {
        ImGui::OpenPopup("Excluir deck?");
        g_openDeleteConfirm = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Excluir deck?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (comp.validDeck(g_deleteDeck)) {
            const Deck& deck = comp.decks[static_cast<size_t>(g_deleteDeck)];
            ImGui::Text("O deck \"%s\" tem %d clip(s).", deck.name.c_str(), deck.clipCount());
            ImGui::TextUnformatted("Excluir mesmo assim? Os clips tocando nele serão parados.");
        }
        ImGui::Spacing();
        if (ImGui::Button("Excluir", ImVec2(dp(120.0f), 0.0f))) {
            const int d = g_deleteDeck;
            deferred.push_back([&comp, &ctx, d] {
                comp.removeDeck(d);
                if (ctx.selection.kind == Selection::Kind::Clip)
                    ctx.selection = {};
            });
            g_deleteDeck = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(dp(120.0f), 0.0f))) {
            g_deleteDeck = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void drawCells(PanelContext& ctx, Deferred& deferred, bool& addColumnRequested, float contentH)
{
    Composition& comp = ctx.composition;
    const int columns = comp.columnCount();
    const int layers = comp.layerCount();
    const int deckIndex = comp.currentDeck;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float rowsY = origin.y + headerH() + gap();
    const float cw = cellW(), ch = cellH(), th = thumbH(), gp = gap();

    DropRequest& drop = ctx.drop;
    const ImVec2 winMin = ImGui::GetWindowPos();
    const ImVec2 winMax(winMin.x + ImGui::GetWindowWidth(), winMin.y + ImGui::GetWindowHeight());
    const bool dropInWindow = drop.pending && contains(winMin, winMax, drop.x, drop.y);
    CellRef dropCell{-1, -1};

    for (int col = 0; col < columns; ++col) {
        bool colActive = false;
        for (int l = 0; l < layers; ++l)
            colActive = colActive || comp.isActiveCell(deckIndex, l, col);

        const ImVec2 a(origin.x + static_cast<float>(col) * (cw + gp), origin.y);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(col);
        if (ImGui::InvisibleButton("##col", ImVec2(cw, headerH())))
            deferred.push_back([&comp, col] { comp.triggerColumn(col); });
        qa::note("coluna " + std::to_string(col + 1));
        const bool hovered = ImGui::IsItemHovered();
        const control::Target colTarget = control::Target::columnOf(deckIndex, col);
        if (ImGui::BeginPopupContextItem("##colctx")) {
            keyAssignMenu(colTarget, false);
            dmxActionMenu(colTarget);
            ImGui::EndPopup();
        }
        const ImVec2 b(a.x + cw, a.y + headerH());
        dl->AddRectFilled(a, b, ImGui::GetColorU32(hovered ? theme::kPanelHover : theme::kPanelRaised), rounding());
        const ImU32 fg = ImGui::GetColorU32(colActive ? theme::kAccent : theme::kText);
        drawPlayTriangle(dl, ImVec2(a.x + dp(14.0f), (a.y + b.y) * 0.5f), dp(10.0f), fg);
        const std::string label = "Coluna " + std::to_string(col + 1);
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        const std::string colHint = keyHint(colTarget);
        const float labelX = colHint.empty() ? a.x + dp(26.0f) + (cw - dp(26.0f) - ts.x) * 0.5f : a.x + dp(26.0f);
        dl->AddText(ImVec2(labelX, a.y + (headerH() - ts.y) * 0.5f), fg, label.c_str());
        if (!colHint.empty())
            drawKeyHint(dl, ImVec2(b.x - keyHintWidth(colHint) - dp(3.0f), a.y + dp(3.0f)), colHint);
        drawKeyFlash(dl, a, b, keyFlash(colTarget), rounding());
        if (hovered && !ImGui::IsPopupOpen("##colctx"))
            ImGui::SetTooltip("Disparar coluna %d", col + 1);
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x + static_cast<float>(columns) * (cw + gp), origin.y));
    if (iconButton(ICON_PLUS, "##coluna_nova", "Adicionar coluna", ImVec2(headerH() + dp(8.0f), headerH())))
        addColumnRequested = true;

    for (int vr = 0; vr < layers; ++vr) {
        const int li = layers - 1 - vr;
        const float y = rowsY + static_cast<float>(vr) * (ch + gp);
        const Layer& layer = comp.layers[static_cast<size_t>(li)];

        ImGui::PushID(li);
        for (int col = 0; col < columns; ++col) {
            Clip& clip = comp.clip(li, col);
            const ImVec2 a(origin.x + static_cast<float>(col) * (cw + gp), y);
            const ImVec2 b(a.x + cw, a.y + ch);
            const ImVec2 thumbMax(b.x, a.y + th);

            ImGui::PushID(col);
            ImGui::SetCursorScreenPos(a);
            const bool pressed = ImGui::InvisibleButton("cell", ImVec2(cw, ch));
            const std::string cellTag = "celula L" + std::to_string(li + 1) + " C" + std::to_string(col + 1);
            qa::note(cellTag);
            const bool hovered = ImGui::IsItemHovered();
            const bool overName = hovered && ImGui::GetIO().MousePos.y >= thumbMax.y;
            const bool overThumb = hovered && !overName;
            qa::noteRect(cellTag + " nome", a.x, thumbMax.y, b.x, b.y);

            if (dropInWindow && contains(a, b, drop.x, drop.y))
                dropCell = {li, col};

            if (pressed) {
                const bool onName = ImGui::GetIO().MouseClickedPos[ImGuiMouseButton_Left].y >= thumbMax.y;
                deferred.push_back([&comp, &ctx, li, col, onName] {
                    ctx.selection = {Selection::Kind::Clip, li, col};
                    if (!onName)
                        comp.trigger(li, col);
                });
            } else if (ImGui::IsItemActivated()) {
                ctx.selection = {Selection::Kind::Clip, li, col};
            }

            if (!clip.empty() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                const CellRef ref{li, col};
                ImGui::SetDragDropPayload("BSL_CLIP_CELL", &ref, sizeof(ref));
                ImGui::TextUnformatted(clip.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("BSL_MEDIA_PATH")) {
                    const std::string id(static_cast<const char*>(p->Data));
                    deferred.push_back([&comp, &ctx, li, col, id] {
                        Clip c = Clip::fromId(id);
                        if (c.empty()) {
                            log::warn("Formato não suportado: " + id);
                            return;
                        }
                        comp.setClip(li, col, std::move(c));
                        ctx.selection = {Selection::Kind::Clip, li, col};
                        afterClipPlaced(comp, li, col);
                    });
                } else if (const ImGuiPayload* fxp = ImGui::AcceptDragDropPayload(kEffectPayload)) {
                    const std::string type(static_cast<const char*>(fxp->Data));
                    if (!clip.empty()) {
                        deferred.push_back([&comp, &ctx, li, col, type] {
                            comp.clip(li, col).effects.add(type);
                            ctx.selection = {Selection::Kind::Clip, li, col};
                        });
                    } else {
                        log::warn("Solte o efeito sobre um clip (a célula está vazia).");
                    }
                } else if (const ImGuiPayload* q = ImGui::AcceptDragDropPayload("BSL_CLIP_CELL")) {
                    CellRef from{};
                    std::memcpy(&from, q->Data, sizeof(from));
                    deferred.push_back([&comp, &ctx, from, li, col] {
                        comp.swapClips(from.layer, from.column, li, col);
                        ctx.selection = {Selection::Kind::Clip, li, col};
                    });
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem("##ctx")) {
                if (!clip.empty() && ImGui::MenuItem("Limpar"))
                    deferred.push_back([&comp, li, col] { comp.clearClip(li, col); });
                if (clip.missing && !clip.missingPlugin && ImGui::MenuItem("Localizar…")) {
                    const int deck = deckIndex;
                    deferred.push_back([&ctx, deck, li, col] { ctx.project.requestLocate(deck, li, col); });
                }
                if (clip.isPaged()) {
                    const bool known = media::docPageCount(clip.path) >= 1;
                    if (ImGui::MenuItem("Expandir páginas na linha", nullptr, false, known && !clip.missing)) {
                        deferred.push_back([&comp, &ctx, li, col] {
                            if (comp.expandPages(li, col) > 0)
                                ctx.selection = {Selection::Kind::Clip, li, col};
                        });
                    }
                    if (!known && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("O número de páginas ainda não é conhecido: aguarde a miniatura ou dispare o clip.");
                }
                if (!clip.empty() && ImGui::MenuItem("Abrir local do arquivo", nullptr, false,
                                                     clip.type == Clip::Type::File && !clip.missing)) {
                    const std::filesystem::path file = clip.path;
                    deferred.push_back([file] { paths::revealInExplorer(file); });
                }
                keyAssignMenu(control::Target::cell(deckIndex, li, col), !clip.empty());
                dmxActionMenu(control::Target::cell(deckIndex, li, col), false);
                ImGui::EndPopup();
            }

            const bool selected = ctx.selection.kind == Selection::Kind::Clip && ctx.selection.layer == li
                                  && ctx.selection.column == col;
            const bool active = comp.isActiveCell(deckIndex, li, col) && clip.isOpen();

            if (clip.empty()) {
                dl->AddRectFilled(a, b, ImGui::GetColorU32(theme::kPanelRaised), rounding());
                drawCenteredMono(dl, a, b, std::to_string(col + 1).c_str(), ImGui::GetColorU32(theme::kPanelActive));
            } else if (clip.missing) {
                drawMissingCell(dl, a, thumbMax, b, clip.name, clip.missingPlugin ? "plugin ausente" : "não encontrado");
            } else {
                dl->AddRectFilled(a, b, ImGui::GetColorU32(theme::kBackground), rounding());
                bool drewThumb = false;
                const float px = std::max(1.0f, dp(1.0f));
                if (clip.isTestPattern()) {
                    drawPatternThumb(dl, ImVec2(a.x + px, a.y + px), ImVec2(thumbMax.x - px, thumbMax.y));
                    drewThumb = true;
                } else if (clip.isGenerator() || clip.isWeb()) {
                    const ImVec2 t0(a.x + px, a.y + px), t1(thumbMax.x - px, thumbMax.y);
                    if (clip.liveThumb.id) {
                        dl->AddImage(static_cast<ImTextureID>(clip.liveThumb.id), t0, t1);
                    } else if (clip.generatorId == kSolidColorId) {
                        const media::Color c = media::ParamSet(clip.paramDescs(), clip.params).c("color");
                        dl->AddRectFilled(t0, t1, ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, 1.0f)));
                    } else {
                        const GeneratorInfo* info = clip.generatorInfo();
                        drawCenteredFit(dl, t0, t1, info ? info->name : "Página web", ImGui::GetColorU32(theme::kTextDim));
                    }
                    drewThumb = true;
                } else if (const GLuint tex = ctx.thumbs.get(clip.path)) {
                    dl->AddImage(static_cast<ImTextureID>(tex), a, thumbMax);
                    drewThumb = true;
                }
                if (!drewThumb)
                    drawCenteredMono(dl, a, thumbMax, "...", ImGui::GetColorU32(theme::kTextDim));

                dl->PushClipRect(ImVec2(a.x + dp(2.0f), thumbMax.y), ImVec2(b.x - dp(2.0f), b.y), true);
                drawEllipsized(dl, ImVec2(a.x + dp(5.0f), thumbMax.y + dp(2.0f)), cw - dp(overName ? 28.0f : 10.0f),
                               ImGui::GetColorU32(theme::kText), clip.name);
                dl->PopClipRect();

                if (active && clip.isRemote()) {
                    const media::SourceStatus st = clip.source->status();
                    if (!st.text.empty()) {
                        const bool warn = st.level == media::SourceStatus::Level::Warning;
                        const bool err = st.level == media::SourceStatus::Level::Error;
                        const ImVec2 t0(a.x + px, a.y + px), t1(thumbMax.x - px, thumbMax.y);
                        dl->AddRectFilled(t0, t1, IM_COL32(0, 0, 0, 150));
                        drawCenteredFit(dl, ImVec2(t0.x + dp(4.0f), t0.y), ImVec2(t1.x - dp(4.0f), t1.y), st.text.c_str(),
                                        ImGui::GetColorU32(err ? theme::kError : warn ? theme::kWarning : theme::kText));
                    }
                }

                if (active && clip.source->duration() > 0.0) {
                    const float f = static_cast<float>(clip.source->position() / clip.source->duration());
                    dl->AddRectFilled(ImVec2(a.x, thumbMax.y - dp(3.0f)),
                                      ImVec2(a.x + cw * std::clamp(f, 0.0f, 1.0f), thumbMax.y),
                                      ImGui::GetColorU32(theme::kAccent));
                }
            }

            if (!clip.empty() && !clip.missing)
                drawEffectsBadge(dl, ImVec2(b.x - dp(13.0f), a.y + dp(13.0f)), clip.effects);
            const control::Target cellTarget = control::Target::cell(deckIndex, li, col);
            if (const std::string cellHint = keyHint(cellTarget); !cellHint.empty())
                drawKeyHint(dl, ImVec2(a.x + dp(4.0f), a.y + dp(4.0f)), cellHint);

            const bool usable = !clip.empty() && !clip.missing && !ImGui::IsMouseDragging(ImGuiMouseButton_Left);
            if (overThumb) {
                dl->AddRectFilled(a, thumbMax, IM_COL32(255, 255, 255, 14), rounding(), ImDrawFlags_RoundCornersTop);
                if (usable) {
                    const ImVec2 c((a.x + thumbMax.x) * 0.5f, (a.y + thumbMax.y) * 0.5f);
                    dl->AddCircleFilled(c, dp(14.0f), IM_COL32(0, 0, 0, 150));
                    drawPlayTriangle(dl, c, dp(14.0f), IM_COL32(255, 255, 255, 235));
                }
            } else if (overName) {
                dl->AddRectFilled(ImVec2(a.x, thumbMax.y), b, IM_COL32(255, 255, 255, 30), rounding(),
                                  ImDrawFlags_RoundCornersBottom);
                if (usable)
                    drawIcon(dl, ICON_EYE, ImVec2(b.x - dp(12.0f), thumbMax.y + labelH() * 0.5f),
                             ImGui::GetColorU32(theme::kTextStrong));
            }
            if (selected)
                dl->AddRect(ImVec2(a.x - dp(1.5f), a.y - dp(1.5f)), ImVec2(b.x + dp(1.5f), b.y + dp(1.5f)),
                            IM_COL32(246, 246, 247, 210), rounding(), 0, dp(1.5f));
            if (active)
                dl->AddRect(ImVec2(a.x + dp(1.5f), a.y + dp(1.5f)), ImVec2(b.x - dp(1.5f), b.y - dp(1.5f)),
                            ImGui::GetColorU32(theme::kAccent), rounding(), 0, dp(3.0f));
            drawKeyFlash(dl, a, b, keyFlash(cellTarget), rounding());

            if (hovered && !clip.empty() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const std::string where = layer.name + " - coluna " + std::to_string(col + 1);
                if (clip.missingPlugin)
                    ImGui::SetTooltip("%s\n%s\nPlugin ausente: %s\nA célula não toca, mas o projeto guarda o plugin e os parâmetros.",
                                      clip.name.c_str(), where.c_str(), clip.generatorId.c_str());
                else if (clip.missing)
                    ImGui::SetTooltip("%s\n%s\nArquivo não encontrado: %s\nBotão direito: Localizar…", clip.name.c_str(),
                                      where.c_str(), paths::toUtf8(clip.path).c_str());
                else
                    ImGui::SetTooltip("%s\n%s\n%s", clip.name.c_str(), where.c_str(),
                                      overName ? "Clique: ver na prévia" : "Clique: tocar na saída");
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + contentH - 1.0f));
    ImGui::Dummy(ImVec2((cw + gp) * static_cast<float>(columns) + headerH() + dp(8.0f), 1.0f));

    if (ctx.selection.kind == Selection::Kind::Clip && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
        && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        const Selection sel = ctx.selection;
        deferred.push_back([&comp, sel] { comp.clearClip(sel.layer, sel.column); });
    }

    if (dropInWindow) {
        if (dropCell.layer >= 0)
            applyFileDrop(comp, ctx.selection, dropCell.layer, dropCell.column, drop.files);
        else
            log::warn("Solte os arquivos sobre uma célula da Composição.");
        drop.pending = false;
        drop.files.clear();
    }
}

}

void drawCompositionPanel(PanelContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, dp(10.0f, 10.0f));
    beginPanel(panel::kComposition, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    Composition& comp = ctx.composition;
    const int layers = comp.layerCount();
    const float spacingY = ImGui::GetStyle().ItemSpacing.y;
    const int transportLines = transportRows(ImGui::GetContentRegionAvail().x);
    const float footerH = deckBarH() + static_cast<float>(transportLines) * (transportBarHeight() + spacingY) + spacingY + dp(4.0f);

    Deferred deferred;
    bool addColumnRequested = false;
    bool addLayerRequested = false;

    const float contentH = headerH() + gap() + static_cast<float>(layers) * (cellH() + gap());
    const float neededH = contentH + std::max(ImGui::GetStyle().ScrollbarSize + dp(2.0f), addLayerH()) + dp(4.0f);
    const float bodyH = std::clamp(ImGui::GetContentRegionAvail().y - footerH, dp(40.0f), neededH);

    if (ImGui::BeginChild("##corpo", ImVec2(0.0f, bodyH), ImGuiChildFlags_None)) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float availW = ImGui::GetContentRegionAvail().x;
        const float rowsY = origin.y + headerH() + gap();

        drawMasterHeader(ctx, origin, deferred);
        for (int vr = 0; vr < layers; ++vr) {
            const int li = layers - 1 - vr;
            drawLayerControls(ctx, li, ImVec2(origin.x, rowsY + static_cast<float>(vr) * (cellH() + gap())), deferred);
        }

        const float belowY = rowsY + static_cast<float>(layers) * (cellH() + gap());
        ImGui::SetCursorScreenPos(ImVec2(origin.x, belowY));
        if (ImGui::Button(ICON_PLUS "  Camada##camada_nova", ImVec2(controlsW(), addLayerH())))
            addLayerRequested = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Adicionar camada");

        const float cellsX = origin.x + controlsW() + gap();
        const float childW = std::max(dp(60.0f), availW - controlsW() - gap() - 1.0f);
        ImGui::SetCursorScreenPos(ImVec2(cellsX, origin.y));
        if (ImGui::BeginChild("##celulas", ImVec2(childW, contentH + ImGui::GetStyle().ScrollbarSize + 2.0f),
                              ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
            drawCells(ctx, deferred, addColumnRequested, contentH);
        ImGui::EndChild();

        ImGui::SetCursorScreenPos(ImVec2(origin.x, belowY + addLayerH()));
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
    }
    ImGui::EndChild();

    drawDeckBar(ctx, deferred);
    drawTransportBar(ctx);

    for (auto& fn : deferred)
        fn();
    if (addColumnRequested)
        comp.addColumn();
    if (addLayerRequested)
        comp.addLayer();

    endPanel();
}

}
