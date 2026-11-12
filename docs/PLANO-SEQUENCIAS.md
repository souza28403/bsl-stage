# Plano: Sequências de slices e efeitos por slice

## Decisões tomadas

- D1: modo próprio "Sequências" no topo (Composição | Mapeamento | Pixel Map | Sequências).
- D2: biblioteca de envelopes nas preferências, com cópia no projeto dos envelopes usados.
- D3: "sequência como modo de mistura" pelo parâmetro Fundo = Transparente com mistura Normal; sem mexer no Compositor.
- D4: DMX do blend mantém as faixas de 28 só para os 9 modos antigos; as máscaras por luminância ficam fora do DMX.
- D5: Atraso por slice com qualidade padrão 50% e memória padrão 96 MB (perfil Eco: 48 MB, teto 256 MB).
- D6: exportação do mapa de slices (E22) fica para depois, junto com a importação de mapas de controladora de LED.
- D7: nomes e ids da tabela da seção 3. Os ids não mudam depois de publicados.

Regras de todas as etapas: código sem comentários; nomes em português; nenhum programa de terceiros citado em código, interface ou documentação; testes de console (seqtest sem GL, seqfxtest com janela oculta); uiqa só na rotina noturna das 19h; só um agente compila por vez.

## Diagnóstico

- Os slices chegam aos efeitos pelo `SliceFeed` (`src/fx/bsl/SliceFeed.*`), atualizado a cada quadro no `App`. `FeedSlice::id` é `tela/nome#índice` e muda ao renomear ou reordenar: falta um id estável.
- Efeitos BSL seguem `BslEffect` + `BslProvider` + `BslEffects.h`; escala clip/canvas como no `SliceRefiner3`; parâmetros `ParamDesc`, widget especial por `ParamType::Custom`.
- Para centenas de slices os efeitos novos desenham com instancing (VBO por instância, `glVertexAttribDivisor`, GL 3.3), intensidades calculadas na CPU.
- Mistura em `Layer::blendMode` → `CompositeShaders.h` (`uBlend`, base em `uBase`). DMX de blend: faixas de 28 para 9 modos (`dmx/DmxMap.cpp`).
- Projeto JSON (`io/ProjectFile.cpp`) com chaves aditivas; preferências em `appDataDir()`.

## 1. Modelo de dados

### Slice estável
- `Slice::uid` (int > 0) e `OutputSetup::nextSliceUid`.
- `ensureSliceUids(OutputSetup&)` dá uid a quem não tem e uid novo a quem repete (copiar/colar, duplicar). Roda ao abrir projeto e a cada quadro (O(n)).
- `FeedSlice` ganha `uid`. O uid vai sempre no projeto; projeto antigo ganha uids na ordem tela/slice.

### `seq::SequenceSet` (na `Composition`, chave `"sequences"`, gravada só se não vazia)
- Sequence: `id`, `name`, `steps` (1 a 1024; cada passo = lista ordenada de uids ligados), `gen` opcional (parâmetros do gerador e bordas da grade).
- Visão: `id`, `name`, `previewVisible`, `on` (uids). A visão "Tudo" (id 0) é implícita, não se exclui, copia nem move. Slices novos entram desligados nas visões do usuário.
- `nextId` único para sequências e visões.
- No arquivo cada passo é texto de faixas (`"1-5,9,12-14"`).
- Religar: uids órfãos ficam guardados; "Religar slices" casa por tela+nome e depois por posição.
- Projeto sem a chave: conjunto vazio.

### Envelopes (`seq::Envelope`)
- `id`, `name`, âncoras `{x, y, alça de entrada, alça de saída, alças ligadas, segmento reto}`; primeira em x=0, última em x=1; alças presas ao segmento (curva não volta no tempo).
- Avaliação por bisseção em x → tabela de 256 valores a cada mudança.
- Até 512; sempre resta um; excluir o último restaura a curva S padrão.
- Biblioteca em `%APPDATA%\BSL Stage\envelopes.json` (gravação atômica). O projeto leva cópia dos usados (`sequences.envelopes`); ao abrir, os que faltam entram na biblioteca; id colidindo com conteúdo diferente é renumerado.

### Referência nos efeitos
- Parâmetros com ids inteiros: `sequencia`, `visao`, `envelope`; widgets `Custom` `seq-ref`, `view-ref`, `env-ref` mostram nomes.
- Id inexistente: passa a imagem direto e avisa em `status()`.

### `SequenceFeed`
- Singleton como o `PixelMapFeed`: `shared_ptr<const ...>` com conjunto, tabelas de envelopes e versão; o `App` publica só quando muda.

