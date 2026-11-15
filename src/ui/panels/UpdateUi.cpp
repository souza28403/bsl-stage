#include "ui/panels/UpdateUi.h"

#include "app/Log.h"
#include "app/Version.h"
#include "output/OutputRenderer.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "update/Markdown.h"
#include "update/Service.h"

#include <imgui.h>

#include "platform/OpenUrl.h"

#include <cstdio>

namespace bsl::ui {

namespace {

struct State
{
    bool open = false;
    bool checkOnOpen = false;
    bool showPrefs = false;
};
State g_state;
UpdateUiInfo g_info;

std::string formatBytes(int64_t bytes)
{
    char buf[48];
    if (bytes >= (1ll << 30))
        std::snprintf(buf, sizeof(buf), "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    else
        std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buf;
}

bool showInProgress(PanelContext& ctx)
{
    for (int i = 0; i < ctx.composition.layerCount(); ++i)
        if (ctx.composition.activeClip(i) != nullptr)
            return true;
    return OutputRenderer::anyLive(ctx.output);
}

}

void drawMarkdown(const std::vector<update::MdBlock>& blocks)
{
    ImGui::PushTextWrapPos(0.0f);
    for (const update::MdBlock& b : blocks) {
        switch (b.kind) {
        case update::MdKind::Heading1:
        case update::MdKind::Heading2:
        case update::MdKind::Heading3:
            ImGui::Spacing();
            ImGui::PushFont(fonts().bold, fonts().bold->LegacySize * (b.kind == update::MdKind::Heading1 ? 1.25f : 1.05f));
            ImGui::TextUnformatted(b.text.c_str());
            ImGui::PopFont();
            break;
        case update::MdKind::Bullet:
            if (b.indent > 0)
                ImGui::Indent(dp(14.0f) * static_cast<float>(b.indent));
            ImGui::Bullet();
            ImGui::TextUnformatted(b.text.c_str());
            if (b.indent > 0)
                ImGui::Unindent(dp(14.0f) * static_cast<float>(b.indent));
            break;
        case update::MdKind::Numbered:
            ImGui::TextColored(theme::kTextDim, "%s", b.marker.c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(b.text.c_str());
            break;
        case update::MdKind::Quote:
            ImGui::TextColored(theme::kTextDim, "| %s", b.text.c_str());
            break;
        case update::MdKind::Code:
            pushMono();
            ImGui::TextColored(theme::kTextDim, "%s", b.text.c_str());
            popMono();
            break;
        case update::MdKind::Rule:
            ImGui::Separator();
            break;
        default:
            ImGui::TextUnformatted(b.text.c_str());
            ImGui::Spacing();
            break;
        }
    }
    ImGui::PopTextWrapPos();
}

namespace {

bool isSkippedVersion(const Settings& s, const std::string& versionText)
{
    if (s.update.skippedVersion.empty())
        return false;
    const update::SemVer a = update::parseSemVer(s.update.skippedVersion);
    const update::SemVer b = update::parseSemVer(versionText);
    return a.valid && b.valid ? update::compare(a, b) == 0 : s.update.skippedVersion == versionText;
}

void openReleasePage(const update::Release& r)
{
    const std::string url = r.htmlUrl.rfind("https://github.com/", 0) == 0
                                ? r.htmlUrl
                                : std::string("https://github.com/") + BSLSTAGE_GITHUB_REPO + "/releases";
    bslplat::openUrl(url);
}

}

void openUpdateWindow(bool checkNow)
{
    g_state.open = true;
    g_state.checkOnOpen = checkNow;
}

bool updateNoticeVisible(PanelContext& ctx, std::string* text)
{
    const update::Brief b = ctx.updates.brief();
    if (!b.hasUpdate || b.version.empty())
        return false;
    const bool working = b.phase != update::Phase::Available;
    if (!working && (ctx.updates.reminded() || isSkippedVersion(ctx.settings, b.version)))
        return false;
    if (text) {
        switch (b.phase) {
        case update::Phase::Downloading: {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "Baixando %s: %d%%", b.version.c_str(), static_cast<int>(b.progress * 100.0));
            *text = buf;
            break;
        }
        case update::Phase::ConfirmNoChecksum: *text = "Atualização " + b.version + ": falta confirmar"; break;
        case update::Phase::Ready: *text = "Atualização " + b.version + " pronta"; break;
        default: *text = "Atualização " + b.version + " disponível"; break;
        }
    }
    return true;
}

std::string updateMenuText(PanelContext& ctx)
{
    const update::Brief b = ctx.updates.brief();
    if (!b.hasUpdate || b.version.empty() || isSkippedVersion(ctx.settings, b.version))
        return {};
    return "Atualização " + b.version + " disponível";
}

float updateNoticeWidth(PanelContext& ctx)
{
    std::string text;
    if (!updateNoticeVisible(ctx, &text))
        return 0.0f;
    return dp(14.0f) + ImGui::CalcTextSize(text.c_str()).x;
}

void drawUpdateNotice(PanelContext& ctx)
{
    std::string text;
    g_info.noticeShown = updateNoticeVisible(ctx, &text);
    g_info.noticeText = text;
    if (!g_info.noticeShown)
        return;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(dp(14.0f) + ImGui::CalcTextSize(text.c_str()).x, ImGui::GetFrameHeight());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.06f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.1f));
    const bool clicked = ImGui::Button("##atualizacao_aviso", size);
    ImGui::PopStyleColor(3);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(p.x + dp(4.0f), p.y + size.y * 0.5f), dp(3.0f), ImGui::GetColorU32(theme::kAccent));
    const bool hovered = ImGui::IsItemHovered();
    dl->AddText(ImVec2(p.x + dp(14.0f), p.y + (size.y - ImGui::GetFontSize()) * 0.5f),
                ImGui::GetColorU32(hovered ? theme::kText : theme::kTextDim), text.c_str());
    if (hovered)
        ImGui::SetTooltip("Clique para ver as novidades. Nada é instalado sem você pedir.");
    if (clicked)
        openUpdateWindow(false);
}

