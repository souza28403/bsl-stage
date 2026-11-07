#include "ui/panels/LegalWindow.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "app/Qa.h"
#include "app/Version.h"
#include "legal/Notices.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/panels/UpdateUi.h"
#include "update/Markdown.h"
#include "update/SystemProbe.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace bsl::ui {

namespace {

struct TextBlock
{
    bool loaded = false;
    bool missing = false;
    std::string raw;
    std::vector<std::string> lines;
    int columns = 0;
};

struct DocText
{
    bool loaded = false;
    bool missing = false;
    std::vector<update::MdBlock> blocks;
};

struct State
{
    bool open = false;
    int tab = 0;
    int requestTab = -1;
    bool licenseFull = false;

    bool loadedLists = false;
    std::vector<legal::ThirdParty> third;
    std::map<std::string, TextBlock> texts;
    DocText privacy;
    DocText terms;

    LegalUiInfo info;
};
State g_state;

const char* kSourceNotFound = "Não achei os documentos (LICENSE, THIRD-PARTY-NOTICES.md...) ao lado do programa. "
                              "Eles estão no repositório do código-fonte.";

void ensureLoaded()
{
    if (g_state.loadedLists)
        return;
    g_state.loadedLists = true;
    const std::filesystem::path root = legal::docsRoot();
    g_state.info.docsFound = !root.empty();
    g_state.third = legal::parseThirdParty(legal::readTextFile(root / L"THIRD-PARTY-NOTICES.md"));
    g_state.info.thirdPartyCount = static_cast<int>(g_state.third.size());
}

void loadDoc(DocText& doc, const wchar_t* fileName)
{
    if (doc.loaded)
        return;
    doc.loaded = true;
    const std::string text = legal::readTextFile(legal::docsRoot() / fileName);
    doc.missing = text.empty();
    doc.blocks = update::parseMarkdown(text);
}

TextBlock& textFor(const std::string& relative)
{
    TextBlock& t = g_state.texts[relative];
    if (!t.loaded) {
        t.loaded = true;
        const std::filesystem::path root = legal::docsRoot();
        std::filesystem::path file;
        if (relative == "LICENSE")
            file = root / L"LICENSE";
        else if (legal::safeLicensePath(relative))
            file = root / std::filesystem::path(std::u8string(relative.begin(), relative.end()));
        t.raw = file.empty() || root.empty() ? std::string() : legal::readTextFile(file);
        t.missing = t.raw.empty();
    }
    return t;
}

void drawTextBlock(const char* id, TextBlock& t, float height)
{
    if (t.missing) {
        ImGui::TextColored(theme::kWarning, "Arquivo não encontrado ao lado do programa.");
        return;
    }
    pushMono();
    const float charW = ImGui::CalcTextSize("0").x;
    const float avail = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize - dp(12.0f);
    const int cols = std::clamp(static_cast<int>(avail / std::max(charW, 1.0f)), 40, 160);
    if (cols != t.columns) {
        t.columns = cols;
        t.lines = legal::wrapText(t.raw, cols);
    }
    ImGui::BeginChild(id, ImVec2(-FLT_MIN, height), ImGuiChildFlags_Borders);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(t.lines.size()), ImGui::GetTextLineHeightWithSpacing());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const std::string& line = t.lines[static_cast<size_t>(i)];
            ImGui::TextUnformatted(line.c_str(), line.c_str() + line.size());
        }
    }
    ImGui::EndChild();
    popMono();
}

void drawDocument(DocText& doc, const wchar_t* fileName, const char* shownName)
{
    loadDoc(doc, fileName);
    if (doc.missing) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kWarning, "O arquivo %s não foi encontrado ao lado do programa.", shownName);
        ImGui::TextColored(theme::kTextDim, "Ele acompanha o pacote e o repositório do código-fonte.");
        ImGui::PopTextWrapPos();
        return;
    }
    drawMarkdown(doc.blocks);
}

float footerHeight()
{
    return ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f + dp(4.0f);
}

