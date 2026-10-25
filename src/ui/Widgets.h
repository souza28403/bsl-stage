#pragma once

#include "ui/Icons.h"

#include <imgui.h>

#include <climits>
#include <cmath>
#include <string>

namespace bsl::ui {

// Pecas de UI compartilhadas entre os paineis da Composicao.

// --- Paineis -----------------------------------------------------------------------------------------------
// Use no lugar de ImGui::Begin/End nos paineis encaixados: o painel em foco ganha o contorno vermelho BSL
// (1 dp, ~70% de alpha, desenhado por cima do conteudo e dentro da area do painel). Num unico ponto.
bool beginPanel(const char* name, ImGuiWindowFlags flags = 0);
void endPanel();

void dimLabel(const char* label);

// ImGui::BeginCombo + rotulo para o modo --qa (o ImGui nao informa o rotulo dos combos aos hooks)
bool beginCombo(const char* id, const char* preview);

void pushMono();
void popMono();
// Texto formatado em mono (numeros, tempos)
void monoText(const char* fmt, ...) IM_FMTARGS(1);

std::string formatTime(double seconds); // MM:SS.cc

// Botao de alternancia pequeno com letra (B = bypass, S = solo): aceso em vermelho quando ligado. 'label' pode ter
// "##id" (ex.: "B##camada_1_bypass").
bool toggleButton(const char* label, bool& value, const char* tooltip, ImVec2 size = ImVec2(0.0f, 0.0f)); // size.x <= 0: 28 dp

// --- Icones (Lucide, ver Icons.h) --------------------------------------------------------------------------
// Botoes so de icone: 'icon' = ICON_*, 'id' = "##area_acao" (ID estavel e legivel, e o rotulo que o uiqa clica),
// 'tooltip' em portugues (com o atalho, se houver). Monocromaticos: herdam a cor do texto; vermelho so quando
// ligados (IconButton_Active / iconToggle). size.x <= 0: quadrado da altura de um campo.
enum IconButtonFlags_ {
    IconButton_Active = 1 << 0, // aceso em vermelho (acao em andamento, opcao escolhida)
    IconButton_Flat = 1 << 1,   // sem fundo (icone solto, por exemplo sobre a faixa do nome da camada)
};
bool iconButton(const char* icon, const char* id, const char* tooltip, ImVec2 size = ImVec2(0.0f, 0.0f), int flags = 0,
                ImU32 color = 0);
// Botao de alternancia com icone: 'value' vira !value ao clicar; aceso em vermelho quando ligado
bool iconToggle(const char* icon, const char* id, bool& value, const char* tooltip, ImVec2 size = ImVec2(0.0f, 0.0f),
                int flags = 0);
// Desenha um icone centralizado em 'center' (pela caixa real do glifo), na fonte atual
void drawIcon(ImDrawList* dl, const char* icon, ImVec2 center, ImU32 color, float scale = 1.0f);
void drawPlayTriangle(ImDrawList* dl, ImVec2 center, float size, ImU32 color);

// Miniatura do gerador "Padrao de teste": barras de cor
void drawPatternThumb(ImDrawList* dl, ImVec2 a, ImVec2 b);
// Fundo quadriculado (mostra o alpha)
void drawCheckerboard(ImDrawList* dl, ImVec2 a, ImVec2 b, float cell = 0.0f); // 0 = 10 dp
void drawDashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float dash = 0.0f, float gap = 0.0f); // 0 = 6 dp / 4 dp
void drawCenteredMono(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text, ImU32 color);
// Texto da fonte do corpo centralizado em [a, b], reduzido para caber na largura (nome do tipo nas celulas)
void drawCenteredFit(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* text, ImU32 color);

// --- Numeros arrastaveis estilo After Effects ("hot text") -------------------------------------------------
// Valor em vermelho claro, sem caixa, unidade em cinza. Passar o mouse sublinha e troca o cursor; arrastar na
// horizontal muda o valor ('step' por pixel; Shift = x10, Ctrl = x0,1); clique sem arrastar abre o campo de
// digitacao (Enter confirma, Esc cancela, Tab/Shift+Tab vai para o proximo/anterior); botao direito:
// "Restaurar padrao" (se 'def' for informado). Devolve true quando o valor mudou.
//   width: 0 = largura do texto, > 0 = fixa (dp), < 0 = ocupa o espaco que sobra
//   format: formato de snprintf do numero (ex.: "%.2f"); unit: texto cinza depois dele (ex.: "px", "%")
bool hotNumber(const char* id, float& v, float lo, float hi, float step, const char* format = "%.2f",
               const char* unit = nullptr, float def = NAN, float width = 0.0f);
bool hotInt(const char* id, int& v, int lo, int hi, float step = 1.0f, const char* format = "%d",
            const char* unit = nullptr, int def = INT_MIN, float width = 0.0f);
// Opacidade e afins: 'value' de 0 a 1 mostrado em % com uma barra fina de preenchimento embaixo
bool hotPercent(const char* id, float& value, float def = NAN, float width = 0.0f);
// Separa um formato de slider ("%.0f px") em formato do numero ("%.0f") e unidade ("px")
void splitValueFormat(const char* format, std::string& numberFormat, std::string& unit);

} // namespace bsl::ui
