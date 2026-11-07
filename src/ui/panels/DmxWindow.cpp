#include "ui/panels/DmxWindow.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "app/Qa.h"
#include "control/ActionRegistry.h"
#include "dmx/DmxExport.h"
#include "dmx/DmxService.h"
#include "io/FileDialogs.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace bsl::ui {

namespace {

enum class Tab { Monitor, Patch, Network };

struct State
{
    Composition* comp = nullptr;
    dmx::Service* svc = nullptr;
    Settings* settings = nullptr;

    bool open = false;
    Tab tab = Tab::Monitor;
    bool selectTab = false;
    int monitorUniverse = 1;

    std::vector<dmx::NetInterface> interfaces;
    bool interfacesLoaded = false;
    char universesBuf[256] = {};
    bool universesInit = false;
    char nameBuf[40] = {};
    bool nameInit = false;

    bool exportRequest = false;
    std::string exportStatus;
    std::filesystem::path exportFolder;
    int fxPopupUid = 0;

    std::string toast;
    double toastUntil = 0.0;

    DmxParamOwner owner;
};
State g;

bool bound()
{
    return g.comp && g.svc && g.settings;
}

double nowSeconds()
{
    return dmx::Receiver::clock();
}

void say(const std::string& text, double seconds = 4.0)
{
    g.toast = text;
    g.toastUntil = nowSeconds() + seconds;
}

void saveNet()
{
    g.settings->save();
}

ImVec4 stateColor(dmx::Service::State s)
{
    switch (s) {
    case dmx::Service::State::Receiving: return theme::kSuccess;
    case dmx::Service::State::Waiting: return theme::kWarning;
    case dmx::Service::State::Lost: return theme::kWarning;
    case dmx::Service::State::Error: return theme::kError;
    default: return theme::kTextDim;
    }
}

std::string stateText(dmx::Service::State s, const dmx::NetConfig& cfg)
{
    switch (s) {
    case dmx::Service::State::Receiving: return "Recebendo";
    case dmx::Service::State::Waiting: return "Aguardando dados";
    case dmx::Service::State::Lost:
        return cfg.loss == dmx::LossPolicy::Release ? "Sem sinal (canais soltos)" : "Sem sinal (último valor mantido)";
    case dmx::Service::State::Error: return "Erro na rede";
    default: return "Receptor desligado";
    }
}

std::string agoText(double now, double last)
{
    if (last <= 0.0)
        return "-";
    const double d = std::max(0.0, now - last);
    char buf[32];
    if (d < 10.0)
        std::snprintf(buf, sizeof(buf), "%.1f s", d);
    else
        std::snprintf(buf, sizeof(buf), "há %.0f s", d);
    return buf;
}

std::string sourceText(const dmx::SourceStat& s)
{
    std::string t = s.protocol == dmx::Protocol::Sacn ? "sACN" : "Art-Net";
    if (!s.name.empty())
        t += " " + s.name;
    t += "  " + dmx::ipToString(s.ip);
    if (s.protocol == dmx::Protocol::Sacn)
        t += "  p" + std::to_string(s.priority);
    return t;
}

dmx::Map protoFromRef(const dmx::ParamRef& ref)
{
    dmx::Map m;
    m.type = dmx::Map::Type::Param;
    m.param = ref.kind;
    m.layerId = ref.layerId;
    m.fxScope = ref.fxScope;
    m.deck = ref.deck;
    m.column = ref.column;
    m.effect = ref.effect;
    m.paramId = ref.paramId;
    return m;
}

const dmx::Map* findMapFor(const dmx::Map& proto)
{
    if (!g.comp)
        return nullptr;
    for (const dmx::Map& m : g.comp->dmx.maps)
        if (dmx::sameTarget(m, proto))
            return &m;
    return nullptr;
}

void startLearn(const dmx::Map& proto)
{
    dmx::LearnTarget t;
    t.prototype = proto;
    t.title = dmx::describeMap(*g.comp, proto);
    g.svc->engine().learnBegin(std::move(t), g.svc->snapshot());
}

void assignItems(const dmx::Map& proto, const char* assignLabel, const char* idSuffix)
{
    const dmx::Map* existing = findMapFor(proto);
    std::string label;
    if (existing)
        label = "Trocar canal DMX (" + dmx::universeChannelLabel(existing->universe, existing->channel) + ")…";
    else
        label = assignLabel;
    if (ImGui::MenuItem((label + "##dmx_atribuir" + idSuffix).c_str()))
        startLearn(proto);
    if (existing && ImGui::MenuItem((std::string("Remover canal DMX##dmx_remover") + idSuffix).c_str())) {
        const dmx::Map copy = *existing;
        std::erase_if(g.comp->dmx.maps, [&](const dmx::Map& m) { return dmx::sameTarget(m, copy); });
    }
}

void drawStatusLine(const dmx::NetConfig& cfg)
{
    const dmx::Service::State st = g.svc->state();
    const dmx::Snapshot& snap = g.svc->snapshot();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(p.x + dp(7.0f), p.y + h * 0.5f), dp(5.0f), ImGui::GetColorU32(stateColor(st)));
    ImGui::Dummy(ImVec2(dp(18.0f), h));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(stateColor(st), "%s", stateText(st, cfg).c_str());
    if (st != dmx::Service::State::Off) {
        ImGui::SameLine(0.0f, dp(18.0f));
        pushMono();
        ImGui::TextColored(theme::kTextDim, "%llu pacotes", static_cast<unsigned long long>(snap.totalPackets));
        popMono();
        if (snap.artPolls > 0) {
            ImGui::SameLine(0.0f, dp(18.0f));
            ImGui::TextColored(theme::kTextDim, "ArtPoll: %llu (respondidos: %llu)", static_cast<unsigned long long>(snap.artPolls),
                               static_cast<unsigned long long>(snap.artReplies));
        }
    }
    ImGui::PushTextWrapPos(0.0f);
    if (!g.svc->error().empty())
        ImGui::TextColored(theme::kError, "%s", g.svc->error().c_str());
    else if (!snap.status.empty())
        ImGui::TextColored(theme::kTextDim, "%s", snap.status.c_str());
    else if (!cfg.enabled)
        ImGui::TextColored(theme::kTextDim, "Ligue o receptor na aba Rede para a mesa (ou o software de luz) controlar o BSL Stage.");
    ImGui::PopTextWrapPos();
    if (snap.multicastFailed > 0)
        ImGui::TextColored(theme::kWarning, "Multicast sACN: %d universo(s) sem inscrição (limite do Windows ou placa sem multicast). "
                                            "O unicast e o Art-Net seguem valendo.", snap.multicastFailed);
}

