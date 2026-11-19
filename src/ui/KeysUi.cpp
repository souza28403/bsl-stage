#include "ui/KeysUi.h"

#include "app/Paths.h"
#include "app/ProjectSession.h"
#include "control/KeyBinding.h"
#include "control/KeyService.h"
#include "io/FileDialogs.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"
#include "ui/panels/EffectChainWidget.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <map>
#include <string>
#include <vector>

namespace bsl::ui {

namespace {

using control::ActionArgs;
using control::ActionDef;
using control::Chord;
using control::Key;
using control::KeyBinding;
using control::KeyBindingSet;
using control::Scope;
using control::Target;

ImGuiKey imguiKeyOf(Key k)
{
    static const std::array<ImGuiKey, static_cast<size_t>(Key::Count)> table = [] {
        std::array<ImGuiKey, static_cast<size_t>(Key::Count)> t{};
        auto at = [&t](Key key) -> ImGuiKey& { return t[static_cast<size_t>(key)]; };
        for (int i = 0; i < 26; ++i)
            at(static_cast<Key>(static_cast<int>(Key::A) + i)) = static_cast<ImGuiKey>(ImGuiKey_A + i);
        for (int i = 0; i < 10; ++i) {
            at(static_cast<Key>(static_cast<int>(Key::D0) + i)) = static_cast<ImGuiKey>(ImGuiKey_0 + i);
            at(static_cast<Key>(static_cast<int>(Key::Num0) + i)) = static_cast<ImGuiKey>(ImGuiKey_Keypad0 + i);
        }
        for (int i = 0; i < 12; ++i)
            at(static_cast<Key>(static_cast<int>(Key::F1) + i)) = static_cast<ImGuiKey>(ImGuiKey_F1 + i);
        at(Key::Space) = ImGuiKey_Space;
        at(Key::Enter) = ImGuiKey_Enter;
        at(Key::Tab) = ImGuiKey_Tab;
        at(Key::Escape) = ImGuiKey_Escape;
        at(Key::Backspace) = ImGuiKey_Backspace;
        at(Key::Delete) = ImGuiKey_Delete;
        at(Key::Insert) = ImGuiKey_Insert;
        at(Key::Home) = ImGuiKey_Home;
        at(Key::End) = ImGuiKey_End;
        at(Key::PageUp) = ImGuiKey_PageUp;
        at(Key::PageDown) = ImGuiKey_PageDown;
        at(Key::Left) = ImGuiKey_LeftArrow;
        at(Key::Right) = ImGuiKey_RightArrow;
        at(Key::Up) = ImGuiKey_UpArrow;
        at(Key::Down) = ImGuiKey_DownArrow;
        at(Key::NumAdd) = ImGuiKey_KeypadAdd;
        at(Key::NumSub) = ImGuiKey_KeypadSubtract;
        at(Key::NumMul) = ImGuiKey_KeypadMultiply;
        at(Key::NumDiv) = ImGuiKey_KeypadDivide;
        at(Key::NumDecimal) = ImGuiKey_KeypadDecimal;
        at(Key::NumEnter) = ImGuiKey_KeypadEnter;
        at(Key::Minus) = ImGuiKey_Minus;
        at(Key::Equal) = ImGuiKey_Equal;
        at(Key::LeftBracket) = ImGuiKey_LeftBracket;
        at(Key::RightBracket) = ImGuiKey_RightBracket;
        at(Key::Semicolon) = ImGuiKey_Semicolon;
        at(Key::Apostrophe) = ImGuiKey_Apostrophe;
        at(Key::Comma) = ImGuiKey_Comma;
        at(Key::Period) = ImGuiKey_Period;
        at(Key::Slash) = ImGuiKey_Slash;
        at(Key::Backslash) = ImGuiKey_Backslash;
        at(Key::Grave) = ImGuiKey_GraveAccent;
        return t;
    }();
    return table[static_cast<size_t>(k)];
}

std::uint8_t currentMods()
{
    const ImGuiIO& io = ImGui::GetIO();
    return static_cast<std::uint8_t>((io.KeyCtrl ? control::ModCtrl : 0) | (io.KeyShift ? control::ModShift : 0)
                                     | (io.KeyAlt ? control::ModAlt : 0));
}

struct Capture
{
    bool active = false;
    bool isRow = false;
    Scope scope = Scope::Project;
    int index = -1;
    std::string action;
    ActionArgs args;
    std::string title;
    control::Mode mode = control::Mode::Press;
    int frames = 0;
};

struct Pending
{
    Capture cap;
    Chord chord;
    std::vector<std::string> names;
    bool openRequested = false;
    bool open = false;
};

struct Hint
{
    std::string text;
    bool fixed = false;
};

struct State
{
    Settings* settings = nullptr;
    Composition* comp = nullptr;
    control::KeyService* svc = nullptr;
    Selection selection;
    AppMode mode = AppMode::Composition;

    std::map<std::string, Hint> hints;
    Capture cap;
    Pending pending;