## 2. Editor (modo "Sequências")

Painéis: Sequências e Visões (esquerda), Canvas (centro), Passos (embaixo), Gerador (direita), Envelopes (aba ao lado do Gerador; o botão "Editar…" do parâmetro Envelope abre esse painel).

### Canvas
- Slices de todas as telas pelo retângulo de entrada; opcional a composição ao vivo atrás (textura existente).
- Zoom: roda, Ctrl+/Ctrl-, "Ajustar". Pan: botão do meio ou espaço+arrasto.
- Clique inverte; retângulo inverte; Shift+retângulo liga; Ctrl+retângulo desliga; Alt edita a Visão selecionada; arrasto contínuo (pincel) inverte cada slice uma vez por gesto.
- Fantasma do passo anterior (opcional). Ctrl+Z/Ctrl+Y com `SeqHistory` (um passo por gesto, teto 32 MB).

### Barra de passos
- Células 1..N, clique seleciona, setas navegam; + / – e caixa numérica (1..1024), Alt+setas.
- Arrastar passo sobre outro copia; Ctrl+arrastar copia para cada k-ésimo seguinte.
- Repetir →, Vai e volta ↔, Limpar.
- Lista de sequências: +, renomear (duplo clique), reordenar pela alça, Alt+arrastar copia, X exclui. Aviso quando as sequências têm números de passos diferentes.

### Gerador automático
- Grade pelos centros dos slices (lacunas acima de um limiar viram bordas).
- Direção: Horizontal, Vertical, Linha, Coluna, Aleatório (sem repetir); Inverter X/Y; Bloco; Módulo (desligado ou N, define o número de passos); Espelho X/Y; Embaralhar (com semente).
- Grade: arrastar borda move; Alt+arrastar cria; Backspace remove. Atualiza ao vivo e guarda em `Sequence.gen`.

### Editor de curvas
- Gráfico 0..1 × 0..1; duplo clique na linha adiciona âncora, na âncora remove; arrastar move; Shift trava eixo; Ctrl ignora ímã; alças Bezier ligadas, Alt separa; seleção por retângulo; campos X/Y; botão direito no segmento: "Tornar reto"; desfazer próprio; lista com adicionar, renomear, duplicar, excluir.

## 3. Efeitos e geradores

Base comum: categoria "Sequências", ícone `ICON_ZAP`, alfa reto; `SliceInstancer` (um quad por slice com retângulo, intensidade, índice, vec4 extra); escala `w / compW` como no `SliceRefiner3`; respeita `topOrigin`.
- Fundo: Preto ou Transparente.
- Escala: Preencher (quadro inteiro na caixa dos slices acesos do passo), Máscara (vídeo no lugar, só nos acesos), Esticar (quadro inteiro em cada slice aceso).
- Curva do passo: idade 0..1 na janela de ecos; Sustentação/Liberação (1 até S, cai linear em R) ou Envelope (tabela); cada slice usa o maior valor da janela.

