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
2. Cada clip aplica sua cadeia de efeitos (FBOs ping-pong).
3. Camadas são compostas de baixo para cima no canvas com blend mode + opacidade.
4. Efeitos master.
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
- Com alguma Screen saindo, o canvas é renderizado a 100% (a escala do perfil só vale para a prévia).
- Modo Mapeamento (`ui/panels/MappingPanels.cpp`): layout de dock próprio (dockspace separado, salvo à parte
  no `imgui.ini`) com Lista, Entrada, Saída e Propriedades; "Identificar" desenha borda, grade e nome dos
  slices nas saídas reais.

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

Mais: botão **"Otimizar mídia"** que converte arquivos para HAP/HAP-Q (ou H.264 leve) com FFmpeg,
para quem tem PC fraco.

## Engenharia reversa — regras

O Resolume serve só como referência de **comportamento**. Ver `docs/ENGENHARIA-REVERSA.md`.

## Roadmap

Ver `docs/ROADMAP.md`.
