# Roadmap

Cada fase termina com algo que roda e pode ser testado.

## Fase 0 — Esqueleto ✅ concluída
- CMake + MSVC, dependências via FetchContent
- Janela GLFW, OpenGL 3.3 core, Dear ImGui com docking e tema próprio
- Painéis vazios: Navegador, Grade de Clips, Camadas, Preview, Inspetor, barra de status
- Canvas em FBO com padrão de teste animado exibido no Preview
- Seletor de perfil de desempenho (Eco / Balanceado / Máximo) e contador de FPS

## Fase 1 — Mídia e camadas ✅ concluída (interações com mouse ainda sem teste manual)
- VideoSource com FFmpeg (MP4, MOV, WEBM, MKV, AVI…), loop, velocidade, play/pause
- ImageSource (PNG, JPG, WEBP, GIF animado, BMP, TGA)
- Grade de clips funcional (arrastar arquivos do Windows/Navegador para a grade)
- Várias camadas, opacidade, blend modes (normal, add, multiply, screen, overlay…)
- Thumbnails gerados em segundo plano

## Fase 2 — Saída e mapping
- 2A ✅ (sem teste manual de mouse/2º monitor): janela de saída por monitor, Editor de Screens / Slices
  (recorte do canvas, warp de 4 pontos, soft-edge), Spout out, canvas configurável
- Máscara
- 2B ✅ (sem teste manual de todos os cliques): salvar/abrir projeto (.bslstage), recentes, não salvos,
  autosave + recuperação, arquivos ausentes (Localizar…)

## Fase 3 — Documentos nativos
- **3A ✅ (sem teste manual de cliques/arrastar)**: parâmetros genéricos de fonte, Cor sólida, Texto (cartelas e
  letreiro), captura de tela (WGC + Desktop Duplication, interop D3D11→GL), placa de captura/webcam (dshow)
- **3B ✅ (sem teste manual de cliques/teclado na Prévia, nem do LibreOffice)**: PDF (PDFium), Office (Microsoft Office por
  COM, LibreOffice como reserva) e Web (CEF off-screen) em processo auxiliar `bslstage_worker.exe`: se travar, o show
  continua (reinício automático, "Falhou" depois de 3 tentativas). Textura D3D11 compartilhada com reserva por memória
  compartilhada. Avanço automático por tempo ou batidas, fade, "Expandir páginas na linha", cache de conversão, "Interagir".

## Fase 4 — Efeitos e plugins
- Host FFGL 2.x (carregar os plugins BSL e da comunidade)
- Shaders ISF nativos
- Efeitos nativos essenciais: transform, cor, blur, chroma key, máscara

## Fase 5 — Controle
- MIDI learn, OSC in/out, atalhos de teclado
- BPM (tap, sync), automação de parâmetros

## Fase 6 — Otimização
- Decodificação por hardware (D3D11VA), HAP/DXV como textura comprimida
- H.264/HEVC pelo decodificador do Windows (Media Foundation) e FFmpeg distribuído sem
  `h264`/`hevc` — ver `docs/PATENTES-E-LICENCAS.md`
- "Otimizar mídia" (conversão com FFmpeg)
- Teste em PC antigo (GPU integrada, 4 GB RAM)

## Fase 7 — Extras
- NDI in/out
- Mapa de LED / Art-Net
- Import do XML de Advanced Output do Resolume
