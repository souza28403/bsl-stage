#include "ui/panels/NetStreamWindow.h"

#include "app/Qa.h"
#include "core/Composition.h"
#include "media/NetStreamSource.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

namespace bsl::ui {

namespace {

using Clock = std::chrono::steady_clock;

struct WindowState
{
    bool open = false;
    bool wantOpen = false;
    int deck = -1;
    int layer = -1;
    int column = -1;
    bool reveal = false;
    int settleFrames = 0;
};

WindowState g_state;

constexpr size_t kUrlBuffer = 2100;

const std::vector<std::string>& localAddresses()
{
    static std::vector<std::string> ips;
    static Clock::time_point last{};
    const auto now = Clock::now();
    if (last.time_since_epoch().count() == 0 || now - last > std::chrono::seconds(5)) {
        ips = media::net::localIPv4Addresses();
        last = now;
    }
    return ips;
}

std::string stringParam(const media::ParamValues& v, const char* id, const char* def)
{
    const media::Json* j = v.find(id);
    return (j && j->is_string()) ? j->get<std::string>() : std::string(def);
}

void setParamIfChanged(Clip& clip, const char* id, media::Json value)
{
    const media::Json* cur = clip.params.find(id);
    if (!cur || *cur != value)
        clip.setParam(id, std::move(value));
}

void copyRow(const std::string& address, int index)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, dp(6.0f, 3.0f));
    const float copyW = dp(76.0f);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", address.c_str());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - copyW - ImGui::GetStyle().ItemSpacing.x);
    pushMono();
    const std::string fieldId = "##stream_endereco_" + std::to_string(index);
    ImGui::InputText(fieldId.c_str(), buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
    popMono();
    ImGui::SameLine();
    const std::string btnId = "Copiar##stream_copiar_" + std::to_string(index);
    if (ImGui::Button(btnId.c_str(), ImVec2(copyW, 0.0f)))
        ImGui::SetClipboardText(address.c_str());
    ImGui::PopStyleVar();
}

}

void requestNetStreamWindow(int deck, int layer, int column)
{
    g_state.wantOpen = true;
    g_state.deck = deck;
    g_state.layer = layer;
    g_state.column = column;
}

bool drawNetUrlEditor(const media::Json& current, bool& reveal, media::Json& out)
{
    char buf[kUrlBuffer];
    const std::string cur = current.is_string() ? current.get<std::string>() : std::string();
    std::snprintf(buf, sizeof(buf), "%s", cur.c_str());

    const float eye = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - eye - ImGui::GetStyle().ItemSpacing.x);
    int flags = ImGuiInputTextFlags_AutoSelectAll;
    if (!reveal && !cur.empty())
        flags |= ImGuiInputTextFlags_Password;
    bool changed = false;
    if (ImGui::InputTextWithHint("##stream_url", "rtmp://servidor/app/chave", buf, sizeof(buf), flags)) {
        out = std::string(buf);
        changed = true;
    }
    ImGui::SameLine();
    iconToggle(reveal ? ICON_EYE_OFF : ICON_EYE, "##stream_mostrar", reveal,
               reveal ? "Ocultar o endereço" : "Mostrar o endereço (pode conter a chave da transmissão)");
    return changed;
}

void drawNetAddressRows(const media::net::Config& cfg, int maxRows)
{
    std::vector<std::string> ips = localAddresses();
    if (ips.empty()) {
        ImGui::TextColored(theme::kWarning, "Nenhuma rede ativa encontrada. Use este endereço só neste computador:");
        ips.push_back("127.0.0.1");
    }
    const int shown = std::min(static_cast<int>(ips.size()), maxRows);
    for (int i = 0; i < shown; ++i)
        copyRow(media::net::displayAddress(cfg, ips[static_cast<size_t>(i)]), i + 1);
    if (static_cast<int>(ips.size()) > shown)
        ImGui::TextColored(theme::kTextDim, "+ %d endereço(s) de outras redes", static_cast<int>(ips.size()) - shown);
}