    bool editorOpen = false;
    char search[96] = {};
    std::string status;
    bool exportRequest = false;
    bool importRequest = false;
    int visibleRows = 0;
    std::map<std::string, std::string> probeCache;
};
State g;

KeyBindingSet& setFor(Scope scope)
{
    return scope == Scope::Project ? g.comp->keys : g.settings->keys;
}

void touch(Scope scope)
{
    if (scope == Scope::App && g.settings)
        g.settings->save();
}

const char* scopeLabel(Scope scope)
{
    return scope == Scope::Project ? "Projeto" : "App";
}

std::string describe(const KeyBinding& b)
{
    const ActionDef* def = control::findAction(b.action);
    return def ? control::describeAction(*g.comp, *def, b.args) : b.action;
}

bool mapEditorFocused(AppMode mode)
{
    if (mode == AppMode::Composition || !GImGui || !GImGui->NavWindow)
        return false;
    const ImGuiWindow* nav = GImGui->NavWindow;
    for (const char* name : {panel::kMapInput, panel::kMapOutput, panel::kPmOverview, panel::kSeqList, panel::kSeqCanvas,
                             panel::kSeqSteps, panel::kSeqGen, panel::kSeqEnv}) {
        const ImGuiWindow* w = ImGui::FindWindowByName(name);
        if (w && (nav == w || nav->RootWindow == w))
            return true;
    }
    return false;
}

bool hintAction(const std::string& id)
{
    return id == "clip.trigger" || id == "column.trigger" || id == "layer.power" || id == "effect.bypass";
}

void rebuildHints(const control::SelectionRef& sel)
{
    g.hints.clear();
    auto scan = [&](const KeyBindingSet& set) {
        for (const KeyBinding& b : set.items) {
            if (!b.chord.valid() || !hintAction(b.action))
                continue;
            const ActionDef* def = control::findAction(b.action);
            if (!def)
                continue;
            const Target t = control::targetOf(*g.comp, sel, *def, b.args);
            if (!t.valid())
                continue;
            const bool fixed = !b.args.selected;
            auto it = g.hints.find(t.key());
            if (it == g.hints.end())
                g.hints.emplace(t.key(), Hint{control::chordLabel(b.chord), fixed});
            else if (fixed && !it->second.fixed)
                it->second = Hint{control::chordLabel(b.chord), true};
        }
    };
    scan(g.comp->keys);
    scan(g.settings->keys);
}

void beginCapture(Capture cap)
{
    cap.active = true;
    cap.frames = 0;
    g.cap = std::move(cap);
}

void commitChord(const Capture& cap, int ownScope, int ownIndex, Chord chord)
{
    for (const control::Match& m : control::findConflicts(chord, g.settings->keys, g.comp->keys, ownScope, ownIndex)) {
        setFor(m.scope).items[static_cast<size_t>(m.index)].chord = Chord{};
        touch(m.scope);
    }
    if (ownIndex >= 0) {
        setFor(static_cast<Scope>(ownScope)).items[static_cast<size_t>(ownIndex)].chord = chord;
        touch(static_cast<Scope>(ownScope));
    } else {
        KeyBinding b;
        b.action = cap.action;
        b.args = cap.args;
        b.chord = chord;
        b.mode = cap.mode;
        setFor(cap.scope).items.push_back(std::move(b));
        touch(cap.scope);
    }
    g.status = "Tecla " + control::chordLabel(chord) + " atribuída.";
}

void assignChord(const Capture& cap, Chord chord)
{
    int ownScope = -1, ownIndex = -1;
    if (cap.isRow) {
        const KeyBindingSet& set = setFor(cap.scope);
        if (cap.index >= 0 && cap.index < static_cast<int>(set.items.size())) {
            ownScope = static_cast<int>(cap.scope);
            ownIndex = cap.index;
        }
    } else {
        const int i = setFor(cap.scope).find(cap.action, cap.args);
        if (i >= 0) {
            ownScope = static_cast<int>(cap.scope);
            ownIndex = i;
        }
    }
    const std::vector<control::Match> conflicts =
        control::findConflicts(chord, g.settings->keys, g.comp->keys, ownScope, ownIndex);
    if (conflicts.empty()) {
        commitChord(cap, ownScope, ownIndex, chord);
        return;
    }
    g.pending = Pending{};
    g.pending.cap = cap;
    g.pending.chord = chord;
    for (const control::Match& m : conflicts)
        g.pending.names.push_back(describe(*m.binding) + " (" + (m.scope == Scope::Project ? "projeto" : "app") + ")");
    g.pending.openRequested = true;
}

void drawCaptureWindow()
{
    if (!g.cap.active)
        return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->GetCenter().x, vp->WorkPos.y + dp(70.0f)), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.96f);
    if (ImGui::Begin("Gravar atalho###keys_capture", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse
                         | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::PushFont(fonts().bold, fonts().bold->LegacySize);
        ImGui::TextUnformatted("Aperte a tecla");
        ImGui::PopFont();
        ImGui::TextUnformatted(g.cap.title.c_str());
        ImGui::TextColored(theme::kTextDim, "Combine com Ctrl, Shift ou Alt se quiser. Esc cancela.");
        if (ImGui::Button("Cancelar##key_cancelar_gravar"))
            g.cap.active = false;
    }
    ImGui::End();

    if (!g.cap.active || ++g.cap.frames < 3)
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        g.cap.active = false;
        return;
    }
    for (int i = 1; i < static_cast<int>(Key::Count); ++i) {
        const Key k = static_cast<Key>(i);
        if (control::keyIsReservedForCapture(k) || !ImGui::IsKeyPressed(imguiKeyOf(k), false))
            continue;
        const Capture cap = g.cap;
        g.cap.active = false;
        assignChord(cap, Chord{k, currentMods()});
        return;
    }
}