void drawUniverseTable(const dmx::NetConfig& cfg)
{
    const dmx::Snapshot& snap = g.svc->snapshot();
    ImGui::SeparatorText("Universos");
    const float rowH = ImGui::GetFrameHeight();
    const float tableH = std::min(rowH * (static_cast<float>(std::max<size_t>(snap.universes.size(), 1)) + 1.4f), dp(170.0f));
    if (!ImGui::BeginTable("##dmx_universos", 5,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_SizingFixedFit,
                           ImVec2(0.0f, tableH)))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Universo", ImGuiTableColumnFlags_WidthFixed, dp(70.0f));
    ImGui::TableSetupColumn("Estado", ImGuiTableColumnFlags_WidthFixed, dp(112.0f));
    ImGui::TableSetupColumn("Pacotes/s", ImGuiTableColumnFlags_WidthFixed, dp(78.0f));
    ImGui::TableSetupColumn("Origem", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Último quadro", ImGuiTableColumnFlags_WidthFixed, dp(100.0f));
    ImGui::TableHeadersRow();
    if (snap.universes.empty()) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(theme::kTextDim, "-");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextColored(theme::kTextDim, cfg.enabled ? "Nenhum universo escutado (aba Rede)" : "Receptor desligado");
    }
    for (const dmx::UniverseView& u : snap.universes) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        char label[32];
        std::snprintf(label, sizeof(label), "%d##dmx_univ_%d", u.universe, u.universe);
        if (ImGui::Selectable(label, g.monitorUniverse == u.universe, ImGuiSelectableFlags_SpanAllColumns))
            g.monitorUniverse = u.universe;
        ImGui::TableSetColumnIndex(1);
        if (u.active)
            ImGui::TextColored(theme::kSuccess, "Recebendo");
        else if (u.lost)
            ImGui::TextColored(theme::kWarning, u.released ? "Sem sinal (solto)" : "Sem sinal (mantido)");
        else
            ImGui::TextColored(theme::kTextDim, "Aguardando");
        ImGui::TableSetColumnIndex(2);
        pushMono();
        ImGui::Text("%.1f", static_cast<double>(u.packetsPerSec));
        popMono();
        ImGui::TableSetColumnIndex(3);
        const dmx::SourceStat* first = nullptr;
        int live = 0;
        for (const dmx::SourceStat& s : u.sources) {
            if (s.live) {
                ++live;
                if (!first || s.inMerge)
                    first = &s;
            }
        }
        if (!first && !u.sources.empty())
            first = &u.sources.back();
        if (first) {
            std::string text = sourceText(*first);
            if (live > 1)
                text += "  (+" + std::to_string(live - 1) + " fonte(s), " + (cfg.merge == dmx::MergeMode::Htp ? "HTP" : "LTP") + ")";
            ImGui::TextUnformatted(text.c_str());
            if (u.sources.size() > 1 && ImGui::IsItemHovered()) {
                std::string tip;
                for (const dmx::SourceStat& s : u.sources)
                    tip += sourceText(s) + (s.live ? (s.inMerge ? "" : "  (prioridade menor: fora da mistura)") : "  (sem sinal)") + "\n";
                ImGui::SetTooltip("%s", tip.c_str());
            }
        } else {
            ImGui::TextColored(theme::kTextDim, "-");
        }
        ImGui::TableSetColumnIndex(4);
        pushMono();
        ImGui::TextUnformatted(agoText(snap.now, u.lastFrame).c_str());
        popMono();
    }
    ImGui::EndTable();
}