void drawNetStreamWindow(PanelContext& ctx)
{
    if (g_state.wantOpen) {
        g_state.wantOpen = false;
        g_state.open = true;
        g_state.reveal = false;
        g_state.settleFrames = 3;
    }
    if (!g_state.open)
        return;

    Composition& comp = ctx.composition;
    Clip* clip = nullptr;
    if (comp.validCell(g_state.deck, g_state.layer, g_state.column)) {
        Clip& c = comp.clipAt(g_state.deck, g_state.layer, g_state.column);
        if (c.isGenerator() && c.generatorId == kNetStreamId)
            clip = &c;
    }
    if (!clip) {
        g_state.open = false;
        return;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(dp(640.0f, 560.0f), ImGuiCond_Appearing);
    bool keepOpen = true;
    if (!ImGui::Begin("Stream de rede###janela_stream", &keepOpen,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        g_state.open = keepOpen;
        return;
    }

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim,
                       "Traga uma transmissão ao vivo (câmera IP, celular, programa de transmissão) para este clip. "
                       "Dispare o clip para conectar.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const media::net::Config cfg = media::NetStreamSource::configFrom(clip->params);
    const bool listen = cfg.mode == media::net::Mode::Listen;
    const bool settling = g_state.settleFrames > 0;
    if (settling)
        --g_state.settleFrames;

    if (ImGui::BeginTabBar("##stream_abas")) {
        const ImGuiTabItemFlags linkFlags = (settling && !listen) ? ImGuiTabItemFlags_SetSelected : 0;
        const ImGuiTabItemFlags recvFlags = (settling && listen) ? ImGuiTabItemFlags_SetSelected : 0;

        const bool linkTab = ImGui::BeginTabItem("Abrir link##stream_aba_link", nullptr, linkFlags);
        qa::note("##stream_aba_link");
        if (linkTab) {
            if (!settling && listen)
                setParamIfChanged(*clip, "mode", "pull");
            ImGui::Spacing();
            ImGui::TextUnformatted("Endereço da transmissão");
            media::Json out;
            if (drawNetUrlEditor(clip->params.find("url") ? *clip->params.find("url") : media::Json(std::string()),
                                 g_state.reveal, out))
                setParamIfChanged(*clip, "url", out);
            if (ImGui::Button("Colar##stream_colar")) {
                if (const char* text = ImGui::GetClipboardText())
                    setParamIfChanged(*clip, "url", std::string(text));
            }
            ImGui::SameLine();
            if (ImGui::Button("Limpar##stream_limpar"))
                setParamIfChanged(*clip, "url", std::string());
            const std::string err = media::net::validateConfig(cfg);
            if (!err.empty() && !cfg.url.empty())
                ImGui::TextColored(theme::kWarning, "%s", err.c_str());

            ImGui::Spacing();
            ImGui::SeparatorText("Exemplos (clique para usar de modelo)");
            struct Example
            {
                const char* text;
                const char* note;
            };
            const Example examples[] = {
                {"rtmp://servidor/app/chave", "RTMP: servidor de transmissão"},
                {"rtsp://usuario:senha@192.168.0.20/stream1", "RTSP: câmera IP"},
                {"srt://servidor:9000", "SRT: baixa latência pela internet"},
                {"udp://0.0.0.0:5000", "UDP: MPEG-TS enviado para cá"},
                {"https://servidor/ao-vivo/lista.m3u8", "HLS: lista .m3u8 (atraso maior)"},
            };
            int i = 0;
            for (const Example& e : examples) {
                const std::string id = std::string("##stream_exemplo_") + std::to_string(++i);
                pushMono();
                if (ImGui::Selectable((std::string(e.text) + id).c_str()))
                    setParamIfChanged(*clip, "url", std::string(e.text));
                popMono();
                ImGui::SameLine(dp(340.0f));
                ImGui::TextColored(theme::kTextDim, "%s", e.note);
            }
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(theme::kTextDim,
                               "A chave da transmissão fica escondida no campo; use o olho para mostrá-la. O projeto "
                               "salvo guarda o endereço completo.");
            ImGui::PopTextWrapPos();
            ImGui::EndTabItem();
        }

        const bool recvTab = ImGui::BeginTabItem("Receber transmissão##stream_aba_receber", nullptr, recvFlags);
        qa::note("##stream_aba_receber");
        if (recvTab) {
            if (!settling && !listen)
                setParamIfChanged(*clip, "mode", "listen");
            ImGui::Spacing();

            const bool srt = cfg.protocol == media::net::ListenProtocol::Srt;
            ImGui::AlignTextToFramePadding();
            dimLabel("Protocolo");
            ImGui::SameLine(dp(120.0f));
            ImGui::SetNextItemWidth(dp(160.0f));
            if (beginCombo("##stream_protocolo", srt ? "SRT" : "RTMP")) {
                if (ImGui::Selectable("RTMP", !srt)) {
                    setParamIfChanged(*clip, "protocol", "rtmp");
                    if (cfg.port == media::net::kDefaultSrtPort)
                        setParamIfChanged(*clip, "port", media::net::kDefaultRtmpPort);
                }
                if (ImGui::Selectable("SRT", srt)) {
                    setParamIfChanged(*clip, "protocol", "srt");
                    if (cfg.port == media::net::kDefaultRtmpPort)
                        setParamIfChanged(*clip, "port", media::net::kDefaultSrtPort);
                }
                ImGui::EndCombo();
            }

            ImGui::AlignTextToFramePadding();
            dimLabel("Porta");
            ImGui::SameLine(dp(120.0f));
            ImGui::SetNextItemWidth(dp(160.0f));
            int port = cfg.port;
            if (ImGui::InputInt("##stream_porta", &port, 0, 0)) {
                port = std::clamp(port, 1, 65535);
                setParamIfChanged(*clip, "port", port);
            }

            if (!srt) {
                ImGui::AlignTextToFramePadding();
                dimLabel("Nome");
                ImGui::SameLine(dp(120.0f));
                ImGui::SetNextItemWidth(dp(280.0f));
                std::string path = media::net::normalizePath(stringParam(clip->params, "path", "/live/stream1"));
                std::string name = path.substr(path.find_last_of('/') + 1);
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%s", name.c_str());
                if (ImGui::InputText("##stream_nome", buf, sizeof(buf)))
                    setParamIfChanged(*clip, "path", "/live/" + media::net::sanitizeName(buf));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Nome da transmissão: vira o final do endereço (/live/<nome>).");
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Endereço para colocar no programa de transmissão");
            drawNetAddressRows(cfg, 4);
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(theme::kTextDim,
                               srt ? "No programa de transmissão escolha SRT no modo chamador (caller) e use o endereço acima."
                                   : "No programa de transmissão use o servidor e a chave: o servidor é o endereço até /live "
                                     "e a chave é o nome.");
            ImGui::TextColored(theme::kTextDim,
                               "Um envio por clip. Na primeira vez o Windows pede liberação no firewall: permita redes "
                               "privadas. O BSL Stage não altera o firewall.");
            ImGui::PopTextWrapPos();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Sinal");
    {
        const std::string loss = stringParam(clip->params, "loss", "hold");
        ImGui::AlignTextToFramePadding();
        dimLabel("Ao perder o sinal");
        ImGui::SameLine(dp(160.0f));
        ImGui::SetNextItemWidth(dp(220.0f));
        if (beginCombo("##stream_perda", loss == "black" ? "Preto" : "Manter última imagem")) {
            if (ImGui::Selectable("Manter última imagem", loss != "black"))
                setParamIfChanged(*clip, "loss", "hold");
            if (ImGui::Selectable("Preto", loss == "black"))
                setParamIfChanged(*clip, "loss", "black");
            ImGui::EndCombo();
        }
    }

    ImGui::Spacing();
    {
        media::SourceStatus st;
        bool haveStatus = false;
        if (media::ISource* src = clip->liveSource()) {
            st = src->status();
            haveStatus = !st.text.empty();
        }
        ImGui::PushTextWrapPos(0.0f);
        if (!haveStatus) {
            ImGui::TextColored(theme::kTextDim, "Parado: dispare o clip para conectar.");
        } else {
            const bool live = st.text.rfind("Ao vivo", 0) == 0;
            ImGui::TextColored(st.level == media::SourceStatus::Level::Error     ? theme::kError
                               : st.level == media::SourceStatus::Level::Warning ? theme::kWarning
                               : live                                            ? theme::kSuccess
                                                                                 : theme::kTextDim,
                               "%s", st.text.c_str());
        }
        qa::note("stream:estado");
        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();
    const bool sameDeck = comp.currentDeck == g_state.deck;
    ImGui::BeginDisabled(!sameDeck);
    if (ImGui::Button("Conectar agora##stream_conectar", ImVec2(dp(140.0f), 0.0f))) {
        ctx.selection = {Selection::Kind::Clip, g_state.layer, g_state.column};
        comp.trigger(g_state.layer, g_state.column);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Fechar##stream_fechar", ImVec2(dp(110.0f), 0.0f)))
        keepOpen = false;

    ImGui::End();
    g_state.open = keepOpen;
}

}
