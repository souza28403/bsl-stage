#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace bsl::qa {

// Modo de teste de interface (--qa <pasta>). Registra todos os itens do ImGui (janela, rotulo, retangulo)
// por hooks, detecta conflitos de ID e asserts, e conversa com o driver externo (tools/uiqa.py) por arquivos
// em <pasta>:
//   snapshot.json   escrito pelo app a cada poucos frames (itens + estado)
//   cmd.<n>.txt     escrito pelo driver; o app executa e apaga (drop / shot / quit)
//   summary.json    escrito ao encerrar (contagem de asserts e conflitos)
bool enabled();
void init(const std::filesystem::path& dir); // depois de ImGui::CreateContext
void beginFrame();                           // antes de ImGui::NewFrame
// Depois de drawUi: confere conflitos e (a cada N frames) grava o snapshot. 'stateJson' = objeto JSON do estado do app.
void endFrame(int frame, const std::string& stateJson, float scale, int fbW, int fbH, int winW, int winH);
std::vector<std::string> takeCommands();     // linhas dos arquivos cmd.*.txt
void finish();                               // resumo no log + summary.json

// Registra o ultimo item desenhado com um nome explicito (celulas da grade, abas de deck...)
void note(const std::string& name);
// Da o rotulo ao proximo item que o ImGui registrar (combos, que nao informam rotulo)
void label(const char* name);
// Idem, com um retangulo explicito (tela, px)
void noteRect(const std::string& name, float x0, float y0, float x1, float y1);

} // namespace bsl::qa

// Definida em Qa.cpp (declarada em ui/BslImConfig.h)
void bslImGuiAssertFailed(const char* expr, const char* file, int line);
