#pragma once

#include "ui/PanelContext.h"

#include <string>

namespace bsl::ui {

// Nomes das janelas (tambem usados pelo layout de docking). O trecho depois de "###" e o ID
// da janela: o texto antes dele e so o que aparece na aba. Assim "Saída" e "Composição" podem
// existir como painel e como aba sem colidir (e sem colidir com o modo Mapeamento).
namespace panel {
// Modo Composição
inline constexpr const char* kOutput = "Saída###cmp_saida";
inline constexpr const char* kPreview = "Prévia###cmp_previa";
inline constexpr const char* kComposition = "Composição###cmp_grade";
inline constexpr const char* kClip = "Clip###cmp_clip";
inline constexpr const char* kFiles = "Arquivos###cmp_arquivos";
inline constexpr const char* kGenerators = "Fontes###cmp_geradores";
inline constexpr const char* kEffects = "Efeitos###cmp_efeitos";
inline constexpr const char* kCompositionTab = "Composição###cmp_aba_composicao";
inline constexpr const char* kLayerTab = "Camada###cmp_aba_camada";
inline constexpr const char* kOutputTab = "Saída###cmp_aba_saida";
// Modo Mapeamento
inline constexpr const char* kMapList = "Lista";
inline constexpr const char* kMapInput = "Entrada";
inline constexpr const char* kMapOutput = "Saída";
inline constexpr const char* kMapProps = "Propriedades";
// Modo Pixel Map
inline constexpr const char* kPmConfig = "Configuração###pm_config";
inline constexpr const char* kPmOverview = "Overview###pm_overview";
inline constexpr const char* kPmTable = "Tabela###pm_tabela";
} // namespace panel

// Desenha dockspace, paineis e barra de status. Chamar entre NewFrame e Render.
void drawUi(PanelContext& ctx);

// Mostra uma das abas do canto inferior direito depois de montar o layout (testes: --tab efeitos).
// 'key': arquivos | fontes (ou geradores, o nome antigo) | efeitos | composicao | camada | saida
void requestFocusTab(const std::string& key);

} // namespace bsl::ui
