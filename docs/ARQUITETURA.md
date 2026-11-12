# BSL Stage — Arquitetura

> Nome provisório. Software de VJ / mapping / painel LED, gratuito e open source (GPL-3.0).
> Clips, camadas, plugins e screens/slices, com código, visual e decisões próprias.

## Princípios

1. **Nativo antes de plugin.** Vídeo (incl. WEBM), imagem, GIF, PDF, documentos, texto, webcam e
   captura de tela são fontes nativas. Plugin é para efeito criativo, não para "abrir arquivo".
2. **Leve por padrão.** Só decodifica o que está tocando. Sem módulos pesados carregados sem uso.
   Recursos opcionais (Web, NDI) são módulos separados, carregados só quando usados.
3. **Roda em PC antigo.** Alvo mínimo: OpenGL 3.3 core, 4 GB RAM, GPU integrada Intel HD 4000+.
4. **Compatível onde importa.** Host FFGL 2.x (os plugins BSL e da comunidade funcionam), shaders
   ISF, Spout, OSC, MIDI, Art-Net. Importação de XML de slices.
5. **Arquivos legíveis.** Projeto salvo em JSON (`.bslstage`), fácil de versionar e depurar.

## Stack

| Camada | Escolha | Licença |
|---|---|---|
| Linguagem / build | C++20, CMake, MSVC (VS 2026) | — |
| Janela / input | GLFW 3.4 | zlib |
| GPU | OpenGL 3.3 core via glad2 | MIT/domínio público |
| Interface | Dear ImGui (branch docking) com tema próprio | MIT |
| Vídeo / áudio | FFmpeg (build LGPL, shared) — H.264/H.265/VP8/VP9/AV1/ProRes/HAP/DXV3 | LGPL |
| PDF | PDFium (binários bblanchon/pdfium-binaries, no worker) | BSD-3 |
| Office (pptx/docx/xlsx/odp…) | Microsoft Office via COM → PDF, ou LibreOffice headless → PDF (do usuário, não distribuídos) → PDFium | — |
| Web | CEF (Chromium Embedded Framework) off-screen, no worker | BSD-3 (Chromium: licenças em `CREDITS.html` do CEF) |
| Imagens | stb_image (+ FFmpeg para formatos raros) | MIT/domínio público |
| JSON | nlohmann/json | MIT |
| MIDI | RtMidi | MIT-like |
| OSC | implementação própria (UDP) | — |
| DMX (entrada) | Art-Net 4 e sACN (E1.31) próprios, Winsock UDP; perfis GDTF (padrão aberto) | — |
| Spout | Spout2 SDK | BSD-2 |
| Plugins | FFGL SDK (já em `ffgl-master`) | BSD |
| NDI (opcional) | NDI SDK carregado dinamicamente em runtime | proprietária (não distribuída junto) |

## Visão geral dos módulos

```
src/
  app/        loop principal, janela, settings, perfis de desempenho
  core/       modelo de dados: Composition, Deck, Layer, Clip, Parameter, Transport, BPM
  media/      fontes nativas (ISource): Video, Image, ImageSequence, Text, Color/Gradient, Webcam,
              ScreenCapture; PDF/Office/Web via RemoteSource (processo auxiliar)   + cache de thumbnails
  worker/     bslstage_worker.exe: PDFium, conversão de Office e CEF (processo separado)
  render/     GL: Fbo, Shader, Texture, pipeline de composição (blend modes), ISF
  fx/         efeitos nativos + host FFGL (carrega DLLs de plugin)
  plugin/     plugins do usuário: modelos do assistente "Novo plugin", criar/duplicar, pacote .bslplugin, validador ISF (ver "Plugins do usuário")
  output/     Screens, Slices, warp (4 pontos / malha), máscaras, soft-edge,
              janelas fullscreen por monitor, Spout out, mapa LED
  control/    atalhos de teclado e ações nomeadas (MIDI e OSC entram nos mesmos ganchos); o DMX já usa
  dmx/        DMX por Art-Net e sACN: receptor Winsock (thread própria), mistura HTP/LTP, patch das camadas, DMX learn, exportação de perfis
  ui/         tema, menu Exibir (painéis ocultos, janelas soltas, trava do layout), painéis (Navegador, Grade de Clips, Camadas, Preview, Inspetor,
              Editor de Saída, Configurações)
  io/         salvar/abrir projeto (.bslstage), import XML de saída
  update/     atualizações pelo GitHub Releases e reportes pelo GitHub Issues (núcleo sem GL nem ImGui; ver "Atualizações e reportes")
  legal/      avisos legais: versão do aviso, leitura de LICENSE / THIRD-PARTY-NOTICES.md / licenses/ (ver "Licenças, privacidade e termos")
```

### Modelo de dados (core)

```
Composition (canvas ex.: 1920x1080, fps, BPM)
 ├─ Deck[]                 bancos de clips (abas)
 │   └─ Clip[col][layer]   referência a uma ISource + parâmetros + cadeia de efeitos
 ├─ Layer[]                opacidade, blend mode, clip ativo, cadeia de efeitos, transição
 └─ Master                 cadeia de efeitos final
OutputSetup
 └─ Screen[]               destino (monitor, Spout, NDI, LED)
     └─ Slice[]            recorte do canvas (input rect) → forma na saída (warp, máscara)
```

Todo parâmetro animável é um `Parameter` (valor, range, mapeamento MIDI/OSC, automação por BPM).

### Fontes de mídia (`ISource`)

Interface única: `open()`, `close()`, `update(dt)`, `texture()`, `duration()`, `seek()`,
`nativeSize()`. Cada fonte decide como gerar a textura. Exemplos:

- **VideoSource** — FFmpeg em thread própria, fila curta de frames, upload via PBO.
  Decodificação por hardware (D3D11VA) quando disponível. HAP/DXV enviados como textura
  comprimida (DXT) direto para a GPU — o formato mais leve para PC fraco.
- **PdfSource / WebSource** (`media/RemoteSource`) — PDF, documentos do Office (convertidos para PDF) e páginas web
  vivem num processo auxiliar; ver "PDF, Office e Web num processo auxiliar (Fase 3B)" abaixo.
- **TextSource** — texto/legenda com fonte, cor, alinhamento (cartelas, letreiros).

### Pipeline de render (por frame)

1. Fontes ativas atualizam suas texturas (só as que estão tocando).
2. Cada clip aplica sua cadeia de efeitos no tamanho da fonte, antes do encaixe (FBOs ping-pong do pool por tamanho).
3. Camadas são compostas de baixo para cima no canvas com blend mode + opacidade; a camada com efeitos passa antes pelo
   encaixe em FBO próprio e pela cadeia da camada (tamanho do canvas).
   O **Transform** (posição, tamanho, rotação, âncora e recorte) entra no passe de blend, depois dos efeitos da camada.
4. Efeitos master, depois o fade do master.
5. Saída: cada Screen desenha seus Slices lendo o canvas (warp/máscara) para o destino.
6. Preview na UI reaproveita as mesmas texturas (sem render extra).

### OutputSetup (Fase 2A)

`src/output/OutputSetup.h` guarda só dados (serializáveis): `Screen[]` (destino Desativado / Monitor /
Janela / Spout, resolução) → `Slice[]` (retângulo de entrada em px do canvas, 4 cantos de saída em px da
tela, flip, opacidade, ganho RGB + brilho, soft edge por lado). `OutputRenderer` cuida dos recursos GL:

- Cada Screen é renderizada num FBO do tamanho dela. Cada slice é um triângulo de tela cheia (com scissor
  na caixa do quad) cujo fragment shader inverte a **homografia** (`output/Warp.cpp`) para achar o texel do
  canvas: warp perspectivo correto, sem a dobra de dois triângulos afins. Mistura aditiva; soft edge por
  distância em px até cada lado do quad, com rampa simétrica (gamma).
- Janelas de saída: GLFW com contexto compartilhado com o principal (texturas/programas compartilhados;
  FBOs e VAOs não: um VAO por contexto). Monitor = janela sem borda, topmost, do tamanho do monitor (sem
  trocar modo de vídeo), cursor oculto, Esc/Alt+F4 não fecham. Sincronização entre contextos por fence.
  Monitores são reencontrados pelo nome (`glfwSetMonitorCallback`); se sumir, a Screen fica sem saída e
  avisa no log.
- **VSync**: só uma janela de saída (a primeira em Monitor, senão a primeira Janela) troca buffers com
  vsync; as demais e a janela principal ficam em intervalo 0, para as janelas não se atrasarem umas às
  outras. Havendo janela de saída com vsync, ela dita o ritmo do loop (também no perfil Máximo); sem ela,
  o Máximo continua usando vsync na principal.
- **Spout**: SpoutGL do Spout2 SDK 2.007.018 compilado do código-fonte (FetchContent, sem DLL); um
  sender por Screen. `SpoutOutput` isola o SDK (que carrega o próprio GL e conflita com o glad).
- **Compatibilidade com outras APIs gráficas**: o motor fica em OpenGL (os plugins FFGL dependem dele); a compatibilidade é trocar
  imagem com programas de qualquer API (OpenGL, DirectX 9/11/12, Vulkan, WebGL). Ver Exibir > Compatibilidade de vídeo
  (`ui/panels/VideoCompat.*`): OpenGL e DirectX por Spout/NDI, Vulkan por NDI ou Captura de janela, WebGL pela fonte Web (CEF)
  ou Captura de janela do navegador.
- **Spout de entrada**: fonte "Spout" (`media/SpoutSource.*`, ao vivo). `SpoutInput` isola o SDK como o `SpoutOutput`; a
  textura recebida sai com a linha 0 = topo (`ReceiveTexture(..., bInvert=false)`); a lista de senders ativos vem da memória
  compartilhada do Spout e atualiza sozinha (1 leitura por segundo). Mesmo caminho GL/DX do `bslstage_spouttest`.
- **NDI®** (entrada e saída), sem SDK no projeto: `third_party/ndi-min/ndi_min.h` (cabeçalho mínimo próprio) +
  `ndi/NdiRuntime.*` (carrega `Processing.NDI.Lib.x64.dll` em tempo de execução: `--ndi-lib`, pasta do exe, `NDI_RUNTIME_DIR_V6/V5`;
  sem a DLL tudo segue funcionando e a UI mostra "NDI Runtime não encontrado"). `ndi/NdiFinder.*` mantém a lista de fontes da
  rede numa thread de fundo. Entrada: `media/NdiSource.*` (ao vivo; recebe sem bloquear, UYVY convertido na GPU em BT.709,
  reconecta sozinha, opções "baixa latência" e "largura de banda baixa"). Saída: destino "NDI" da Screen
  (`output/NdiOutput.*`): passada na GPU (inverte linhas e empacota UYVY 4:2:2, ou BGRA) -> PBO -> cópia no quadro seguinte ->
  `send_video_async_v2`, no frame rate da composição e só quando há receptores. Testes: `bslstage_ndittest --ndi-lib <dll>`.