void labelValue(const char* label, const std::string& value, bool warn = false)
{
    if (value.empty())
        return;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim, "%s", label);
    ImGui::SameLine();
    if (warn)
        ImGui::TextColored(theme::kWarning, "%s", value.c_str());
    else
        ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
}

void drawLicenseTab()
{
    ImGui::BeginChild("##legal_licenca_corpo", ImVec2(-FLT_MIN, -footerHeight()));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::PushFont(fonts().bold, fonts().bold->LegacySize * 1.2f);
    ImGui::TextUnformatted("BSL Stage é software livre");
    ImGui::PopFont();
    ImGui::TextColored(theme::kTextDim, "© 2026 BSL Light  ·  versão %s  ·  GNU General Public License, versão 3 (GPL-3.0)", BSLSTAGE_VERSION);
    ImGui::Spacing();
    ImGui::BulletText("Você pode usar o programa para qualquer fim, inclusive comercial (shows, eventos, locação).");
    ImGui::BulletText("Pode estudar como ele funciona e modificá-lo.");
    ImGui::BulletText("Pode compartilhar cópias. Se distribuir o programa (original ou modificado), precisa manter a GPL-3.0, manter os "
                      "avisos de copyright e oferecer o código-fonte.");
    ImGui::BulletText("Não há garantia de nenhum tipo (GPL-3.0, seções 15 e 16): o uso é por sua conta e risco.");
    ImGui::BulletText("Os nomes \"BSL Light\" e \"BSL Stage\" e o logo não são cobertos pela GPL (veja a aba Termos).");
    ImGui::Spacing();
    ImGui::TextColored(theme::kTextDim, "Código-fonte:");
    ImGui::SameLine();
    ImGui::TextUnformatted(sourceCodeUrl().c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button(ICON_LINK "  Abrir o código-fonte##legal_codigo"))
        openSourceCode();
    ImGui::SameLine();
    if (ImGui::Button(g_state.licenseFull ? "Ocultar o texto completo##legal_texto_completo"
                                          : "Ver texto completo##legal_texto_completo"))
        g_state.licenseFull = !g_state.licenseFull;
    if (g_state.licenseFull) {
        ImGui::Spacing();
        TextBlock& t = textFor("LICENSE");
        drawTextBlock("##legal_texto_gpl", t, std::max(dp(160.0f), ImGui::GetContentRegionAvail().y - dp(6.0f)));
    }
    ImGui::EndChild();
}

void drawThirdPartyTab()
{
    ensureLoaded();
    g_state.info.openedTexts = 0;
    if (g_state.third.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kWarning, "%s", kSourceNotFound);
        ImGui::PopTextWrapPos();
        return;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim,
                       "%d componentes de terceiros acompanham o BSL Stage, cada um com a sua licença. Abra um item para ver versão, "
                       "copyright, endereço e o texto completo da licença.",
                       static_cast<int>(g_state.third.size()));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    ImGui::BeginChild("##legal_terceiros_lista", ImVec2(-FLT_MIN, -footerHeight()), ImGuiChildFlags_Borders);
    int index = 0;
    for (const legal::ThirdParty& tp : g_state.third) {
        const std::string title = tp.name + (tp.version.empty() ? "" : "  " + tp.version) + "   ·   " + tp.license + "###tp_" + std::to_string(index);
        const bool pending = !tp.status.empty();
        if (pending)
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kWarning);
        const bool openItem = ImGui::CollapsingHeader(title.c_str());
        if (pending)
            ImGui::PopStyleColor();
        if (openItem) {
            ImGui::Indent(dp(8.0f));
            labelValue("Versão:   ", tp.version);
            labelValue("Licença:  ", tp.license);
            labelValue("Copyright:", tp.copyright);
            labelValue("Endereço: ", tp.url);
            labelValue("Uso:      ", tp.usage);
            labelValue("Pendente: ", tp.status, true);
            if (!tp.url.empty() && ImGui::SmallButton(("Copiar endereço##tp_url_" + std::to_string(index)).c_str()))
                ImGui::SetClipboardText(tp.url.c_str());
            int fileIndex = 0;
            for (const std::string& rel : tp.licenseFiles) {
                const std::string nodeId = "Texto da licença: " + rel + "###tp_" + std::to_string(index) + "_" + std::to_string(fileIndex++);
                if (ImGui::TreeNode(nodeId.c_str())) {
                    ++g_state.info.openedTexts;
                    TextBlock& t = textFor(rel);
                    drawTextBlock(("##tp_texto_" + std::to_string(index) + "_" + std::to_string(fileIndex)).c_str(), t, dp(260.0f));
                    ImGui::TreePop();
                }
            }
            if (tp.licenseFiles.empty() && pending)
                ImGui::TextColored(theme::kWarning, "Texto da licença: pendente (ainda não há arquivo em licenses/).");
            ImGui::Unindent(dp(8.0f));
            ImGui::Spacing();
        }
        ++index;
    }
    ImGui::EndChild();
}

