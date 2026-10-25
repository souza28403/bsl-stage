#pragma once

#include <imgui.h>

namespace bsl::ui {

// Todas as cores e medidas do tema ficam aqui: e so mexer neste bloco para trocar o visual.
namespace theme {

constexpr ImVec4 rgb(unsigned hex, float alpha = 1.0f)
{
    return ImVec4(static_cast<float>((hex >> 16) & 0xFF) / 255.0f,
                  static_cast<float>((hex >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(hex & 0xFF) / 255.0f, alpha);
}

// Estrutura no estilo dos apps de video da Adobe (After Effects / Premiere): cinzas neutros,
// paineis separados por vaos escuros, campos rebaixados, densidade alta e so o painel em foco
// destacado. O acento continua o vermelho BSL #EC3337 (faisca, nunca inundacao).
constexpr unsigned kAccentHex = 0xEC3337;

constexpr ImVec4 kBackground = rgb(0x121212);   // vaos entre paineis, fundo do app
constexpr ImVec4 kPanel = rgb(0x1F1F1F);        // superficie dos paineis
constexpr ImVec4 kPanelRaised = rgb(0x2C2C2C);  // botões, cabeçalhos
constexpr ImVec4 kPanelHover = rgb(0x383838);
constexpr ImVec4 kPanelActive = rgb(0x454545);
constexpr ImVec4 kField = rgb(0x171717);        // campos de valor (rebaixados)
constexpr ImVec4 kBorder = rgb(0x333333);       // hairline
constexpr ImVec4 kText = rgb(0xD8D8D8);         // texto principal (cinza claro, nao branco puro)
constexpr ImVec4 kTextStrong = rgb(0xF2F2F2);   // titulos, aba em foco
constexpr ImVec4 kTextDim = rgb(0x8C8C8C);
constexpr ImVec4 kAccent = rgb(kAccentHex);     // vermelho BSL (red-500)
constexpr ImVec4 kAccentHover = rgb(0xF05155);  // red-400
constexpr ImVec4 kAccentDim = rgb(0xB0181C);    // red-700
constexpr ImVec4 kAccentSoft = rgb(kAccentHex, 0.18f);
constexpr ImVec4 kOnAccent = rgb(0xFFFFFF);     // texto sobre o vermelho

constexpr ImVec4 kSuccess = rgb(0x1EA75A);
constexpr ImVec4 kWarning = rgb(0xF5A524);
constexpr ImVec4 kInfo = rgb(0x2F7FE0);

// Cantos pequenos e densidade alta (mais informacao por tela, como nos apps Adobe)
constexpr float kRounding = 4.0f;
constexpr float kRoundingWindow = 6.0f;
constexpr float kSpacing = 8.0f;
constexpr float kPadding = 10.0f;
constexpr float kPanelGap = 4.0f;               // vao escuro entre paineis encaixados
constexpr float kFontSize = 16.0f;
constexpr float kMonoSize = 14.0f;

} // namespace theme

// Aplica cores/medidas do tema ao ImGui, multiplicadas pela escala da interface
// (escala do Windows ou a escolhida em Ver > Tamanho da interface). Pode ser chamada de novo.
void applyTheme(float scale = 1.0f);

// Escala atual da interface e conversao de medidas fixas (px a 100%) para px reais
float uiScale();
inline float dp(float px) { return px * uiScale(); }
inline ImVec2 dp(float x, float y) { return ImVec2(x * uiScale(), y * uiScale()); }

// Cantos arredondados e margem padrao para desenho manual (ImDrawList), ja na escala atual
inline float rounding() { return dp(theme::kRounding); }
inline float padding() { return dp(theme::kPadding); }

// Tamanho em px reais para AddText(font, size, ...) / CalcTextSizeA: o ImGui escala as fontes
// empilhadas por PushFont, mas nao o tamanho passado direto ao ImDrawList.
inline float fontPx(const ImFont* font) { return font->LegacySize * uiScale(); }

// Fontes do Design System: Barlow (UI), Barlow Bold (titulos), JetBrains Mono (numeros).
// Cai para Segoe UI / fonte padrao se os arquivos nao existirem.
void loadFont(ImGuiIO& io);

struct Fonts {
    ImFont* body = nullptr;
    ImFont* bold = nullptr;
    ImFont* mono = nullptr;
};
const Fonts& fonts();

} // namespace bsl::ui
