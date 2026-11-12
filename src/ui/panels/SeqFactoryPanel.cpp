#include "ui/panels/SeqFactoryPanel.h"

#include "seq/SeqFactory.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/panels/SeqShared.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>

namespace bsl::ui {

namespace {

using namespace sequi;

constexpr const char* kDirecoes[] = {"Horizontal", "Vertical", "Linha", "Coluna", "Aleatório"};

void dim(const char* texto)
{
    ImGui::TextColored(theme::kTextDim, "%s", texto);
}

}

void drawSeqGeneratorPanel(PanelContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding(), padding()));
    beginPanel(panel::kSeqGen);

    Estado& e = estado();
    seq::Sequencia* s = sequenciaAtual(ctx);
    if (!s) {
        dim("Selecione ou crie uma sequência na lista.");
        endPanel();
        ImGui::PopStyleVar();
        return;
    }

    seq::ParametrosGerador p = s->gerador;
    bool ligado = s->temGerador;
    bool regerar = false;

    ImGui::TextColored(theme::kTextStrong, "%s", s->nome.empty() ? "(sem nome)" : s->nome.c_str());
    ImGui::Checkbox("Gerador ligado##ger_ligado", &ligado);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ligado: os passos da sequência são recriados a cada mudança aqui");
    if (!ligado) {
        dim("Desligado: os passos ficam como estão e podem ser editados à mão.");
        if (ligado != s->temGerador) {
            abrirGesto(ctx);
            s->temGerador = ligado;
        }
        e.editarGrade = false;
    }

    ImGui::BeginDisabled(!ligado);
    ImGui::SeparatorText("Ordem");
    dim("Direção");
    ImGui::SetNextItemWidth(-FLT_MIN);
    int direcao = static_cast<int>(p.direcao);
    if (beginCombo("##ger_direcao", kDirecoes[std::clamp(direcao, 0, 4)])) {
        for (int i = 0; i < 5; ++i)
            if (ImGui::Selectable(kDirecoes[i], i == direcao))
                direcao = i;
        ImGui::EndCombo();
    }
    p.direcao = static_cast<seq::DirecaoGerador>(direcao);
    ImGui::Checkbox("Inverter X##ger_inv_x", &p.inverterX);
    ImGui::SameLine();
    ImGui::Checkbox("Inverter Y##ger_inv_y", &p.inverterY);

    ImGui::SeparatorText("Agrupamento");
    dim("Bloco (grupos por passo)");
    hotInt("##ger_bloco", p.bloco, 1, seq::kMaxPassos, 1.0f, "%d", nullptr, 1, -1.0f);
    dim("Módulo (0 = desligado; N define o número de passos)");
    hotInt("##ger_modulo", p.modulo, 0, seq::kMaxPassos, 1.0f, "%d", nullptr, 0, -1.0f);
    ImGui::Checkbox("Espelho X##ger_esp_x", &p.espelhoX);
    ImGui::SameLine();
    ImGui::Checkbox("Espelho Y##ger_esp_y", &p.espelhoY);
    ImGui::Checkbox("Embaralhar##ger_embaralhar", &p.embaralhar);
    const bool usaSemente = p.embaralhar || p.direcao == seq::DirecaoGerador::Aleatorio;
    ImGui::BeginDisabled(!usaSemente);
    ImGui::SameLine();
    hotInt("##ger_semente", p.semente, 0, 999999, 1.0f, "%d", "semente", 1, dp(110.0f));
    ImGui::SameLine();
    if (ImGui::Button("Outra##ger_outra_semente"))
        p.semente = (p.semente + 1) % 1000000;
    ImGui::EndDisabled();

    ImGui::SeparatorText("Grade");
    const std::vector<seq::GeoSlice> geo = seq::coletarGeometria(ctx.output);
    bool manual = p.bordasManuais;
    ImGui::Checkbox("Grade manual##ger_manual", &manual);
    if (manual != p.bordasManuais) {
        if (manual) {
            const seq::Grade detectada = seq::gradeDoGerador(geo, p);
            p.bordasX = detectada.bordasX;
            p.bordasY = detectada.bordasY;
        } else {
            p.bordasX.clear();
            p.bordasY.clear();
        }
        p.bordasManuais = manual;
    }
    if (!p.bordasManuais) {
        dim("Limite para separar colunas e linhas");
        hotNumber("##ger_limiar", p.limiar, 0.01f, 4.0f, 0.01f, "%.2f", nullptr, 0.5f, -1.0f);
    }
    bool editarGrade = e.editarGrade;
    if (toggleButton("Editar a grade no Canvas##ger_editar_grade", editarGrade,
                     "Arrastar uma borda a move; Alt + clique cria uma borda vertical (Alt + Shift, horizontal); Backspace remove a borda selecionada",
                     ImVec2(-FLT_MIN, 0.0f)))
        e.editarGrade = editarGrade;
    if (p.bordasManuais) {
        if (ImGui::Button("Voltar à detecção automática##ger_auto", ImVec2(-FLT_MIN, 0.0f))) {
            p.bordasManuais = false;
            p.bordasX.clear();
            p.bordasY.clear();
        }
    }
    const seq::Grade grade = seq::gradeDoGerador(geo, p);
    pushMono();
    ImGui::TextColored(theme::kTextDim, "%d coluna(s) x %d linha(s)  |  %d slice(s)", grade.colunas(), grade.linhas(),
                       static_cast<int>(geo.size()));
    popMono();
    ImGui::EndDisabled();

    if (ligado && (!(p == s->gerador) || ligado != s->temGerador)) {
        abrirGesto(ctx);
        s->gerador = p;
        s->temGerador = true;
        regerar = true;
    }
    if (regerar) {
        regerarSequenciaAtual(ctx);
        const int n = static_cast<int>(s->passos.size());
        e.passo = std::clamp(e.passo, 0, std::max(n - 1, 0));
    }

    ImGui::Spacing();
    pushMono();
    ImGui::TextColored(theme::kTextDim, "%d passo(s)", static_cast<int>(s->passos.size()));
    popMono();
    if (ligado)
        ImGui::TextColored(theme::kTextDim, "Mudar o gerador recria os passos desta sequência.");

    endPanel();
    ImGui::PopStyleVar();
}

}
