# Port dos plugins FFGL BSL para o BSL Stage

Documento de leitura do código-fonte dos plugins FFGL do usuário (`ffgl-master\source\plugins\`).
Base: leitura dos arquivos `.cpp`/`.h`/`.cpp` de shader, `MANUAL_DE_USO.md` (AlphaReactor, AudioReactiveTypo, DatamoshPixelSort) e `SPEC.md` (BSLSliceRefiner). O código não foi compilado nem executado; comportamentos descritos são os do código-fonte. Pastas `build*`, `lpng*` e exemplos do SDK foram ignoradas.

Convenções:
- "Tipo FFGL" usa os nomes do SDK: `FF_TYPE_STANDARD` (float 0–1 no host), `FF_TYPE_BOOLEAN`, `FF_TYPE_EVENT`, `FF_TYPE_TEXT`, `FF_TYPE_FILE`, `FF_TYPE_OPTION`, `FF_TYPE_HUE`, `FF_TYPE_SATURATION`, `FF_TYPE_BRIGHTNESS`.
- Efeito = `FF_EFFECT` (1 entrada de vídeo). Fonte = `FF_SOURCE` (sem entrada).
- Linhas citadas são do arquivo indicado na seção.

---

## 1. AlphaReactor

**Identificação**
- ID: `ALPR`
- Tipo: efeito (`FF_EFFECT`), 1 entrada
- Nome exibido: `BSL Alpha Reactor`
- Pasta: `source/plugins/AlphaReactor/` (arquivo principal `AlphaReactor.cpp`)

**O que faz**
Trabalha sobre a câmera já recortada (canal alpha, p.ex. uma remoção de fundo feita antes no host) e desenha, atrás do DJ, um halo de "god rays" (aura) que pulsa com os agudos, e rastros infinitos (feedback) cuja escala reage aos graves, com separação RGB opcional nos rastros. A câmera é composta por cima do feedback (over pré-multiplicado).

**Parâmetros**

| Nome | Tipo FFGL | Faixa / opções | Padrão | O que controla |
|---|---|---|---|---|
| Audio Source | STANDARD | `<0.34` loopback (desktop); `0.34–0.66` entrada (DJ/mic); `≥0.67` FFT do host | 0 (loopback) | Origem do áudio |
| Audio Gain | STANDARD | 0–1 → ganho `v*4+0.1` (0.1×–4.1×) | 0.5 (2.1×) | Ganho do áudio capturado |
| Noise Gate | STANDARD | 0–1 → gate `v*0.15` | 0.05 | Corte de ruído de fundo |
| Aura Intensity | STANDARD | 0–1 | 0.5 | Força dos raios de luz |
| Aura Length | STANDARD | 0–1 | 0.5 | Alcance dos raios (12 amostras) |
| Trail Length | STANDARD | 0–1 | 0.8 | Decaimento do rastro (`v*0.98`); 0 = sem rastro |
| Trail Scale | STANDARD | 0–1 | 0.5 | Zoom progressivo do rastro (0.5 = travado) |
| Trail RGB Split | STANDARD | 0–1 | 0.5 | Aberração cromática nos rastros |
| Bass React | STANDARD | 0–1 | 0.5 | Quanto o grave amplifica os rastros (×2) |
| High React | STANDARD | 0–1 | 0.5 | Quanto o agudo amplifica a aura (×2) |
| FFT (buffer, 3 elementos) | buffer `FF_USAGE_FFT` | — | — | Só usado quando Audio Source ≥0.67: elemento 0 = grave, elemento 2 = agudo (×5, clamp) |

**Dependências externas**
- Captura de áudio: sim, módulo compartilhado `AudioCapture` (ver seção "Módulos compartilhados"). Loopback WASAPI no endpoint de saída ativo (escolhido pelo maior pico) por padrão; entrada pelo endpoint de captura padrão; FFT radix-2 de 512 pontos. Diferença desta cópia: grava log de depuração em `C:\Users\GABRIEL - BSL LIGHT\Desktop\AlphaReactor_Audio.log` (modo append, a cada start).
- CEF / servidor HTTP / porta: não.
- Arquivos lidos: não.
- stb_image: não.
- COM / PowerPoint: não diretamente (WASAPI usa COM com `CoInitializeEx` MTA na thread de captura).

**Shaders**
- 1 passe. Vertex shader em `AlphaReactor.cpp:22–37`; fragment em `AlphaReactor.cpp:39–122`.
- Usa feedback: textura `m_lastFrameTex` recebe `glCopyTexSubImage2D` do viewport ao fim de cada frame e é lida no frame seguinte (rastros). Não é FBO explícito; é cópia do framebuffer.
- Aura: 12 amostras do alpha da entrada em escalas crescentes.

**Interface web**
Não possui.

**Estado / arquivos guardados**
- Nada persistido em disco (apenas o log de depuração citado acima).
- Em memória: `m_bassLevel`, `m_highLevel` (envelopes), textura de feedback, tempo.

**Dificuldade para reescrever no app: Média**
Shader simples de 1 passe (GLSL 410 precisa virar 330), mas depende do módulo de áudio (WASAPI + FFT) e de feedback por cópia de framebuffer.

---

## 2. AudioReactiveTypo

**Identificação**
- ID: `ARTY`
- Tipo: efeito (`FF_EFFECT`), 1 entrada
- Nome exibido: `BSL AudioReactive Typo` (descrição: "Audio-Reactive DJ Camera Typography & HUD Suite")
- Pasta: `source/plugins/AudioReactiveTypo/` (`AudioReactiveTypo.cpp`, 1021 linhas)

**O que faz**
Sobre a câmera do DJ, desenha tipografia cinética com dois textos (nome do DJ e legenda) em 4 modos (HUD Boiler Room, Kinetic Marquee, Glitch Cyber Typo, Typo Mask Cutout) e 4 modos de mesclagem (Difference/Invert, Alpha Overlay, Neon Glow/Add, Cutout Mask). Inclui HUD de transmissão (cantos de enquadramento, REC piscante, timecode, VU) e efeitos de câmera (kick shake, RGB split, P&B "rave", scanlines, vinheta e grão). Tudo reage ao áudio capturado.

**Parâmetros** (28 no total: 2 textos + 26 numéricos/booleanos)

| Nome | Tipo FFGL | Faixa / opções | Padrão | O que controla |
|---|---|---|---|---|
| DJ Text | TEXT | string | `DJ NAME` | Texto principal (GDI, fonte Impact, peso black) |
| Sub Text | TEXT | string | `LIVE STREAM // 140 BPM` | Legenda (GDI, Arial bold 68 px) |
| Mode | STANDARD | `0–0.24` Boiler Room HUD; `0.25–0.49` Kinetic Marquee; `0.5–0.74` Glitch Cyber Typo; `0.75–1` Typo Mask Cutout | 0 | Modo visual |
| Blend Mode | STANDARD | `0–0.24` Difference/Invert; `0.25–0.49` Alpha Overlay; `0.5–0.74` Neon Glow (Add); `0.75–1` Cutout Mask | 0 | Mesclagem da tipografia |
| Text Scale | STANDARD | 0–1 | 0.65 | Escala do texto |
| Position Y | STANDARD | 0–1 | 0.48 | Posição vertical |
| Text Tracking | STANDARD | 0–1 | 0.15 | Espaçamento entre letras (força re-render do texto) |
| Audio Source | STANDARD | `0–0.33` YouTube/Desktop (loopback); `0.34–0.66` Ext Mic/Line-In; `0.67–1` FFT do host (sliders) | 0 | Origem do áudio; no modo 3 usa Bass/High React como envelope direto, sem captura |
| Audio Gain | STANDARD | 0–1 → `v*4+0.1` | 0.5 | Ganho |
| Noise Gate | STANDARD | 0–1 → `v*0.15` | 0.05 | Corte de ruído |
| Bass React | STANDARD | 0–1 | 0.0 | Soma ao grave capturado (× Audio Sensitivity) |
| High React | STANDARD | 0–1 | 0.0 | Soma ao agudo capturado (× Audio Sensitivity) |
| Audio Sensitivity | STANDARD | 0–1 (mult.) | 1.0 | Multiplicador geral do áudio |
| Glitch Trigger | STANDARD | 0–1 | 0.0 | >0.05 força novo embaralhamento de texto (scramble) |
| Kick Shake | STANDARD | 0–1 | 0.45 | Intensidade do tremor no bumbo |
| RGB Split | STANDARD | 0–1 | 0.35 | Aberração cromática |
| Scroll Speed | STANDARD | 0–1 | 0.40 | Velocidade do marquee |
| BPM Sync | STANDARD | 0–1 | 0.5 | Sincronização de movimento com BPM (uso no shader) |
| Repetition Count | STANDARD | 0–1 → `clamp(v*8+2, 2, 10)` linhas | 0.35 (≈4 linhas) | Quantidade de faixas no Kinetic Marquee |
| Ghost Blur | STANDARD | 0–1 | 0.0 | Rastro temporal (feedback) |
| BW Rave Grade | STANDARD | 0–1 | 0.85 | Intensidade do P&B de alto contraste |
| Scanlines | STANDARD | 0–1 | 0.30 | Linhas tipo CRT |
| Vignette & Grain | STANDARD | 0–1 | 0.40 | Vinheta e grão |
| HUD Elements | STANDARD | 0–1 | 1.0 | 0 = só tipografia e câmera; 1 = HUD completo |
| Text Color R / G / B | STANDARD | 0–1 cada | 1.0 cada | Cor da tipografia |
| Master Invert | BOOLEAN | 0 / 1 | 0 (OFF) | Inversão geral da saída |

**Dependências externas**
- Captura de áudio: sim, módulo compartilhado `AudioCapture` (WASAPI loopback no endpoint de saída ativo, ou entrada; FFT 512; bandas graves/médios/agudos; kick; AGC). Modo 3 (FFT do host) não captura: usa os sliders Bass/High React como envelope. O display mostra o nível (`Bass: 85% [YOUTUBE]`).
- CEF / servidor HTTP / porta: não.
- Arquivos lidos: não.
- stb_image: não.
- COM / PowerPoint: não diretamente (COM só na captura WASAPI). Usa GDI (`CreateDIBSection`, `CreateFontA`, `DrawTextA`) para rasterizar texto, que é Windows-only.

**Shaders**
- 1 passe. Vertex em `AudioReactiveTypo.cpp:22–37`; fragment em `AudioReactiveTypo.cpp:39–421` (`main` em 165).
- Texturas de entrada: `InputTexture` (câmera), `TypoTexture` (texto renderizado por GDI, 2048×1024 RGBA), `FeedbackTexture` (ghost blur).
- Feedback: sim, por `glCopyTexSubImage2D` ao fim do frame (ghost blur).
- Modos e mesclagens são ramificações dentro do mesmo fragment shader.

**Interface web**
Não possui.

**Estado / arquivos guardados**
- Em memória: `m_djText`, `m_subText`, `m_params[28]`, envelopes de áudio, `m_scrambleSeed` (atualizado por transientes de agudo).
- Textura de texto (`m_textTexture`) só é re-renderizada quando texto, tracking ou scramble mudam (`UpdateTextTexture`, linha 590).
- Nada persistido em disco.

**Dificuldade para reescrever no app: Média**
Um shader só, mas exige substituir a rasterização GDI do texto (Windows-only) por uma solução do app e reaproveitar o módulo de áudio; são ~1000 linhas de C++ com 28 parâmetros.

---

## 3. BSLSliceRefiner

**Identificação**
- ID: `BSR3`
- Tipo: efeito (`FF_EFFECT`), 1 entrada
- Nome exibido: `BSL Slice Refiner` (about: "BSL LIGHT")
- Pasta: `source/plugins/BSLSliceRefiner/` (SPEC em `SPEC.md`)
- Saída prevista: `BSL LIGHT Slice Refiner.dll` + `control.html`

**O que faz**
Recorta o vídeo do clipe nos formatos dos slices de um XML de saída, com até 8 grupos. Cada grupo tem cantos arredondados, offset, união de slices (merge suave), borda com 12 efeitos (noise, spiral, radial, zebra, disco, chaser, dashes, rainbow, sparkle, glitch, pulse, scanner), blinker, bounce e rotação de cor. Toda a configuração é feita por um painel web (aba Slices do BSL Studio) via HTTP; o plugin serve o painel e a API, e persiste o estado em JSON. Recebe tempo e BPM do host (`SetTime`, `SetBeatInfo`).

**Parâmetros** (6, conforme `BSLSliceRefiner.cpp`, linhas 62–75)

| Nome | Tipo FFGL | Faixa / opções | Padrão | O que controla |
|---|---|---|---|---|
| Opacity | STANDARD | 0–1 | 1.0 | Opacidade do resultado sobre a entrada |
| Black BG | BOOLEAN | 0 / 1 | 1 | Fora dos slices: preto (1) ou transparente (0) |
| Master Speed | STANDARD | 0–1 → 0–2× | 0.5 (1×) | Velocidade global dos efeitos |
| Blinker | BOOLEAN | 0 / 1 | 0 | Força todos os contornos a piscar |
| Reload Slices | EVENT | — | — | Relê o XML de slices |
| Open Panel | EVENT | — | — | Abre `http://localhost:8083/` no navegador padrão (`ShellExecuteW`) |

**Dependências externas**
- Captura de áudio: não.
- CEF: não.
- Servidor HTTP: sim, próprio, Winsock puro, singleton por processo, thread própria. **Porta 8083** (se ocupada, registra aviso e tenta de novo). CORS `*`.
  - `GET /` ou `/control.html` ou `/index.html`: `control.html` (procura ao lado da DLL e em `web_app_bg\`); 404 se não achar.
  - `GET /api/slices`: JSON `{v, comp:{w,h}, slices:[{id,name,screen,x,y,w,h,angle}], state, bpm}`.
  - `POST /api/slices/state`: corpo = estado completo (JSON v1); valida, aplica e persiste; `{"ok":true}` ou 400 com `error`.
  - `GET|POST /api/slices/reload`: relê o XML e devolve o mesmo que `/api/slices`.
  - `GET /api/slices/screen`: JPEG ~640 px (GDI+), no máximo 10×/s; 204 se não houver quadro.
  - `OPTIONS`: 204.
- Arquivos lidos:
  - o XML de slices padrão da pasta Documentos do usuário (verifica mtime a cada ~2 s; limite 64 MB).
  - `state.json` na pasta de dados do plugin, em Documentos (limite 4 MB).
  - Hooks de teste: variáveis `BSL_STATE_DIR` e `BSL_SLICE_XML`.
- stb_image: não (JSON próprio em `Json.h`).
- COM / PowerPoint: não. GDI+ (`gdiplus.h`) para codificar JPEG.

**Shaders**
- 1 passe de desenho em tela cheia (`m_quad.Draw()` em `BSLSliceRefiner.cpp:400`).
- Vertex em `Shaders.h:4–21`. Fragment dividido em três partes: `kFsPart1` (`Shaders.h:26–132`: ruído, SDF de caixa, utilidades), `kFsPart2` (`Shaders.h:137–261`: `effectColor`, os 12 efeitos de borda), `kFsPart3` (`Shaders.h:266–436`: `main`, união de SDFs por grupo, borda, vídeo dentro do formato).
- Dados de grupos e slices vão por uniforms (`sl[]`, `gd[]`).
- Sem feedback. Para o preview, faz blit do FBO do host para `m_capture` (`BSLSliceRefiner.cpp:422–464`, `glReadPixels` para o JPEG).

**Interface web**
- Painel `control.html` (aba Slices do BSL Studio, em `BSL_Background_Studio/src/control/slices/`; não está neste repositório).
- Protocolo: as rotas da tabela acima. Estado JSON v1 com 8 grupos (`name`, `enabled`, `fill`, `masterSize`, `corners[4]`, `equalCorners`, `offset`, `merge`, `mergeSmooth`, `softEdge`, `stroke{width,mode,color,colorB,effect,speed,variation,smooth,symX,symY,glow,dual}`, `blinker`, `bounce`, `colorRotate`) e `assign` (sliceId → grupo 0..7). Detalhes em `SPEC.md`.

**Estado / arquivos guardados**
- `Core` (singleton intencionalmente vazado): `m_state`, `m_slices` (máx. 64), `m_groupOf`, versão compartilhada, mtime do XML, último quadro/JPEG.
- `State.cpp`/`State.h`: modelo do estado e (de)serialização; `SliceXml.cpp`: leitura do XML de saída.
- Persistência: `state.json` com escrita atômica (`state.json.tmp` + `MoveFileExW`), agendada ~0,5 s após mudanças.
- Por instância: fases de efeitos e bounce (`GroupPhase`), blink, `m_beats`.

**Dificuldade para reescrever no app: Alta**
Servidor HTTP próprio, parser XML e JSON, persistência, 8 grupos com SDF e 12 efeitos de borda num único shader e preview por JPEG. Reaproveitável só se o app já tiver mapeamento de slices e painel web.

---

## 4. DatamoshPixelSort

**Identificação**
- ID: `DMSH`
- Tipo: efeito (`FF_EFFECT`), 1 entrada
- Nome exibido: `BSL Datamosh & Pixel Sort`
- Pasta: `source/plugins/DatamoshPixelSort/`

**O que faz**
Combina pixel sorting (pixels escorrem a partir de um limiar de luminância, em horizontal, vertical ou diagonal), datamosh por feedback (mistura com o frame anterior, com tolerância controlada por decay), contorno neon via Sobel com cor RGB, máscara circular de proteção do DJ (raio, borda, posição) e ghost trails (blur temporal). Graves e agudos modulam limiar, comprimento e intensidade.

**Parâmetros** (19)

| Nome | Tipo FFGL | Faixa / opções | Padrão | O que controla |
|---|---|---|---|---|
| Audio Source | STANDARD | `<0.34` loopback; `0.34–0.66` entrada; `≥0.67` "FFT do host" (sem captura, ver observação) | 0 | Origem do áudio |
| Audio Gain | STANDARD | 0–1 → `v*4+0.1` | 0.5 | Ganho |
| Noise Gate | STANDARD | 0–1 → `v*0.15` | 0.05 | Corte de ruído |
| Sort Threshold | STANDARD | 0–1 | 0.70 | Limiar de luma para escorrer (o grave reduz esse limiar) |
| Sort Length | STANDARD | 0–1 | 0.0 | Comprimento do rastro do escorrido (reage a agudos) |
| Sort Direction | STANDARD | `<0.33` horizontal; `<0.66` vertical; `≥0.66` diagonal | 0 | Sentido do escorrido |
| Mosh Amount | STANDARD | 0–1 | 0.0 | Quanto do buffer anterior sobrevive |
| Mosh Decay | STANDARD | 0–1 → tolerância `0.02+v*0.3` | 0.5 | Tolerância do datamosh (valores altos esticam cores) |
| Protect Radius | STANDARD | 0–1 | 0.0 | Raio do círculo protegido (0 = desligado) |
| Protect Feather | STANDARD | 0–1 | 0.2 | Suavidade da borda do círculo |
| Protect X | STANDARD | 0–1 | 0.5 | Posição horizontal da máscara |
| Protect Y | STANDARD | 0–1 | 0.5 | Posição vertical da máscara |
| Neon Edges | STANDARD | 0–1 | 0.0 | Mistura do contorno Sobel (1 = fundo escuro, só contornos) |
| Neon Red | STANDARD | 0–1 | 1.0 | Cor do neon |
| Neon Green | STANDARD | 0–1 | 0.0 | Cor do neon |
| Neon Blue | STANDARD | 0–1 | 0.0 | Cor do neon |
| Ghost Blur | STANDARD | 0–1 (limitado a 0.94) | 0.0 | Rastro temporal |
| Bass React | STANDARD | 0–1 (×2) | 0.5 | Quanto o grave age no escorrido e no mosh |
| High React | STANDARD | 0–1 (×2) | 0.5 | Quanto o agudo alonga o escorrido |

**Dependências externas**
- Captura de áudio: sim, módulo compartilhado `AudioCapture`. Observação: o manual diz que o modo "FFT do host" obedece aos sliders de FFT do host, mas esta versão não registra buffer FFT (`SetBufferParamInfo` ausente) e, nesse modo, para a captura (`Stop()`) e zera grave/agudo. Ou seja, no modo 3 o efeito fica sem reatividade.
- CEF / servidor HTTP / porta: não.
- Arquivos lidos: não.
- stb_image: não.
- COM / PowerPoint: não diretamente (COM só na captura WASAPI).

**Shaders**
- 1 passe. Vertex em `DatamoshPixelSort.cpp:22–37`; fragment em `DatamoshPixelSort.cpp:39–195` (`main` em 87).
- Pixel sort: loop de 28 amostras na direção escolhida (linhas ~97–100 para a direção).
- Datamosh: lê `FeedbackTexture` (`m_lastFrameTex`) com deslocamento por fluxo de luma/croma e troca o quadro se a diferença passar da tolerância (simulação de quebra de I-frame).
- Neon: Sobel com 8 vizinhos sobre a entrada.
- Feedback: sim, `glCopyTexSubImage2D` ao fim do frame (`DatamoshPixelSort.cpp:396`).

**Interface web**
Não possui.

**Estado / arquivos guardados**
- Em memória: `m_params[19]`, envelopes de grave/agudo, textura de feedback, tempo.
- Nada persistido.

**Dificuldade para reescrever no app: Média**
Um passe com feedback por cópia de framebuffer e um loop de 28 amostras; portar é direto, o custo está no módulo de áudio compartilhado.

---

## 5. GenBackgrounds

**Identificação**
- ID: `GBG1`
- Tipo: fonte (`FF_SOURCE`), sem entrada
- Nome exibido: `BSL Background` (descrição: "BSL Generative Backgrounds")
- Pasta: `source/plugins/GenBackgrounds/` (`GenBackgrounds.cpp`, 619 linhas)

**O que faz**
Gera fundos procedurais animados num único fragment shader com 22 estilos selecionáveis (ondas, células, estrelas, nuvens, plasma, matrix, espiral, cyber grid, aurora líquida, Balatro, beams, CRT warp, dither, linhas flutuantes, galáxia, gradient blinds, grid scan, prism, radar, ripple grid, silk, acid squares). Três cores em HSB (cor 1, cor 2, fundo) e controles de detalhe, densidade, suavidade, distorção, rotação, velocidade e escala. Aplica grão de filme no final.

**Parâmetros** (17)

| Nome | Tipo FFGL | Faixa / opções | Padrão | O que controla |
|---|---|---|---|---|
| Style | OPTION (22) | 0 Waves, 1 Cells, 2 Stars, 3 Clouds, 4 Plasma, 5 Matrix, 6 Spiral, 7 Cyber Grid, 8 Liquid Aurora, 9 Balatro, 10 Beams, 11 CRT Warp, 12 Dither, 13 Floating Lines, 14 Galaxy, 15 Gradient Blinds, 16 Grid Scan, 17 Prism, 18 Radar, 19 Ripple Grid, 20 Silk, 21 Acid Squares | 0 (Waves) | Estilo do fundo |
| Speed | STANDARD | 0–1 | 0.5 | Velocidade (`t = iTime*speed*5`) |
| Scale | STANDARD | 0–1 | 0.5 | Escala espacial |
| Hue 1 | HUE | 0–1 | 0 | Matiz da cor 1 |
| Saturation 1 | SATURATION | 0–1 | 1.0 | Saturação da cor 1 |
| Brightness 1 | BRIGHTNESS | 0–1 | 1.0 | Brilho da cor 1 |
| Hue 2 / Saturation 2 / Brightness 2 | HUE / SATURATION / BRIGHTNESS | 0–1 | 0 / 1 / 1 | Cor 2 |
| Detalhe | STANDARD | 0–1 | 0.5 | Octavas e detalhe de ruído |
| Densidade | STANDARD | 0–1 | 0.5 | Quantidade de padrão |
| Suavidade | STANDARD | 0–1 | 0.5 | Suavidade das bordas |
| Distorcao | STANDARD | 0–1 | 0.0 | Deformação do padrão |
| Rotacao | STANDARD | 0–1 | 0.0 | Rotação do padrão |
| Fundo Hue / Fundo Sat / Fundo Bri | HUE / SATURATION / BRIGHTNESS | 0–1 | 0 / 1 / 1 | Cor de fundo (cor 3) |

**Dependências externas**
- Captura de áudio: não.
- CEF / servidor HTTP / porta: não.
- Arquivos lidos: não.
- stb_image: não.
- COM / PowerPoint: não.
- Tempo: `std::chrono` steady clock desde o início.

**Shaders**
- 1 passe. Vertex em `GenBackgrounds.cpp:42–50`; fragment em `GenBackgrounds.cpp:52–434` (`main` em 369).
- Os 22 estilos são funções `fx*` (ruído `noise`, `fbm`) chamadas por cadeia `if/else` em `GenBackgrounds.cpp:398–419`.
- Sem feedback, sem textura de entrada.

**Interface web**
Não possui.

**Estado / arquivos guardados**
- Apenas valores de parâmetro e tempo em memória.
- Nada persistido.

**Dificuldade para reescrever no app: Baixa**
Fonte sem estado, sem dependências externas e 1 passe; o trabalho é portar o GLSL (~380 linhas) e ligar os 17 parâmetros.

---

## 6. LEDMapperPro

**Identificação**
- ID: `LMPR`
- Tipo: fonte (`FF_SOURCE`), sem entrada
- Nome exibido: `LED Mapper Pro` (descrição: "Generates LED physical mapping grid and XML slices")
- Pasta: `source/plugins/LEDMapperPro/` (`LEDMapperPro.cpp`, 261 linhas)

**O que faz**
Desenha uma grade de cabinets de LED (colunas × linhas, com tamanho e espaçamento) centralizada na tela, com cabinets em dois tons alternados, borda branca, gap vermelho e fundo preto. Ao acionar "Export XML", grava um XML no formato de XML de slices com um Screen e um Slice por cabinet (InputRect e OutputRect iguais) para o Desktop.

**Parâmetros** (7)

| Nome | Tipo FFGL | Faixa / opções | Padrão | O que controla |
|---|---|---|---|---|
| Columns | STANDARD | 0–1 → `1 + int(v*49)` (1–50) | 0 (1 coluna) | Número de colunas |
| Rows | STANDARD | 0–1 → `1 + int(v*49)` (1–50) | 0 (1 linha) | Número de linhas |
| Cabinet Width | STANDARD | 0–1 → `v*2000` px | 0.064 (128 px) | Largura do cabinet |
| Cabinet Height | STANDARD | 0–1 → `v*2000` px | 0.064 (128 px) | Altura do cabinet |
| Gap X | STANDARD | 0–1 → `v*100` px | 0 | Espaço horizontal entre cabinets |
| Gap Y | STANDARD | 0–1 → `v*100` px | 0 | Espaço vertical entre cabinets |
| Export XML | EVENT | valor > 0 | — | Gera o XML no próximo `ProcessOpenGL` |

**Dependências externas**
- Captura de áudio: não.
- CEF / servidor HTTP / porta: não.
- Arquivo gravado: um XML de slices no Desktop, com nome começando em `LEDMapperPro_` (somente escrita; nada é lido).
- stb_image: não.
- COM / PowerPoint: não.
- IDs de Screen e Slice: `rand()` (sem semente).

**Shaders**
- 1 passe procedural, sem textura de entrada. Vertex em `LEDMapperPro.cpp:31–41`; fragment em `LEDMapperPro.cpp:43–99`.
- Resolução obtida por `glGetIntegerv(GL_VIEWPORT)`.

**Interface web**
Não possui.

**Estado / arquivos guardados**
- Parâmetros em memória e flag `m_buttonPressed`.
- Nada persistido além do XML exportado.

**Dificuldade para reescrever no app: Baixa**
Shader procedural simples e exportação de XML em texto; trivial de portar.

---

## 7. PPTManager

**Identificação**
- ID: `BSPT`
- Tipo: fonte (`FF_SOURCE`), sem entrada
- Nome exibido: `BSL PPT Manager` (descrição: "Loads PPTX and extracts slides")
- Pasta: `source/plugins/PPTManager/` (`PPTManager.cpp`, 407 linhas; `stb_image.h`)

**O que faz**
Carrega um documento (PPTX, PPT ou PDF), converte cada slide/página em JPG numa pasta temporária e exibe o slide atual como textura. A navegação é por eventos (anterior, próximo), por índice (slider) e por recarga.

**Parâmetros** (6, incluindo rótulo visual)

| Nome | Tipo FFGL | Faixa / opções | Padrão | O que controla |
|---|---|---|---|---|
| Document (PPTX/PDF) | FILE (texto) | caminho | vazio | Define o arquivo; dispara a conversão síncrona |
| Prev Slide | EVENT | >0.5 | — | Slide anterior |
| Next Slide | EVENT | >0.5 | — | Próximo slide |
| Slide Index | STANDARD | 0–1 → 0 até total−1 | 0 | Vai ao slide pelo índice |
| Reload | EVENT | >0.5 | — | Refaz a conversão do documento |
| "--- BSL LIGHT ---" | EVENT | — | — | Rótulo visual, sem efeito |

**Dependências externas**
- Captura de áudio: não.
- CEF / servidor HTTP / porta: não.
- Arquivos lidos: o documento; depois os JPGs gerados (`SlideN.jpg`), ordenados pelo número no nome.
- Arquivos escritos: `%TEMP%\BSLPPTManager_<pid>\` (PDF convertido, `SlideN.jpg`, `export.ps1`, `pdf_extract.log`). A pasta é apagada na conversão seguinte.
- stb_image: sim, `stb_image.h` com `STB_IMAGE_IMPLEMENTATION` em `PPTManager.cpp:17`, para carregar os JPGs em RGBA (`stbi_load`).
- COM / PowerPoint:
  - PDF: WinRT `Windows.Data.Pdf` in-process (`PdfDocument`, `RenderToStreamAsync`, largura 2× até 3840 px), em thread de trabalho com apartamento single-threaded.
  - PPTX/PPT, tentativa 1: LibreOffice headless (`C:\Program Files\LibreOffice\program\soffice.exe --convert-to pdf`) via PowerShell, depois o PDF segue o caminho WinRT.
  - PPTX/PPT, tentativa 2 (fallback): PowerPoint via COM, script `export.ps1` com `New-Object -ComObject PowerPoint.Application`, `Presentations.Open` e `SaveAs(pasta, 17)` (17 = JPG).
  - Observações: o fallback marca sucesso sem verificar (`extractionSuccess = true`); a linha de comando monta caminhos por concatenação de string (caminho com aspas quebra); a conversão roda na thread do host dentro de `SetTextParameter`, então bloqueia o host enquanto converte.

**Shaders**
- 1 passe de blit da textura do slide para a tela (`ProcessOpenGL`, uniform `InputTexture`). Sem feedback.
- Upload para GPU com `glTexImage2D` quando `needsTextureUpdate` está marcado.

**Interface web**
Não possui.

**Estado / arquivos guardados**
- `documentFilePath`, `tempDir`, `slideFiles`, `currentSlideIndex`, `totalSlides`, `imageBuffer` (stbi), `m_textureID`.
- Entre sessões: nada em memória; a pasta temporária fica no disco até a próxima conversão.

**Dificuldade para reescrever no app: Média**
A conversão é específica do Windows (WinRT, LibreOffice ou PowerPoint COM). Como o app já tem conversão de Office, o reaproveitamento é direto; a fonte de slides e a navegação são simples.

---

## 8. SliceRefiner2 (versão antiga, "BSS2")

**Identificação**
- ID: `BSS2`
- Tipo: efeito (`FF_EFFECT`), 1 entrada
- Nome exibido: `BSL Slice Source 2.0` (descrição: "BSL LIGHT Advanced Slice Source & Mask")
- Pasta: `source/plugins/SliceRefiner2/` (`SliceRefiner2.cpp`, 394 linhas). Legado; o SPEC do BSLSliceRefiner diz para não mexer nele.

**O que faz**
Versão anterior do recorte por slices. Lê os slices de um XML de saída (ou de um XML escolhido pelo usuário) e desenha até 8 slices, cada um com modo de contorno/preenchimento (11 modos), arredondamento, espessura, velocidade e cor próprios.

**Parâmetros** (50 índices)

| Nome | Tipo FFGL | Faixa / opções | Padrão | O que controla |
|---|---|---|---|---|
| Load Custom XML | FILE | caminho | vazio | Carrega um XML de slices próprio |
| Sync Slices | EVENT | — | — | Relê o XML de slices |
| S1…S8 Enable | BOOLEAN | 0 / 1 | 1 | Liga o slice |
| S1…S8 Mode | OPTION | 0–10 | 0 | Modo: 0 Neon Solid, 1 Gradient, 2 Radar, 3 Chaser, 4 Bumper, 5 Video Mask, 6 Sparkles, 7 Glow Pulse, 8 RGB Wave, 9 Dotted, 10 Glitch (rótulos dos comentários do shader) |
| S1…S8 Roundness | STANDARD | 0–1 | 0 | Arredondamento |
| S1…S8 Stroke | STANDARD | 0–1 | ver construtor | Espessura da borda |
| S1…S8 Speed | STANDARD | 0–1 | ver construtor | Velocidade do efeito |
| S1…S8 Color | HUE | 0–1 | ver construtor | Cor |

Índices: `0` = XML, `1 + (i−1)*6 .. +5` = parâmetros do slice `i`, `49` = sync. Os padrões de Stroke, Speed e Color estão no construtor (`SliceRefiner2.cpp`, ~linhas 160–200); não foram transcritos.

**Dependências externas**
- Captura de áudio: não.
- CEF / servidor HTTP / porta: não.
- Arquivos lidos:
  - o XML de slices padrão da pasta Documentos do usuário (no init e no Sync).
  - XML escolhido pelo parâmetro FILE.
  - Parser por busca de strings (`find("<Slice")`, `"<Param name=\"Name\""`, `"<InputRect"`, `"<v x=\""`), sem parser XML de verdade.
- stb_image: não.
- COM / PowerPoint: não.

**Shaders**
- 1 passe. Vertex em `SliceRefiner2.cpp:24–37`; fragment em `SliceRefiner2.cpp:39–165` (`main` em 72).
- Struct `Slice` com 8 entradas enviadas por uniform; modos em cadeia `if (mode < …)`.
- Sem feedback.

**Interface web**
Não possui.

**Estado / arquivos guardados**
- `m_slices` (vetor de `SliceData`), `m_xmlPath`, `m_compWidth/Height` (1920×1080 por padrão), `m_params[100]`.
- Nada persistido.

**Dificuldade para reescrever no app: Baixa**
Um passe e um parser simples; se o app já usar o parser do BSLSliceRefiner, a tarefa fica menor ainda.

---

## 9. WebBackgrounds

**Identificação**
- ID: `WBBG`
- Tipo: fonte (`FF_SOURCE`), sem entrada
- Nome exibido: `BSL WebBackgrounds` (descrição: "BSL WebBackgrounds & Video Player Source")
- Pasta: `source/plugins/WebBackgrounds/` (`WebBackgrounds.cpp`, 2565 linhas; `WebBackgrounds.h`; `subprocess_main.cpp`; `web_app/index.html`)

**O que faz**
Renderiza uma página web com CEF em off-screen (OSR) e entrega o resultado como textura de vídeo. A página padrão é `http://localhost:8081/app` (fundos WebGL e logo animada, servida do próprio plugin). Um painel HTTP na porta 8081 troca o fundo, ajusta a configuração, e controla a logo (texto ou URL, efeito, entrada, tamanho e tempos). O plugin mantém o último estado e a última logo como "fonte de verdade" para os painéis.

**Parâmetros**
Nenhum parâmetro FFGL é registrado: o construtor não chama `SetParamInfo`. O enum `PT_*` (URL, Clean Fullscreen, Play, Pause, Restart, Mute, Volume, Reload, Mouse Click, Mouse X, Mouse Y) existe e é tratado em `SetFloatParameter`/`SetTextParameter`, mas o host não o expõe. O controle é feito só pelo HTTP.

**Dependências externas**
- CEF:
  - `libcef.dll` na pasta do plugin (cada pasta pode ter sua cópia); o código procura a subpasta `WebBrowser.Cef` (`WebBackgrounds.cpp`, ~1196–1202) se ela existir.
  - Switches em `BrowserApp` (`WebBackgrounds.h`): `autoplay-policy`, user-agent Chrome 134, `enable-gpu`, `disable-gpu-vsync`, `enable-begin-frame-scheduling`, `allow-file-access-from-files`, `disable-web-security`, `remote-debugging-port=9222`.
  - `CefSettings`: `no_sandbox`, `multi_threaded_message_loop`, `windowless_rendering_enabled`, `remote_debugging_port=8088`. São duas portas de depuração diferentes.
  - Cache: `WebBackgroundsCache` em `%LOCALAPPDATA%`.
  - Subprocesso: o código procura `WebBrowserSubprocess.exe` (nome herdado do WebBrowser), mas o CMake deste plugin gera `WebBackgroundsSubprocess.exe`. Verificar.
  - Modelo: o processo browser fica no processo do host (CefInitialize in-process); os renderers rodam no subprocesso.
- Servidor HTTP: sim, Winsock próprio, singleton, **porta 8081** (`WebBackgrounds.cpp:2165`). O comentário no header diz 8080; o código usa 8081.
- Arquivos lidos: `<plugin>\web_app_bg\control.html` (painel; fallback: HTML embutido) e `<plugin>\web_app_bg\index.html` (página `/app`). No repositório só existe `web_app/index.html`; a pasta `web_app_bg` provavelmente é copiada no deploy.
- stb_image: não.
- GDI+: sim, para JPEG do `/api/screen`.
- COM / PowerPoint: não.

Rotas (`WebBackgrounds.cpp:2259–2543`):
- `GET /`, `/index*`, `/control*`: `control.html` (ou HTML embutido).
- `GET /app*`: `web_app_bg/index.html`.
- `GET /api/screen`: JPEG 640×360 qualidade 50 (`CaptureJpeg`); se não houver quadro, `{"status":"no_frame"}`.
- `GET /api/state`: `{"rev":N,"logoRev":N,...}`.
- `GET /api/logo`: última logo (texto).
- `GET|POST /api/action?cmd=…&val=…`: `cmd` ∈ `bg`, `bg_param`, `config`, `logo_url`, `logo_text`, `logo_effect`, `logo_size`, `logo_entrance`, `logo_toggle`, `logo_time_anim`, `logo_time_hold`, `logo_time_off`, `state`. Cada um chama `window.<função>(…)` na página via `ExecuteJavaScript`; resposta `{"status":"ok","rev":N}`.
- `/api/click?x=&y=`, `/api/load?url=`, `/api/toggle_fullscreen`, `/api/skip_ad`, `/api/dismiss_popups`, `/api/scroll_up`, `/api/scroll_down`, `/api/play`, `/api/pause`, `/api/restart`, `/api/mute`, `/api/reload`, `/api/volume?val=`.
- Demais: `{"status":"not_found"}`.
- CORS: `Access-Control-Allow-Origin: *`.

**Shaders**
- 1 passe de blit: upload do BGRA do CEF com `glTexImage2D`/`glTexSubImage2D` e desenho em tela cheia. O vertex inverte Y (CEF tem origem no topo).
- Vertex em `WebBackgrounds.cpp:37–50`; fragment em `WebBackgrounds.cpp:51` em diante.
- Sem feedback no shader.

**Interface web**
- `web_app/index.html` (repositório): renderizador WebGL com overlay de texto ("BSL LIGHT"), exposto como `window.updateParams({speed, scale, text, color1, color2})`.
- Painel `control.html` (BSL Studio, fora deste repositório) com bancos de fundos (AcidSquares, Aurora, Balatro, Beams, ColorBends, DarkVeil, Dither, Ferrofluid, FloatingLines, Galaxy, GhostFibers, …) e configuração de logo.
- Protocolo: rotas acima.

**Estado / arquivos guardados**
- Em memória e `static`: `s_lastLogo`, `s_stateRev`, `s_logoRev` (perdidos ao fechar), `m_currentUrl` (padrão `http://localhost:8081/app`), `m_cleanMode`, volume.
- Disco: apenas o cache do CEF.

**Dificuldade para reescrever no app: Média**
O app já tem CEF em processo separado, então o motor está pronto; o trabalho é replicar o protocolo do painel (rotas, revisões de estado, logo) e a ponte JS.

---

## 10. WebMotion

**Identificação**
- ID: `WB01` (**mesmo ID do WebBrowser; ver observações**)
- Tipo: fonte (`FF_SOURCE`), sem entrada
- Nome exibido: `BSL WebMotion` (descrição: "BSL WebMotion & Video Player Source")
- Pasta: `source/plugins/WebMotion/` (`WebMotion.cpp`, 1627 linhas; `web_app/index.html`)

**O que faz**
Renderiza com CEF OSR a página local `file:///…/web_app_motion/index.html`, que recebe uma imagem em base64 (`window.receiveImage(base64, w, h)`), separa as letras pela transparência (alpha por coluna) e anima cada letra com atraso escalonado (estilo tipografia em movimento). Tem painel HTTP na porta 8082 para vídeo, clique e URL.

**Parâmetros**
Nenhum FFGL registrado (sem `SetParamInfo` em `WebMotion.cpp`), mesmo caso do WebBackgrounds.

**Dependências externas**
- CEF: mesmo modelo do WebBackgrounds (switches com `remote-debugging-port=9222` no `BrowserApp`, `remote_debugging_port=8088` no `CefSettings`, `no_sandbox`, OSR). Cache: `WebMotionCache` em `%LOCALAPPDATA%`. Subprocesso procurado como `WebBrowserSubprocess.exe`, mas o CMake gera `WebMotionSubprocess.exe`. Verificar.
- Servidor HTTP: sim, Winsock próprio, singleton, **porta 8082** (`WebMotion.cpp:1429`). O comentário diz 8082 e o código também.
- Arquivos lidos: `web_app_motion/index.html` (no repositório o arquivo está em `web_app/index.html`; o nome do diretório de deploy é diferente).
- stb_image: não.
- COM / PowerPoint: não.

Rotas (`WebMotion.cpp:1482–1610`):
- `GET /`, `/index*`: HTML embutido (`s_remoteHtmlPage`, linha 277).
- `GET /api/screen`: JPEG (com `Cache-Control: no-cache`).
- `/api/click?x=&y=`, `/api/load?url=`, `/api/toggle_fullscreen`, `/api/skip_ad`, `/api/dismiss_popups`, `/api/scroll_up`, `/api/scroll_down`, `/api/play`, `/api/pause`, `/api/restart`, `/api/mute`, `/api/reload`, `/api/volume?val=`.
- **Inconsistência:** o HTML embutido chama `/api/action?cmd=text&val=…&anim=…`, `/api/action?cmd=logo&val=…` e `/api/action?cmd=clear` (linhas 323–330). O servidor não tem essa rota e responde `{"status":"not_found"}`. Os controles de texto e logo do painel não funcionam como escritos.

**Shaders**
- 1 passe de passthrough: `texture(webTexture, uv)` (`WebMotion.cpp:41–50`), com upload do BGRA do CEF em `ProcessOpenGL` (linhas ~1165–1220).

**Interface web**
- `web_app/index.html` (renderizador): expõe `window.receiveImage(base64, w, h)`. Nenhum código deste plugin chama essa função; quem chama é externo, ou nunca é chamada.
- HTML embutido (painel 8082): URL, vídeo, clique, texto e logo (com as rotas inconsistentes citadas).

**Estado / arquivos guardados**
- Em memória: `m_currentUrl`, `m_cleanMode`, volume, posições do mouse.
- Disco: cache do CEF.

**Dificuldade para reescrever no app: Média**
Mesma base do WebBackgrounds (motor CEF já existe no app), mas com o painel inconsistente e a animação de letras em JavaScript; precisa decidir se reaproveita a lógica de letras em shader ou em JS.

---

## 11. WebBrowser

**Identificação**
- ID: `WB01` (**mesmo ID do WebMotion; ver observações**)
- Tipo: fonte (`FF_SOURCE`), sem entrada
- Nome exibido: `BSL WebBrowser` (descrição: "BSL WebBrowser & Video Player Source")
- Pasta: `source/plugins/WebBrowser/` (`WebBrowser.cpp`, 1953 linhas; `WebBrowser.h`; `subprocess_main.cpp`)

**O que faz**
Navegador web completo (CEF OSR) como fonte de vídeo. URL padrão `https://www.youtube.com`. Permite trocar a URL, ajustar fullscreen limpo (oculta a interface de players como YouTube), dar play, pause, restart, mudo, volume, reload e clique em coordenadas normalizadas. Painel HTTP na porta 8080 (acessível pelo celular via IP do computador) com URL, clique, volume, skip ad e pop-ups.

**Parâmetros** (11, todos registrados)

| Nome | Tipo FFGL | Faixa / opções | Padrão | O que controla |
|---|---|---|---|---|
| URL | TEXT | string | `https://www.youtube.com` | Navega para a URL (URLs com "youtube" ou "youtu.be" são ignoradas em silêncio) |
| Clean Fullscreen | BOOLEAN | 0 / 1 | 0 | Estilo fullscreen (sem UI do site) |
| Play | EVENT | >0 | — | `video.play()` em todos os `<video>` |
| Pause | EVENT | >0 | — | `video.pause()` |
| Restart | EVENT | >0 | — | `currentTime = 0` e play |
| Mute | EVENT | >0 | — | Alterna `muted` |
| Volume | STANDARD | 0–1 | 1.0 | Volume dos vídeos |
| Reload | EVENT | >0 | — | Recarrega a página |
| Click | EVENT | >0 | — | Clique em (Mouse X, Mouse Y) |
| Mouse X | STANDARD | 0–1 | 0.5 | Coordenada X normalizada do clique |
| Mouse Y | STANDARD | 0–1 | 0.5 | Coordenada Y normalizada do clique |

**Dependências externas**
- CEF: mesmo modelo dos outros dois (OSR, `no_sandbox`, `remote_debugging_port=8088` e `9222` no `BrowserApp`). Carrega `WebBrowser.Cef` quando existe (`SetDllDirectoryW`, linhas 69–72). Subprocesso `WebBrowserSubprocess.exe` (bate com o CMake deste plugin). Cache: `WebBrowserCache` em `%LOCALAPPDATA%`.
- Servidor HTTP: sim, Winsock próprio, singleton, **porta 8080** (`WebBrowser.cpp:1756`).
- Arquivos lidos: nenhum (painel HTML embutido em `s_remoteHtmlPage`, linha 277).
- stb_image: não.
- COM / PowerPoint: não.

Rotas (`WebBrowser.cpp:1809–1930`): `GET /`, `/index*` (HTML embutido); `GET /api/screen` (JPEG); `/api/click?x=&y=`; `/api/load?url=` (URL-encoded); `/api/toggle_fullscreen`; `/api/skip_ad`; `/api/dismiss_popups`; `/api/scroll_up`; `/api/scroll_down`; `/api/play`; `/api/pause`; `/api/restart`; `/api/mute`; `/api/reload`; `/api/volume?val=` (0–1). Painel chama `fetch('/api/' + action)`.

**Shaders**
- 1 passe de passthrough (vertex em `WebBrowser.cpp:27`), com upload do BGRA do CEF por frame.
- Sem feedback.

**Interface web**
- Painel HTML embutido (porta 8080), com controles de URL, clique, volume, pop-ups e skip ad.
- Não há pasta `web_app` neste plugin.

**Estado / arquivos guardados**
- Em memória: `m_currentUrl`, `m_cleanMode`, `m_volume`, `m_mouseX/Y`.
- Disco: cache do CEF.

**Dificuldade para reescrever no app: Baixa**
É o equivalente direto do que o app já tem (fonte Web via CEF em processo separado); a adaptação é mapear os 11 parâmetros e as rotas do painel.

---

## Observações e anomalias encontradas

1. **ID duplicado:** `WB01` aparece em `WebBrowser.cpp:15` e em `WebMotion.cpp:15`. O host identifica plugins pelo ID; um dos dois precisa de ID novo.
2. **Nome do subprocesso CEF:** `WebBackgrounds` e `WebMotion` procuram `WebBrowserSubprocess.exe` (nome do WebBrowser), mas o CMake de cada um gera `WebBackgroundsSubprocess.exe` e `WebMotionSubprocess.exe`. Se a cópia não existir com o nome procurado, o CEF não sobe. Confirmar no deploy.
3. **Portas de depuração e segurança do CEF:** `BrowserApp` define `remote-debugging-port=9222` e `CefSettings` define `8088`. Todos os três plugins web usam `disable-web-security`, `allow-file-access-from-files` e `no_sandbox`. Se a porta de depuração for exposta, qualquer página carregada tem acesso total.
4. **Log de depuração no AlphaReactor:** `AudioCapture.cpp` da cópia do AlphaReactor grava em `C:\Users\GABRIEL - BSL LIGHT\Desktop\AlphaReactor_Audio.log` a cada start (append). Remover antes de distribuir.
5. **DatamoshPixelSort sem reatividade no modo "FFT do host":** o manual promete obedecer aos sliders FFT do host, mas não há buffer FFT registrado e, nesse modo, a captura para e grave/agudo ficam em 0.
6. **WebMotion:** o painel embutido chama `/api/action?cmd=text|logo|clear`, rota que o servidor não implementa (`not_found`).
7. **WebBackgrounds:** o comentário do header diz porta 8080; o código usa 8081.
8. **Parâmetros não registrados:** WebBackgrounds e WebMotion não expõem nenhum parâmetro no host (controle só por HTTP).
9. **WebBrowser:** URLs com "youtube" ou "youtu.be" passadas pelo parâmetro URL são ignoradas sem aviso (`SetTextParameter`).
10. **PPTManager:** conversão síncrona (bloqueia o host); fallback de PowerPoint marca sucesso sem checar; caminhos concatenados em linha de comando.
11. **Arquivos fora do repositório:** `control.html` (BSLSliceRefiner e WebBackgrounds) e `web_app_bg/` (WebBackgrounds) não estão neste repositório; o painel do BSL Studio é o que os usa.
12. **Taxas de frequência do áudio:** as faixas de bins (ex.: grave = bins 1–4) assumem 48 kHz. Se o dispositivo estiver em outra taxa, as bandas mudam de Hz (efeito pequeno).

---

## Módulos compartilhados que o app precisa ter

1. **Análise de áudio (WASAPI loopback + entrada + FFT + bandas + kick + AGC + gate + envelopes).** Usado por AlphaReactor, AudioReactiveTypo e DatamoshPixelSort. As três cópias de `AudioCapture.h/.cpp` são idênticas, exceto pelo log do AlphaReactor e pelos clamps de nível.
2. **Feedback de frame (copiar o framebuffer para uma textura e lê-la no frame seguinte).** Usado por AlphaReactor, AudioReactiveTypo (ghost blur) e DatamoshPixelSort (mosh e ghost).
3. **Rasterização de texto no CPU para textura (tipografia animada).** Usado por AudioReactiveTypo (GDI, Impact e Arial). No app, pode ser substituído por fonte própria ou atlas.
4. **Host CEF off-screen (inicialização singleton, subprocesso, cache, `OnPaint` → textura BGRA, `ExecuteJavaScript`, redimensionamento, mouse e scroll).** Usado por WebBackgrounds, WebMotion e WebBrowser. O app já tem isso.
5. **Servidor HTTP embutido (Winsock, roteamento por path, CORS, respostas JSON e JPEG, singleton).** Usado por WebBackgrounds (8081), WebMotion (8082), WebBrowser (8080) e BSLSliceRefiner (8083).
6. **Preview JPEG (readback do quadro + codificação com GDI+).** Usado por WebBackgrounds, WebMotion, WebBrowser e BSLSliceRefiner.
7. **Leitura e escrita de XML de slices (slices, InputRect, mapeamento) e exportação de XML.** Usado por BSLSliceRefiner (`SliceXml`), SliceRefiner2 (leitura por string) e LEDMapperPro (exportação).
8. **Estado JSON persistido em Documentos com escrita atômica.** Usado por BSLSliceRefiner (`Json.h`, `State`, `Core`).
9. **Conversão de documentos em slides (PDF nativo, LibreOffice, PowerPoint COM) e carregamento de imagens.** Usado por PPTManager. O app já tem conversão de Office.
10. **Relógio do host e BPM (`SetTime`, `SetBeatInfo`).** Usado por BSLSliceRefiner. Os demais usam `std::chrono`.
11. **Quad de tela cheia, compilação de shader e passthrough de textura.** Usado por todos; já vem do SDK.

---

## Resumo rápido

| Plugin | ID | Tipo | Nome exibido | Áudio | CEF / HTTP | Arquivos | Office / COM | Dificuldade |
|---|---|---|---|---|---|---|---|---|
| AlphaReactor | ALPR | efeito | BSL Alpha Reactor | sim | não | não | não | Média |
| AudioReactiveTypo | ARTY | efeito | BSL AudioReactive Typo | sim | não | não | não (GDI) | Média |
| BSLSliceRefiner | BSR3 | efeito | BSL Slice Refiner | não | HTTP 8083 | XML de slices, state.json | não | Alta |
| DatamoshPixelSort | DMSH | efeito | BSL Datamosh & Pixel Sort | sim | não | não | não | Média |
| GenBackgrounds | GBG1 | fonte | BSL Background | não | não | não | não | Baixa |
| LEDMapperPro | LMPR | fonte | LED Mapper Pro | não | não | grava XML no Desktop | não | Baixa |
| PPTManager | BSPT | fonte | BSL PPT Manager | não | não | lê PPTX/PDF, grava JPGs em %TEMP% | WinRT PDF, LibreOffice, PowerPoint COM | Média |
| SliceRefiner2 | BSS2 | efeito | BSL Slice Source 2.0 | não | não | XML de slices, XML próprio | não | Baixa |
| WebBackgrounds | WBBG | fonte | BSL WebBackgrounds | não | CEF + HTTP 8081 | web_app_bg, cache CEF | não | Média |
| WebMotion | WB01 (duplicado) | fonte | BSL WebMotion | não | CEF + HTTP 8082 | web_app_motion, cache CEF | não | Média |
| WebBrowser | WB01 (duplicado) | fonte | BSL WebBrowser | não | CEF + HTTP 8080 | cache CEF | não | Baixa |

---

## Etapa A (feita): reescrita nativa

Sem DLL e sem importar; já vêm nas abas Efeitos e Fontes, na seção "BSL Light". Código em `src/fx/bsl/`, áudio em `src/audio/`.

- Áudio: `AudioAnalyzer` (WASAPI loopback ou entrada, escolha em Ver > Áudio, medidor na barra de status). `EffectContext::audio`
  entrega o `AudioSnapshot`. As bandas seguem as contas dos `AudioCapture.cpp` (FFT de 512, bins por frequência).
- Efeitos `bsl:alpha-reactor`, `bsl:datamosh-pixelsort`, `bsl:audio-typo`, `bsl:slice-refiner-2`; fontes `bsl:gen-backgrounds`
  e `bsl:led-mapper`.
- Conferência: `bslstage_bsltest` (testes) e `bslstage_bsltest --compare docs/bsl-port` (LEDMapperPro e SliceRefiner2 pelo host
  FFGL; os demais pelo shader original lido do código-fonte). PNGs lado a lado (original | nativo) em `docs/bsl-port/`.
- O SliceRefiner2 original não desenha nada em host nenhum: o vertex shader faz `uv = vUV * MaxUV` e o plugin nunca define `MaxUV`.

---

## Etapa B (feita): Slice Refiner 3, PPT Manager, WebBackgrounds e WebMotion

Quatro plugins que tinham servidor HTTP próprio, CEF ou conversão de documentos agora são nativos, sem DLL e sem o
`libcef` duplicado. Todos aparecem na seção "BSL Light" (Efeitos: Slice Refiner; Fontes: WebBackgrounds, WebMotion, PPT Manager).

| typeId | Substitui | Tipo | Onde roda | Controle remoto (padrão desligado) |
|---|---|---|---|---|
| `bsl:slice-refiner` | BSLSliceRefiner `BSR3` | efeito | shader no GL do app (`SliceRefiner3.cpp`) | porta 8083 |
| `bsl:web-backgrounds` | WebBackgrounds `WBBG` | fonte | página `assets/bsl/web_bg/index.html` na fonte Web (worker + CEF) | porta 8081 |
| `bsl:web-motion` | WebMotion `WB01` | fonte | página `assets/bsl/web_motion/index.html` na fonte Web | porta 8082 |
| `bsl:ppt-manager` | PPTManager `BSPT` | fonte | `PdfSource` (worker: PowerPoint COM ou LibreOffice, depois PDFium) | não tem |

### Slice Refiner (BSR3)

- Os slices vêm do próprio BSL Stage (`SliceFeed`, publicado a cada quadro a partir do `OutputSetup`); a origem "XML de
  slices" importa um arquivo XML escolhido pelo usuário, inclusive slices girados (retângulo
  orientado pelos 4 vértices do `InputRect`).
- O `state.json` virou o parâmetro `state` (o mesmo JSON v1 do plugin: 8 grupos e `assign` slice -> grupo, com os mesmos limites),
  salvo no projeto. Os parâmetros do FFGL (Opacity, Black BG, Master Speed, Blinker, Reload) viraram `opacity` (0 a 100 %),
  `blackBg`, `masterSpeed` (0 a 2x, padrão 1x), `blinker` e `reload`. O botão "Open Panel" deixou de existir (o editor é nativo).
- Editor nativo no painel Clip (`src/ui/panels/SliceEditor.cpp`, parâmetro Custom `bsl-slices`): preview dos slices com seleção por
  clique/Shift/retângulo, lista por tela, envio da seleção para o grupo 1 a 8 e todos os controles de cada grupo (formato, borda, os
  12 efeitos, animação, copiar estilo). O preview mostra os retângulos (o plugin mostrava um JPEG da saída).
- Controle remoto: `remote` + `remotePort`. Sobe `net::HttpServer` (Winsock, `src/net/MiniHttp.cpp`) que serve o mesmo painel
  (`assets/bsl/web_bg/control.html`, a aba Slices do BSL Studio) e a mesma API: `GET /api/slices`, `POST /api/slices/state`,
  `GET|POST /api/slices/reload`, `GET /api/slices/screen` (JPEG, só captura quando alguém pede). Uma mudança vinda da rede
  volta para o parâmetro `state` (`IEffect::pollParamChanges`), então é salva e aparece no editor. A cópia da cadeia na Prévia
  (`EffectContext::preview`) nunca abre o servidor.

### PPT Manager (BSPT)

Nada nele era exclusivo em relação à fonte PDF/Office, então é essa fonte com o parâmetro `document` ("Document (PPTX/PDF)"):
`prev`/`next`/`reload` (eventos), `page` (o "Slide Index"), mais o que o `PdfSource` já tinha (cor de fundo, avanço automático por
segundos ou batidas, voltar ao início, transição em fade). Setas de slide no cabeçalho do painel Clip. Trocar `document` reabre.

### WebBackgrounds (WBBG)

- A página é a mesma do plugin v1.0 (`web_app_bg/index.html`, 3 MB, build do BSL Studio), carregada por `file://` na fonte Web. O estado
  inteiro vai por `window.applyState(json)` (script "persistente" no worker: reexecutado a cada carga da página e no reinício do
  worker); a logo, por `window.updateLogo`.
- Parâmetros nativos: os do painel original, com as mesmas faixas e ids de campo (`fx_brightness` = `state.fx.brightness`...):
  fundo (41 estilos + "nenhum") com os ajustes de cada um (288 ajustes, só os do fundo escolhido aparecem), paleta (16 + personalizada de
  5 cores), girar matiz/ciclo, efeitos de imagem, movimento, espelho/pixel, textura, ritmo/blackout/flash, texto e logo com as
  animações de entrada/loop/saída. Não foram portados: as 8 cenas e os "looks" do painel (só existem no painel web).
- O painel web remoto fala com `GET /api/state`, `GET /api/logo`, `/api/action` (`cmd=state`, `bg`, `bg_param`, `config`, `logo_*`), `/api/screen`,
  e as rotas de vídeo/clique; tudo vira parâmetros do clip. Logo mandada pelo painel (data URL) vira um arquivo em
  `%APPDATA%\BSL Stage\web-logos` e o parâmetro `logoFile`.
- "Fundo transparente": o plugin desenhava sobre preto; aqui há a opção (CSS tira o `#000` da raiz da página; use com o fundo "nenhum").

### WebMotion (WB01)

- Página `assets/bsl/web_motion/index.html` = a do plugin + `setMotion`, `receiveImageAuto`, `showText`, `showLogo`, `clearAll`, `replay`.
- Parâmetros: texto animado (texto, Slide UP/Zoom IN, cor, tamanho, "Projetar texto"), logo (URL ou arquivo), "Apagar tela", imagem PNG
  separada em letras pela transparência, atraso entre letras (0,1 s) e duração (2 s), página própria (URL), vídeo (tocar/pausar/
  reiniciar/volume/mudo), clique (X, Y) e recarregar.
- O painel remoto (porta 8082) é o do plugin, agora funcionando.

### Bugs e inconsistências dos originais corrigidos na versão nativa

- **BSR3, emenda entre slices encostados:** com "Suavizar junção" = 0 a união por mínimo de distâncias deixava uma linha de borda
  na emenda (nos dois lados dela a distância até a borda é ~0), contrariando o "sem linha no meio" do SPEC. Agora o raio mínimo da
  união é 4 x (meia largura da borda + 1) px; com a junção suave maior que isso nada muda (o estado real do usuário usa 45 e 77).
- **BSR3, estado global:** o plugin tinha um estado único por processo (todas as instâncias dividiam); agora é por instância/clip.
- **BSR3/WBBG/WebMotion, rede sempre aberta:** os servidores subiam sozinhos em todas as interfaces, com CORS `*` e sem opção de
  desligar. Agora só com "Controle remoto pela rede" ligado.
- **WBBG/WebMotion, dois `libcef`:** cada pasta carregava a sua cópia, o subprocesso era procurado como `WebBrowserSubprocess.exe` (nome do
  WebBrowser) e as portas de depuração 9222/8088 ficavam abertas com `disable-web-security`. O worker tem um CEF só, sem portas de depuração.
- **WBBG, `/api/action`:** `cmd=`, `val=` e `param=` eram achados com `find()` na requisição inteira (um valor com "val=" ou "cmd=" embaralhava
  tudo); agora é um parser de formulário de verdade.
- **WebMotion, rotas mortas:** o painel chamava `/api/action?cmd=text|logo|clear` e o servidor respondia `not_found`; ninguém chamava
  `window.receiveImage`; atraso (0,1 s) e duração (2 s) eram fixos. Tudo funciona e é parâmetro.
- **WebMotion, fundo:** a página declarava fundo transparente mas o CEF pintava preto opaco; agora é explícito (opção Fundo transparente).
- **PPTManager:** a conversão bloqueava o host dentro de `SetTextParameter`; o caminho do PowerPoint marcava sucesso sem verificar; os
  caminhos eram montados por concatenação (aspas quebravam); os JPGs ficavam em `%TEMP%`. O worker converte em segundo plano, com cache.

### Conferência

- `bslstage_bsltest` (testes) e `bslstage_bsltest --only-b` (só a etapa B).
- `bslstage_bsltest --compare docs/bsl-port`: BSR3 pelo host FFGL com o mesmo estado e o mesmo XML (diferença média 0,000,
  `bsr3-slice-refiner.png`) e WebBackgrounds pelo host FFGL (luminância e cor médias, `wbbg-web-backgrounds.png`; os fundos animam
  pelo relógio, então não dá comparar pixel a pixel).
- `bslstage_bsltest --compare-motion docs/bsl-port`: o WebMotion original sozinho (no host ele falha junto do WebBackgrounds por
  causa do `libcef`), `wb01-web-motion.png`; `wb01-web-motion-texto.png` é o texto animado nativo.
- `tools/data/bsr3-demo.py` gera o projeto do `docs/screenshot-bsl-nativos-b.png`; `tools/gen_webstudio_data.mjs` gera `assets/bsl/web_bg/studio.json`.


## Etapa C: limpeza dos plugins e painel de controle do WebBackgrounds

**Removidos** (registro, interface, `--demo`, testes e código): os efeitos `bsl:alpha-reactor`, `bsl:datamosh-pixelsort` e
`bsl:audio-typo` e as fontes `bsl:web-motion`, `bsl:gen-backgrounds`, `bsl:led-mapper` e `bsl:ppt-manager` (com `assets/bsl/web_motion/`,
`BslShaderSource` e o parâmetro `bsl-export`). Ficam: o módulo de áudio, `SliceFeed`, BSR3, SR2, WebBackgrounds, VHS, os efeitos de IA
e o padrão de calibração. O `BodyGlitch` já tinha o próprio datamosh no shader e não dependia do arquivo removido.

Projetos antigos com esses ids abrem: a célula fica em âmbar como "plugin ausente" (não toca) e o efeito aparece em âmbar na cadeia;
o id e os parâmetros ficam no projeto e são salvos de volta sem perda (`Clip::absentGenerator`).

**PPT Manager para a fonte PDF/Office nativa.** O que o plugin tinha: Documento (o clip já é o arquivo), Slide anterior/Próximo, Slide
Index (= parâmetro Página) e Recarregar. Trazido para a nativa: Próximo/Anterior/Voltar ao início/Último/Recarregar como ações
nomeadas (`core/Actions.h`: `doc.next`, `doc.prev`, `doc.first`, `doc.last`, `doc.reload`, prontas para o editor de atalhos);
parâmetro "Slide inicial" (`startPage`, 0 = o campo Página); botões "voltar ao início" e "recarregar documento" no cabeçalho do
painel Clip. As ações gravam o slide no clip, como as setas.

**Item 7 (HTML reiniciava ao trocar de clip).** `Composition::stopLayer` destruía a fonte do clip anterior. Agora, com "Ao disparar de
novo" = Retomar (padrão para Web, ao vivo e documentos), a fonte vai para `Clip::parked` (viva, só não é desenhada; atualizada 4x/s
para manter conexão e eventos) e volta a mesma instância; vídeo guarda a posição e reabre com seek. Limite: `maxBackgroundSources`
(8; 2 no Eco), a mais antiga fecha com aviso no log. "Limpar camada" e "Limpar tudo" fecham de verdade.

**Padrão de calibração.** O app publicava o Pixel Map certo (também fora do modo Pixel Map), mas com o Pixel Map vazio a fonte saía
preta (só havia uma linha pequena de status). Agora mostra o padrão do canvas inteiro com o aviso "PIXEL MAP VAZIO"; e o feed é
publicado antes do update da composição (antes chegava um quadro atrasado).

**Painel de controle do WebBackgrounds.** Botão "Abrir painel de controle" no painel Clip: liga o servidor local da própria instância
(só `127.0.0.1`, porta do parâmetro, 8081) e abre `http://127.0.0.1:<porta>/` no navegador (ShellExecute) ou numa janela própria
(`bslstage_worker.exe --panel`, CEF em janela comum). O acesso pela rede continua sendo o parâmetro "Controle remoto pela rede"
(desligado). O comando `--compare-motion` do bsltest saiu junto com o WebMotion.