UpdateUiInfo updateUiInfo()
{
    UpdateUiInfo i = g_info;
    i.windowOpen = g_state.open;
    return i;
}

void drawUpdateWindow(PanelContext& ctx)
{
    if (!g_state.open)
        return;
    update::Service& sv = ctx.updates;
    if (g_state.checkOnOpen) {
        g_state.checkOnOpen = false;
        sv.startCheck(ctx.settings.update, true);
    }
    const update::Status st = sv.status();
    Settings& settings = ctx.settings;

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dp(580.0f), dp(540.0f)), ImGuiCond_Appearing);
    if (!ImGui::Begin("Atualizações###janela_atualizacao", &g_state.open,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive()
        && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        g_state.open = false;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("BSL Stage");
    ImGui::SameLine();
    pushMono();
    ImGui::TextColored(theme::kTextDim, "%s", BSLSTAGE_VERSION);
    popMono();
    ImGui::SameLine(0.0f, dp(16.0f));
    ImGui::TextColored(theme::kTextDim, "Última verificação: %s", update::formatLocalDateTime(settings.update.lastCheckUnix).c_str());
    ImGui::Spacing();

    const bool downloading = st.phase == update::Phase::Downloading;
    const bool hasRelease = st.hasUpdate;

    if (st.checking) {
        ImGui::TextColored(theme::kTextDim, "Procurando atualizações…");
        ImGui::Spacing();
    }

    if (downloading) {
        ImGui::TextWrapped("Baixando %s…", st.assetName.c_str());
        const float frac = st.verifying ? 1.0f : (st.total > 0 ? static_cast<float>(st.done) / static_cast<float>(st.total) : 0.0f);
        char overlay[96];
        if (st.verifying)
            std::snprintf(overlay, sizeof(overlay), "Verificando a integridade (SHA-256)…");
        else
            std::snprintf(overlay, sizeof(overlay), "%s de %s", formatBytes(st.done).c_str(), formatBytes(st.total).c_str());
        ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, dp(18.0f)), overlay);
        if (ImGui::Button("Cancelar##atualizacao_cancelar"))
            sv.cancel();
    } else if (st.phase == update::Phase::ConfirmNoChecksum) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kWarning,
                           "Este release não traz o arquivo SHA256SUMS.txt, então não dá para conferir se o download chegou intacto "
                           "e é mesmo o publicado pelo projeto.");
        ImGui::TextColored(theme::kTextDim, "SHA-256 calculado: %s", st.sha256.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Instalar mesmo assim##atualizacao_sem_checksum"))
            sv.acceptWithoutChecksum();
        ImGui::SameLine();
        if (ImGui::Button("Descartar##atualizacao_descartar"))
            sv.discard();
    } else if (st.phase == update::Phase::Ready) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kSuccess, "Versão %s baixada%s.", st.release.version.toString().c_str(),
                           st.checksumVerified ? " e verificada (SHA-256)" : "");
        ImGui::TextUnformatted("Ela será instalada quando você fechar o BSL Stage. Seus projetos e configurações não são tocados; "
                               "a versão atual fica guardada para voltar atrás se algo der errado.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        const bool busy = showInProgress(ctx);
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Reiniciar agora##atualizacao_reiniciar")) {
            sv.requestRestart(true);
            ctx.quitRequested = true;
            g_state.open = false;
        }
        ImGui::EndDisabled();
        if (busy && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Há clips tocando ou uma saída ligada. Deixe para instalar quando fechar o programa.");
        ImGui::SameLine();
        if (ImGui::Button("Descartar##atualizacao_descartar"))
            sv.discard();
    } else if (hasRelease) {
        const update::Release& r = st.release;
        ImGui::PushFont(fonts().bold, fonts().bold->LegacySize * 1.2f);
        ImGui::Text("BSL Stage %s", r.version.toString().c_str());
        ImGui::PopFont();
        const std::string date = update::formatPublishedDate(r.published);
        ImGui::TextColored(theme::kTextDim, "%s%s%s", date.empty() ? "" : "Publicada em ", date.c_str(),
                           r.isBeta() ? "  ·  versão beta (pré-lançamento)" : "");
        if (isSkippedVersion(settings, r.version.toString()))
            ImGui::TextColored(theme::kTextDim, "Você tinha pulado esta versão.");
        ImGui::Spacing();

        const float notesH = std::max(dp(120.0f), ImGui::GetContentRegionAvail().y - dp(190.0f));
        ImGui::BeginChild("##atualizacao_notas", ImVec2(-FLT_MIN, notesH), ImGuiChildFlags_Borders);
        const std::vector<update::MdBlock> blocks = update::parseMarkdown(r.body);
        if (blocks.empty())
            ImGui::TextColored(theme::kTextDim, "Sem notas para esta versão.");
        else
            drawMarkdown(blocks);
        ImGui::EndChild();

        ImGui::Spacing();
        if (st.assetName.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(theme::kWarning, "Esta versão não traz um arquivo para Windows. Abra a página dela no GitHub.");
            ImGui::PopTextWrapPos();
        } else {
            ImGui::TextColored(theme::kTextDim, "Arquivo: %s%s%s", st.assetName.c_str(), st.assetSize > 0 ? " (" : "",
                               st.assetSize > 0 ? (formatBytes(st.assetSize) + ")").c_str() : "");
        }
        if (!st.error.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(theme::kWarning, "%s", st.error.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(st.assetName.empty());
        if (ImGui::Button(ICON_DOWNLOAD "  Baixar e instalar##atualizacao_baixar"))
            sv.startDownload();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Lembrar depois##atualizacao_lembrar")) {
            sv.remindLater();
            g_state.open = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Pular esta versão##atualizacao_pular")) {
            settings.update.skippedVersion = r.version.toString();
            settings.save();
            g_state.open = false;
        }
        if (!st.assetName.empty() || r.htmlUrl.size() > 0) {
            ImGui::SameLine();
            if (ImGui::Button("Ver no GitHub##atualizacao_pagina"))
                openReleasePage(r);
        }
    } else if (!st.checking) {
        ImGui::PushTextWrapPos(0.0f);
        if (!st.checkError.empty()) {
            if (st.checkOffline)
                ImGui::TextColored(theme::kTextDim, "Sem conexão com o GitHub agora. Tente de novo quando houver internet.");
            else
                ImGui::TextColored(theme::kWarning, "Não foi possível verificar: %s", st.checkError.c_str());
        } else if (!st.checkNote.empty()) {
            ImGui::TextColored(theme::kTextDim, "%s", st.checkNote.c_str());
        } else if (st.upToDate || settings.update.lastCheckUnix > 0) {
            ImGui::TextColored(theme::kSuccess, "Você está na versão mais recente (%s).", BSLSTAGE_VERSION);
        } else {
            ImGui::TextColored(theme::kTextDim, "Ainda não verificado.");
        }
        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Preferências");
    bool changed = false;
    bool autoCheck = settings.update.autoCheck;
    if (ImGui::Checkbox("Verificar atualizações automaticamente##atualizacao_auto", &autoCheck)) {
        settings.update.autoCheck = autoCheck;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("No máximo uma vez por dia, em segundo plano, alguns segundos depois de abrir o programa.\n"
                          "Nunca durante o show (saída ligada, tela cheia ou layout bloqueado) nem no perfil Eco.");
    bool beta = settings.update.beta;
    if (ImGui::Checkbox("Receber versões beta##atualizacao_beta", &beta)) {
        settings.update.beta = beta;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Inclui versões de teste (pré-lançamentos). Podem ter erros.");
    if (!settings.update.skippedVersion.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::kTextDim, "Versão pulada: %s", settings.update.skippedVersion.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Esquecer##atualizacao_esquecer")) {
            settings.update.skippedVersion.clear();
            changed = true;
        }
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(st.checking || downloading || st.phase == update::Phase::Ready || st.phase == update::Phase::ConfirmNoChecksum);
    if (ImGui::Button(ICON_REFRESH "  Procurar agora##atualizacao_procurar"))
        sv.startCheck(settings.update, true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(theme::kTextDim, "Só o nome \"BSL-Stage/%s\" é enviado ao GitHub. Nenhum dado seu.", BSLSTAGE_VERSION);

    ImGui::End();
    if (changed)
        settings.save();
}

}