void drawDocTab(DocText& doc, const wchar_t* fileName, const char* shownName, const char* childId)
{
    ImGui::BeginChild(childId, ImVec2(-FLT_MIN, -footerHeight()), ImGuiChildFlags_Borders);
    drawDocument(doc, fileName, shownName);
    ImGui::EndChild();
}

}

std::string sourceCodeUrl()
{
    return std::string("https://github.com/") + BSLSTAGE_GITHUB_REPO;
}

void openSourceCode()
{
    g_state.info.lastUrl = sourceCodeUrl();
    if (qa::enabled())
        return;
    if (!update::openGithubUrl(sourceCodeUrl()))
        log::warn("Não foi possível abrir o navegador para " + sourceCodeUrl());
}

void openLegalWindow(LegalTab tab)
{
    g_state.open = true;
    g_state.requestTab = static_cast<int>(tab);
}

LegalUiInfo legalUiInfo()
{
    LegalUiInfo i = g_state.info;
    i.windowOpen = g_state.open;
    i.tab = g_state.tab;
    i.licenseFullOpen = g_state.open && g_state.licenseFull;
    return i;
}

void drawLegalWindow(PanelContext&)
{
    if (!g_state.open) {
        g_state.info.openedTexts = 0;
        return;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dp(760.0f), dp(620.0f)), ImGuiCond_Appearing);
    if (!ImGui::Begin("Licenças e avisos###janela_legal", &g_state.open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive()
        && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        g_state.open = false;

    if (ImGui::BeginTabBar("##legal_abas")) {
        struct TabDef { const char* label; int index; };
        static const TabDef tabs[] = {{"Licença###legal_aba_licenca", 0},
                                      {"Terceiros###legal_aba_terceiros", 1},
                                      {"Privacidade###legal_aba_privacidade", 2},
                                      {"Termos###legal_aba_termos", 3}};
        for (const TabDef& t : tabs) {
            const ImGuiTabItemFlags flags = g_state.requestTab == t.index ? ImGuiTabItemFlags_SetSelected : 0;
            const bool tabOpen = ImGui::BeginTabItem(t.label, nullptr, flags);
            qa::note(std::strchr(t.label, '#'));
            if (tabOpen) {
                g_state.tab = t.index;
                switch (t.index) {
                case 0: drawLicenseTab(); break;
                case 1: drawThirdPartyTab(); break;
                case 2: drawDocTab(g_state.privacy, L"PRIVACIDADE.md", "PRIVACIDADE.md", "##legal_privacidade_corpo"); break;
                default: drawDocTab(g_state.terms, L"TERMOS-DE-USO.md", "TERMOS-DE-USO.md", "##legal_termos_corpo"); break;
                }
                ImGui::EndTabItem();
            }
        }
        g_state.requestTab = -1;
        ImGui::EndTabBar();
    }
    if (g_state.tab != 1)
        g_state.info.openedTexts = 0;

    ImGui::Spacing();
    if (ImGui::Button("Fechar##legal_fechar", ImVec2(dp(110.0f), 0.0f)))
        g_state.open = false;
    ImGui::SameLine();
    ImGui::TextColored(theme::kTextDim, "Não é aconselhamento jurídico. Versão do aviso: %d", legal::kNoticeVersion);
    ImGui::End();
}

}