void drawChannelMonitor()
{
    const dmx::Snapshot& snap = g.svc->snapshot();
    ImGui::SeparatorText("Monitor de canais");
    std::vector<int> options;
    for (const dmx::UniverseView& u : snap.universes)
        options.push_back(u.universe);
    if (options.empty())
        options.push_back(g.monitorUniverse);
    if (std::find(options.begin(), options.end(), g.monitorUniverse) == options.end())
        g.monitorUniverse = options.front();
    dimLabel("Universo");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dp(110.0f));
    if (beginCombo("##dmx_monitor_universo", std::to_string(g.monitorUniverse).c_str())) {
        for (int u : options)
            if (ImGui::Selectable((std::to_string(u) + "##dmx_mon_" + std::to_string(u)).c_str(), u == g.monitorUniverse))
                g.monitorUniverse = u;
        ImGui::EndCombo();
    }

    const dmx::UniverseView* view = snap.find(g.monitorUniverse);
    const std::vector<std::string> owners = dmx::channelOwnersFor(*g.comp, g.monitorUniverse);

    constexpr int kCols = 32, kRows = 16;
    const float gap = dp(1.5f);
    const float labelW = dp(34.0f);
    const float cellW = std::clamp((ImGui::GetContentRegionAvail().x - labelW) / static_cast<float>(kCols) - gap, dp(12.0f), dp(22.0f));
    const float cellH = cellW * 0.86f;
    const float gridW = kCols * (cellW + gap), gridH = kRows * (cellH + gap);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* mono = fonts().mono;
    const float monoSize = fontPx(mono) * 0.82f;

    ImGui::InvisibleButton("##dmx_grade", ImVec2(labelW + gridW, gridH));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int hoverIndex = -1;
    for (int row = 0; row < kRows; ++row) {
        const float y = origin.y + static_cast<float>(row) * (cellH + gap);
        char lab[8];
        std::snprintf(lab, sizeof(lab), "%d", row * kCols + 1);
        dl->AddText(mono, monoSize, ImVec2(origin.x, y + (cellH - monoSize) * 0.5f), ImGui::GetColorU32(theme::kTextDim), lab);
        for (int col = 0; col < kCols; ++col) {
            const int idx = row * kCols + col;
            const float x = origin.x + labelW + static_cast<float>(col) * (cellW + gap);
            const ImVec2 a(x, y), b(x + cellW, y + cellH);
            const int v = view ? view->data[static_cast<size_t>(idx)] : 0;
            dl->AddRectFilled(a, b, ImGui::GetColorU32(theme::kField), dp(1.5f));
            if (v > 0) {
                const float fill = std::max(1.0f, (cellH * static_cast<float>(v)) / 255.0f);
                dl->AddRectFilled(ImVec2(a.x, b.y - fill), b, ImGui::GetColorU32(view && view->active ? theme::kAccent : theme::kAccentDim),
                                  dp(1.5f));
            }
            if (!owners[static_cast<size_t>(idx)].empty())
                dl->AddRect(a, b, ImGui::GetColorU32(theme::kInfo), dp(1.5f), 0, std::max(1.0f, dp(1.0f)));
            if (hovered && mouse.x >= a.x && mouse.x < b.x + gap && mouse.y >= a.y && mouse.y < b.y + gap)
                hoverIndex = idx;
        }
    }
    if (hoverIndex >= 0) {
        const int v = view ? view->data[static_cast<size_t>(hoverIndex)] : 0;
        std::string tip = dmx::universeChannelLabel(g.monitorUniverse, hoverIndex + 1) + " = " + std::to_string(v) + "  (" +
                          std::to_string(static_cast<int>(std::lround(v * 100.0 / 255.0))) + "%)";
        if (!owners[static_cast<size_t>(hoverIndex)].empty())
            tip += "\n" + owners[static_cast<size_t>(hoverIndex)];
        ImGui::SetTooltip("%s", tip.c_str());
    }
    ImGui::TextColored(theme::kTextDim, "Contorno azul = canal usado pelo patch ou por um canal aprendido. Passe o mouse para ver o valor.");
}

void nextFree(const dmx::Config& cfg, int count, int& universe, int& channel)
{
    universe = 1;
    channel = 1;
    for (const dmx::Block& b : cfg.blocks) {
        if (b.universe > universe || (b.universe == universe && b.lastChannel() + 1 > channel)) {
            universe = b.universe;
            channel = b.lastChannel() + 1;
        }
    }
    if (channel + count - 1 > dmx::kChannels) {
        ++universe;
        channel = 1;
    }
}

dmx::Block& addLayerBlock(int layerId)
{
    dmx::Block b;
    b.kind = dmx::BlockKind::Layer;
    b.layerId = layerId;
    b.mode = dmx::LayerMode::Standard;
    nextFree(g.comp->dmx, b.channelCount(), b.universe, b.channel);
    return g.comp->dmx.addBlock(std::move(b));
}

bool hasBlockForLayer(int layerId)
{
    for (const dmx::Block& b : g.comp->dmx.blocks)
        if (b.kind == dmx::BlockKind::Layer && b.layerId == layerId)
            return true;
    return false;
}

std::string overlapWith(const dmx::Config& cfg, size_t index)
{
    const dmx::Block& a = cfg.blocks[index];
    if (a.lastChannel() > dmx::kChannels)
        return "Passa do canal 512 deste universo.";
    for (size_t i = 0; i < cfg.blocks.size(); ++i) {
        if (i == index)
            continue;
        const dmx::Block& b = cfg.blocks[i];
        if (!b.enabled || b.universe != a.universe)
            continue;
        if (a.channel <= b.lastChannel() && b.channel <= a.lastChannel())
            return "Sobrepõe " + dmx::describeBlock(*g.comp, b) + " (canais " + std::to_string(b.channel) + "-" +
                   std::to_string(b.lastChannel()) + ").";
    }
    return {};
}