| Id | Nome | Tipo | Parâmetros | Render |
|---|---|---|---|---|
| `bsl:sequencia` | Sequência de slices | efeito | sequência; avanço (Velocidade passos/s, BPM batidas por passo 1/16–8, Fixo = passo escolhido); direção (frente, trás, vai e volta, aleatório); deslocamento; ecos 0–16; sustentação 0.2 / liberação 0.8 ou envelope; escala; fundo; visão; intensidade | intensidade por slice na CPU (`SeqTiming`) → draw instanciado |
| `bsl:disparo` | Disparo manual | efeito | sequência; Próximo e Anterior (eventos); Rajada (segurar) + velocidade; reset automático + atraso (s); velocidade do apagar; S/L ou envelope; escala; fundo; visão; Reiniciar | máquina de estados na CPU → instanciado |
| `bsl:atraso-slices` | Atraso por slice | efeito | sequência (passo k atrasa k × unidade; fora da sequência = sem atraso); atraso; unidade (quadros, segundos, batidas); qualidade 25–100% (padrão 50%); memória máxima MB (padrão 96, Eco 48, teto 256); inverter; reset após pausa (1–60 min) | histórico `GL_TEXTURE_2D_ARRAY` RGBA8 reduzido; camadas = MB ÷ quadro (teto 256); grava até 30 q/s; alocação preguiçosa e liberação ao remover/ocioso; slice lê `cabeça - atraso`; `status()` com quadros, MB, atraso máximo; na Prévia no máximo 16 camadas a 25% |
| (modo de mistura) | Máscara por luminância / (invertida) | `BlendMode` | opacidade da camada | `CompositeShaders`: `base * luma(src)·src.a`, misturado pela opacidade |
| `bsl:slices-aleatorios` | Slices aleatórios | efeito | visão; máximo de slices (1–64, preso à metade dos disponíveis); velocidade ou BPM; ecos; S/L ou envelope; escala; fundo; semente; sem repetir | sorteio na CPU → instanciado |
| `bsl:contorno` (+ gerador `bsl:gerador-contorno`) | Contorno | efeito e gerador | visão; cor; espessura px; suavizar para dentro (sem animação); animar; velocidade ou BPM; fase; leque; aleatorizar; comprimento; quantidade; inverter direção; alternar pares/ímpares; mostrar original; envelope | quad instanciado expandido pela espessura; fragmento calcula distância à borda e posição no perímetro |
| `bsl:ladrilhos` (+ gerador) | Ladrilhos | efeito e gerador | visão; tamanho; modo (Arco-íris, Mínimo, Textura, Matriz); fase/velocidade; semente; aleatorizar; leque; comprimento; forma (quadrado, círculo) + raio, suavidade, só borda; grade (espessura, cor); mostrar original | instanciado; hash por ladrilho no fragmento |
| `bsl:amostrador` | Amostrador | efeito | visão; amostragem (Centro, Média, Mistura); cor (Cor, Luminância + tinta); ganho; suavização no tempo | reduz a entrada (≤512 de largura) + mipmap; um texel por slice num FBO N×1 com par persistente; instanciado com a cor |
| `bsl:varredura` | Varredura | efeito | sequência; avanço (velocidade, BPM, fixo); comprimento da borda; sobreposição; voltas 1–8; inverter direção; alternar pares/ímpares; trocar direções H↔V; escala (Preencher, Máscara); envelope; fundo | progresso por slice na CPU → gradiente no fragmento |
| `bsl:borda-neon` (+ gerador `bsl:gerador-borda-neon`) | Borda neon | efeito e gerador | forma (Retângulo ou Slices de uma visão); retângulo X/Y/L/A em % + raio do canto; espessura; segmentos 4–32; padrão (Um por vez, Opostos, Alternado, Personalizado `1,3;2,4`); comprimento aceso; direção (Normal, Invertida, Vai e volta); velocidade 0.4 ou Seguir BPM (divisão); barras laterais (liga, quantidade 1–4, largura, distância, altura); cor (azul claro); cor do flash (branco); brilho/glow; intensidade; cintilação; Flash (evento) + automático a cada N compassos + duração 1/8–1 batida + ruído do flash; fundo (Preto, Transparente, Original) | uma passada com SDF do retângulo arredondado e comprimento de arco do perímetro; glow por queda exponencial; ruído do flash = deslocamento horizontal por linha + grão só durante o flash |

Geradores usam o adaptador `EffectSource` (`ISource` que roda um `IEffect` sobre transparente), registrado em `bslGeneratorInfo`.

## 4. Pixel Map

Já existe: padrão HD por painel, overview, exportação de padrões, overview e CSV.
Falta (E22, depois): exportar o mapa dos slices do Mapeamento por tela; xadrez por gabinete; coordenadas; pitch → tamanho físico; liga/desliga de informações; decoração; logo com posição; fundo transparente.

## 5. Etapas

