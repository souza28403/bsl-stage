#include "ui/panels/ReportWindow.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "app/Qa.h"
#include "app/Version.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "update/SystemProbe.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <fstream>
#include <iterator>
#include <sstream>

namespace bsl::ui {

namespace {

struct State
{
    bool open = false;
    update::ReportKind kind = update::ReportKind::Bug;
    char title[256] = {};
    char description[4096] = {};
    char steps[4096] = {};
    bool includeSystem = false;
    bool includeLog = false;
    std::string logTail;
    std::string message;
    bool messageWarn = false;
    ReportUiInfo info;
    bool focusTitle = false;
};
State g_report;

std::string readLogTail()
{
    std::ifstream in(paths::appDataDir() / L"bslstage.log", std::ios::binary);
    if (!in)
        return {};
    const std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string tail = update::tailLines(all, 40, 4000);
    return update::redactPersonalData(tail, update::userProfileDir(), update::userNameText(), update::computerNameText());
}

update::ReportInput currentInput(PanelContext& ctx)
{
    update::ReportInput in;
    in.kind = g_report.kind;
    in.title = g_report.title;
    in.description = g_report.description;
    in.steps = g_report.steps;
    in.includeSystem = g_report.includeSystem;
    in.includeLog = g_report.includeLog;
    in.logTail = g_report.logTail;
    in.system.appVersion = BSLSTAGE_VERSION;
    in.system.windows = update::windowsVersionText();
    in.system.gpu = ctx.glInfo.gpu;
    in.system.openGl = ctx.glInfo.version;
    in.system.profile = settingsFor(ctx.profile).label;
    in.system.ram = update::totalRamText();
    return in;
}

bool blank(const char* s)
{
    for (; *s; ++s)
        if (!std::isspace(static_cast<unsigned char>(*s)))
            return false;
    return true;
}

}

void openReportWindow(update::ReportKind kind)
{
    if (!g_report.open || g_report.kind != kind) {
        g_report.title[0] = g_report.description[0] = g_report.steps[0] = '\0';
        g_report.includeSystem = g_report.includeLog = false;
        g_report.logTail.clear();
        g_report.message.clear();
    }
    g_report.kind = kind;
    g_report.open = true;
    g_report.focusTitle = true;
}

ReportUiInfo reportUiInfo()
{
    ReportUiInfo i = g_report.info;
    i.open = g_report.open;
    i.kind = g_report.kind == update::ReportKind::Bug ? 0 : 1;
    i.includeSystem = g_report.includeSystem;
    i.includeLog = g_report.includeLog;
    return i;
}

void drawReportWindow(PanelContext& ctx)
{
    if (!g_report.open)
        return;
    const bool bug = g_report.kind == update::ReportKind::Bug;
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dp(640.0f), dp(700.0f)), ImGuiCond_Appearing);
    const char* windowId = bug ? "Reportar problema###janela_reportar" : "Sugerir melhoria###janela_reportar";
    if (!ImGui::Begin(windowId, &g_report.open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive()
        && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        g_report.open = false;

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim,
                       "O BSL Stage não envia nada sozinho. \"Abrir no GitHub\" abre o navegador com o texto abaixo já "
                       "preenchido; você revisa e confirma lá (precisa de uma conta no GitHub). O texto fica público no repositório.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const float labelW = dp(150.0f);
    ImGui::AlignTextToFramePadding();
    dimLabel("Título");
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (g_report.focusTitle) {
        ImGui::SetKeyboardFocusHere();
        g_report.focusTitle = false;
    }
    ImGui::InputTextWithHint("##reportar_titulo", bug ? "Resumo curto do problema" : "Resumo curto da ideia", g_report.title,
                             sizeof(g_report.title));

    dimLabel(bug ? "Descrição do problema" : "Descrição da ideia");
    ImGui::InputTextMultiline("##reportar_descricao", g_report.description, sizeof(g_report.description),
                              ImVec2(-FLT_MIN, dp(100.0f)));
    dimLabel(bug ? "Passos para reproduzir (opcional)" : "Exemplo de uso (opcional)");
    ImGui::InputTextMultiline("##reportar_passos", g_report.steps, sizeof(g_report.steps), ImVec2(-FLT_MIN, dp(80.0f)));

    ImGui::Spacing();
    ImGui::Checkbox("Incluir informações do sistema##reportar_sistema", &g_report.includeSystem);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Versão do BSL Stage, Windows, placa de vídeo e driver OpenGL, perfil de desempenho e memória (RAM).\n"
                          "Não inclui nome de usuário, nome do computador, pastas nem projetos.");
    if (ImGui::Checkbox("Incluir as últimas linhas do log##reportar_log", &g_report.includeLog) && g_report.includeLog)
        g_report.logTail = readLogTail();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Até 40 linhas do log desta sessão. O nome de usuário e a pasta pessoal são trocados por marcadores.");

    const update::ReportInput input = currentInput(ctx);
    const std::string body = update::buildReportBody(input);
    const update::IssueUrl issue = update::buildIssueUrl(BSLSTAGE_GITHUB_REPO, g_report.kind, g_report.title, body);
    g_report.info.bodyLength = body.size();
    g_report.info.urlLength = issue.url.size();
    g_report.info.truncated = issue.truncated;

    ImGui::Spacing();
    ImGui::SeparatorText("Prévia do que será enviado");
    pushMono();
    std::string preview = "Título: " + std::string(g_report.title) + "\nEtiqueta: " + update::labelFor(g_report.kind) + "\n\n" + body;
    ImGui::InputTextMultiline("##reportar_previa", preview.data(), preview.size() + 1, ImVec2(-FLT_MIN, dp(150.0f)),
                              ImGuiInputTextFlags_ReadOnly);
    popMono();
    if (issue.truncated) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kWarning,
                           "O texto é grande demais para o endereço do GitHub (limite de %zu caracteres). Os últimos %zu caracteres "
                           "vão para a área de transferência: depois de abrir o GitHub, cole (Ctrl+V) no fim do texto.",
                           update::kMaxIssueUrl, issue.remainder.size());
        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();
    const bool noTitle = blank(g_report.title);
    ImGui::BeginDisabled(noTitle);
    if (ImGui::Button(ICON_LINK "  Abrir no GitHub##reportar_abrir")) {
        if (issue.truncated) {
            ImGui::SetClipboardText(issue.remainder.c_str());
            g_report.info.lastClipboard = issue.remainder;
        }
        g_report.info.lastUrl = issue.url;
        if (qa::enabled()) {
            g_report.message = "(teste) o navegador não foi aberto.";
            g_report.messageWarn = false;
        } else if (update::openGithubUrl(issue.url)) {
            g_report.message = issue.truncated
                                   ? "Aberto no navegador. A parte final do texto está na área de transferência: cole (Ctrl+V) onde "
                                     "está o aviso. Confirme o envio no GitHub."
                                   : "Aberto no navegador. Revise e confirme o envio no GitHub.";
            g_report.messageWarn = issue.truncated;
        } else {
            g_report.message = "Não foi possível abrir o navegador. Use \"Copiar\" e cole em github.com/" BSLSTAGE_GITHUB_REPO "/issues.";
            g_report.messageWarn = true;
        }
        log::info("Reporte: abriu a página de novo issue no GitHub (" + std::to_string(issue.url.size()) + " caracteres)");
    }
    ImGui::EndDisabled();
    if (noTitle && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Escreva um título.");
    ImGui::SameLine();
    if (ImGui::Button(ICON_COPY "  Copiar##reportar_copiar")) {
        const std::string text = std::string(g_report.title) + "\n\n" + body;
        ImGui::SetClipboardText(text.c_str());
        g_report.info.lastClipboard = text;
        g_report.message = "Texto copiado.";
        g_report.messageWarn = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Fechar##reportar_fechar"))
        g_report.open = false;
    if (!g_report.message.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(g_report.messageWarn ? theme::kWarning : theme::kSuccess, "%s", g_report.message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

}
