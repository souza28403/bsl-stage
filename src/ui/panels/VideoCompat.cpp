#include "ui/panels/VideoCompat.h"

#include "app/Qa.h"
#include "media/SpoutSource.h"
#include "ndi/NdiFinder.h"
#include "ndi/NdiRuntime.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cfloat>

namespace bsl::ui {

namespace {

bool g_open = false;

struct Row
{
    const char* api;
    const char* how;    // resumo
    const char* detail; // explicação
};

const Row kRows[] = {
    {"OpenGL", "Spout e NDI",
     "Para trazer, use as fontes Spout e NDI. Para enviar, ponha uma Screen com destino Spout ou NDI."},
    {"DirectX 9/11/12", "Spout e NDI; jogos e apps via Captura de tela/janela",
     "O Spout recebe de DirectX 9, 11 e 12. Jogos e aplicativos que não enviam nada podem ser pegos pela fonte "
     "Captura de tela (monitor ou janela)."},
    {"Vulkan", "NDI ou Captura de janela, porque Spout não tem Vulkan nativo",
     "Programas em Vulkan não compartilham textura com o Spout. Use NDI (se o programa enviar) ou a Captura de janela."},
    {"WebGL", "Fonte Web, que roda WebGL no CEF, e Captura de janela do navegador",
     "A fonte Web carrega a página num navegador embutido (CEF) com WebGL. Uma página já aberta no seu navegador entra "
     "pela Captura de janela."},
};

void statusLine(bool ok, const char* qaName, const char* text)
{
    ImGui::TextColored(ok ? theme::kSuccess : theme::kWarning, "%s", text);
    qa::note(qaName);
}

} // namespace

void openCompatWindow()
{
    g_open = true;
}

void drawNdiRuntimeNotice()
{
    ImGui::TextColored(theme::kWarning, ICON_ALERT "  %s", ndi::kMissingText);
    qa::note("ndi:sem_runtime");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim,
                       "Instale o NDI Tools/Runtime gratuito (distribuído pela NDI, este programa não baixa nem inclui "
                       "nada dele) e abra o BSL Stage de novo:");
    ImGui::PopTextWrapPos();
    pushMono();
    ImGui::TextUnformatted(ndi::kDownloadUrl);
    popMono();
    qa::note("ndi:link");
    if (ImGui::Button("Abrir a página##ndi_link_abrir")) {
        if (!qa::enabled()) // o teste de interface não abre navegador
            ShellExecuteA(nullptr, "open", ndi::kDownloadUrl, nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("Copiar o link##ndi_link_copiar"))
        ImGui::SetClipboardText(ndi::kDownloadUrl);
}

bool drawNdiSourcePicker(const std::string& current, std::string& out)
{
    ndi::Runtime& rt = ndi::Runtime::instance();
    if (!rt.available()) {
        drawNdiRuntimeNotice();
        return false;
    }
    ndi::Finder& finder = ndi::Finder::shared();
    finder.start();
    const std::vector<ndi::SourceInfo> sources = finder.sources();

    std::string preview = current;
    if (preview.empty())
        preview = sources.empty() ? "Procurando fontes na rede…" : "Escolha uma fonte…";
    bool changed = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (beginCombo("##ndi_fonte", preview.c_str())) {
        if (sources.empty())
            ImGui::TextColored(theme::kTextDim, "Nenhuma fonte NDI na rede (a lista atualiza sozinha)");
        bool curListed = current.empty();
        for (const ndi::SourceInfo& s : sources) {
            curListed = curListed || s.name == current;
            if (ImGui::Selectable(s.name.c_str(), s.name == current)) {
                out = s.name;
                changed = true;
            }
        }
        if (!curListed)
            ImGui::TextColored(theme::kWarning, "%s (fora do ar)", current.c_str());
        ImGui::EndCombo();
    }
    qa::note("ndi:lista");
    ImGui::TextColored(theme::kTextDim, "%zu fonte(s) na rede", sources.size());
    return changed;
}

bool drawSpoutSenderPicker(const std::string& current, std::string& out)
{
    const std::vector<std::string> senders = media::SpoutSource::activeSenders();
    std::string preview = current;
    if (preview.empty())
        preview = senders.empty() ? "Primeiro disponível (nenhum ativo)" : "Primeiro disponível (" + senders.front() + ")";
    bool changed = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (beginCombo("##spout_sender", preview.c_str())) {
        if (ImGui::Selectable("Primeiro disponível", current.empty())) {
            out.clear();
            changed = true;
        }
        bool curListed = current.empty();
        for (const std::string& n : senders) {
            curListed = curListed || n == current;
            if (ImGui::Selectable(n.c_str(), n == current)) {
                out = n;
                changed = true;
            }
        }
        if (!curListed)
            ImGui::TextColored(theme::kWarning, "%s (fora do ar)", current.c_str());
        ImGui::EndCombo();
    }
    qa::note("spout:lista");
    ImGui::TextColored(theme::kTextDim, "%zu sender(s) ativo(s)", senders.size());
    return changed;
}

void drawCompatWindow(PanelContext& /*ctx*/)
{
    if (!g_open)
        return;
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dp(700.0f), dp(640.0f)), ImGuiCond_Appearing);
    if (!ImGui::Begin("Compatibilidade de vídeo###janela_compat", &g_open,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim,
                       "O BSL Stage desenha em OpenGL (os plugins FFGL dependem disso) e troca imagem com programas de "
                       "qualquer API gráfica: a compatibilidade está na entrada e na saída de vídeo.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    if (ImGui::BeginTable("##compat_tabela", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("API", ImGuiTableColumnFlags_WidthFixed, dp(130.0f));
        ImGui::TableSetupColumn("Como trazer / enviar imagem", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const Row& r : kRows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushFont(fonts().bold, fonts().bold->LegacySize);
            ImGui::TextUnformatted(r.api);
            ImGui::PopFont();
            ImGui::TableSetColumnIndex(1);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(r.how);
            ImGui::TextColored(theme::kTextDim, "%s", r.detail);
            ImGui::PopTextWrapPos();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Estado agora");

    ndi::Runtime& rt = ndi::Runtime::instance();
    if (rt.available()) {
        statusLine(true, "compat:ndi_status", "NDI: runtime encontrado: sim");
        ImGui::TextColored(theme::kTextDim, "Versão:");
        ImGui::SameLine();
        pushMono();
        ImGui::TextUnformatted(rt.version().empty() ? "?" : rt.version().c_str());
        popMono();
        ImGui::TextColored(theme::kTextDim, "Arquivo:");
        ImGui::SameLine();
        pushMono();
        ImGui::TextWrapped("%s", rt.libraryPath().string().c_str());
        popMono();
        ndi::Finder::shared().start();
        const std::vector<ndi::SourceInfo> sources = ndi::Finder::shared().sources();
        ImGui::TextColored(theme::kTextDim, "Fontes NDI na rede: %zu", sources.size());
        qa::note("compat:ndi_fontes");
    } else {
        statusLine(false, "compat:ndi_status", "NDI: runtime encontrado: não");
        drawNdiRuntimeNotice();
    }

    ImGui::Spacing();
    const std::vector<std::string> senders = media::SpoutSource::activeSenders();
    statusLine(true, "compat:spout_status", "Spout: ok (SDK embutido)");
    ImGui::TextColored(theme::kTextDim, "Senders Spout ativos: %zu", senders.size());
    qa::note("compat:spout_senders");

    ImGui::Spacing();
    if (ImGui::Button("Fechar##compat_fechar", ImVec2(dp(120.0f), 0.0f)))
        g_open = false;
    ImGui::End();
}

} // namespace bsl::ui
