#pragma once

#include "ui/PanelContext.h"

#include <string>
#include <vector>

namespace bsl::ui {

// Janela "Compatibilidade de vídeo" (menu Ver): tabela, em português, de como trazer e enviar imagem de programas de
// cada API gráfica (OpenGL, DirectX, Vulkan, WebGL) e o estado ao vivo do NDI (runtime e versão) e do Spout.
void openCompatWindow();
void drawCompatWindow(PanelContext& ctx);

// "NDI Runtime não encontrado" + o link do NDI Tools/Runtime gratuito (só texto, botão para abrir a página e botão
// para copiar o link; o programa não baixa nada). Mostrar onde o NDI é necessário e não está disponível.
void drawNdiRuntimeNotice();

// Seletor de fonte NDI (combo com a lista da rede, atualizada em segundo plano, e o nome atual mesmo ausente).
// 'current' = nome salvo; devolve true e grava em 'out' se o usuário escolheu outro.
bool drawNdiSourcePicker(const std::string& current, std::string& out);

// Seletor de sender Spout (lista dos ativos, atualizada sozinha). Vazio = "Primeiro disponível".
bool drawSpoutSenderPicker(const std::string& current, std::string& out);

} // namespace bsl::ui
