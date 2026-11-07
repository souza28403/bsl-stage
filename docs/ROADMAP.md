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
- **4A ✅ (sem teste manual de arrastar fora do uiqa, nem em GPU que não seja NVIDIA)**: cadeia de efeitos em Clip, Camada e
  Master (`IEffect`, `EffectChain`, `EffectRegistry`, pool de FBOs, perfil Eco), 15 efeitos nativos, shaders ISF (pastas,
  parser, GLSL 3.30, passes persistentes, geradores e efeitos, erros com a linha certa) e aba Efeitos. Ver
  `docs/ARQUITETURA.md`.
- **4B ✅ (host FFGL 2.x de terceiros; sem teste manual com plugins comerciais)**: descoberta segura (filtro de PE, varredura em processo separado com cache e bloqueio), execução com SEH, parâmetros, fontes FFGL como geradores, janela Plugins e "Importar plugins…". Plugado no mesmo `IEffect`/`EffectRegistry` (`ffgl:`). Os plugins BSL **não** vão como DLL: viram efeitos e fontes nativos (etapa à parte). Ver `docs/ARQUITETURA.md`.
- Pendente: mixers FFGL; conta-gotas do Chroma key na Prévia

## Fase 5 — Controle
- MIDI learn, OSC in/out, atalhos de teclado
- **DMX de entrada ✅** (Art-Net e sACN, patch por camada, DMX learn, perfis GDTF/CSV/HTML; sem teste com mesa de luz real): ver `docs/DMX.md`. Pendente: saída Art-Net/sACN (Pixel Map)
- BPM (tap, sync), automação de parâmetros

## Fase 6 — Otimização
- Decodificação por hardware (D3D11VA), HAP/DXV como textura comprimida
- H.264/HEVC pelo decodificador do Windows (Media Foundation) e FFmpeg distribuído sem
  `h264`/`hevc` — ver `docs/PATENTES-E-LICENCAS.md`
- "Otimizar mídia" (conversão com FFmpeg)
- Teste em PC antigo (GPU integrada, 4 GB RAM)

## Fase 7 — Extras
- **Atualizações e reportes pelo GitHub ✅** (sem teste contra o GitHub de verdade: nada foi publicado): verificação discreta uma vez por dia (WinHTTP), aviso na barra de status, notas da
  versão, download com SHA-256, `bslstage_updater.exe` com backup e retorno ao estado anterior, "Receber versões beta", "Pular esta versão"; Ajuda > Reportar problema / Sugerir melhoria
  abrem o novo issue do GitHub já preenchido. Workflow de release em `.github/workflows/release.yml`; passos manuais em `docs/RELEASE.md`. Pendente: assinatura de código,
  FFmpeg sem H.264/HEVC e CEF no pacote do CI, atualização diferencial (hoje o pacote é completo)
- **Plugins do cliente ✅** (item 11 de `docs/Melhorias.md`; sem teste manual com editores externos além do Bloco de Notas/VS Code): assistente "Novo plugin…", recarga automática dos `.fs` com erro mantendo o efeito antigo, Abrir no editor / Mostrar na pasta / Duplicar, exportar e importar `.bslplugin` com validação e licença à vista, `bslstage_isfcheck`, áudio (`audioFFT`) no ISF, `sdk/ffgl-template`, `docs/plugins/`, `AGENTS.md` e a skill `skills/bsl-stage-plugin`. Pendente: assinatura/verificação de autoria dos pacotes, loja de plugins
- NDI in/out (feito: fonte NDI, destino NDI da Screen e fonte Spout; ver ARQUITETURA.md)
- Mapa de LED / Art-Net
- Importação de XML de slices