- E0 [rapido] Andaime: `src/seq/{SeqModel,SeqJson,Envelope,EnvelopeStore,SeqFactory,SeqEdit,SeqHistory,SeqTiming}.{h,cpp}` vazios em `bslsources`; `tools/seqtest.cpp` + `tools/seqtest_{model,env,factory,edit,timing}.cpp` (alvo `bslstage_seqtest`, sem GL); `tools/seqfxtest.cpp` + `tools/seqfx_*.cpp` (alvo `bslstage_seqfxtest`, janela oculta como o `fxtest`). Pronto: os dois alvos compilam e saem com 0.
- E1 [executor] uid de slice: `output/OutputSetup.{h,cpp}`, `io/ProjectFile.cpp`, `fx/bsl/SliceFeed.{h,cpp}`, `app/App.cpp`. Testes: `projecttest` (ida e volta, migração, duplicado, colar repetido), `maptest`. Pronto: `projecttest`, `maptest`, `bsltest`.
- E2 [executor] Máscara por luminância: `core/Layer.h`, `render/CompositeShaders.h`, `io/ProjectFile.cpp` (`luma-mask`, `luma-mask-inv`), combos em `ui/panels/CompositionPanel.cpp` e `TabPanels.cpp`, `dmx/DmxMap.cpp` e `DmxExport.cpp` (DMX só os 9 antigos). Testes: `fxtest`, `projecttest`; `dmxtest` igual.
- E3 [executor] Modelo + projeto: `seq/SeqModel.*`, `seq/SeqJson.*`, `core/Composition.h`, `io/ProjectFile.cpp`. Testes: `seqtest_model`, `projecttest` (sem a chave; 1024 passos; tamanho do arquivo).
- E4 [executor] Envelopes: `seq/Envelope.*`, `seq/EnvelopeStore.*`. Testes: `seqtest_env`.
- E5 [executor] Gerador: `seq/SeqFactory.*`. Testes: `seqtest_factory` (grade 4×3, todas as combinações, aleatório sem repetir, semente).
- E6 [executor] Edição e desfazer: `seq/SeqEdit.*`, `seq/SeqHistory.*`. Testes: `seqtest_edit`.
- E7 [executor] Tempo e intensidades: `seq/SeqTiming.*` (velocidade/BPM/fixo, direções, ecos, S/L e envelope, disparo, aleatórios, varredura). Testes: `seqtest_timing` com relógio injetado.
- E8a [executor] Feed e app: `fx/bsl/SequenceFeed.*`, `app/App.cpp`, `io/ProjectFile.cpp` (envelopes usados no projeto), `ui/panels/ParamWidgets.cpp` (`seq-ref`, `view-ref`, `env-ref`). Testes: `projecttest`, `seqtest`.
- E8b [executor] Renderizador + registro: `fx/bsl/SliceInstancer.*`, `BslCommon.h`, `BslEffects.h`, `BslProvider.cpp`, `fx/bsl/Seq{Sequencia,Disparo,Atraso,Aleatorios,Contorno,Ladrilhos,Amostrador,Varredura,BordaNeon}.cpp` com passagem direta. Testes: `seqfxtest` (intensidades 1/0.5/0, três escalas, `topOrigin`, clip com tamanho diferente do canvas, 2000 slices sem erro de GL).
- E9–E16 [executor] Um efeito por etapa, cada uma só no próprio `Seq*.cpp` e `tools/seqfx_*.cpp`: E9 Sequência; E10 Disparo; E11 Atraso; E12 Aleatórios; E13 Contorno; E14 Ladrilhos; E15 Amostrador; E16 Varredura; E16b Borda neon. Pronto: casos do `seqfxtest_<nome>` passam e `status()` vazio no caso normal.
- E17 [executor] Adaptador de gerador: `fx/bsl/EffectSource.*`, `BslSources.h`, `BslProvider.cpp`. Testes: `seqfxtest`.
- E18 [executor] Modo Sequências + Canvas + Passos + Lista: `ui/PanelContext.h`, `ui/Ui.cpp`, `ui/ViewState.h`, `ui/ViewUi.cpp`, `app/App.cpp`, `app/Qa.cpp`, `ui/panels/SequencePanels.*`, `ui/panels/SeqCanvas.*`. Testes: `uitest`, `keystest`; noturno: cenário `sequencias` do uiqa.
- E19 [executor] Painel Gerador: `ui/panels/SeqFactoryPanel.*`.
- E20 [executor] Editor de Envelopes: `ui/panels/EnvelopeEditor.*`.
- E21 [executor] Atalhos e DMX do Disparo manual: ação `fx.event` e modo segurar; `control/ActionRegistry.cpp`, `dmx/DmxEngine.cpp`. Testes: `keystest`, `dmxtest`.
- E22 [executor] Exportação do mapa de slices (depois).
- E23 [rapido] Documentação: `docs/ARQUITETURA.md`, `docs/SEQUENCIAS.md`.
- E24 [rapido] Revisão de sala limpa: grep por nomes de produto de terceiros e por `//` e `/*` nos arquivos novos. Pronto: zero ocorrências.

Ordem: E0+E1 → E2..E5 → E6+E7 → E8a+E8b → E9..E17 → E18 → E19+E20 → E21 → E23+E24.

## 6. Riscos

- Memória: Atraso limitado por MB, criado só quando usado, liberado ao sair; Prévia reduzida.
- uid repetido ao colar: corrigido a cada quadro.
- Disparo manual na Prévia pode divergir do Compositor (documentar).
- Contorno e Borda neon parecem com o Slice Refiner 2: descrições precisam deixar a diferença clara.
- Tamanho do projeto e do desfazer: faixas compactas e teto de 32 MB.
