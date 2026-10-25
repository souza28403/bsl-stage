#pragma once

// Alvos da captura de tela: monitores e janelas (Win32, sem GL nem WinRT). O alvo e gravado no clip como
// JSON: {"kind":"monitor","index":1}  ou  {"kind":"window","exe":"chrome.exe","title":"..."}.

#include "media/SourceParams.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bsl::media {

struct MonitorTarget
{
    int index = 1;                // 1 = principal; depois da esquerda para a direita
    std::uintptr_t handle = 0;    // HMONITOR
    int x = 0, y = 0, w = 0, h = 0;
    bool primary = false;
};

struct WindowTarget
{
    std::uintptr_t hwnd = 0;
    std::string title;            // UTF-8
    std::string exe;              // nome do .exe em minusculas
    unsigned long pid = 0;
    int w = 0, h = 0;
};

// Janelas do proprio BSL Stage (principal e de saida) e de outras instancias do BSLStage.exe ficam de fora:
// capturar a propria saida faz um espelho infinito.
std::vector<MonitorTarget> listMonitors();
std::vector<WindowTarget> listWindows();

// Reencontra uma janela pelo exe + titulo parecido. 0 = nao achou.
std::uintptr_t findWindow(const std::string& exe, const std::string& title);
// 0..1: quanto dois titulos se parecem (prefixo comum e palavras em comum; ignora maiusculas)
double titleSimilarity(const std::string& a, const std::string& b);

Json monitorTargetJson(int index);
Json windowTargetJson(const WindowTarget& w);
// "Monitor 1 (1920x1080)" / "Janela: titulo (exe)"
std::string targetLabel(const Json& target);

// Miniatura RGBA (w x h, proporcao preservada com fundo escuro) via GDI. false se nao conseguiu.
struct ThumbRgba
{
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
};
bool grabMonitorThumb(const MonitorTarget& m, int w, int h, ThumbRgba& out);
bool grabWindowThumb(const WindowTarget& win, int w, int h, ThumbRgba& out);

} // namespace bsl::media
