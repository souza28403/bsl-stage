#include "ui/AtivacaoUi.h"

#include "app/Log.h"
#include "license/Ativacao.h"
#include "license/Licenca.h"
#include "platform/Paths.h"
#include "ui/Brand.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <string>

namespace bsl::ui {

namespace {

char g_email[256] = {};
char g_chave[1024] = {};
bool g_erro = false;

bool tentarAtivar()
{
    if (!licenca::ativar(bslplat::configDir() / "licenca.txt", g_email, g_chave)) {
        g_erro = true;
        return false;
    }
    log::info(std::string("Licença ativada para ") + licenca::normalizarEmail(g_email));
    g_chave[0] = '\0';
    g_erro = false;
    return true;
}

}

bool drawAtivacao(bool& sair)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::kBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    bool ativou = false;
    if (ImGui::Begin("##ativacao", nullptr, flags)) {
        const float largura = dp(480.0f);
        const float x = (ImGui::GetWindowWidth() - largura) * 0.5f;
        ImGui::SetCursorPosY(dp(60.0f));

        const Texture& marca = brand().horizontal;
        if (marca) {
            const float h = dp(36.0f);
            const float w = h * static_cast<float>(marca.width) / static_cast<float>(marca.height);
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - w) * 0.5f);
            ImGui::Image(static_cast<ImTextureID>(marca.id), ImVec2(w, h));
            ImGui::Dummy(dp(1.0f, 20.0f));
        }

        ImGui::SetCursorPosX(x);
        ImGui::PushFont(fonts().bold);
        ImGui::TextColored(theme::kTextStrong, "Ativar o BSL Stage");
        ImGui::PopFont();

        ImGui::SetCursorPosX(x);
        ImGui::PushTextWrapPos(x + largura);
        ImGui::TextColored(theme::kTextDim,
                           "Informe o e-mail e a chave de ativação recebidos. A chave começa com BSL1-.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(dp(1.0f, 12.0f));

        bool enviar = false;
        ImGui::SetCursorPosX(x);
        dimLabel("E-mail");
        ImGui::SetCursorPosX(x);
        ImGui::SetNextItemWidth(largura);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        enviar |= ImGui::InputText("##ativacao_email", g_email, sizeof(g_email), ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Dummy(dp(1.0f, 6.0f));
        ImGui::SetCursorPosX(x);
        dimLabel("Chave de ativação");
        ImGui::SetCursorPosX(x);
        ImGui::SetNextItemWidth(largura);
        enviar |= ImGui::InputText("##ativacao_chave", g_chave, sizeof(g_chave), ImGuiInputTextFlags_EnterReturnsTrue);

        if (g_erro) {
            ImGui::SetCursorPosX(x);
            ImGui::PushTextWrapPos(x + largura);
            ImGui::TextColored(theme::kError, "O e-mail e a chave não conferem. Confira e cole a chave inteira.");
            ImGui::PopTextWrapPos();
        }

        ImGui::Dummy(dp(1.0f, 8.0f));
        ImGui::SetCursorPosX(x);
        ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::kAccentDim);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kOnAccent);
        if (ImGui::Button("Ativar", dp(120.0f, 0.0f)))
            enviar = true;
        ImGui::PopStyleColor(4);
        ImGui::SameLine();
        if (ImGui::Button("Sair", dp(120.0f, 0.0f)))
            sair = true;

        if (enviar)
            ativou = tentarAtivar();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    return ativou;
}

}