void drawConflictModal()
{
    if (g.pending.openRequested) {
        ImGui::OpenPopup("Tecla já usada###keys_conflito");
        g.pending.openRequested = false;
        g.pending.open = true;
    }
    if (!g.pending.open)
        return;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Tecla já usada###keys_conflito", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("A tecla %s já está atribuída a:", control::chordLabel(g.pending.chord).c_str());
        for (const std::string& n : g.pending.names)
            ImGui::BulletText("%s", n.c_str());
        ImGui::Spacing();
        ImGui::TextColored(theme::kTextDim, "Substituir tira a tecla do atalho acima e a dá para o novo.");
        ImGui::Spacing();
        if (ImGui::Button("Substituir##key_conflito_substituir", ImVec2(dp(140.0f), 0.0f))) {
            const Capture cap = g.pending.cap;
            int ownScope = -1, ownIndex = -1;
            if (cap.isRow) {
                ownScope = static_cast<int>(cap.scope);
                ownIndex = cap.index;
            } else if (const int i = setFor(cap.scope).find(cap.action, cap.args); i >= 0) {
                ownScope = static_cast<int>(cap.scope);
                ownIndex = i;
            }
            commitChord(cap, ownScope, ownIndex, g.pending.chord);
            g.pending.open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar##key_conflito_cancelar", ImVec2(dp(140.0f), 0.0f))
            || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            g.pending.open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    } else {
        g.pending.open = false;
    }
}

bool editArgs(const ActionDef& def, ActionArgs& a)
{
    bool changed = false;
    const bool effect = (def.args & control::ArgEffect) != 0;
    if (effect) {
        static const char* kScopes[] = {"Clip", "Camada", "Master"};
        ImGui::SetNextItemWidth(dp(130.0f));
        if (beginCombo("Cadeia de efeitos##atalho_fxescopo", kScopes[std::clamp(a.fxScope, 0, 2)])) {
            for (int i = 0; i < 3; ++i)
                if (ImGui::Selectable(kScopes[i], a.fxScope == i)) {
                    a.fxScope = i;
                    changed = true;
                }
            ImGui::EndCombo();
        }
    }
    const bool needsLayer = (def.args & control::ArgLayer) != 0 && !(effect && a.fxScope == 2);
    if (needsLayer) {
        changed |= ImGui::Checkbox("Camada selecionada (a que estiver em foco)##atalho_sel", &a.selected);
        if (!a.selected) {
            const int cur = a.layerId > 0 ? g.comp->findLayerById(a.layerId) : a.layer;
            int v = cur + 1;
            ImGui::SetNextItemWidth(dp(130.0f));
            if (ImGui::InputInt("Camada (1 = a de baixo)##atalho_camada", &v)) {
                const int idx = std::max(0, v - 1);
                a.layerId = idx < g.comp->layerCount() ? g.comp->layers[static_cast<size_t>(idx)].id : 0;
                a.layer = a.layerId > 0 ? 0 : idx;
                changed = true;
            }
            if (cur < 0)
                ImGui::TextColored(theme::kWarning, "A camada deste atalho foi removida: ele não faz nada.");
        }
    }
    if ((def.args & control::ArgColumn) != 0 && !(effect && a.fxScope != 0)) {
        const bool followsSelection = std::string(def.id) == "clip.trigger" && a.selected;
        int v = a.column + 1;
        ImGui::SetNextItemWidth(dp(130.0f));
        if (followsSelection) {
            if (ImGui::InputInt("Coluna (0 = a do clip selecionado)##atalho_coluna", &v)) {
                a.column = std::max(0, v) - 1;
                changed = true;
            }
        } else if (effect && a.selected) {
            ImGui::TextColored(theme::kTextDim, "Coluna: a do clip selecionado");
        } else if (ImGui::InputInt("Coluna##atalho_coluna", &v)) {
            a.column = std::max(0, v - 1);
            changed = true;
        }
    }
    if ((def.args & control::ArgDeck) != 0 && !(effect && a.fxScope != 0)) {
        int v = a.deck + 1;
        ImGui::SetNextItemWidth(dp(130.0f));
        if (ImGui::InputInt("Deck (0 = o atual)##atalho_deck", &v)) {
            a.deck = std::max(0, v) - 1;
            changed = true;
        }
    }
    if (std::string(def.id) == "layer.transform.preset") {
        ImGui::SetNextItemWidth(dp(260.0f));
        const int cur = std::clamp(a.index, 0, kTransformPresetCount - 1);
        if (beginCombo("Preset de transform##atalho_preset_transform", transformPresetName(cur))) {
            for (int i = 0; i < kTransformPresetCount; ++i)
                if (ImGui::Selectable(transformPresetName(i), i == cur)) {
                    a.index = i;
                    changed = true;
                }
            ImGui::EndCombo();
        }
    } else if ((def.args & control::ArgIndex) != 0) {
        int v = a.index + 1;
        ImGui::SetNextItemWidth(dp(130.0f));
        const char* label = std::string(def.id) == "ui.layoutPreset" ? "Preset de layout nº##atalho_indice"
                                                                       : "Deck nº##atalho_indice";
        if (ImGui::InputInt(label, &v)) {
            a.index = std::max(0, v - 1);
            changed = true;
        }
    }
    if (effect) {
        int v = a.effect + 1;
        ImGui::SetNextItemWidth(dp(130.0f));
        if (ImGui::InputInt("Efeito nº (1 = o primeiro)##atalho_efeito", &v)) {
            a.effect = std::max(0, v - 1);
            changed = true;
        }
    }
    if ((def.args & control::ArgParam) != 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s", a.param.c_str());
        ImGui::SetNextItemWidth(dp(160.0f));
        if (ImGui::InputText("Parâmetro (nome interno)##atalho_param", buf, sizeof(buf))) {
            a.param = buf;
            changed = true;
        }
    }
    if ((def.args & control::ArgValue) != 0) {
        int pct = static_cast<int>(a.value * 100.0f + 0.5f);
        ImGui::SetNextItemWidth(dp(160.0f));
        if (ImGui::SliderInt("Opacidade##atalho_valor", &pct, 0, 100, "%d%%")) {
            a.value = static_cast<float>(pct) / 100.0f;
            changed = true;
        }
    }
    return changed;
}