- Com alguma Screen saindo, o canvas é renderizado a 100% (a escala do perfil só vale para a prévia).
- Modo Mapeamento (`ui/panels/MappingPanels.cpp`): layout de dock próprio (dockspace separado, salvo à parte
  no `imgui.ini`) com Lista, Entrada, Saída e Propriedades; "Identificar" desenha borda, grade e nome dos
  slices nas saídas reais.
- Editor de Entrada e Saída (`ui/panels/MapView.cpp`, um só código para as duas vistas): seleção múltipla (Ctrl/Shift+clique,
  retângulo, Ctrl+A), mover, redimensionar (Shift = proporção, Alt = do centro), girar (Shift = 15°), zoom na roda, pan (botão
  do meio ou espaço+arrasto), régua e grade, ímã com linhas-guia, alinhar/distribuir/"Encostar" e teclado (setas, Ctrl+D,
  Delete, Esc, Ctrl+Z/Y). Na Saída há dois modos: **Transformar** (o quad como retângulo com posição, tamanho e rotação; um quad
  deformado usa a caixa dos cantos, com aviso) e **Pontos (warp)** (4 cantos livres). A lógica sem ImGui/GL fica em
  `output/Snap.*` (ímã, alinhar, distribuir, encostar), `output/MapEdit.*` (retângulo orientado, redimensionar, girar) e
  `output/MapHistory.*` (desfazer/refazer: snapshots do `OutputSetup`, até 100 passos, um por gesto: o histórico observa o
  `OutputSetup` a cada frame e só registra quando o mouse/campo de texto é solto). Testes: `bslstage_maptest`.
  O limiar do ímã é `dp(8)` em px de **tela** (vira px de conteúdo pelo zoom). Alt desliga o ímã enquanto segura (e o ImGui não
  trata esse Alt como "abrir a barra de menu": a vista toma a posse da tecla).

Argumentos de teste: `--output-window`, `--screenshot-output <png> <s>`, `--mode mapping`, `--identify`,
`--canvas <w> <h>`, `--test-missing-monitor`. Testes: `bslstage_spouttest` (sender → receiver no mesmo
processo, confere pixels).

### Fontes ao vivo e geradores (Fase 3A)

- **Parâmetros genéricos** (`media/SourceParams.h`): cada gerador declara uma lista de `ParamDesc` (id, rótulo PT,
  tipo — float, int, bool, cor RGBA, texto, texto multilinha, enum, fonte, arquivo, custom —, faixa, padrão,
  `enabledIf`). O `Clip` guarda os valores num `ParamValues` (JSON id → valor), que persistem com a fonte fechada
  e vão para o `.bslstage` (`"params"` por clip). A fonte recebe uma cópia antes do `open()` e a cada mudança
  (`ISource::setParams` → `onParamsChanged`) e lê com `ParamSet` (valor do clip ou padrão, com clamp).
  O `ClipPanel` desenha qualquer lista (`ui/panels/ParamWidgets`); editores especiais entram por `ParamType::Custom`.
- **Registro** (`core/Generators.h`): `id` (`gen:padrao-de-teste`, `gen:cor-solida`, `gen:texto`,
  `gen:captura-de-tela`, `gen:placa-de-captura`), nome, `live`, `describe(valores)`, `create(w, h)`, nome do clip.
- **Texto**: `TextRaster` (stb_truetype, sem GL) gera uma "faixa" RGBA só quando um parâmetro muda; `TextSource` a
  desenha no canvas com deslocamento de UV. No letreiro a faixa tem um período (texto + espaço) e repete com
  `GL_REPEAT`: loop sem emenda e sem re-rasterizar. Fontes: `FontCatalog` (assets/fonts + pasta de fontes do
  Windows; lê só a tabela `name`, em thread de fundo).
- **Captura de tela** (`ScreenCaptureSource`): Windows Graphics Capture (C++/WinRT, frame pool *free-threaded*
  consumido na thread principal) para monitor/janela; fallback DXGI Desktop Duplication (só monitor, sem cursor).
  Janela fechada/reaberta é reencontrada por exe + título parecido (`media/CaptureTargets`); as janelas do próprio
  BSL Stage ficam fora da lista.
- **D3D11 → GL** (`render/D3D11Interop`, reutilizável pela Fase 3B): `WGL_NV_DX_interop2` com fallback automático
  por CPU (staging → `glTexSubImage2D`). O device D3D11 é criado no adaptador do mesmo fabricante do `GL_VENDOR`
  e o interop é confirmado com um registro real; o caminho usado vai para o log.
- **Placa de captura / webcam** (`CaptureDeviceSource`): FFmpeg `dshow` via `VideoDecoder` em modo ao vivo (`fflags
  nobuffer`, sem frame threading, fila que descarta os frames atrasados, `tryGetLatest`). Abrir/reabrir roda em
  thread de fundo; sem sinal = textura preta + "Sem sinal", nova tentativa a cada 2 s. `DeviceCatalog` lista os
  dispositivos e os formatos (`-list_options`) em segundo plano.
- **Stream de rede** (`gen:stream-de-rede`, `media/NetStream.*` sem GL + `media/NetStreamSource.*`): puxa um link (rtmp, rtmps,
  rtsp, srt, udp, http/https) ou escuta RTMP/SRT (`listen=1` / `mode=listener`, em `0.0.0.0`). Uma thread por fonte abre, lê e
  decodifica; **toda** chamada bloqueante do avformat tem `AVIOInterruptCB` com flag de parada e prazo (abrir 10 s, probe 8 s,
  leitura 5 s; 20 s em HLS), então fechar o clip leva menos de 0,5 s (o `StreamWorker` solta a thread se ela demorar mais que
  250 ms). Baixa latência: `fflags nobuffer`, `probesize`/`analyzeduration` pequenos (com segunda tentativa maior se faltar vídeo),
  decodificação com `low_delay` e fatiamento, descarte até o primeiro keyframe e um único quadro pendente (troca de buffers) para a
  textura. Reconexão com espera 1, 2, 4, 8 e 10 s; no modo receber volta a escutar. A porta é conferida antes (um `bind` sem
  `SO_REUSEADDR`) para dar "porta em uso" em vez de roubá-la. Só vídeo (sem áudio). Detalhes: `docs/STREAM.md`. Teste:
  `bslstage_streamtest`.
- **Exclusividade**: fonte ao vivo (`isLive()`) nunca tem segunda instância; a Prévia mostra a instância ativa ou
  um aviso. As células de geradores mostram miniatura vinda da fonte ativa (1x/s, `render/LiveThumb`).
- Testes: `bslstage_livetest` (texto, letreiro, cor, D3D11→GL, captura, webcam), `bslstage_projecttest` (inclui
  `params`). Argumentos de teste novos: `--trigger <camada> <coluna>`, `--choose-target`.

### PDF, Office e Web num processo auxiliar (Fase 3B)

- **Um `bslstage_worker.exe` por clip** de documento/web, iniciado quando o clip é disparado e encerrado quando ele para.
  `media/WorkerHost` (lado do app) roda numa thread própria: cria o pipe, inicia o worker, vigia e relança. A UI nunca
  espera por ele. O worker entra num **job object com KILL_ON_JOB_CLOSE** (morre se o app morrer) e também vigia o
  processo pai.
- **Controle**: named pipe `\\.\pipe\bslstage-<pid>-…` com JSON por linha (`media/WorkerProtocol.h` documenta as mensagens:
  `open`, `params`, `page`, `resize`, `url`, `reload`, `mouse`, `wheel`, `key`, `ping/pong`, `quit` e, de volta, `hello`,
  `transport`, `tex`/`shm`, `frame`, `doc`, `status`, `error`, `log`). Heartbeat de 1 s; 12 s sem nenhuma mensagem =
  travado (o worker é encerrado e relançado).
- **Quadros** (escolha automática, registrada no log: `Worker: quadros por textura D3D11 compartilhada (adaptador: …)` ou
  `… por memória compartilhada (motivo)`):
  1. **Textura D3D11 compartilhada**: duas texturas BGRA com NT handle e keyed mutex, criadas no adaptador (LUID) do
     contexto GL do app (Optimus) e duplicadas para o processo do app. O host copia cada quadro para uma textura
     "caixa de correio" no device do app e a thread GL a leva à textura GL por `render/D3D11Interop`
     (`WGL_NV_DX_interop2`, com cópia por CPU de reserva dentro do próprio `D3D11Interop`).
  2. **Memória compartilhada**: mapeamento com cabeçalho + dois buffers RGBA (alpha reta), seqlock e a mensagem `frame`
     como evento. Entra sozinha quando o D3D11 não existe, o adaptador não bate ou o app não consegue abrir a textura
     (o app responde `transport: shm` e o worker troca na hora).
- **Cai, trava ou termina**: a fonte mantém o último quadro na tela, mostra "Reiniciando… (tentativa n/3)" e o host relança
  com espera de 1 s, 2 s e 4 s; depois disso "Falhou: <motivo>". Erro fatal do worker (arquivo inválido, Office ausente…)
  não relança.
- **PDF**: PDFium no worker (binários bblanchon/pdfium-binaries baixados no configure, `third_party/pdfium`). Renderiza a
  página atual no tamanho do canvas (papel branco encaixado sobre a cor de fundo) e pré-renderiza a anterior e a próxima.
  Parâmetros: página, cor de fundo, avanço automático (desligado / a cada X s / a cada X batidas do BPM da composição),
  voltar ao início no fim, transição corte/fade (o fade é feito no app, `media/FadeBlender`, misturando o quadro anterior
  com o novo). O ClipPanel mostra ◀ ▶ e "página X de N"; o menu da célula tem "Expandir páginas na linha".
