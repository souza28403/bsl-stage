# BSL Stage

Software de VJ / mapping / painel LED, gratuito e open source (GPL-3.0), em C++20 com OpenGL 3.3.
Estado atual: **Fase 0** (esqueleto: janela, tema, docking, pain¨¦is vazios e um canvas de teste).
Veja `docs/ARQUITETURA.md` e `docs/ROADMAP.md`.

## Como compilar (Windows 11)

Requisitos: Visual Studio 2026 (C++), CMake 3.25+ e Git. As depend¨ºncias (GLFW, Dear ImGui,
nlohmann/json) sÃ£o baixadas automaticamente pelo CMake. O loader OpenGL (glad) j¨¢ est¨¢ em `third_party/glad`.

```
cmake --preset windows
cmake --build build --config Release
build\Release\BSLStage.exe
```

As configuraÃ§Ãµes ficam em `%APPDATA%\BSL Stage\settings.json`.

## Teste de midia (Fase 1A)

O CMake baixa o FFmpeg (BtbN, LGPL shared) para `third_party/ffmpeg/` no configure. Para testar a decodificacao:

```
bash tools/make_testmedia.sh testmedia      # requer ffmpeg no PATH
build\Releaseslstage_mediatest.exe testmedia [--eco]
build\Releaseslstage_gltest.exe testmedia   # lado GPU (janela oculta)
```

## Regenerar o glad

```
pip install glad2
glad --api gl:core=3.3 --out-path third_party/glad c
```

## LicenÃ§a

GPL-3.0 ¡ª veja `LICENSE`.