void drawEditPopup(KeyBinding& b)
{
    if (!ImGui::BeginPopup("##atalho_editar"))
        return;
    ImGui::TextColored(theme::kTextDim, "Ação");
    const ActionDef* cur = control::findAction(b.action);
    ImGui::SetNextItemWidth(dp(280.0f));
    if (beginCombo("##atalho_acao", cur ? cur->label : b.action.c_str())) {
        std::string lastGroup;
        for (const ActionDef& d : control::allActions()) {
            if (lastGroup != d.group) {
                ImGui::SeparatorText(d.group);
                lastGroup = d.group;
            }
            const std::string label = std::string(d.label) + "##acao_" + d.id;
            if (ImGui::Selectable(label.c_str(), b.action == d.id) && b.action != d.id) {
                b.action = d.id;
                b.args = ActionArgs{};
                if (!control::supportsMode(b.action, b.mode))
                    b.mode = control::Mode::Press;
            }
        }
        ImGui::EndCombo();
    }
    if (cur)
        editArgs(*cur, b.args);
    ImGui::EndPopup();
}

std::string globalWarning(const KeyBinding& b)
{
    if (!b.global || !b.chord.valid())
        return {};
    const std::string id = control::chordId(b.chord);
    auto it = g.probeCache.find(id);
    if (it == g.probeCache.end())
        it = g.probeCache.emplace(id, control::SystemHotkeys::probe(b.chord)).first;
    std::string text = it->second;
    if ((b.chord.mods & (control::ModCtrl | control::ModAlt | control::ModShift)) == 0) {
        if (!text.empty())
            text += "\n";
        text += "Sem Ctrl, Alt ou Shift, esta tecla fica indisponível em todos os programas enquanto o BSL Stage está "
                "em segundo plano.";
    }
    return text;
}

struct RowOp
{
    enum class Kind { None, Remove, Move } kind = Kind::None;
    Scope scope = Scope::Project;
    int index = 0;
};