- **Office** (pptx, ppt, docx, doc, xlsx, xls, odp, odt, ods): o worker converte para PDF numa thread (a célula mostra
  "Convertendo…") e depois trata como PDF. Ordem: Microsoft Office por COM (PowerPoint `SaveAs` PDF, Word/Excel
  `ExportAsFixedFormat`; invisível, sem alertas; o documento é sempre fechado; só encerra o aplicativo se foi o próprio worker
  que o iniciou, instâncias já abertas pelo usuário não são tocadas) → LibreOffice `soffice --headless --convert-to pdf` →
  "Instale o Microsoft Office ou o LibreOffice (gratuito) para abrir este arquivo". Cache em
  `%LOCALAPPDATA%\BSL Stage\cache\docs\` (hash de caminho + tamanho + mtime). Uma conversão por vez no sistema (mutex nomeado).
- **Web**: CEF off-screen (`windowless_rendering_enabled`, `shared_texture_enabled` quando o transporte é textura; se a
  textura do navegador não abre no adaptador do app, cai sozinho para `OnPaint` → cópia por CPU). Multiprocesso usando o
  próprio worker como subprocesso (`--type=…`); `libcef.dll` e `Resources` ficam em `build/Release/cef/` (carga atrasada: o
  worker de PDF não carrega o CEF). Cache e perfil em `%LOCALAPPDATA%\BSL Stage\cef\slot-N\` (uma vaga por instância
  simultânea). Parâmetros: URL, largura/altura (0 = canvas), zoom, FPS 30/60, fundo transparente, recarregar a cada N min,
  áudio mudo (padrão ligado), CSS extra. `.html`/`.htm` soltos abrem como `file://`. "Interagir" (ClipPanel) encaminha
  clique, rolagem e teclado da Prévia para a página.
  A opção CMake `BSL_CEF_DIR` aponta para a distribuição binária do CEF; sem ela a Web fica desabilitada (aviso na aba
  Geradores) e o resto compila. A GPU do navegador é fixada no adaptador do app com `--use-adapter-luid`.
- **Exclusividade**: PDF/Office/Web são fontes exclusivas, como as ao vivo: a Prévia mostra a instância ativa (ou a
  miniatura da página 1) e nunca abre um segundo worker. Miniaturas: `media/DocThumb` roda o worker em modo `--thumb` numa
  thread de fundo (a conversão do Office também sai dali).
- **Testes**: `bslstage_doctest` (PDF de 3 páginas escrito à mão, troca de página, transporte textura × memória, avanço
  automático por tempo/batidas, fade, redimensionar, worker morto à força, worker travado, falha definitiva depois de 3
  reinícios, miniatura, `.docx`/`.pptx` criados pelo próprio teste via COM e convertidos, cache, Web `file://` vermelho
  opaco / fundo transparente com alpha 0 / CSS injetado / URL por parâmetro / clique encaminhado, e a conferência de que não
  sobrou worker nem Office aberto). `bslstage_doctest --make-demo-docs <pasta>` gera `sample.pdf/.pptx/.html` para o `--demo`.

### Efeitos, efeitos nativos e ISF (Fase 4A)

Base genérica em `src/fx/`: a mesma interface serve aos efeitos nativos, aos shaders ISF e (Fase 4B) aos plugins FFGL.

- **`IEffect`** (`fx/IEffect.h`): `id()`/`name()`/`category()`, `paramDescs()` (os mesmos `ParamDesc` das fontes),
  `setParams(ParamSet)`, `process(const EffectContext&, GLuint inputTex, int w, int h, Fbo& output)`, `needsInput()`
  (false = gerador), `isLive()` e `status()`. `EffectContext`: tempo, dt, índice do quadro, BPM, batida contínua, fase da
  batida e do compasso, tamanho do canvas, `topOrigin`, `eco` e o `FboPool` para temporários. O efeito pode mexer em
  qualquer estado do GL (o `ChainRunner` restaura) e não deve lançar exceção: se falhar, copia a entrada e informa em `status()`.
- **`EffectInstance`** = dados serializáveis (`typeId`, `bypass`, `params` em JSON) + o `IEffect` criado sob demanda
  (`ensureEffect()`, no primeiro quadro; copiar a instância copia só os dados). `typeId`: `native:blur`,
  `isf:<caminho relativo à pasta ISF>`, depois `ffgl:<...>`. Tipo desconhecido é preservado no projeto (a UI avisa).
- **`EffectChain`**: lista de instâncias com `add`, `insert`, `remove`, `move`, `duplicate`, `setBypass`. Existe em
  `Clip::effects`, `Layer::effects` e `Composition::masterEffects`.
- **`EffectRegistry`**: catálogo de provedores (`IEffectProvider`: `prefix`, `list`, `info`, `create`, `describe`, `version`).
  Já traz `native:` e `isf:`; o FFGL (`ffgl:`) tambem, ver "Host FFGL".
- **Convenção de orientação**: as texturas de fonte e as cadeias do clip e da camada têm a linha 0 no TOPO da imagem
  (`topOrigin = true`); o canvas do Compositor e a cadeia do master têm a linha 0 na BASE (`topOrigin = false`). O efeito
  escreve na mesma convenção que recebeu. Os shaders nativos trabalham num espaço de imagem com y para baixo (o prelúdio
  traduz), então "Posição Y" e "Rotação" andam no mesmo sentido nas três cadeias (testado de ponta a ponta no `fxtest`).
- **Compositor** (`render/Compositor`): para cada camada ativa, cadeia do clip no tamanho da fonte (antes do encaixe) →
  encaixe no canvas num FBO próprio, só quando a camada tem efeitos → cadeia da camada no tamanho do canvas → blend. Depois
  de todas as camadas, cadeia do master e o fade. `render/FboPool` guarda FBOs por tamanho (ping-pong, sem alocar por
  quadro; entradas sem uso por 240 quadros são liberadas). Efeito em bypass é pulado e cadeia vazia (ou toda em bypass) não
  custa nada. Perfil **Eco**: o desfoque e os ISF rodam em meia resolução e a cadeia do clip trabalha no máximo no tamanho
  do canvas. A Prévia roda uma cópia da cadeia do clip selecionado (os efeitos de execução do clip pertencem ao Compositor).
- **Efeitos nativos** (`fx/NativeEffects.cpp`, shaders próprios; parâmetros e rótulos em PT): Transformar, Ajuste de cor,
  Níveis, Desfoque (gaussiano separável, com redução de resolução para raios grandes), Chroma key, Luma key, Máscara,
  Recortar, Espelho, Caleidoscópio, Pixelizar (com modo painel LED), Strobe (BPM com divisão 1/4..4 e duty, ou Hz), Inverter,
  Vinheta e Deslocamento de cor. Alfa reto em todos; o desfoque trabalha pré-multiplicado.
- **ISF** (`fx/IsfDoc`, `IsfRenderer`, `IsfLibrary`, `IsfEffect`, `IsfSource`): spec aberta da Vidvox, parser e geração de
  código escritos do zero. Pastas: `Documents\BSL Stage\ISF` (criada no primeiro uso com os exemplos de `assets/isf/`) +
  pastas extras em `settings.json` (`isfFolders`); varredura em thread de fundo, "Recarregar" varre de novo e os efeitos já
  em uso recompilam se o arquivo mudou. O cabeçalho JSON vira `IsfDoc` (INPUTS float/bool/long/color/point2D/event/image,
  PASSES com TARGET/PERSISTENT/FLOAT/WIDTH/HEIGHT em expressões, IMPORTED, CATEGORIES, DESCRIPTION, CREDIT); `audio`,
  `audioFFT` vira uma textura 32 x 1 com o espectro (ver "Plugins do usuário"); `cube` e `audio` (forma de onda) viram aviso. O código é GLSL 3.30 core: preâmbulo com os uniforms (`PASSINDEX`, `RENDERSIZE`, `TIME`,
  `TIMEDELTA`, `DATE`, `FRAMEINDEX`, `isf_FragNormCoord`, cada imagem com `_<nome>_imgSize` e `_<nome>_flip`) e reescrita de
  `IMG_PIXEL`, `IMG_NORM_PIXEL`, `IMG_THIS_PIXEL`, `IMG_THIS_NORM_PIXEL`, `IMG_SIZE`, `gl_FragColor`, `texture2D` e
  `varying` (mantendo o número de linhas). Vertex shader padrão com `isf_vertShaderInit()`. O log do driver (NVIDIA e Mesa/AMD)
  é corrigido descontando as linhas do preâmbulo: o erro aparece com a linha do arquivo do usuário, no cabeçalho do efeito
  (painel Clip, aba Camada, aba Composição) ou na célula do gerador. Buffers de passe: `PERSISTENT` com dois FBOs
  alternados, `FLOAT` em RGBA32F. Com `inputImage` o ISF é efeito; sem ele é **gerador** (`isf:<arquivo>` como id do clip,
  seção ISF da aba Geradores). `point2D` = dois números arrastáveis, `color` = seletor de cor, `event` = botão (o
  shader vê verdadeiro por um quadro).
- **UI**: aba **Efeitos** (busca, categorias recolhíveis, Nativos / ISF / FFGL, Recarregar e Pastas ISF…;
  itens arrastáveis com payload `BSL_EFFECT`, duplo clique adiciona ao clip selecionado ou, sem clip, à camada). O widget da
  cadeia (`ui/panels/EffectChainWidget`, um só para Clip, Camada e Composição/master) tem alça para reordenar, recolher,
  ícone, bypass (olho), menu (Duplicar, Restaurar padrões, Remover), parâmetros pelo `ParamWidgets`, a área tracejada de
  soltar e o botão "+ Efeito" (catálogo com busca). Soltar um efeito numa célula adiciona ao clip; no cabeçalho de uma camada,
  à camada. Célula ou camada com efeitos mostram um pequeno ícone `sparkles` no canto.
- **Testes**: `bslstage_fxtest` (janela oculta): cada efeito nativo sobre uma textura conhecida (pixels esperados), orientação
  nas três cadeias, bypass igual à entrada, ordem importa, pool estável, Compositor com prévia reduzida e Eco, parser ISF (os
  4 shaders próprios + multipasso PERSISTENT, `long` com VALUES, erro de sintaxe com a linha certa, IMPORTED, event, point2D),
  expressões, macros e logs. `bslstage_projecttest` ganhou o caso com efeitos. `tools/uiqa` ganhou o cenário `efeitos`.

### Host FFGL 2.x (Fase 4B)

Plugins FFGL de terceiros (efeitos e fontes) carregados como DLL no processo, plugados no mesmo `IEffect`/`EffectRegistry`
(`ffgl:`) e na lista de geradores. Os plugins BSL **não** vão como DLL: viram efeitos e fontes nativos (etapa à parte).
Cabeçalhos do SDK (BSD) em `third_party/ffgl` (sem código do SDK no executável); código do host em `src/ffgl/`.

