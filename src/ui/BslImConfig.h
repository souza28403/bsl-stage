#pragma once

// Configuracao do Dear ImGui para o BSL Stage (IMGUI_USER_CONFIG, ver CMakeLists.txt).

// Hooks de itens (id, retangulo, rotulo) usados pelo modo --qa para clicar pelo rotulo. Sem custo fora
// do --qa: as chamadas ficam atras de um bool (ImGuiContext::TestEngineHookItems).
#define IMGUI_ENABLE_TEST_ENGINE

// Assert do ImGui: em Release o assert() padrao some. Aqui ele registra no log (e conta, para o --qa)
// e segue em frente em vez de abortar.
void bslImGuiAssertFailed(const char* expr, const char* file, int line);
#define IM_ASSERT(_EXPR) ((void)((_EXPR) || (bslImGuiAssertFailed(#_EXPR, __FILE__, __LINE__), false)))
