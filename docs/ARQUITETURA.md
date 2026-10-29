# BSL Stage — Arquitetura

> Nome provisório. Software de VJ / mapping / painel LED, gratuito e open source (GPL-3.0).
> Inspirado na *ideia* do Resolume (clips, camadas, plugins, screens/slices), com código, visual e
> decisões próprias.

## Princípios

1. **Nativo antes de plugin.** Vídeo (incl. WEBM), imagem, GIF, PDF, documentos, texto, webcam e
   captura de tela são fontes nativas. Plugin é para efeito criativo, não para "abrir arquivo".
2. **Leve por padrão.** Só decodifica o que está tocando. Sem módulos pesados carregados sem uso.
   Recursos opcionais (Web, NDI) são módulos separados, carregados só quando usados.
3. **Roda em PC antigo.** Alvo mínimo: OpenGL 3.3 core, 4 GB RAM, GPU integrada Intel HD 4000+.
4. **Compatível onde importa.** Host FFGL 2.x (os plugins BSL e da comunidade funcionam), shaders
   ISF, Spout, OSC, MIDI, Art-Net. Importação do XML de Advanced Output do Resolume.
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
  output/     Screens, Slices, warp (4 pontos / malha), máscaras, soft-edge,
              janelas fullscreen por monitor, Spout out, mapa LED
  control/    MIDI, OSC, atalhos de teclado, mapeamento de parâmetros
  ui/         tema, painéis (Navegador, Grade de Clips, Camadas, Preview, Inspetor,
              Editor de Saída, Configurações)
  io/         salvar/abrir projeto (.bslstage), import XML Advanced Output
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
  imagem com programas de qualquer API (OpenGL, DirectX 9/11/12, Vulkan, WebGL). Ver Ver > Compatibilidade de vídeo
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
  `audioFFT` e `cube` viram aviso. O código é GLSL 3.30 core: preâmbulo com os uniforms (`PASSINDEX`, `RENDERSIZE`, `TIME`,
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
  varredura: a DLL vai para a lista de bloqueados com o motivo (Ver > Plugins > "Tentar de novo"). A varredura roda numa thread
  de fundo: a UI nunca espera. O `WebBrowser.dll` do Resolume nunca é varrido (a aba Efeitos sugere a fonte Web nativa).
- **Pastas** (`app/FfglSetup`): `Documents\BSL Stage\Plugins` (criada), pastas extras (`ffglFolders` no `settings.json`) e,
  opcional e desligado por padrão, `Documents\Resolume Arena|Avenue\Extra Effects` ("Usar plugins do Resolume"). Varredura
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
  Plugins (menu Ver e botão da aba Efeitos): pastas, Resolume, importar, reescanear e bloqueados.
- **Testes**: `bslstage_ffgltest` compila DLLs de teste (`tools/ffgl-test-plugins/`: dois exemplos do SDK, uma sonda
  derivada deles para orientação/batida/leitura de volta, e três DLLs que falham: violação de acesso no
  `FF_PROCESS_OPENGL`, trava no `FF_GET_INFO`, DLL sem `plugMain`). Opcional: `bslstage_ffgltest --resolume [pasta]
  [--only arquivo.dll]` varre o Extra Effects (só leitura), instancia cada plugin, processa 60 quadros e imprime a tabela.
  Observação: plugins que carregam um Chromium próprio (WebMotion e WebBackgrounds, assembly `WebBrowser.Cef`) não carregam sem
  a pasta `WebBrowser.Cef` ao lado e dois deles de pastas diferentes (duas cópias do `libcef.dll`) não convivem no mesmo processo.

### Threads

- **Principal**: UI + render GL (um contexto). Simples e previsível.
- **Decodificação**: uma thread por fonte de vídeo/documento ativa.
- **I/O**: thumbnails, carregamento de projeto.
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
  perfil (ou a de Ver > IA). No Eco `acquire()` devolve `Off` sem tocar em GL nem carregar a DLL.
- Efeitos: `AiEffect` (`fx/bsl/BslCommon.h`) pede o resultado em `prepare()`; fora de `Ready` a imagem passa direto e `status()` diz por quê.
- Painel **Ver > IA**: dispositivo, ms por inferência, fps de IA, modelos, taxa, dispositivo e botão "Testar".
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

Mais: botão **"Otimizar mídia"** que converte arquivos para HAP/HAP-Q (ou H.264 leve) com FFmpeg,
para quem tem PC fraco.

## Engenharia reversa — regras

O Resolume serve só como referência de **comportamento**. Ver `docs/ENGENHARIA-REVERSA.md`.

## Roadmap

Ver `docs/ROADMAP.md`.