- **Descoberta segura** (`FfglPe`, `FfglCatalog`, `worker/FfglScan`): o filtro de PE lê só o cabeçalho do arquivo (x64 e
  exporta `plugMain`); DLL que não passa nunca é carregada (um `libcef.dll` de 300 MB, por exemplo). Cada DLL candidata é
  varrida num processo separado, `bslstage_worker --scan-ffgl <dll> <saida.json>`, com limite de 10 s; o JSON traz
  informações, tipo (efeito/fonte/mixer), versão da API, parâmetros (nome, tipo, padrão, faixa, grupo, elementos de option,
  extensões de file). Cache em `%LOCALAPPDATA%\BSL Stage\ffgl-cache.json` (caminho + tamanho + mtime). Travou ou caiu na
  varredura: a DLL vai para a lista de bloqueados com o motivo (Exibir > Plugins > "Tentar de novo"). A varredura roda numa thread
  de fundo: a UI nunca espera. O `WebBrowser.dll` nunca é varrido (a aba Efeitos sugere a fonte Web nativa).
- **Pastas** (`app/FfglSetup`): `Documents\BSL Stage\Plugins` (criada), pastas extras escolhidas pelo usuário (`ffglFolders` no `settings.json`). Varredura
  recursiva. "Importar plugins…" (`FfglImport`) **copia** uma DLL (ou pasta) para `Documents\BSL Stage\Plugins` com as
  subpastas de dependências ao lado (as que não têm outros plugins) e reescaneia; a origem nunca é alterada.
- **Execução** (`FfglInstance : fx::IEffect`): `LoadLibraryEx(LOAD_WITH_ALTERED_SEARCH_PATH)`; `FF_INITIALISE_V2` uma vez por DLL
  (e `FF_DEINITIALISE` quando a última instância sai), `FF_INSTANTIATE_GL` (viewport), `FF_SET_HOSTINFO` ("BSL Stage"),
  `FF_RESIZE` quando o tamanho muda, por quadro `FF_SET_BEATINFO` (BPM + fase do compasso) e `FF_SET_TIME`, e
  `FF_PROCESS_OPENGL` com a textura de entrada e `HostFBO`. Antes de cada chamada o estado do GL é o "padrão do FFGL"
  (FBO ligado e viewport, blend/depth/scissor/cull off, programa 0, VAO 0, unidade 0, sem texturas); depois é restaurado.
  Os plugins trabalham em **alpha premultiplicado** e com a **linha 0 na base**: o host converte a entrada e desfaz na saída
  (um passe de shader cada) e inverte na vertical quando `ctx.topOrigin`. O plugin desenha num FBO temporário do pool.
- **SEH** (`FfglCall.cpp`, sem objetos C++ com destrutor): toda chamada ao `plugMain` passa por `__try/__except`. Em falha a
  instância fica `faulted` (passagem direta, status vermelho "Plugin falhou: <código>"), vai para o log e o plugin é
  bloqueado na sessão (`Catalog::markSessionFault`) até o usuário reativar; as outras instâncias do plugin passam direto
  também. O show continua.
- **Parâmetros** (`FfglParams`): standard → float (faixa do `FF_GET_RANGE`; os valores trocados com o plugin são os reais, não
  normalizados), boolean → bool, event → botão (pulso 1 e volta a 0 no quadro seguinte), integer → int, option → enum com
  os nomes dos elementos, text/file → texto/arquivo (com as extensões), red/green/blue(+alpha) consecutivos → uma cor,
  hue/saturation/brightness(+alpha) → cor HSB (converte), xpos/ypos → point2D, grupos → seções da UI. A cada ~100 ms
  `pollParamChanges` lê de volta `FF_GET_PARAMETER`: o que o plugin mudou sozinho (os plugins com interface web) aparece na
  UI; não sobrescreve nos 400 ms depois de uma edição do usuário. Fontes do tipo FF_SOURCE são geradores (`FfglSource`,
  seção FFGL da aba Geradores); mixers aparecem como "não suportado ainda".
- **Provedor** (`FfglProvider`): typeId `ffgl:<id de 4 caracteres>:<nome do arquivo>`; ao abrir um projeto procura pelo id e
  depois pelo nome do arquivo. `describe()` vem do cache, sem GL. Plugin ausente: aparece em âmbar e os parâmetros são
  preservados no projeto.
- **UI**: aba Efeitos, seção "FFGL" (busca, categorias por origem, tooltip com descrição/autor/versão/caminho); janela
  Plugins (menu Exibir e botão da aba Efeitos): pastas, importar, reescanear e bloqueados.
- **Testes**: `bslstage_ffgltest` compila DLLs de teste (`tools/ffgl-test-plugins/`: dois exemplos do SDK, uma sonda
  derivada deles para orientação/batida/leitura de volta, e três DLLs que falham: violação de acesso no
  `FF_PROCESS_OPENGL`, trava no `FF_GET_INFO`, DLL sem `plugMain`). Opcional: `bslstage_ffgltest --scan <pasta>
  [--only arquivo.dll]` varre a pasta de plugins (só leitura), instancia cada plugin, processa 60 quadros e imprime a tabela.
  Observação: plugins que carregam um Chromium próprio (WebMotion e WebBackgrounds, assembly `WebBrowser.Cef`) não carregam sem
  a pasta `WebBrowser.Cef` ao lado e dois deles de pastas diferentes (duas cópias do `libcef.dll`) não convivem no mesmo processo.

### Plugins do usuário: Novo plugin, recarga automática e .bslplugin (item 11)

O cliente cria o próprio plugin pelo caminho fácil (ISF) ou pelo avançado (FFGL), ambos padrões abertos. Guia do usuário e referência: `docs/plugins/`. Para IAs: `AGENTS.md`, `docs/plugins/PARA-IA.md` e `skills/bsl-stage-plugin/`.