void drawFxPopup()
{
    if (!ImGui::BeginPopup("##dmx_fx"))
        return;
    dmx::Block* block = nullptr;
    for (dmx::Block& b : g.comp->dmx.blocks)
        if (b.uid == g.fxPopupUid)
            block = &b;
    const int layerIndex = block ? g.comp->findLayerById(block->layerId) : -1;
    if (!block || layerIndex < 0) {
        ImGui::TextColored(theme::kTextDim, "Camada não encontrada.");
        ImGui::EndPopup();
        return;
    }
    fx::EffectChain& chain = g.comp->layers[static_cast<size_t>(layerIndex)].effects;
    ImGui::TextUnformatted("Canais de efeito da camada");
    ImGui::TextColored(theme::kTextDim, "Cada canal FX comanda um parâmetro de um efeito da camada (0-255 = mínimo-máximo).");
    ImGui::Spacing();
    if (chain.empty())
        ImGui::TextColored(theme::kWarning, "Esta camada não tem efeitos. Adicione um efeito na aba Camada.");
    int removeAt = -1;
    for (size_t i = 0; i < block->fx.size(); ++i) {
        dmx::FxSlot& slot = block->fx[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("FX %zu", i + 1);
        ImGui::SameLine(dp(52.0f));
        ImGui::SetNextItemWidth(dp(190.0f));
        std::string effectLabel = chain.valid(slot.effect)
                                      ? std::to_string(slot.effect + 1) + ": " + chain.items[static_cast<size_t>(slot.effect)].displayName()
                                      : "(efeito " + std::to_string(slot.effect + 1) + " não existe)";
        if (beginCombo("##dmx_fx_efeito", effectLabel.c_str())) {
            for (int e = 0; e < chain.size(); ++e)
                if (ImGui::Selectable((std::to_string(e + 1) + ": " + chain.items[static_cast<size_t>(e)].displayName() + "##e" + std::to_string(e)).c_str(),
                                      e == slot.effect)) {
                    slot.effect = e;
                    slot.param.clear();
                }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(dp(190.0f));
        std::string paramLabel = slot.param.empty() ? "(escolher)" : slot.param;
        std::vector<media::ParamDesc> descs;
        if (chain.valid(slot.effect))
            descs = chain.items[static_cast<size_t>(slot.effect)].paramDescs();
        for (const media::ParamDesc& d : descs)
            if (d.id == slot.param)
                paramLabel = d.label;
        if (beginCombo("##dmx_fx_param", paramLabel.c_str())) {
            for (const media::ParamDesc& d : descs) {
                if (d.type != media::ParamType::Float && d.type != media::ParamType::Int && d.type != media::ParamType::Bool &&
                    d.type != media::ParamType::Enum)
                    continue;
                if (ImGui::Selectable((d.label + "##p_" + d.id).c_str(), d.id == slot.param))
                    slot.param = d.id;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (iconButton(ICON_TRASH, "##dmx_fx_remover", "Remover este canal de efeito"))
            removeAt = static_cast<int>(i);
        ImGui::PopID();
    }
    if (removeAt >= 0)
        block->fx.erase(block->fx.begin() + removeAt);
    ImGui::BeginDisabled(static_cast<int>(block->fx.size()) >= dmx::kMaxFxSlots);
    if (ImGui::Button("+ Canal de efeito##dmx_fx_novo")) {
        dmx::FxSlot s;
        s.effect = 0;
        block->fx.push_back(s);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(theme::kTextDim, "%d canais no total", block->channelCount());
    ImGui::EndPopup();
}

void drawBlocksTable()
{
    dmx::Config& cfg = g.comp->dmx;
    ImGui::SeparatorText("Patch (camadas e Master)");
    if (cfg.blocks.empty())
        ImGui::TextColored(theme::kTextDim, "Nada no patch ainda. Use \"Patch automático\" ou \"+ Camada\", depois crie os fixtures na mesa com os perfis exportados.");

    int removeAt = -1;
    bool openFx = false;
    if (!cfg.blocks.empty() &&
        ImGui::BeginTable("##dmx_patch", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("##on", ImGuiTableColumnFlags_WidthFixed, dp(26.0f));
        ImGui::TableSetupColumn("Camada", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Personalidade", ImGuiTableColumnFlags_WidthStretch, 1.7f);
        ImGui::TableSetupColumn("Universo", ImGuiTableColumnFlags_WidthFixed, dp(70.0f));
        ImGui::TableSetupColumn("Endereço", ImGuiTableColumnFlags_WidthFixed, dp(70.0f));
        ImGui::TableSetupColumn("Canais", ImGuiTableColumnFlags_WidthFixed, dp(78.0f));
        ImGui::TableSetupColumn("FX", ImGuiTableColumnFlags_WidthFixed, dp(54.0f));
        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, dp(30.0f));
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < cfg.blocks.size(); ++i) {
            dmx::Block& b = cfg.blocks[i];
            ImGui::PushID(b.uid);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("##dmx_on", &b.enabled);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (b.kind == dmx::BlockKind::Master) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Master");
            } else {
                const bool missing = g.comp->findLayerById(b.layerId) < 0;
                if (missing)
                    ImGui::PushStyleColor(ImGuiCol_Text, theme::kWarning);
                const bool open = beginCombo("##dmx_camada", dmx::layerLabel(*g.comp, b.layerId).c_str());
                if (missing)
                    ImGui::PopStyleColor();
                if (open) {
                    for (const Layer& l : g.comp->layers)
                        if (ImGui::Selectable((dmx::layerLabel(*g.comp, l.id) + "##l" + std::to_string(l.id)).c_str(), l.id == b.layerId))
                            b.layerId = l.id;
                    ImGui::EndCombo();
                }
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (b.kind == dmx::BlockKind::Master) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(theme::kTextDim, "Opacidade, blackout, coluna");
            } else if (beginCombo("##dmx_modo", dmx::layerModeLabel(b.mode))) {
                for (int m = 0; m < dmx::kLayerModeCount; ++m) {
                    const auto mode = static_cast<dmx::LayerMode>(m);
                    if (ImGui::Selectable(dmx::layerModeLabel(mode), mode == b.mode))
                        b.mode = mode;
                }
                ImGui::EndCombo();
            }

            ImGui::TableSetColumnIndex(3);
            hotInt("##dmx_universo", b.universe, 1, dmx::kMaxUniverse, 0.1f, "%d", nullptr, INT_MIN, -1.0f);
            ImGui::TableSetColumnIndex(4);
            hotInt("##dmx_endereco", b.channel, 1, dmx::kChannels, 0.1f, "%d", nullptr, INT_MIN, -1.0f);

            ImGui::TableSetColumnIndex(5);
            const std::string warn = overlapWith(cfg, i);
            pushMono();
            ImGui::TextColored(warn.empty() ? theme::kText : theme::kWarning, "%d-%d", b.channel, b.lastChannel());
            popMono();
            if (!warn.empty() && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", warn.c_str());

            ImGui::TableSetColumnIndex(6);
            if (b.kind == dmx::BlockKind::Layer) {
                const std::string fxLabel = (b.fx.empty() ? std::string("+ FX") : std::to_string(b.fx.size()) + " FX") + "##dmx_fx_btn";
                if (ImGui::SmallButton(fxLabel.c_str())) {
                    g.fxPopupUid = b.uid;
                    openFx = true;
                }
            }
            ImGui::TableSetColumnIndex(7);
            if (iconButton(ICON_TRASH, "##dmx_remover", "Remover do patch"))
                removeAt = static_cast<int>(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeAt >= 0)
        cfg.blocks.erase(cfg.blocks.begin() + removeAt);
    if (openFx)
        ImGui::OpenPopup("##dmx_fx");
    drawFxPopup();

    if (ImGui::Button("+ Camada##dmx_add_camada")) {
        int layerId = 0;
        for (const Layer& l : g.comp->layers)
            if (!hasBlockForLayer(l.id)) {
                layerId = l.id;
                break;
            }
        if (layerId == 0 && !g.comp->layers.empty())
            layerId = g.comp->layers.front().id;
        if (layerId > 0)
            addLayerBlock(layerId);
    }
    ImGui::SameLine();
    const bool hasMaster = std::any_of(cfg.blocks.begin(), cfg.blocks.end(), [](const dmx::Block& b) { return b.kind == dmx::BlockKind::Master; });
    ImGui::BeginDisabled(hasMaster);
    if (ImGui::Button("+ Master##dmx_add_master")) {
        dmx::Block b;
        b.kind = dmx::BlockKind::Master;
        nextFree(cfg, dmx::masterChannelCount(), b.universe, b.channel);
        cfg.addBlock(std::move(b));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Patch automático##dmx_auto")) {
        for (const Layer& l : g.comp->layers)
            if (!hasBlockForLayer(l.id))
                addLayerBlock(l.id);
        if (!hasMaster) {
            dmx::Block b;
            b.kind = dmx::BlockKind::Master;
            nextFree(cfg, dmx::masterChannelCount(), b.universe, b.channel);
            cfg.addBlock(std::move(b));
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Cria um bloco para cada camada que ainda não está no patch (e o Master), em sequência a partir do universo 1");
    ImGui::SameLine();
    if (ImGui::Button((std::string(ICON_DOWNLOAD) + "  Exportar perfis…##dmx_exportar").c_str()))
        g.exportRequest = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Gera os perfis de fixture (arquivos GDTF e tabelas de canais) numa pasta");
    if (!g.exportStatus.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(theme::kTextDim, "%s", g.exportStatus.c_str());
        if (!g.exportFolder.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", paths::toUtf8(g.exportFolder).c_str());
    }
}

void drawMapsTable()
{
    dmx::Config& cfg = g.comp->dmx;
    ImGui::SeparatorText("Canais aprendidos (DMX learn)");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim, "Botão direito num parâmetro (opacidade, blend, transform, efeito) ou numa célula, coluna ou "
                                        "camada, \"Atribuir DMX…\", e mexa o fader na mesa.");
    ImGui::PopTextWrapPos();
    if (cfg.maps.empty())
        return;
    int removeAt = -1;
    if (ImGui::BeginTable("##dmx_aprendidos", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("##on", ImGuiTableColumnFlags_WidthFixed, dp(26.0f));
        ImGui::TableSetupColumn("Controle", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Universo", ImGuiTableColumnFlags_WidthFixed, dp(70.0f));
        ImGui::TableSetupColumn("Canal", ImGuiTableColumnFlags_WidthFixed, dp(70.0f));
        ImGui::TableSetupColumn("16 bits", ImGuiTableColumnFlags_WidthFixed, dp(54.0f));
        ImGui::TableSetupColumn("Disparo", ImGuiTableColumnFlags_WidthFixed, dp(160.0f));
        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, dp(30.0f));
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < cfg.maps.size(); ++i) {
            dmx::Map& m = cfg.maps[i];
            ImGui::PushID(m.uid);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("##dmx_on", &m.enabled);
            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(dmx::describeMap(*g.comp, m).c_str());
            ImGui::TableSetColumnIndex(2);
            hotInt("##dmx_m_universo", m.universe, 1, dmx::kMaxUniverse, 0.1f, "%d", nullptr, INT_MIN, -1.0f);
            ImGui::TableSetColumnIndex(3);
            hotInt("##dmx_m_canal", m.channel, 1, dmx::kChannels, 0.1f, "%d", nullptr, INT_MIN, -1.0f);
            ImGui::TableSetColumnIndex(4);
            ImGui::Checkbox("##dmx_m_16", &m.bit16);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("16 bits: canal grosso + o seguinte como fino");
            ImGui::TableSetColumnIndex(5);
            if (m.type == dmx::Map::Type::Action) {
                ImGui::SetNextItemWidth(dp(64.0f));
                hotInt("##dmx_m_limiar", m.threshold, 1, 255, 0.5f, "%d", nullptr, 128, 64.0f);
                if (control::supportsHold(m.binding.action)) {
                    ImGui::SameLine();
                    bool hold = m.binding.mode == control::Mode::Hold;
                    if (ImGui::Checkbox("Seguir##dmx_m_seguir", &hold))
                        m.binding.mode = hold ? control::Mode::Hold : control::Mode::Press;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Seguir: liga enquanto o canal está acima do limiar e desfaz ao descer.\nDesligado: cada subida dispara.");
                }
            } else {
                ImGui::Checkbox("Inverter##dmx_m_inverter", &m.invert);
            }
            ImGui::TableSetColumnIndex(6);
            if (iconButton(ICON_TRASH, "##dmx_m_remover", "Remover este canal aprendido"))
                removeAt = static_cast<int>(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeAt >= 0)
        cfg.maps.erase(cfg.maps.begin() + removeAt);
}

void drawChannelList()
{
    const dmx::Config& cfg = g.comp->dmx;
    if (cfg.blocks.empty())
        return;
    if (!ImGui::CollapsingHeader("Tabela de canais do patch##dmx_tabela"))
        return;
    if (ImGui::BeginTable("##dmx_canais", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                                 ImGuiTableFlags_SizingFixedFit,
                          ImVec2(0.0f, dp(190.0f)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Canal", ImGuiTableColumnFlags_WidthFixed, dp(80.0f));
        ImGui::TableSetupColumn("Fixture", ImGuiTableColumnFlags_WidthFixed, dp(170.0f));
        ImGui::TableSetupColumn("Função", ImGuiTableColumnFlags_WidthFixed, dp(150.0f));
        ImGui::TableSetupColumn("Valores", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const dmx::Block& b : cfg.blocks) {
            if (!b.enabled)
                continue;
            const std::vector<dmx::ChannelRow> rows =
                b.kind == dmx::BlockKind::Master ? dmx::masterRows() : dmx::layerRows(b.mode, static_cast<int>(b.fx.size()));
            for (const dmx::ChannelRow& r : rows) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                pushMono();
                std::string ch = dmx::universeChannelLabel(b.universe, b.channel + r.offset - 1);
                if (r.span == 2)
                    ch += "+" + std::to_string(b.channel + r.offset);
                ImGui::TextUnformatted(ch.c_str());
                popMono();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(dmx::describeBlock(*g.comp, b).c_str());
                ImGui::TableSetColumnIndex(2);
                std::string name = r.name;
                if (r.span == 2)
                    name += " (16 bits)";
                ImGui::TextUnformatted(name.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextColored(theme::kTextDim, "%s", r.range.c_str());
            }
        }
        ImGui::EndTable();
    }
}

void drawPatchTab()
{
    drawBlocksTable();
    drawMapsTable();
    ImGui::Spacing();
    drawChannelList();
}

void drawNetworkTab()
{
    dmx::NetConfig cfg = g.settings->dmxNet;
    bool changed = false;
    if (!g.interfacesLoaded) {
        g.interfaces = dmx::listInterfaces(true);
        g.interfacesLoaded = true;
    }
    if (!g.universesInit) {
        std::snprintf(g.universesBuf, sizeof(g.universesBuf), "%s", cfg.universesText.c_str());
        g.universesInit = true;
    }
    if (!g.nameInit) {
        std::snprintf(g.nameBuf, sizeof(g.nameBuf), "%s", cfg.nodeName.c_str());
        g.nameInit = true;
    }

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim,
                       "O BSL Stage recebe DMX por Art-Net e sACN (E1.31) e aparece como \"%s\" na lista de nós da mesa ou do software de luz. "
                       "Desligado por padrão: ao ligar, o Windows pode perguntar sobre o Firewall.",
                       cfg.nodeName.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    if (ImGui::Checkbox("Receber DMX##dmx_ligar", &cfg.enabled))
        changed = true;

    ImGui::SeparatorText("Rede");
    auto row = [](const char* label) {
        ImGui::AlignTextToFramePadding();
        dimLabel(label);
        ImGui::SameLine(dp(190.0f));
    };
    row("Placa de rede");
    {
        std::string preview = "Todas as placas";
        for (const dmx::NetInterface& n : g.interfaces)
            if (n.ip == cfg.interfaceIp)
                preview = n.name + "  " + n.ip;
        if (!cfg.interfaceIp.empty() && preview == "Todas as placas")
            preview = cfg.interfaceIp + " (não encontrada)";
        const float btn = dp(30.0f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btn - ImGui::GetStyle().ItemSpacing.x);
        if (beginCombo("##dmx_placa", preview.c_str())) {
            if (ImGui::Selectable("Todas as placas##dmx_placa_todas", cfg.interfaceIp.empty())) {
                cfg.interfaceIp.clear();
                changed = true;
            }
            for (const dmx::NetInterface& n : g.interfaces) {
                std::string label = n.name + "  " + n.ip + (n.loopback ? "  (somente testes neste PC)" : "");
                if (ImGui::Selectable((label + "##dmx_placa_" + n.ip).c_str(), n.ip == cfg.interfaceIp)) {
                    cfg.interfaceIp = n.ip;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (iconButton(ICON_REFRESH, "##dmx_placas_atualizar", "Procurar as placas de rede de novo"))
            g.interfaces = dmx::listInterfaces(true);
    }
    row("Universos");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##dmx_universos_texto", "ex.: 1-4, 10", g.universesBuf, sizeof(g.universesBuf));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        cfg.universesText = dmx::formatUniverseList(dmx::parseUniverseList(g.universesBuf));
        std::snprintf(g.universesBuf, sizeof(g.universesBuf), "%s", cfg.universesText.c_str());
        changed = true;
    }
    row("Protocolos");
    if (ImGui::Checkbox("Art-Net (UDP 6454)##dmx_artnet", &cfg.artNet))
        changed = true;
    ImGui::SameLine(0.0f, dp(18.0f));
    if (ImGui::Checkbox("sACN / E1.31 (UDP 5568)##dmx_sacn", &cfg.sacn))
        changed = true;
    row("Nome do nó (ArtPollReply)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##dmx_nome", g.nameBuf, sizeof(g.nameBuf));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        cfg.nodeName = g.nameBuf[0] ? std::string(g.nameBuf) : std::string("BSL Stage");
        if (cfg.nodeName.size() > 17)
            cfg.nodeName.resize(17);
        std::snprintf(g.nameBuf, sizeof(g.nameBuf), "%s", cfg.nodeName.c_str());
        changed = true;
    }
    row("Universo 1 no Art-Net é o");
    ImGui::SetNextItemWidth(dp(150.0f));
    if (beginCombo("##dmx_artnet_base", cfg.artNetFirstUniverse == 0 ? "Art-Net 0" : "Art-Net 1")) {
        if (ImGui::Selectable("Art-Net 0 (padrão)##dmx_base0", cfg.artNetFirstUniverse == 0)) {
            cfg.artNetFirstUniverse = 0;
            changed = true;
        }
        if (ImGui::Selectable("Art-Net 1##dmx_base1", cfg.artNetFirstUniverse == 1)) {
            cfg.artNetFirstUniverse = 1;
            changed = true;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Os universos do BSL Stage começam em 1, como no sACN. No Art-Net o primeiro é o 0 na maioria das mesas.");

    ImGui::SeparatorText("Mistura e perda de sinal");
    row("Duas fontes no mesmo universo");
    ImGui::SetNextItemWidth(dp(220.0f));
    if (beginCombo("##dmx_mistura", cfg.merge == dmx::MergeMode::Htp ? "HTP (maior valor)" : "LTP (o último que mexeu)")) {
        if (ImGui::Selectable("HTP (maior valor)##dmx_htp", cfg.merge == dmx::MergeMode::Htp)) {
            cfg.merge = dmx::MergeMode::Htp;
            changed = true;
        }
        if (ImGui::Selectable("LTP (o último que mexeu)##dmx_ltp", cfg.merge == dmx::MergeMode::Ltp)) {
            cfg.merge = dmx::MergeMode::Ltp;
            changed = true;
        }
        ImGui::EndCombo();
    }
    row("Perda de sinal após");
    {
        float timeout = static_cast<float>(cfg.timeoutSec);
        if (hotNumber("##dmx_timeout", timeout, 0.5f, 60.0f, 0.05f, "%.1f", "s", 2.5f, 90.0f)) {
            cfg.timeoutSec = static_cast<double>(timeout);
            changed = true;
        }
    }
    row("Ao perder o sinal");
    ImGui::SetNextItemWidth(dp(260.0f));
    if (beginCombo("##dmx_perda", cfg.loss == dmx::LossPolicy::Hold ? "Manter o último valor" : "Soltar (valores neutros)")) {
        if (ImGui::Selectable("Manter o último valor##dmx_manter", cfg.loss == dmx::LossPolicy::Hold)) {
            cfg.loss = dmx::LossPolicy::Hold;
            changed = true;
        }
        if (ImGui::Selectable("Soltar (valores neutros)##dmx_soltar", cfg.loss == dmx::LossPolicy::Release)) {
            cfg.loss = dmx::LossPolicy::Release;
            changed = true;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Manter: cabo desconectado não apaga o show.\nSoltar: o patch volta ao valor neutro (opacidade 0, velocidade 1x, "
                          "posição no centro).");
    row("Prioridade do Art-Net");
    {
        int prio = cfg.artNetPriority;
        if (hotInt("##dmx_prioridade", prio, 0, 200, 0.2f, "%d", nullptr, 100, 90.0f)) {
            cfg.artNetPriority = static_cast<std::uint8_t>(prio);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("O Art-Net não tem prioridade. Com sACN e Art-Net no mesmo universo, vence a fonte de maior prioridade "
                              "(sACN de 0 a 200, padrão 100).");
    }

    if (changed) {
        g.settings->dmxNet = cfg;
        saveNet();
    }
}

}

void openDmxWindowTab(int tab)
{
    openDmxWindow();
    g.tab = static_cast<Tab>(std::clamp(tab, 0, 2));
    g.selectTab = true;
}

void openDmxWindow()
{
    g.open = true;
    g.interfacesLoaded = false;
    g.universesInit = false;
    g.nameInit = false;
}

DmxUiInfo dmxUiInfo()
{
    DmxUiInfo info;
    info.open = g.open;
    info.tab = static_cast<int>(g.tab);
    info.learning = g.svc && g.svc->engine().learning();
    info.toast = nowSeconds() < g.toastUntil ? g.toast : std::string();
    if (g.comp) {
        info.blocks = static_cast<int>(g.comp->dmx.blocks.size());
        info.maps = static_cast<int>(g.comp->dmx.maps.size());
    }
    return info;
}

void dmxFrame(PanelContext& ctx)
{
    g.comp = &ctx.composition;
    g.svc = &ctx.dmx;
    g.settings = &ctx.settings;

    dmx::Engine& engine = g.svc->engine();
    std::string message;
    if (engine.takeLearned(&message) > 0) {
        say("DMX: " + message);
        log::info("DMX learn: " + message);
    }

    if (!engine.learning())
        return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->GetCenter().x, vp->WorkPos.y + dp(70.0f)), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.96f);
    if (ImGui::Begin("DMX learn###dmx_learn", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::PushFont(fonts().bold, fonts().bold->LegacySize);
        ImGui::TextUnformatted("Mexa o fader na mesa");
        ImGui::PopFont();
        ImGui::TextUnformatted(engine.learnTarget().title.c_str());
        const dmx::Service::State st = g.svc->state();
        if (!ctx.settings.dmxNet.enabled) {
            ImGui::TextColored(theme::kWarning, "O receptor DMX está desligado.");
            if (ImGui::Button("Ligar o receptor##dmx_learn_ligar")) {
                ctx.settings.dmxNet.enabled = true;
                ctx.settings.save();
            }
            ImGui::SameLine();
        } else if (st == dmx::Service::State::Waiting || st == dmx::Service::State::Error) {
            ImGui::TextColored(theme::kWarning, "Nenhum dado chegando ainda (veja a janela DMX / Art-Net).");
        } else {
            ImGui::TextColored(theme::kTextDim, "O primeiro canal que mudar liga a este controle. Esc cancela.");
        }
        if (ImGui::Button("Cancelar##dmx_learn_cancelar"))
            engine.learnCancel();
    }
    ImGui::End();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        engine.learnCancel();
}

bool dmxIndicator(const char* id, ImVec2 size)
{
    const bool clicked = ImGui::InvisibleButton(id, size);
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dmx::Service::State st = dmx::Service::State::Off;
    std::string text = "desligado";
    std::string tip = "Receptor DMX desligado. Clique para configurar (Saída > DMX / Art-Net)";
    if (g.svc && g.settings) {
        st = g.svc->state();
        const dmx::Snapshot& snap = g.svc->snapshot();
        int active = 0;
        float pps = 0.0f;
        for (const dmx::UniverseView& u : snap.universes) {
            active += u.active ? 1 : 0;
            pps += u.packetsPerSec;
        }
        switch (st) {
        case dmx::Service::State::Receiving:
            text = std::to_string(active) + (active == 1 ? " univ." : " univ.");
            break;
        case dmx::Service::State::Waiting: text = "aguardando"; break;
        case dmx::Service::State::Lost: text = "sem sinal"; break;
        case dmx::Service::State::Error: text = "erro"; break;
        default: break;
        }
        tip = stateText(st, g.settings->dmxNet);
        if (!snap.status.empty())
            tip += "\n" + snap.status;
        if (st == dmx::Service::State::Receiving) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "\n%.0f pacotes/s", static_cast<double>(pps));
            tip += buf;
        }
        tip += "\nClique para abrir a janela DMX / Art-Net";
    }
    const float r = dp(4.0f);
    const float cy = (a.y + b.y) * 0.5f;
    dl->AddCircleFilled(ImVec2(a.x + r + dp(1.0f), cy), r, ImGui::GetColorU32(stateColor(st)));
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(a.x + r * 2.0f + dp(7.0f), cy - ImGui::GetFontSize() * 0.5f),
                ImGui::GetColorU32(st == dmx::Service::State::Off ? theme::kTextDim : theme::kText), text.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip.c_str());
    return clicked;
}

void drawDmxWindow(PanelContext& ctx)
{
    if (!g.open)
        return;
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dp(760.0f), dp(680.0f)), ImGuiCond_Appearing);
    if (!ImGui::Begin("DMX / Art-Net###janela_dmx", &g.open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    const dmx::NetConfig& cfg = ctx.settings.dmxNet;
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        g.open = false;
        ImGui::End();
        return;
    }

    drawStatusLine(cfg);
    if (nowSeconds() < g.toastUntil)
        ImGui::TextColored(theme::kSuccess, "%s", g.toast.c_str());
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##dmx_abas")) {
        auto tabItem = [&](const char* label, Tab tab) {
            const bool force = g.selectTab && g.tab == tab;
            const bool open = ImGui::BeginTabItem(label, nullptr, force ? ImGuiTabItemFlags_SetSelected : 0);
            qa::note(tab == Tab::Monitor ? "##dmx_aba_monitor" : tab == Tab::Patch ? "##dmx_aba_patch" : "##dmx_aba_rede");
            if (open)
                g.tab = tab;
            return open;
        };
        if (tabItem("Monitor###dmx_aba_monitor", Tab::Monitor)) {
            drawUniverseTable(cfg);
            drawChannelMonitor();
            ImGui::EndTabItem();
        }
        if (tabItem("Patch###dmx_aba_patch", Tab::Patch)) {
            drawPatchTab();
            ImGui::EndTabItem();
        }
        if (tabItem("Rede###dmx_aba_rede", Tab::Network)) {
            drawNetworkTab();
            ImGui::EndTabItem();
        }
        g.selectTab = false;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void runDmxDialogs(HWND owner)
{
    if (!g.exportRequest)
        return;
    g.exportRequest = false;
    if (!g.comp)
        return;
    const auto folder = dialogs::pickFolder(owner);
    if (!folder)
        return;
    const Composition* comp = g.comp;
    const dmx::ExportResult res = dmx::exportProfiles(*folder, comp->dmx, [comp](int id) { return dmx::layerLabel(*comp, id); });
    if (res.ok) {
        g.exportFolder = res.folder;
        g.exportStatus = "Exportado em " + paths::toUtf8(res.folder.filename()) + " (" + std::to_string(res.files.size()) + " arquivos)";
        paths::revealInExplorer(res.files.front());
        log::info("DMX: perfis exportados em " + paths::toUtf8(res.folder));
    } else {
        g.exportStatus = "Falha ao exportar: " + res.error;
        log::warn("DMX: " + g.exportStatus);
    }
}

int dmxLayerIdOf(int layerIndex)
{
    if (!g.comp || layerIndex < 0 || layerIndex >= g.comp->layerCount())
        return 0;
    return g.comp->layers[static_cast<size_t>(layerIndex)].id;
}

void dmxParamMenu(const dmx::ParamRef& ref)
{
    if (!bound())
        return;
    const dmx::Map proto = protoFromRef(ref);
    assignItems(proto, "Atribuir DMX…", "");
}

void dmxActionMenu(const control::Target& target, bool leadingSeparator)
{
    if (!bound())
        return;
    std::string id;
    control::ActionArgs args;
    if (!control::actionForTarget(target, id, args))
        return;
    const control::ActionDef* def = control::findAction(id);
    if (!def || !def->show)
        return;
    control::stampLayerId(*def, args, *g.comp);
    dmx::Map proto;
    proto.type = dmx::Map::Type::Action;
    proto.binding.action = id;
    proto.binding.args = args;
    proto.binding.mode = control::supportsHold(id) ? control::Mode::Hold : control::Mode::Press;
    if (leadingSeparator)
        ImGui::Separator();
    const char* label = target.kind == control::Target::Kind::Layer     ? "Atribuir DMX para ligar/desligar a camada…"
                        : target.kind == control::Target::Kind::Column  ? "Atribuir DMX para disparar a coluna…"
                        : target.kind == control::Target::Kind::Effect  ? "Atribuir DMX para ligar/desligar o efeito…"
                        : target.kind == control::Target::Kind::Cell    ? "Atribuir DMX para disparar o clip…"
                                                                        : "Atribuir DMX…";
    assignItems(proto, label, "_acao");
}

void hotDmx(const dmx::ParamRef& ref)
{
    if (!bound())
        return;
    hotContextExtra([ref] { dmxParamMenu(ref); });
}

void dmxSetParamOwner(const DmxParamOwner& owner)
{
    g.owner = owner;
}

bool dmxParamOwnerValid()
{
    return bound() && g.owner.valid;
}

dmx::ParamRef dmxEffectRef(const std::string& paramId)
{
    dmx::ParamRef ref;
    ref.kind = dmx::ParamKind::EffectParam;
    ref.fxScope = g.owner.scope;
    ref.layerId = g.owner.scope == 2 ? 0 : dmxLayerIdOf(g.owner.layerIndex);
    ref.deck = g.owner.deck;
    ref.column = g.owner.column;
    ref.effect = g.owner.effect;
    ref.paramId = paramId;
    return ref;
}

}