void drawEditor(PanelContext& ctx)
{
    if (!g.editorOpen)
        return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(dp(900.0f, 500.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(vp->GetCenter().x, vp->WorkPos.y + vp->WorkSize.y - dp(8.0f)), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 1.0f));
    if (!ImGui::Begin("Atalhos###keys_editor", &g.editorOpen, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    (void)ctx;

    ImGui::SetNextItemWidth(dp(260.0f));
    ImGui::InputTextWithHint("##atalhos_busca", "Buscar ação ou tecla…", g.search, sizeof(g.search));
    ImGui::SameLine();
    if (ImGui::Button(ICON_PLUS "  Novo atalho##atalhos_novo")) {
        KeyBinding b;
        b.action = "clip.trigger";
        g.comp->keys.items.push_back(b);
        Capture cap;
        cap.isRow = true;
        cap.scope = Scope::Project;
        cap.index = static_cast<int>(g.comp->keys.items.size()) - 1;
        cap.title = describe(b);
        beginCapture(cap);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_UNDO "  Restaurar padrões##atalhos_restaurar"))
        ImGui::OpenPopup("Restaurar padrões?###keys_restaurar");
    ImGui::SameLine();
    if (ImGui::Button(ICON_UPLOAD "  Exportar…##atalhos_exportar"))
        g.exportRequest = true;
    ImGui::SameLine();
    if (ImGui::Button(ICON_DOWNLOAD "  Importar…##atalhos_importar"))
        g.importRequest = true;

    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Restaurar padrões?###keys_restaurar", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted("Volta todos os atalhos ao padrão: os do aplicativo e os deste projeto.");
        ImGui::Spacing();
        if (ImGui::Button("Restaurar##atalhos_restaurar_ok", ImVec2(dp(140.0f), 0.0f))) {
            g.settings->keys = control::defaultAppKeys();
            g.comp->keys = control::defaultProjectKeys();
            g.settings->save();
            g.status = "Atalhos restaurados ao padrão.";
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar##atalhos_restaurar_cancelar", ImVec2(dp(140.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    const float footerH = ImGui::GetTextLineHeightWithSpacing() * 2.0f + ImGui::GetFrameHeightWithSpacing() + dp(6.0f);
    RowOp op;
    const std::string needle = foldSearchText(g.search);
    int shown = 0;
    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY
                                       | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##atalhos_tabela", 6, kFlags, ImVec2(0.0f, -footerH))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Ação", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Tecla", ImGuiTableColumnFlags_WidthFixed, dp(190.0f));
        ImGui::TableSetupColumn("Escopo", ImGuiTableColumnFlags_WidthFixed, dp(96.0f));
        ImGui::TableSetupColumn("Modo", ImGuiTableColumnFlags_WidthFixed, dp(128.0f));
        ImGui::TableSetupColumn("Global", ImGuiTableColumnFlags_WidthFixed, dp(70.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, dp(34.0f));
        ImGui::TableHeadersRow();

        for (const Scope scope : {Scope::Project, Scope::App}) {
            KeyBindingSet& set = setFor(scope);
            for (int i = 0; i < static_cast<int>(set.items.size()); ++i) {
                KeyBinding& b = set.items[static_cast<size_t>(i)];
                const std::string desc = describe(b);
                if (!needle.empty()) {
                    const std::string hay = foldSearchText(desc + " " + control::chordLabel(b.chord) + " "
                                                           + control::chordId(b.chord) + " " + scopeLabel(scope));
                    if (hay.find(needle) == std::string::npos)
                        continue;
                }
                ++shown;
                const std::string n = std::to_string(shown);
                ImGui::PushID(static_cast<int>(scope) * 100000 + i);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                const ImVec4 textCol = b.chord.valid() ? theme::kText : theme::kTextDim;
                ImGui::PushStyleColor(ImGuiCol_Text, textCol);
                if (ImGui::Selectable((desc + "##atalho_" + n + "_acao").c_str()))
                    ImGui::OpenPopup("##atalho_editar");
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Clique para trocar a ação ou os argumentos (camada, coluna...)");
                drawEditPopup(b);

                ImGui::TableSetColumnIndex(1);
                const bool recording = g.cap.active && g.cap.isRow && g.cap.scope == scope && g.cap.index == i;
                const std::vector<control::Match> conflicts =
                    control::findConflicts(b.chord, g.settings->keys, g.comp->keys, static_cast<int>(scope), i);
                const std::string keyText = recording ? std::string("aperte…")
                                            : !b.chord.valid() ? std::string("sem tecla")
                                                               : control::chordLabel(b.chord);
                const ImVec4 keyCol = recording            ? theme::kAccentHover
                                      : !b.chord.valid()   ? theme::kTextDim
                                      : conflicts.empty()  ? theme::kTextStrong
                                                           : theme::kWarning;
                const float capH = ImGui::GetFrameHeight();
                const float capW = std::max(dp(34.0f), ImGui::CalcTextSize(keyText.c_str()).x + dp(14.0f));
                const ImVec2 capPos = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(capW, capH));
                ImDrawList* rowDl = ImGui::GetWindowDrawList();
                rowDl->AddRectFilled(capPos, ImVec2(capPos.x + capW, capPos.y + capH), ImGui::GetColorU32(theme::kField),
                                     rounding());
                rowDl->AddRect(capPos, ImVec2(capPos.x + capW, capPos.y + capH),
                               ImGui::GetColorU32(conflicts.empty() ? theme::kBorder : theme::kWarning), rounding());
                const ImVec2 keySize = ImGui::CalcTextSize(keyText.c_str());
                rowDl->AddText(ImVec2(capPos.x + (capW - keySize.x) * 0.5f, capPos.y + (capH - keySize.y) * 0.5f),
                               ImGui::GetColorU32(keyCol), keyText.c_str());
                if (!conflicts.empty() && ImGui::IsItemHovered()) {
                    std::string tip = "Esta tecla também está em:";
                    for (const control::Match& m : conflicts)
                        tip += "\n  " + describe(*m.binding) + " (" + (m.scope == Scope::Project ? "projeto" : "app") + ")";
                    ImGui::SetTooltip("%s", tip.c_str());
                }
                ImGui::SameLine(0.0f, dp(10.0f));
                if (ImGui::Button(("Gravar##atalho_" + n + "_gravar").c_str())) {
                    Capture cap;
                    cap.isRow = true;
                    cap.scope = scope;
                    cap.index = i;
                    cap.title = desc;
                    beginCapture(cap);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Aperte a tecla (com Ctrl/Shift/Alt, se quiser) para atribuir");

                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (beginCombo(("##atalho_" + n + "_escopo").c_str(), scopeLabel(scope))) {
                    for (const Scope s : {Scope::Project, Scope::App})
                        if (ImGui::Selectable(scopeLabel(s), s == scope) && s != scope)
                            op = {RowOp::Kind::Move, scope, i};
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Projeto: vai no arquivo do projeto (atalhos de show).\nApp: vale em qualquer projeto.");

                ImGui::TableSetColumnIndex(3);
                ImGui::SetNextItemWidth(-FLT_MIN);
                const bool canHold = control::supportsHold(b.action);
                const bool canRepeat = control::supportsRepeat(b.action);
                const int rate = control::snapRate(b.rate);
                const char* const rateNames[] = {"Repetir 1/4", "Repetir 1/8", "Repetir 1/16", "Repetir 1/32"};
                const char* const rateItems[] = {"Repetir 1/4 (uma vez por batida)", "Repetir 1/8", "Repetir 1/16",
                                                 "Repetir 1/32"};
                const int rates[] = {1, 2, 4, 8};
                const char* modeLabel = b.mode == control::Mode::Hold ? "Segurar" : "Pressionar";
                if (b.mode == control::Mode::Repeat)
                    for (int r = 0; r < 4; ++r)
                        if (rates[r] == rate)
                            modeLabel = rateNames[r];
                if (beginCombo(("##atalho_" + n + "_modo").c_str(), modeLabel)) {
                    if (ImGui::Selectable("Pressionar", b.mode == control::Mode::Press) && b.mode != control::Mode::Press) {
                        b.mode = control::Mode::Press;
                        touch(scope);
                    }
                    ImGui::BeginDisabled(!canHold);
                    if (ImGui::Selectable("Segurar", b.mode == control::Mode::Hold) && b.mode != control::Mode::Hold) {
                        b.mode = control::Mode::Hold;
                        touch(scope);
                    }
                    ImGui::EndDisabled();
                    ImGui::Separator();
                    ImGui::BeginDisabled(!canRepeat);
                    for (int r = 0; r < 4; ++r) {
                        const bool selected = b.mode == control::Mode::Repeat && rate == rates[r];
                        if (ImGui::Selectable(rateItems[r], selected) && !selected) {
                            b.mode = control::Mode::Repeat;
                            b.rate = rates[r];
                            touch(scope);
                        }
                    }
                    ImGui::EndDisabled();
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Pressionar: dispara ao apertar.\nSegurar: liga enquanto a tecla está apertada e desliga ao "
                                      "soltar (flash, blackout). Só para ações de ligar/desligar.\nRepetir: enquanto a tecla "
                                      "está apertada, dispara de novo no ritmo do BPM (strobo). Ao soltar, volta como estava.");

                ImGui::TableSetColumnIndex(4);
                if (ImGui::Checkbox(("##atalho_" + n + "_global").c_str(), &b.global))
                    touch(scope);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Funcionar com o BSL Stage em segundo plano (atalho global do sistema).\nUse quando "
                                      "outra janela está em foco.");
                if (const std::string warn = globalWarning(b); !warn.empty()) {
                    ImGui::SameLine();
                    ImGui::TextColored(theme::kWarning, "!");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", warn.c_str());
                }

                ImGui::TableSetColumnIndex(5);
                if (iconButton(ICON_TRASH, ("##atalho_" + n + "_remover").c_str(), "Remover atalho", ImVec2(0.0f, 0.0f),
                               IconButton_Flat))
                    op = {RowOp::Kind::Remove, scope, i};
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    g.visibleRows = shown;

    if (op.kind != RowOp::Kind::None) {
        KeyBindingSet& from = setFor(op.scope);
        if (op.index >= 0 && op.index < static_cast<int>(from.items.size())) {
            KeyBinding b = from.items[static_cast<size_t>(op.index)];
            from.items.erase(from.items.begin() + op.index);
            if (op.kind == RowOp::Kind::Move)
                setFor(op.scope == Scope::Project ? Scope::App : Scope::Project).items.push_back(std::move(b));
            touch(Scope::App);
            g.status = op.kind == RowOp::Kind::Remove ? "Atalho removido." : "Atalho movido de escopo.";
        }
    }

    ImGui::TextColored(theme::kTextDim, "Atalhos de show (clips, camadas, blackout) ficam no projeto e vencem os do aplicativo.");
    ImGui::TextColored(theme::kTextDim, "Não disparam enquanto você digita num campo nem com o foco no editor de Mapeamento.");
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::kTextDim, "%s", g.status.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - dp(110.0f) - ImGui::GetStyle().WindowPadding.x);
    if (ImGui::Button("Fechar##atalhos_fechar", ImVec2(dp(110.0f), 0.0f)))
        g.editorOpen = false;
    ImGui::End();
}

}

control::SelectionRef selectionRef(const Selection& selection)
{
    control::SelectionRef r;
    r.kind = selection.kind == Selection::Kind::Layer  ? control::SelectionRef::Kind::Layer
             : selection.kind == Selection::Kind::Clip ? control::SelectionRef::Kind::Clip
                                                       : control::SelectionRef::Kind::None;
    r.layer = selection.layer;
    r.column = selection.column;
    return r;
}

void keysFrame(PanelContext& ctx)
{
    g.settings = &ctx.settings;
    g.comp = &ctx.composition;
    g.svc = &ctx.keys;
    g.selection = ctx.selection;
    g.mode = ctx.mode;

    control::KeyService& svc = ctx.keys;
    const control::SelectionRef sel = selectionRef(ctx.selection);
    control::ActionContext ac = svc.context(ctx.composition, sel);
    const ImGuiIO& io = ImGui::GetIO();

    if (svc.dispatcher.heldCount() > 0) {
        for (const control::Fired& f : svc.dispatcher.releaseUp(
                 [](const KeyBinding& b) { return b.global || ImGui::IsKeyDown(imguiKeyOf(b.chord.key)); }, ac))
            svc.note(f);
        for (const control::Fired& f : svc.dispatcher.tick(ac))
            svc.note(f);
    }

    control::KeyGate gate;
    gate.textInput = io.WantTextInput;
    gate.mapEditor = mapEditorFocused(ctx.mode);
    gate.popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    gate.busy = ctx.project.busy();
    gate.capturing = g.cap.active;
    gate.navKeys = io.NavVisible;

    if (!io.KeySuper) {
        for (int i = 1; i < static_cast<int>(Key::Count); ++i) {
            const Key k = static_cast<Key>(i);
            if (!ImGui::IsKeyPressed(imguiKeyOf(k), false))
                continue;
            if (const auto fired = svc.dispatcher.press(Chord{k, currentMods()}, ctx.settings.keys, ctx.composition.keys,
                                                         gate, ac))
                svc.note(*fired);
        }
    }

    rebuildHints(sel);
}

void drawKeysWindows(PanelContext& ctx)
{
    drawEditor(ctx);
    drawCaptureWindow();
    drawConflictModal();
}

void openKeysEditor()
{
    g.editorOpen = true;
    g.probeCache.clear();
}

void runKeysDialogs(void* owner)
{
    if (!g.settings || !g.comp) {
        g.exportRequest = g.importRequest = false;
        return;
    }
    if (g.exportRequest) {
        g.exportRequest = false;
        if (const auto file = dialogs::saveFile(owner, L"Exportar atalhos", L"Atalhos do BSL Stage (*.bslkeys)", L"*.bslkeys",
                                                L"atalhos", L"bslkeys")) {
            std::string error;
            g.status = control::saveKeysFile(*file, g.settings->keys, g.comp->keys, &error)
                           ? "Exportado: " + paths::toUtf8(file->filename())
                           : "Não foi possível exportar: " + error;
        }
    }
    if (g.importRequest) {
        g.importRequest = false;
        if (const auto file = dialogs::openFile(owner, L"Atalhos do BSL Stage", L"*.bslkeys;*.json")) {
            KeyBindingSet app, project;
            std::string error;
            std::vector<std::string> warnings;
            if (control::loadKeysFile(*file, app, project, &error, &warnings)) {
                g.settings->keys = std::move(app);
                g.comp->keys = std::move(project);
                g.settings->save();
                g.status = "Importado: " + paths::toUtf8(file->filename()) + " (" + std::to_string(g.settings->keys.items.size())
                           + " do app, " + std::to_string(g.comp->keys.items.size()) + " do projeto)";
            } else {
                g.status = "Não foi possível importar: " + error;
            }
        }
    }
}

std::string keyHint(const control::Target& target)
{
    const auto it = g.hints.find(target.key());
    return it == g.hints.end() ? std::string() : it->second.text;
}

float keyFlash(const control::Target& target)
{
    return g.svc ? g.svc->flash(target) : 0.0f;
}

float keyHintWidth(const std::string& text)
{
    ImFont* mono = fonts().mono;
    const float size = fontPx(mono) * 0.82f;
    return mono->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x + dp(7.0f);
}

void drawKeyHint(ImDrawList* dl, ImVec2 topLeft, const std::string& text)
{
    ImFont* mono = fonts().mono;
    const float size = fontPx(mono) * 0.82f;
    const ImVec2 ts = mono->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
    const float pad = dp(3.5f);
    dl->AddRectFilled(topLeft, ImVec2(topLeft.x + ts.x + pad * 2.0f, topLeft.y + ts.y + dp(2.0f)), IM_COL32(0, 0, 0, 150),
                      dp(3.0f));
    dl->AddText(mono, size, ImVec2(topLeft.x + pad, topLeft.y + dp(1.0f)), IM_COL32(232, 232, 232, 210), text.c_str());
}

void drawKeyFlash(ImDrawList* dl, ImVec2 a, ImVec2 b, float amount, float rounding)
{
    if (amount <= 0.01f)
        return;
    const int fill = static_cast<int>(95.0f * amount);
    const int line = static_cast<int>(255.0f * amount);
    dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, fill), rounding);
    dl->AddRect(a, b, IM_COL32(236, 51, 55, line), rounding, 0, std::max(1.0f, dp(2.0f)));
}

void keyAssignMenu(const control::Target& target, bool leadingSeparator)
{
    std::string id;
    ActionArgs args;
    if (!g.settings || !g.comp || !control::actionForTarget(target, id, args))
        return;
    if (const control::ActionDef* targetDef = control::findAction(id))
        control::stampLayerId(*targetDef, args, *g.comp);
    const Scope scope = control::defaultScopeFor(id);
    const int own = setFor(scope).find(id, args);
    const bool hasKey = own >= 0 && setFor(scope).items[static_cast<size_t>(own)].chord.valid();

    if (leadingSeparator)
        ImGui::Separator();
    std::string label;
    if (hasKey) {
        label = "Trocar tecla (" + control::chordLabel(setFor(scope).items[static_cast<size_t>(own)].chord) + ")…";
    } else {
        switch (target.kind) {
        case Target::Kind::Layer: label = "Atribuir tecla para ligar/desligar camada…"; break;
        case Target::Kind::Column: label = "Atribuir tecla para disparar a coluna…"; break;
        case Target::Kind::Effect: label = "Atribuir tecla para ligar/desligar o efeito…"; break;
        default: label = "Atribuir tecla…"; break;
        }
    }
    if (ImGui::MenuItem((label + "##key_atribuir").c_str())) {
        Capture cap;
        cap.scope = scope;
        cap.action = id;
        cap.args = args;
        const control::ActionDef* def = control::findAction(id);
        cap.title = def ? control::describeAction(*g.comp, *def, args) : id;
        beginCapture(cap);
    }
    if (hasKey && ImGui::MenuItem("Remover tecla##key_remover")) {
        setFor(scope).items.erase(setFor(scope).items.begin() + own);
        touch(scope);
    }
}

void keyParamMenu(const control::ActionArgs& argsIn, bool offerHold)
{
    if (!g.settings || !g.comp)
        return;
    const std::string id = "fx.event";
    const control::ActionDef* def = control::findAction(id);
    if (!def)
        return;
    ActionArgs args = argsIn;
    control::stampLayerId(*def, args, *g.comp);
    const Scope scope = control::defaultScopeFor(id);
    const int own = setFor(scope).find(id, args);
    const bool hasKey = own >= 0 && setFor(scope).items[static_cast<size_t>(own)].chord.valid();

    auto start = [&](control::Mode mode) {
        Capture cap;
        cap.scope = scope;
        cap.action = id;
        cap.args = args;
        cap.mode = mode;
        cap.title = control::describeAction(*g.comp, *def, args);
        beginCapture(cap);
    };

    if (hasKey) {
        const std::string label = "Trocar tecla (" + control::chordLabel(setFor(scope).items[static_cast<size_t>(own)].chord) + ")…";
        if (ImGui::MenuItem((label + "##key_param_trocar").c_str()))
            start(setFor(scope).items[static_cast<size_t>(own)].mode);
        if (ImGui::MenuItem("Remover tecla##key_param_remover")) {
            setFor(scope).items.erase(setFor(scope).items.begin() + own);
            touch(scope);
        }
        return;
    }
    if (!offerHold) {
        if (ImGui::MenuItem("Atribuir atalho para disparar…##key_param_disparar"))
            start(control::Mode::Press);
        return;
    }
    if (ImGui::MenuItem("Atribuir atalho (alterna a cada toque)…##key_param_alternar"))
        start(control::Mode::Press);
    if (ImGui::MenuItem("Atribuir atalho (ativo enquanto segurar)…##key_param_segurar"))
        start(control::Mode::Hold);
}

std::string actionShortcut(const std::string& actionId, const control::ActionArgs& args)
{
    if (!g.settings || !g.comp)
        return {};
    for (const KeyBindingSet* set : {&g.settings->keys, &g.comp->keys}) {
        const int i = set->find(actionId, args);
        if (i >= 0 && set->items[static_cast<size_t>(i)].chord.valid())
            return control::chordLabel(set->items[static_cast<size_t>(i)].chord);
    }
    return {};
}

KeysUiInfo keysUiInfo()
{
    KeysUiInfo info;
    info.editorOpen = g.editorOpen;
    info.capturing = g.cap.active;
    info.conflictOpen = g.pending.open;
    info.rows = g.visibleRows;
    return info;
}

}