| Arquivo | Papel |
|---|---|
| `plugin/PluginTemplates` | Pedido do assistente (`PluginSpec`: nome, tipo, categoria, modelo, parâmetros) e geração do `.fs`. Seis modelos (em branco, cor/ajuste, distorção UV, feedback, padrão, reativo a áudio); parâmetro que o usuário tira do modelo vira `const` no código. Nomes de variável validados (ASCII, sem palavras do GLSL/ISF) |
| `plugin/PluginFiles` | Criar (arquivo gravado por troca atômica, nome único, imagens copiadas), duplicar (em outra pasta leva as imagens), nomes de arquivo seguros para o Windows |
| `plugin/PluginPackage` | `.bslplugin`: exportar (`.fs`, `.vs`, imagens do `IMPORTED`, `manifest.json`) e importar. A importação valida **antes** de gravar: caminhos (`..`, absolutos, `:`, `\`, nomes de dispositivo, profundidade), extensões permitidas (`.fs .vs .png .jpg .bmp .tga .txt .md` e só o `manifest.json`), conteúdo (MZ, ELF, PK, `#!`), tamanhos (24 MB o pacote, 16 MB cada arquivo, 48 MB o total, 64 arquivos, razão de compressão), repetidos, manifest (nome e licença obrigatórios) e se cada shader passa no leitor ISF. Leitura pelo `update::ZipReader` (inflate próprio) |
| `plugin/IsfCheck` | Motor do validador: mesmo `parseIsf` e `IsfRenderer` do app, desenha fora da tela (passes persistentes e `TIME` simulados quadro a quadro), erros com linha e texto da linha, resumo dos parâmetros, avisos de boas práticas, estatísticas do quadro, custo por quadro e PNG |
| `ui/panels/PluginWizard` | Janelas Novo plugin, Importar (mostra autor, versão e **licença**; só instala com o aceite) e Exportar; Abrir no editor (VS Code se existir, senão o programa associado a `.fs`, senão o Bloco de Notas), Mostrar na pasta e Duplicar |

- **Pasta "Meus plugins"**: `Documents\BSL Stage\Plugins` (a mesma dos plugins FFGL) passa a ser também uma pasta ISF (`app/IsfSetup`). É onde o assistente e o importador gravam; o assistente deixa escolher uma pasta ISF extra.
- **Recarga automática** (`fx/IsfLibrary`): `startWatcher()` roda uma thread que confere a cada 600 ms os carimbos (data e tamanho) dos `.fs` e `.vs` das pastas, sem ler o conteúdo. Mudança confirmada em duas conferências seguidas (para não ler um arquivo
  pela metade) pede uma varredura; a varredura **reaproveita** a entrada de quem não mudou (mesmo `doc`), então só o plugin editado recompila. `checkForChanges(true)` confere na hora (testes).
- **Erro sem perder o efeito** (`fx/IsfReload`): `IsfEffect` e `IsfSource` trocam o renderer por um novo só depois de compilá-lo (no contexto GL, dentro de `process`). Se o GLSL não compila ou o JSON ficou inválido, o renderer antigo continua e o erro, com a linha do arquivo
  (o mesmo `remapGlslLog` do ISF), vai para `status()` com o aviso "o arquivo foi alterado e tem erro; a versão anterior continua rodando". Os valores dos parâmetros ficam no `EffectInstance` e não mudam.
- **Áudio no ISF**: a entrada `audioFFT` vira uma textura RGBA16F de 32 x 1 com `AudioSnapshot::spectrum` (0 a 1, graves à esquerda), atualizada só quando o shader declara a entrada. `audio` (forma de onda) continua não suportado.
- **QA**: estado `state.plugins`; IDs `##menu_novo_plugin`, `##menu_importar_plugin`, `##plugin_novo`, `##plugin_importar`, `##plug_*` (assistente, importar e exportar), `##isf_ctx_{editor,pasta,duplicar,exportar}`. No `--qa` nada abre editor nem Explorer e os diálogos de arquivo são
  trocados por `BSLSTAGE_QA_PLUGIN_FILE` e `BSLSTAGE_QA_PLUGIN_EXPORT`.
- **Testes**: `bslstage_plugintest` (um plugin por modelo compila e renderiza não-preto; parâmetros e validações; recarga: edita, mantém valores, erro mantém o antigo e informa a linha 7, JSON inválido, arquivo novo e removido, vigia em thread; `.bslplugin` ida e volta e dezenas de pacotes
  maliciosos recusados sem gravar nada; `bslstage_isfcheck` contra os modelos da skill, os exemplos e todos os blocos de shader dos documentos; códigos de saída do executável), `bslstage_ffgltest` (a DLL de `sdk/ffgl-template`) e o cenário `plugins` do uiqa
  (`tools/uiqa/pluginshots.py` gera `docs/screenshot-novo-plugin.png`).
- **Modelo C++** (`sdk/ffgl-template`): `BSLExample.dll`, efeito FFGL 2.x com dois parâmetros, compilado pelo CMake (também sozinho, fora do repositório principal) com o FFGL SDK BSD de `tools/ffgl-test-plugins/sdk`.

### Threads

- **Principal**: UI + render GL (um contexto). Simples e previsível.
- **Decodificação**: uma thread por fonte de vídeo/documento ativa.
- **I/O**: thumbnails, carregamento de projeto.
- **Rede DMX** (`dmx/DmxNet`): uma thread só, quando o receptor está ligado. Nunca toca em GL nem em ImGui; a thread principal lê a
  foto com `try_lock` (não espera).
- **Processos auxiliares**: PDF, Office (conversão) e Web rodam em `bslstage_worker.exe`; no app só há a thread
  supervisora de cada `WorkerHost` (pipe, heartbeat, reinício, recebimento dos quadros).

### Perfis de desempenho

| Opção | Eco | Balanceado | Máximo |
|---|---|---|---|
| FPS da UI | 30 | 60 | vsync |
| Resolução do preview | 50% | 75% | 100% |
| Decodificação | reduz vídeo ao tamanho do canvas | hardware se disponível | hardware |
| Pré-carregar próximo clip | não | sim | sim |
| Thumbnails animados | não | não | sim |
| IA local (efeitos com selo "IA") | desligada | 15 fps de inferência por fonte | 30 fps |

### IA local opcional (`src/ai`)

Profundidade (Depth Anything V2 Small) e pessoa (MODNet) para os efeitos BSL Relight 2.5D e Cybernetic Body-Parts Glitch (e, na
parte 2, Aura e Z-Slice). ONNX Runtime + DirectML (GPU DX12 de maior desempenho; NVIDIA em notebook Optimus) com fallback de CPU; as
DLLs são carregadas em tempo de execução (sem elas, ou sem os modelos de `assets/models`, os efeitos mostram "IA indisponível" e
passam a imagem). Licenças e downloads em `docs/PATENTES-E-LICENCAS.md`.

- `AiRuntime` (sem GL): carrega `onnxruntime.dll`/`DirectML.dll` da pasta do exe pelo caminho completo, escolhe o adaptador
  (`EnumAdapterByGpuPreference`, sem WARP), cria o dispositivo DirectML e as `Session` (formas fixas 378x210 e 384x224).
- `AiEngine` (singleton): **uma thread própria**. A thread de render só reduz a entrada (16 amostras por pixel) para o tamanho do
  modelo, lê por PBO + `glFenceSync` (sem travar) e, quando a thread da IA entrega, sobe o resultado para texturas **R16F**
  (profundidade 0..1, maior = mais perto, normalizada por percentis suavizados e com suavização temporal) e **R8** (pessoa). Cache por
  fonte (`EffectContext::sourceId`: clip, camada ou master): efeitos da mesma cadeia dividem a inferência, que respeita a taxa do
  perfil (ou a de Exibir > IA). No Eco `acquire()` devolve `Off` sem tocar em GL nem carregar a DLL.
- Efeitos: `AiEffect` (`fx/bsl/BslCommon.h`) pede o resultado em `prepare()`; fora de `Ready` a imagem passa direto e `status()` diz por quê.
- Painel **Exibir > IA**: dispositivo, ms por inferência, fps de IA, modelos, taxa, dispositivo e botão "Testar".
- Teste: `bslstage_aitest` (precisa de `testmedia/person.jpg`, ver o doc de licenças).

**Performer Aura & Fluid** (`fx/bsl/PerformerAura.cpp`, pede só a máscara de pessoa). Por quadro, em FBOs RGBA32F a 1/8, 1/4 ou 1/2
da imagem: máscara suavizada → derivadas (Ix, Iy, It) → fluxo óptico de Lucas-Kanade de 1 nível (janela 5x5, movimento mantido e
esmaecido entre as atualizações da IA) → velocidade (advecção semi-lagrangiana, difusão, amortecimento; perto das bordas em movimento
ela é puxada para o movimento da borda, e a "espessura da aura" é o alcance disso) → densidade (advecção, dissipação exponencial,
injeção nas bordas que se movem). Máscara parada = sem injeção: tudo só decai. O desenho final tem aura/fumaça, partículas
(densidade pontilhada, uma passada) e grade de lasers deformada pela velocidade; os graves reforçam a força (`ctx.audio`).

**Z-Slice e roteamento dos slices** (`fx/bsl/ZSlice.cpp`, `render/ZoneMasks.*`, `output/OutputRenderer.cpp`). O Z-Slice da cadeia
da camada ou do master (`EffectContext::chain` / `layerIndex`) calcula pesos de 3 ou 4 zonas por limiares com suavização e publica
uma textura RGBA8 (R = Frente, G = Meio, B = Fundo, A = Zona 4, espaço do canvas) em `ZoneMasks`. O `Compositor` chama
`ZoneMasks::beginFrame()` antes de cada quadro: só vale a máscara publicada nele (efeito em bypass, removido ou sem IA pronta =
fonte sem máscara). Cada `Slice` tem `zFilter` (Todas, Frente, Meio, Fundo, Zona 4) e `zSource` (Master ou o índice da camada);
o shader do slice multiplica pelo canal da zona escolhida, e sem Z-Slice ativo o slice mostra tudo (a interface avisa). Com todos
em "Todas" o caminho é o de antes (uniform `uZone = -1`). O assistente "Rotear zonas para slices…" (`ui/panels/ZoneRouting.cpp`)
aplica várias zonas de uma vez e entra no desfazer do Mapeamento. Salvo no projeto (`zFilter`, `zSource` por slice, só se usados).

**Slice ligado a uma camada** (`core/Layer.h`, `output/OutputSetup.h`, `render/Compositor.*`, `output/OutputRenderer.cpp`). Cada
`Layer` tem um `id` estável (único na `Composition`, salvo no projeto junto com `nextLayerId`, que nunca reaproveita ids) e a flag
`sliceOnly` ("só nos slices vinculados": a camada não entra no canvas principal). Cada `Slice` tem `source` (Composição, o padrão, ou
Camada), `layerId` e `includeMaster`. O App pede ao `Compositor` as vistas que os slices usam (`collectLayerViews`: slices ativos das
Screens saindo e todos os da Screen aberta no editor) e o `Compositor::render` processa cada camada UMA vez por quadro (clip,
encaixe, efeitos da camada; os efeitos têm estado) e usa o resultado no canvas principal e nas vistas: cada vista é o blend da
camada sobre preto num FBO do pool, preso até o próximo quadro. Sem slice ligado a camada nada é alocado nem desenhado. "Incluir
efeitos do master" roda o master numa cópia da cadeia (`EffectChain::syncFrom`, como a Prévia), para não passar o master do canvas
duas vezes no mesmo quadro. Solo vale só entre as camadas do canvas principal; o bypass da própria camada deixa o slice preto; o
bypass do master só afeta vistas que incluem o master. O `OutputRenderer` escolhe a textura de cada slice (canvas ou vista); camada
removida ou sem clip tocando = o slice não soma nada (preto) e a interface avisa. Atalhos no Mapeamento: botão direito no slice
("Vincular à camada…", "Criar camada para este slice" = camada nova, só nos slices vinculados, com o nome do slice) e, no Pixel Map,
"Criar uma camada por painel". O desfazer do Mapeamento cobre o vínculo (não remove a camada criada).

Mais: botão **"Otimizar mídia"** que converte arquivos para HAP/HAP-Q (ou H.264 leve) com FFmpeg,
para quem tem PC fraco.

### Ações e atalhos de teclado (`src/control`, `src/ui/KeysUi.*`)

Sem ImGui nem janela no núcleo (testado por `bslstage_keystest`); o DMX (seção "DMX" abaixo) já dispara as mesmas ações e o MIDI
entra pelo mesmo caminho depois.

- **`ActionRegistry`**: ações nomeadas (`clip.trigger`, `column.trigger`, `layer.power`, `master.blackout`, `deck.next`,
  `bpm.tap`, `transport.toggle`, `doc.next`, `effect.bypass`, `ui.layoutPreset`, `project.save`...), com id estável, rótulo em
  português, grupo e os argumentos que usam (`ActionArgs`: camada fixa ou "selecionada", coluna, deck, valor, efeito).
  Ações de **alternar** (`get`/`set`) aceitam o modo segurar. As do aplicativo (salvar, modos, presets, Identificar) rodam por
  ganchos (`AppHooks`) que o `App` preenche. `targetOf` diz qual elemento a ação atinge (célula, coluna, camada...): é o que a
  interface usa para mostrar a tecla e dar o flash.
- **`KeyBinding`**: tecla + modificadores -> ação + argumentos, modo **pressionar** ou **segurar**, e a opção "global" (atalho
  do sistema). Duas camadas: **do aplicativo** (`settings.json`, `Settings::keys`: salvar, modos, presets de layout) e **do
  projeto** (`.bslstage`, `Composition::keys`, chave `"keys"`: disparos de clips e camadas; sem a chave = padrões). O do projeto
  vence para ações de show; o do app vence para ações do aplicativo (`resolveKey`). `.bslkeys` exporta/importa os dois.
- **`KeyDispatcher`**: do toque da tecla à ação. `gateAllows` bloqueia ações de show com campo de texto ativo, foco no editor de
  Mapeamento/Pixel Map, menu aberto ou gravação em curso (Ctrl+S e afins continuam valendo digitando). No modo segurar guarda o
  estado de antes e o restaura ao soltar (ou quando o foco se perde).
- **`KeyService`**: ganchos, TAP, despachante, atalhos do sistema (`SystemHotkeys`, `RegisterHotKey`, só enquanto a janela está
  fora de foco) e o registro do que disparou (flash). `ui/KeysUi`: despacho por quadro, editor (Exibir > Atalhos…), gravação, conflito,
  atalho rápido no botão direito (célula, camada, coluna, olho do efeito) e dicas/flash nos painéis.

Padrões: 1-9/0 = colunas 1-10; Q W E R T Y U I O P = células da camada selecionada; Espaço = tocar/pausar; Delete = limpar a camada
selecionada; B (segurando) = blackout; Shift+Delete = remover o clip da célula selecionada; Ctrl+S/Ctrl+Shift+S/Ctrl+O/Ctrl+N; F11 = tela
cheia; Alt+1..9 = presets de layout. O grupo **Exibir** do editor de atalhos traz, sem tecla padrão: `view.panel.<chave>` (alternar cada painel),
`view.lock` (bloquear layout), `view.showAll` (mostrar todos os painéis) e `view.resetLayout` (restaurar padrão); `view.fullscreen` e
`ui.layoutPreset` (preset N) são as que têm F11 e Alt+1..9. Os painéis vêm da tabela `ui/ViewState.h` (`view::kPanels`), então a lista de
ações acompanha a dos painéis sozinha; as ações rodam por ganchos (`AppHooks::panelVisible`, `setFullscreen`...) que o `App` preenche.

### Menu Exibir: painéis, janelas soltas e layout (`ui/ViewState.*`, `ui/ViewUi.*`, `ui/LayoutUi.*`)

O menu **Exibir** (antigo "Ver", sem duplicar itens: Layout, Tamanho da interface, Áudio, IA, Plugins, Compatibilidade de vídeo e Atalhos
continuam nele) deixa o usuário montar a tela como quiser:

- **Painéis**: uma caixa de seleção por painel do modo atual (Saída, Prévia, Composição, Clip, submenu Abas com Arquivos / Fontes / Efeitos /
  Composição / Camada / Saída, e Barra de status; no Mapeamento e no Pixel Map, os painéis deles). Todo painel passa por `beginPanel()`
  (`ui/Widgets.cpp`) que chama `viewBegin()`: liga o **X** da aba à visibilidade (fechar pelo X desmarca a caixa), aplica a trava e cuida de soltar/
  encaixar. Painel oculto não é desenhado (`panelVisible`): o vizinho ocupa o espaço, a Prévia oculta solta a fonte própria e a vista de camada
  que pedia ao Compositor, e o custo some. O `dockspace` guarda o lugar do painel oculto (o ImGui mantém o `DockId` da janela), então "mostrar"
  devolve cada um ao seu lugar. `ViewState` (`Settings::view`, chave `"view"` do `settings.json`) guarda `hidden`, `detached`, `locked`, o último
  nó de dock (`dockIds`) e o retângulo dele como fração da área dos paineis (`dockRects`); grava na hora que algo muda (os retângulos, 1,5 s
  depois de o splitter parar).
- **Soltar em janela** (submenu, vale para qualquer painel): usa os **viewports** do ImGui (`ImGuiConfigFlags_ViewportsEnable`, barra de título do
  sistema: maximizar, outro monitor, Alt+Tab). O painel sai do dock (`SetNextWindowDockID(0)`), vira uma janela do sistema com a classe
  `NoAutoMerge` (não "gruda" de volta só por ficar sobre a principal) e sem barra de título do ImGui; a nova janela recebe o foco (senão a principal,
  ainda ativa, ficaria por cima dela nos cliques e esconderia os menus). Desmarcar o item **encaixa de volta**: no último nó em que o painel esteve
  ou, se o nó sumiu (o painel era o único dele), dividindo o nó que mais cobre o retângulo de antes, do mesmo lado e no mesmo tamanho. Fechar a janela
  pelo X do sistema oculta o painel. O que está solto é lido de volta a cada quadro (`detached`) e fica em `settings.json`; a posição e o tamanho das
  janelas soltas ficam no `imgui.ini` (`ViewportPos`, `Pos`, `Size`), que o ImGui grava sozinho; ao abrir, uma janela solta que ficou fora de todos os
  monitores (monitor desligado) é trazida de volta (`rescueViewports`, a cada 30 quadros).
- **Contexto GL**: cada janela solta tem contexto próprio **compartilhado** com o da janela principal (o backend GLFW do ImGui cria com
  `share = principal`), o mesmo grupo das janelas de saída por monitor: as texturas dos monitores (Saída, Prévia, miniaturas) aparecem nelas. FBOs e
  VAOs não são compartilhados, e o backend OpenGL3 cria o VAO de cada janela. Depois de `RenderPlatformWindowsDefault()` o `App` restaura o
  contexto principal (`glfwMakeContextCurrent(backup)`) antes do `glfwSwapBuffers` e das capturas. As janelas soltas trocam buffers com
  intervalo 0; o vsync continua só onde já estava. Atalhos do sistema (`RegisterHotKey`) tratam uma janela solta com foco como "o app em foco".
- **Coordenadas**: com viewports ligados o ImGui trabalha em coordenadas da tela, não da janela. O arquivo solto do Explorer (`App::onDrop`) soma a
  posição da área útil da janela principal, e o `--qa` (`app/Qa.cpp`) subtrai a mesma origem de todos os retângulos do snapshot (o driver segue em
  coordenadas da janela principal).
- **Escala da interface**: `ui::dp()` é única para o app (Acompanhar a janela, Automático ou fixa). As janelas soltas usam a mesma escala da principal:
  o ImGui não escala nada por monitor (`ConfigDpiScaleViewports` desligado) e o framebuffer de cada janela é 1:1 com os pixels (GLFW 3.4, per-monitor
  DPI v2), então não há borrão nem clique deslocado em monitor de DPI diferente; o tamanho físico muda com o DPI do monitor, e em "Automático" a escala é a
  do monitor da janela principal. "Tamanho da interface" ajusta tudo junto.
- **Layout**: os layouts prontos (Padrão, Grade grande, Saída grande), **Salvar layout atual como…**, os presets do usuário (Alt+1..9),
  **Excluir layout** (pede confirmação), **Gerenciar…** e **Restaurar padrão** (refaz o layout do modo atual: tudo visível e encaixado; os layouts
  prontos fazem o mesmo). O preset guarda também os painéis ocultos (`hidden`; preset antigo, sem a chave, deixa tudo visível) e, pelo
  trecho do `imgui.ini`, as janelas soltas. Ao abrir o app a disposição vem do `imgui.ini` (o que o usuário deixou, inclusive ajustes depois do
  preset); só sem ele o último preset usado volta, e as opções da Prévia do preset continuam voltando.
- **Bloquear layout**: sem arrastar, redimensionar, desencaixar nem encaixar (`NoResize | NoUndocking | NoDockingSplit | NoDockingOverMe` no
  dockspace e `NoMove` nas janelas), e sem o X das abas, para um clique errado não derrubar um painel no show. Ocultar pelo menu continua valendo.
- **Tela cheia** (F11): janela principal sem borda do tamanho do monitor em que está, sem trocar o modo de vídeo (janelas soltas e saídas
  continuam por cima; Alt+Tab funciona). Sair devolve posição, tamanho e "maximizada". Não é restaurada ao abrir o app.
- **Janela principal**: posição, tamanho e "maximizada" também vão para o `settings.json` (a posição só vale se ainda cair em algum monitor).

Testes: `bslstage_uitest` (JSON do `State`, tabela de painéis, preset com ocultos, janela solta no trecho do `imgui.ini`), `bslstage_keystest` (ações
`view.*`, F11 padrão) e os cenários `exibir` e `exibir_reinicio` do uiqa (ocultar/mostrar por menu e pelo X, soltar e encaixar, trava, F11, preset,
Restaurar padrão; fechar e abrir o app de novo com o mesmo appdata, janela fora dos monitores). `tools/uiqa/docshots.py exibir` gera
`docs/screenshot-exibir.png`.

### Transform de camada e de clip (`core/LayerTransform.h`, `ui/panels/TransformEditor.*`)

O enquadramento da camada no canvas, Geometria pura em `core/LayerTransform.h` (sem GL nem ImGui):

- **O que é**: a "imagem da camada" é o quadro do tamanho do canvas que o Compositor já montava (clip com o encaixe Ajustar/
  Preencher/..., depois os efeitos da camada). O Transform leva esse quadro para uma **caixa**: posição `x, y` (canto superior esquerdo,
  px do canvas), tamanho `w, h` (0 = o do canvas, então "tela cheia" sobrevive a uma troca de canvas), `rotation` (graus, horário, em
  volta da **âncora** `anchorX/Y`, 0..1 da caixa) e **recorte** `cropL/T/R/B` (px da caixa: o resto não anda nem muda de escala).
  O padrão (tudo zero, âncora no centro) é a camada ocupando o canvas inteiro, exatamente como antes. `settled()` normaliza
  (`w == canvas` vira 0); `isIdentity()` ignora a âncora.
- **Onde mora**: `Layer::transform` e, opcional, `Clip::transform` com `Clip::ownTransform` ("o clip traz o próprio
  enquadramento"): enquanto o clip toca, o dele vence o da camada, que continua guardado. `Composition::transformRef(i)` devolve o
  que vale (é o que o editor mexe), `resetTransform`, `applyTransformPreset` e `layerAtPoint` (a camada de cima num ponto).
- **No render**: o Compositor aplica o Transform no passe de blend, **depois dos efeitos da camada e antes do blend** (opacidade e
  blend continuam onde estavam). O shader (`CompositeShaders.h`, `uXf*`) faz a conta inversa canvas -> quadro da camada, com
  antialias de 1 px nas bordas. Identidade = `uXf = 0`: o caminho de sempre, sem passe nem FBO extra.
- **Slices vinculados a camada** (`Slice::source = Camada`) recebem a camada **sem** o Transform de canvas, porque o slice faz o
  enquadramento dele (recorte de entrada + quad de saída). `Slice::applyLayerTransform` ("Aplicar transform da camada", desligado)
  liga o Transform; ele entra no pedido da vista (`LayerViewRequest::applyTransform`), então a mesma camada com e sem a opção são
  duas vistas.
- **Prévia em "Camada selecionada"**: pede ao Compositor uma vista da camada (`previewWantedLayerId` + `LayerViewRequest{id,
  master=false, transform=true}`), a mesma lógica de vista isolada dos slices; por isso agora mostra **os efeitos da camada** e o
  Transform. Não existe mais um passe de blend próprio da Prévia.
- **Editor** (`TransformEditor`): botão "Transformar" (ícone `move`) na Prévia e na Saída. Ligado, o monitor mostra a composição
  inteira e a caixa da camada selecionada: alças de canto/borda e rotação (`output/MapEdit`), ímã nas bordas/centro do canvas e das
  outras camadas (`output/Snap`), Shift mantém a proporção, Alt redimensiona a partir do centro (e desliga o ímã), modo **Recortar**,
  clique numa área seleciona a camada de cima ali (Ctrl+arrastar mexe na selecionada mesmo coberta), duplo clique reseta, setas
  movem 1 px (Shift 10), menu de **presets** (Tela cheia, Ajustar, Preencher, metades, quadrantes, Grade 2×2 com as camadas 1-4 e
  3×3). A aba Camada tem X, Y, L, A, escala, rotação, âncora e recorte em hot numbers ligados ao mesmo Transform.
- **Desfazer**: histórico **próprio** (`core/TransformHistory`: fotos dos Transforms de camadas e clips, um passo por gesto, a grade
  inteira é um passo; Ctrl+Z / Ctrl+Y nos monitores e na aba Camada). Não foi integrado ao do Mapeamento porque aquele guarda o
  `OutputSetup` (telas e slices) e desfazer lá não pode mexer em camadas; o Transform mora na `Composition`. O comportamento é o
  mesmo (`observe` por quadro, gesto pendente enquanto o mouse está apertado ou há texto em edição).
- **Projeto**: `layers[].transform`, `clips[].ownTransform/transform` e `slices[].applyLayerTransform` só entram no arquivo quando
  diferem do padrão; projetos antigos abrem iguais. Atalhos: `layer.transform.reset` e `layer.transform.preset` (N = o preset).
- **Atalhos por id**: `ActionArgs::layerId` guarda o `Layer::id` estável (reordenar/remover camadas não desloca o atalho; com a
  camada removida ele não faz nada). Projetos antigos (índice) migram ao abrir (`control::stampLayerIds`). Colunas continuam por
  índice (colunas não têm id).

Testes: `bslstage_transformtest` (identidade pixel a pixel, posição/escala/rotação/âncora/recorte, Grade 2×2, clip × camada, slices
vinculados, vista da Prévia com efeitos, escala reduzida, `layerAtPoint`, histórico), `projecttest` (ida e volta e migração) e `keystest`.
UI: cenário `transform` do uiqa (`tools/uiqa/README.md`).

### Sequências de slices (`src/seq`, `fx/bsl/SequenceFeed`, `fx/bsl/Seq*`)

Guia do usuário: `docs/SEQUENCIAS.md`. Sem GL nem ImGui no núcleo:

- **`src/seq`**: `SeqModel` (sequências, passos de 1 a 1024, visões; a visão "Tudo" é implícita), `SeqJson` (cada passo gravado como texto de faixas, `"1-5,9"`), `Envelope` e `EnvelopeStore` (curvas com tabela de 256 valores; biblioteca em `%APPDATA%\BSL Stage\envelopes.json`, gravação atômica; até 512), `SeqFactory` (gerador automático pela grade dos centros), `SeqEdit` e `SeqHistory` (edição e desfazer, teto 32 MB), `SeqTiming` (intensidades: velocidade, BPM ou passo fixo, direções, ecos, sustentação e liberação ou envelope, disparo, aleatórios, varredura, com relógio injetado) e `SeqView` (geometria do Canvas).
- **Slices estáveis**: cada slice tem `uid` (`ensureSliceUids` a cada quadro); as sequências guardam uids, e os uids órfãos ficam no projeto até "Religar slices". Chave `"sequences"` na `Composition`, gravada só se não estiver vazia; envelopes usados vão em `sequences.envelopes`.
- **`SequenceFeed`** (singleton, como o `PixelMapFeed`): `shared_ptr<const SequenceSnapshot>` com o conjunto, as tabelas de envelopes e uma versão; o `App` publica só quando algo muda. Os efeitos leem o snapshot por id (`sequencia`, `visao`, `envelope`); id inexistente passa a imagem direto e avisa em `status()`.
- **Efeitos**: `SliceInstancer` é a base (um quad por slice com retângulo, intensidade, índice e `vec4` extra; VBO por instância com `glVertexAttribDivisor`, GL 3.3; escala `w / compW` como no `SliceRefiner3`; respeita `topOrigin`). Cada efeito tem o próprio `Seq*.cpp`. `EffectSource` roda um `IEffect` sobre transparente e é o adaptador dos geradores (`bslGeneratorInfo`).
- **Testes**: `bslstage_seqtest` (sem GL: modelo, envelopes, gerador, edição, tempo) e `bslstage_seqfxtest` (janela oculta: intensidades 1, 0,5 e 0, três escalas, `topOrigin`, clip de tamanho diferente do canvas, 2000 slices sem erro de GL).

### DMX: Art-Net e sACN (`src/dmx`, `src/ui/panels/DmxWindow.*`)

Uma mesa de luz ou software de luz **controla** o BSL Stage por DMX (só entrada; a saída
Art-Net/sACN, por exemplo para o Pixel Map, ainda não existe). Núcleo sem GL nem ImGui, escrito do zero a partir das especificações
públicas (Art-Net 4, ANSI E1.31, GDTF DIN SPEC 15800). Guia do usuário: `docs/DMX.md`. Teste: `bslstage_dmxtest`.

| Arquivo | Papel |
|---|---|
| `DmxProtocol` | bytes de ArtDmx, ArtPoll, ArtPollReply (239 bytes) e do pacote de dados sACN; lista de universos |
| `DmxMerge` | `UniverseMerger`: fontes de um universo, HTP/LTP, prioridade do sACN, sequência, "stream terminated", timeout e política (manter ou soltar). O relógio entra como argumento |
| `DmxNet` | `Receiver` em Winsock: sockets UDP 6454 e 5568 (`SO_REUSEADDR`), multicast `239.255.h.l` por universo (16 por socket: o Windows limita as inscrições), thread de rede, `poll()` sem bloquear, ArtPollReply "BSL Stage" e a lista de placas (`GetAdaptersAddresses`) |
| `DmxMap` | modelo salvo no projeto: blocos do patch (`Block`: camada por `Layer::id` ou Master, personalidade, universo, endereço, canais FX) e canais aprendidos (`Map`), contas dos canais (8 e 16 bits), tabelas de canais, JSON e migração |
| `DmxEngine` | aplica os níveis na `Composition`: o mesmo caminho do patch e do learn, com as ações pelo `ActionRegistry` e o `KeyDispatcher` |
| `DmxService` | liga o `Receiver` ao motor (reinicia quando a configuração ou os universos mudam) e expõe o estado à interface |
| `DmxExport` | GDTF (`description.xml` num ZIP próprio sem compressão), CSV e HTML das tabelas de canais, pasta completa |

- **Rede**. Desligado por padrão (abrir a porta pode acionar o Firewall do Windows). Com uma placa escolhida liga em `0.0.0.0` (o único jeito de
  receber broadcast e multicast no Windows) e filtra pela sub-rede da origem, e entra no multicast só nessa placa; o loopback liga só em
  `127.0.0.1` (testes). Art-Net: universo do BSL Stage = Port-Address + 1 (ou + 0, opção "Art-Net 1"); sACN: o próprio número. A configuração é por máquina
  (`settings.json`, chave `dmx`: placa, universos, protocolos, HTP/LTP, timeout, política de perda, prioridade do Art-Net, nome do nó).
- **Thread principal sem espera**. `Service::update` roda uma vez por quadro antes de `Composition::update`: `Receiver::poll` usa `try_lock` (se a thread de rede
  estiver escrevendo, vale a foto anterior) e `Engine::apply` aplica os níveis. A thread de rede só mexe nos mergers e nos contadores (pacotes/s a cada 1 s).
- **Patch (personalidades)**. Camada: Básico (opacidade, clip), Padrão (+ velocidade, blend), Completo (+ posição X/Y, escala, rotação) e Completo 16 bits
  (transform em dois canais), mais até 16 canais "Efeito n" (cada um liga a um parâmetro de um efeito da camada). Master: opacidade, blackout, coluna global. O canal de clip
  usa faixas de 10 valores (0-9 nada, 10-19 limpar, 20-29 coluna 1...), o blend faixas de 28. Neutro = meio do canal (posição 0, escala 100%, rotação 0, velocidade 1x).
  O transform escreve em `Composition::transformRef`: posição e escala valem em volta do centro atual da caixa, então não há estado oculto.
- **Regras do motor**. Um valor só é aplicado quando o canal **muda** (editar na interface vale até a mesa mexer de novo). O primeiro valor visto é aplicado no patch
  (a mesa manda) e é só ponto de partida nos canais aprendidos e nos disparos (abrir o projeto com a mesa numa faixa não dispara nada). Sem sinal: "manter" não muda nada;
  "soltar" faz o patch voltar ao neutro de cada canal (opacidade 0, master 100%) sem disparar clips.
- **DMX learn e ações**. Botão direito (`hotDmx`/`dmxParamMenu` nos campos numéricos, `dmxActionMenu` nas células, colunas, camadas e no olho do efeito),
  "Atribuir DMX…", e o primeiro canal que andar 4 passos vira o canal (`Engine::learnBegin`, os mapas ficam suspensos enquanto aprende). Parâmetro contínuo (opacidade, blend, velocidade,
  transform, efeito de clip/camada/master, opacidade do master) ou **ação**: o mesmo par ação + argumentos de um atalho (`KeyBinding`, só ações de show), disparado ao cruzar o
  limiar (padrão 128, histerese de 6) pelo `KeyDispatcher::fireToken` (modo Pressionar ou Seguir, que desfaz ao descer; o token é o `Map::uid`, então soltar uma tecla não afeta o DMX).
  Camadas pelo `Layer::id` estável (`ActionArgs::layerId`).
- **Projeto**. Chave `dmx` no `.bslstage` (`blocks`, `maps`), só quando há algo; projeto antigo abre sem patch. Blocos e mapas que guardam o índice da camada
  (arquivo escrito à mão) migram para o `Layer::id` ao abrir (`dmx::stampLayerIds`, como `control::stampLayerIds`). Ação inexistente ou do aplicativo (salvar, abrir...) é descartada com aviso.
- **Exportar**. Pasta `GDTF` (um `.gdtf` por fixture: camada, com os 4 modos básicos mais os usados no patch, e Master) e pasta `Tabelas` com `canais.csv` (um canal por linha), `patch.csv`,
  `canais.html` (imprimir como PDF) e um LEIA-ME.
- **UI**. Janela flutuante "DMX / Art-Net" (menu Saída): abas Monitor (universos: estado, pacotes/s, origem e IP, último quadro; monitor dos 512 canais com o uso do patch),
  Patch (blocos, canais aprendidos, tabela de canais, Exportar perfis…) e Rede. Indicador **DMX** na barra de status (LED + estado, o clique abre a janela). Tema, ícones Lucide e `dp()`.
  Esc fecha a janela. `--dmx-demo <ip>` (só com `--qa`) liga o receptor nesse IP, cria um patch de exemplo e abre a janela (screenshot e testes).
- **Testes**. `bslstage_dmxtest` (pacotes em memória e por loopback UDP em `127.0.0.1`, mistura, timeout, receptor, serviço, personalidades, 16 bits, limiar, learn, projeto, GDTF/ZIP/CSV/HTML;
  `--multicast` tenta o multicast real na primeira placa; `--send <ip> <s>` só envia dados de exemplo). uiqa: cenário `dmx` (janela, ligar no loopback, patch automático, Art-Net real por UDP, learn pelo botão direito) e
  `tools/uiqa/dmxshot.py` (gera `docs/screenshot-dmx.png` e `docs/screenshot-dmx-patch.png` com dados reais por loopback).

### Atualizações e reportes (`src/update`, `src/ui/panels/UpdateUi.*`, `ReportWindow.*`)

Item 10 de `docs/Melhorias.md`: atualização "sutil" (o cliente atualiza se quiser) e reporte de problemas/ideias, ambos pelo GitHub, **sem servidor, conta
nem token no programa**. Núcleo em `bslupdate_core` (sem GL nem ImGui), testado por `bslstage_updatetest` sem rede. Guia de publicação: `docs/RELEASE.md`.

| Arquivo | Papel |
|---|---|
| `app/Version.h.in` | gera `Version.h`: `BSLSTAGE_VERSION` (de `project(VERSION)`, o único lugar da versão) e `BSLSTAGE_GITHUB_REPO` (variável de cache do CMake, hoje o provisório `bsl-light/bsl-stage`). Aparece em Ajuda > Sobre |
| `SemVer` | semver (aceita `v`, ignora build, pré-release menor que a estável, identificadores numéricos × alfanuméricos) |
| `Release` | JSON da API (`/releases/latest` e lista), escolha do asset pelo nome (`BSL-Stage-<ver>-win64.zip`; nunca `src`/`updater`; instalador `.exe` como alternativa), `SHA256SUMS.txt`, URL de download só do GitHub, nome de arquivo seguro |
| `Policy` | `Prefs` (`Settings::update`: verificar sozinho, beta, versão pulada, última verificação) e as regras: uma vez por dia (relógio que anda para trás conta), nunca com saída ligada / F11 / layout bloqueado / Eco, "pular versão" |
| `Http` | `IHttp`: WinHTTP (sem dependência nova; https, proxy do sistema, sem cookies nem autenticação, timeouts curtos, só `User-Agent: BSL-Stage/<versão>`) e `FakeHttp` (testes e `--qa --fake-release`) |
| `Service` | threads próprias: verificar, baixar (`.part` → SHA-256 → renomear), cancelar, confirmar sem checksum, estado persistido (`last-release.json`, `pending.json`, `last-result.json` em `%LOCALAPPDATA%\BSL Stage\updates`) e `launchInstallerOnExit` |
| `Zip` | leitor de ZIP com inflate próprio (RFC 1951), ZIP64, CRC-32 e proteção contra *zip slip* |
| `Apply` + `updater_main` | `bslstage_updater.exe`: espera o app fechar, extrai o pacote inteiro para uma pasta de preparo (recusa nome inseguro, CRC errado, pacote sem `BSLStage.exe`), guarda cada arquivo trocado em `backup\<versão>`, troca só o que existe no pacote e, **se qualquer passo falhar, restaura tudo**; arquivos do usuário (`settings.json`, `imgui.ini`, `*.bslstage`, `*.bslkeys`) nunca são sobrescritos; nada é apagado. Reabre o app |
| `Markdown` | notas do release em blocos de texto (títulos, listas, citação, código, regra) |
| `IssueReport` | corpo do issue (mesmas seções de `.github/ISSUE_TEMPLATE`), URL-encoding, corte da URL em 7000 caracteres (o que não cabe vai para a área de transferência), log sem usuário/pasta pessoal |

- **Quando verifica** (`App::updateTick`): ~8 s depois de abrir, só se a opção estiver ligada, passou 1 dia da última verificação que deu certo e não há show
  (saída ligada, F11, layout bloqueado, perfil Eco); senão reavalia depois (5 min / 1 h). Sem internet: nada na tela, só uma linha no log, nova tentativa em 6 h.
  Nos testes (`--qa`) e nas capturas de tela o transporte é sempre um `FakeHttp`: nunca há rede.
- **Aviso**: ponto + "Atualização 1.2.0 disponível" na barra de status (clicável) e item em Ajuda; sem pop-up. A janela mostra as notas e **Baixar e instalar**,
  **Lembrar depois** (some da barra nesta sessão), **Pular esta versão** (grava em `Settings`), as preferências e a data da última verificação.
- **Instalar**: o download pronto instala quando o app fechar (o `pending.json` sobrevive a um fechamento) ou em **Reiniciar agora** (só habilitado sem clip tocando nem
  saída ligada; se o usuário cancelar o "salvar alterações?", o reinício é cancelado). O atualizador roda uma cópia de si mesmo fora da pasta do programa e pede UAC só
  se a pasta não for gravável.
- **Reporte**: Ajuda > Reportar problema… / Sugerir melhoria… (`ReportWindow`): título, descrição, passos e as caixas "informações do sistema" (versão, Windows, GPU, driver
  OpenGL, perfil, RAM) e "últimas linhas do log" (40 linhas, sem nome de usuário nem pasta pessoal), ambas desmarcadas por padrão. A prévia é o texto exato. "Abrir no GitHub"
  usa `ShellExecute` em `https://github.com/<repo>/issues/new?title=…&body=…&labels=bug|enhancement`; "Copiar" copia o texto. Nada é enviado pelo programa.
- **Estado do QA** (`state.update`, `state.report`) e IDs: `##menu_atualizar`, `##menu_atualizacao_nova`, `##menu_reportar`, `##menu_sugerir`, `##atualizacao_aviso`,
  `##atualizacao_{baixar,lembrar,pular,auto,beta,procurar,cancelar,reiniciar,descartar}`, `##reportar_{titulo,descricao,passos,sistema,log,abrir,copiar,fechar}`.
  No `--qa` o botão "Abrir no GitHub" não abre o navegador (grava `state.report.lastUrl`). Cenário `atualizacao` do uiqa; `tools/uiqa/updshots.py` gera
  `docs/screenshot-atualizacao.png` e `docs/screenshot-reportar.png` com um release falso (`--fake-release`, fixtures em `tools/fixtures/update`).
- **Testes**: `bslstage_updatetest` (semver, JSON dos fixtures, escolha do asset, SHA-256 com os vetores oficiais, ZIP com deflate real, regra de uma vez por dia e de pular
  versão, serviço completo com `FakeHttp`: verificar, beta, 404, sem internet, baixar, checksum errado, sem checksum, cancelar, URL fora do GitHub; Markdown; URL do issue e
  corte; privacidade do log; atualizador com backup, rollback por falha simulada e por arquivo em uso, pacotes maliciosos recusados).

### Licenças, privacidade e termos (`src/legal`, `src/ui/panels/LegalWindow.*`)

Item 5 de `docs/Melhorias.md`. Como o programa é open source, o conjunto é **LICENSE (GPL-3.0) + avisos de terceiros + política de privacidade + aviso de
"sem garantia"**; não há EULA proprietário. Os documentos ficam na raiz do repositório e **acompanham o programa** (o CMake os copia para a pasta do
executável e o `release.yml` os põe no `.zip`):

| Arquivo | Papel |
|---|---|
| `LICENSE` | texto integral da GPL-3.0 |
| `THIRD-PARTY-NOTICES.md` | uma seção `### Nome` por componente de terceiros (FetchContent do CMake, binários em `build\Release`, `assets/`, bundle web), com as linhas `- **Versão:**`, `**Licença:**`, `**Copyright:**`, `**URL:**`, `**Texto da licença:**` (`` `licenses/x.txt` ``), `**Uso:**` e, se algo faltar, `**Situação:** PENDENTE…`. O formato é lido pelo programa: não mude os nomes das chaves |
| `licenses/*.txt` | textos completos das licenças, copiados dos arquivos que as próprias dependências distribuem (nada escrito de memória). O que não estava no disco está como PENDENTE no `THIRD-PARTY-NOTICES.md`. O `CREDITS.html` do CEF (~9 MB) vai para `licenses/cef-CREDITS.html` só na pasta de saída, quando o CEF é compilado |
| `PRIVACIDADE.md` | offline, nenhuma telemetria, única conexão automática (busca de atualização no GitHub: o que vai, frequência, como desligar), reporte só pelo navegador, servidores locais (127.0.0.1; "Controle remoto pela rede" escuta em todas as interfaces e vem desligado), DMX desligado, onde ficam configurações, logs e projetos |
| `TERMOS-DE-USO.md` | resumo da GPL-3.0, "sem garantia" (seções 15 e 16), marca (nomes e logo não são cobertos pela GPL), patentes H.264/HEVC, marca NDI, e a nota "não é aconselhamento jurídico; revisar com advogado antes da 1.0" |

- **Código** (`legal/Notices.*`, sem GL nem ImGui): `kNoticeVersion` (versão dos avisos, mostrada no rodapé da janela), o parser de `THIRD-PARTY-NOTICES.md`, `safeLicensePath` (só
  `licenses/...`, sem `..`), `readTextFile` (tira o BOM), `findDocsRoot`/`docsRoot` (ao lado do exe e, no desenvolvimento, até 5 pastas acima, até a raiz do projeto) e
  `wrapText` (quebra os textos longos em UTF-8 para a caixa rolável, que usa `ImGuiListClipper`).
- **Interface** (`LegalWindow`): Ajuda > **Licenças e avisos…** (abas Licença com a GPL resumida e "Ver texto completo", Terceiros com a lista rolável e o texto expansível de
  cada licença, Privacidade e Termos lidas dos `.md`); **Ajuda > Sobre** (copyright, GPL-3.0, "sem garantia" e o botão do código-fonte, `https://github.com/<BSLSTAGE_GITHUB_REPO>`, que a GPL
  exige). A tela de boas-vindas da primeira execução foi retirada do programa; o texto dela fica em `docs/instalador/AVISO-INSTALADOR.md`, para o instalador.
- **Telemetria do ONNX Runtime:** a build oficial para Windows liga por padrão eventos de telemetria da plataforma; `AiRuntime` chama `Env::DisableTelemetryEvents()` ao iniciar,
  para que "nenhuma telemetria" seja verdade.
- **Estado do QA** (`state.legal`) e IDs: `##menu_licencas`, `##sobre_{codigo,licencas}`, `##legal_aba_{licenca,terceiros,privacidade,termos}`, `##legal_{codigo,texto_completo,fechar}`. No `--qa` o botão do código-fonte não abre o navegador (grava `state.legal.lastUrl`).
- **Testes:** `bslstage_uitest` confere que `THIRD-PARTY-NOTICES.md` cita todas as bibliotecas do `CMakeLists.txt` (nomes e versões fixadas, inclusive os commits dos modelos), que cada
  entrada tem versão, licença, copyright e URL, que todo arquivo de `licenses/` existe, não está vazio e é citado, que os documentos não têm byte corrompido, o parser, o `wrapText`
  (UTF-8 nunca é cortado) e a leitura de settings.json antigo com `acceptedNoticeVersion` (ignorada). O cenário `licencas` do uiqa abre a janela e
  percorre as abas. `tools/uiqa/legalshots.py` gera
  `docs/screenshot-licencas.png`.
- **Decisões jurídicas** não tomadas ficam como PENDENTE nos próprios documentos (DirectML, React Bits, FFmpeg da BtbN, código-fonte do bundle web, política de marca, H.264/HEVC...).
