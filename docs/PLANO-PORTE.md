# Plano: BSL Stage no macOS e no Linux

## Situação
- O núcleo já é portável: OpenGL 3.3 core, ImGui, JSON, sequências, mapeamento, DMX lógico, ISF e efeitos nativos.
- A dependência do Windows está espalhada em cerca de 60 arquivos (`windows.h`, APIs `W`, `std::wstring`), somando 6 a 7 mil linhas de código de plataforma.
- O CMake linka Spout, D3D11/D3D12, WinRT e WinHTTP sem condição e baixa só binários win64.

## Equivalentes por subsistema

| Subsistema | Windows hoje | macOS | Linux | 1ª versão |
|---|---|---|---|---|
| Troca de vídeo entre programas | Spout | Syphon | (depois) | desligado |
| Worker com textura compartilhada | D3D11 + pipe | IOSurface + socket | dmabuf + socket | só memória compartilhada |
| Navegador embutido | CEF | CEF (bundle) | CEF | desligado |
| Office → PDF | COM | soffice --headless | soffice --headless | ligado |
| PDF | PDFium win64 | PDFium mac | PDFium linux | ligado |
| IA | ONNX + DirectML | ONNX + CoreML | ONNX CPU | ligado |
| Placa de captura/webcam | dshow | avfoundation | video4linux2 | ligado |
| Áudio de entrada/loopback | WASAPI | miniaudio (sem loopback) | miniaudio (monitor do PulseAudio/PipeWire) | ligado |
| Captura de tela/janela | Graphics.Capture | ScreenCaptureKit | x11grab / portal + PipeWire | desligado |
| NDI | LoadLibrary | dlopen | dlopen | ligado |
| Plugins FFGL | DLL | .bundle | .so | desligado |
| Rede DMX/HTTP/stream | winsock | sockets BSD | sockets BSD | ligado |
| Diálogos de arquivo | IFileDialog | NFD (Cocoa) | NFD (portal) | ligado |
| Atalhos globais | RegisterHotKey | RegisterEventHotKey | XGrabKey | desligado |
| Saídas em monitores | GLFW | GLFW (FORWARD_COMPAT) | GLFW forçando X11 | ligado |
| Atualizador/instalador | WinHTTP + MSI | só aviso + .app/.dmg | só aviso + AppImage | só aviso |
| Pastas | %APPDATA% | ~/Library/Application Support | XDG | ligado |

## Riscos
- **macOS + OpenGL:** o contexto precisa de `GLFW_OPENGL_FORWARD_COMPAT`. O GL sobre Metal é obsoleto e tem bugs de driver. Janelas e eventos ficam presos à thread principal.
- **macOS + distribuição:** assinatura e notarização exigem conta Apple Developer (US$ 99/ano) e certificado Developer ID.
- **Linux + Wayland:** posicionar janelas de saída não funciona. A 1ª versão força X11, que também roda via XWayland.
- **FFmpeg:** o das distros e o do Homebrew vêm como GPL. É preciso compilar um LGPL próprio por plataforma.
- **Compilador:** MSVC aceita código que GCC/Clang recusam. Há `sprintf_s`, `localtime_s` e similares em 13 arquivos.

## Etapas
1. Opções de plataforma no CMake (`BSL_WITH_SPOUT/D3D/CEF/AI/SCREENCAP/HOTKEYS/UPDATER/MSI/FFGL`), FFmpeg via pkg-config fora do Windows e downloads por plataforma.
2. `src/platform/`: caminhos, UTF-8 ↔ wide, bibliotecas dinâmicas, caminho do executável, abrir URL e pasta, sockets.
3. Troca das funções exclusivas do MSVC e remoção de `windows.h` onde sobra.
4. Módulos Windows em `*_win.cpp` + `*_stub.cpp` com a mesma interface.
5. DMX, HTTP, stream e NDI no shim de sockets e no `dlopen`.
6. **Marco: núcleo Linux.** Os testes de console passam no Ubuntu.
7. App no Linux: janela, arrastar e soltar, X11 e diálogos NFD.
8. Worker POSIX: socket Unix, memória compartilhada, PDF e soffice.
9. Captura v4l2 e áudio via miniaudio.
10. IA em CPU no Linux.
11. **AppImage** com FFmpeg LGPL próprio, construída em Ubuntu 22.04 para rodar nas distros desde 2022.
12. CI no GitHub Actions: Linux, macOS e Windows.
13. **macOS básico:** `.app`, Info.plist (câmera e microfone), dylibs, avfoundation, CoreML e PDFium.
14. Assinatura e notarização no CI.
15. **macOS completo:** Syphon, FFGL `.bundle`, ScreenCaptureKit e worker sem cópia.
16. **Linux completo:** captura de tela X11/Wayland, atalhos globais e atualizador em modo aviso.
17. Opcional: navegador embutido no macOS e no Linux.

## Marcos

| Etapas | Tamanho | Marco |
|---|---|---|
| 1 a 6 | ~2.500 linhas | núcleo compila e passa nos testes no Linux |
| 7 a 11 | ~2.000 linhas | **Linux básico** em AppImage |
| 16 | ~1.500 linhas | **Linux completo** |
| 12 a 14 | ~1.200 linhas | **macOS básico**, assinado |
| 15 | ~2.000 linhas | **macOS completo** |

## O que depende do dono do projeto
- **Linux (WSL):** instalar as ferramentas de compilação no Ubuntu do WSL, porque o `sudo` pede senha.
- **macOS:**
  - repositório no GitHub, para o CI;
  - conta Apple Developer e certificado Developer ID, guardados como secrets;
  - um Mac para os testes com janela;
  - decidir se distribui só arm64 ou universal.
