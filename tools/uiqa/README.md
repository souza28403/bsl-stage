# Teste de interface com input real (uiqa)

Roda o `BSLStage.exe` de verdade e o opera com mouse e teclado do Windows (`SendInput`), clicando pelo **rotulo**
dos itens (nunca por coordenada fixa).

## Como funciona

- `BSLStage.exe --qa <pasta>` liga o modo de teste:
  - `settings.json`, `imgui.ini`, log e autosave ficam em `<pasta>\appdata` (nada toca no `%APPDATA%` do usuario);
  - o ImGui avisa cada item desenhado (janela, rotulo, retangulo) por hooks (`IMGUI_ENABLE_TEST_ENGINE`, ver
    `src/ui/BslImConfig.h` e `src/app/Qa.cpp`); o app grava `<pasta>\snapshot.json` a cada 3 frames, com os itens e o
    estado (modo, selecao, clips, BPM, projeto, escala, layout...);
  - detecta conflito de ID (cada item adicionado duas vezes na mesma janela e `GImGui->DebugDrawIdConflictsId`) e
    conta asserts do ImGui (`IM_ASSERT` agora registra no log em vez de sumir em Release); resumo em `summary.json`;
  - comandos do driver vao em `<pasta>\cmd.NNNNNN.txt`: `shot <png>`, `drop <arquivo>[|<arquivo>...]`, `quit`.
- Arquivo solto do Explorer: o comando `drop` monta um `DROPFILES` e manda `WM_DROPFILES` para a janela; o GLFW o
  converte em `glfwSetDropCallback` -> `App::onDrop`, o mesmo caminho de um arquivo solto de verdade (a posicao e a do
  cursor real, que o driver colocou sobre a celula).
- Dialogos nativos (Salvar/Abrir): o driver acha a janela `#32770` do processo, digita o caminho ou fecha com `WM_CLOSE`.

## IDs dos controles

Botoes so de icone nao tem texto: o teste clica pelo **ID estavel** (`"##area_acao"`), que o app registra como rotulo do
item. Uma consulta que comeca com `##` casa com qualquer rotulo que *termine* nela (`"##menu_recentes"` acha
`"<icone>  Abrir recente##menu_recentes"`); sem `##`, `"Texto"` ainda casa com `"Texto##id"`.

| Area | IDs |
|---|---|
| Transporte | `##transport_play`, `##transport_pause`, `##transport_stop`, `##transport_resync`, `##bpm_menos`, `##bpm_mais`, `##bpm` (numero arrastavel); `TAP`, `÷2`, `×2` continuam texto |
| Master / camadas | `##master_limpar`, `B##master_bypass`, `##master_opacidade`; por camada `N` (1 = de baixo): `##camada_N_parar`, `##camada_N_visivel` (olho), `B##camada_N_bypass`, `S##camada_N_solo`, `##camada_N_opacidade`; `##camada_nova`, `##coluna_nova`, `##deck_novo` |
| Efeitos | cadeia `C` = `clip`, `camada` (aba Camada) ou `master` (aba Composição); efeito `N` (1 = primeiro): `##C_efeito_N_alca` (arrastar para reordenar), `##C_efeito_N_cabecalho` (clicar recolhe; aceita efeito solto), `##C_efeito_N_bypass`, `##C_efeito_N_menu` (itens `##efeito_duplicar`, `##efeito_restaurar`, `##efeito_remover`), `##C_efeitos_soltar` (área tracejada), `##C_efeito_adicionar` (catálogo; itens `##add_<typeId>`). Aba Efeitos: `##efeitos_busca`, `##isf_recarregar`, `##isf_pastas`, itens pelo nome (`Inverter`). Cabeçalho da camada na grade: `camada N nome` (aceita efeito solto) |
| Painel Clip | `##clip_tocar_pausar`, `##clip_reiniciar`, `##loop_loop`, `##loop_uma_vez`, `##loop_pingpong`, `##velocidade`, `##paginaAnterior`, `##paginaSeguinte` |
| Navegador | `##acima` (pasta acima); as linhas continuam com o nome do arquivo (`foto.jpg`, `docs/`) |
| Mapeamento | Lista: `##map_add_tela`, `##map_add_slice`, `##map_duplicar`, `##map_remover`, `##map_identificar`. Barra das vistas Entrada e Saída (janelas `Entrada` e `Saída`): `##map_ima`, `##map_grade`, `##map_grade_tam`, `##map_regua`, `##map_ajustar`, `##map_100`, `##map_desfazer`, `##map_refazer`, `##map_alinhar_{esq,centro_h,dir,topo,meio,base}`, `##map_distribuir_{h,v}`, `##map_encostar_{esq,dir,cima,baixo}`; só na Saída os chips `Transformar` e `Pontos (warp)`; confirmação de remoção em lote `##map_remover_ok`. Propriedades: `##x ##y ##w ##h` (entrada), `##ox ##oy ##ow ##oh ##orot` (saída no modo Transformar), `##cx ##cy` (cantos, modo Pontos), `##resW ##resH ##g ##brilho ##opacidade` |
| Grade (células) | `celula L<n> C<m>` = a célula inteira (o centro cai na **miniatura**: clicar dispara e seleciona); `celula L<n> C<m> nome` = a faixa do nome (clicar só seleciona e leva para a Prévia, sem disparar) |
| Prévia | `##previa_fonte` (combo; itens `Clip selecionado` / `Camada selecionada`, janela `Combo`), `##previa_ao_vivo` (desabilitado se o clip já está na saída ou no modo Camada), `##previa_tocar_pausar`, `##previa_posicao`, `##previa_quadriculado`, `##previaEncaixe` (itens `Ajustar` / `Preencher`), `previa:area` (retângulo da imagem). Estado: `state.preview` = `mode`, `checker`, `fit`, `layer`/`column` (o que ela mostra), `hasClip`, `live` (espelha o clip da saída), `own` (instância própria), `playing`, `position`, `duration`; `state.layers[i]` ganhou `clipPlaying`, `clipPosition` e `blend` |
| Presets de layout | menu Exibir > Layout: `##layout_salvar`, itens dos presets pelo nome (`Palco A`), `##layout_gerenciar`. Janela `Salvar layout`: `##layout_nome`, `##layout_incluir_escala`, `##layout_salvar_ok`. Janela `Gerenciar layouts`: `##layout_N_aplicar`, `##layout_N_renomear`, `##layout_N_exportar`, `##layout_N_excluir` (N = 1 é o primeiro da lista), `##layout_importar`, `##layout_fechar`. Estado: `state.layoutPresets` (nomes, na ordem), `state.lastLayoutPreset`; o retângulo de um painel vem do item de janela (`app.window_rect("cmp_grade")`, `rect_of` em `run.py`) |
| Menu Arquivo | `##menu_novo`, `##menu_abrir`, `##menu_recentes`, `##menu_salvar`, `##menu_salvar_como`, `##menu_sair` |
| Atalhos de teclado | Menu Ver: `##menu_atalhos`. Botão direito numa célula, no nome da camada (`camada N nome`), no cabeçalho de coluna (`coluna N`) ou no olho do efeito: item `##key_atribuir` (e `##key_remover`). Aviso de gravação (janela `Gravar atalho`): `##key_cancelar_gravar`. Conflito (janela `Tecla já usada`): `##key_conflito_substituir`, `##key_conflito_cancelar`. Editor (janela `Atalhos`): `##atalhos_busca`, `##atalhos_novo`, `##atalhos_restaurar` (+ `##atalhos_restaurar_ok`), `##atalhos_exportar`, `##atalhos_importar` (abrem diálogos nativos: fora do fuzz), `##atalhos_fechar`; por linha visível `N` (1 = a primeira depois da busca): `##atalho_N_gravar`, `##atalho_N_escopo`, `##atalho_N_modo` (itens `Pressionar` / `Segurar`), `##atalho_N_global`, `##atalho_N_remover`. Estado: `state.keys` = `project` e `app` (lista `{action, key, mode, global?, args?}`, só o que difere do padrão), `fired`, `lastAction`, `lastKey`, `lastHit`, `held`, `systemRegistered`, `editorOpen`, `capturing`, `conflictOpen`, `rows`; `state.blackout` |

Stream de rede (cenário `stream`; `docs/STREAM.md`): a fonte "Stream de rede" na aba Fontes abre a janela `janela_stream` ao ser solta na grade. IDs: `##stream_aba_link`, `##stream_aba_receber`,
`##stream_url`, `##stream_mostrar` (olho), `##stream_colar`, `##stream_limpar`, `##stream_exemplo_N`, `##stream_protocolo`, `##stream_porta`, `##stream_nome`, `##stream_endereco_N`, `##stream_copiar_N`,
`##stream_perda`, `##stream_conectar`, `##stream_fechar`; item `stream:estado` (texto do estado); no painel Clip `##stream_abrir`. O cenário só dispara o modo Link (127.0.0.1): nunca o modo Receber, que abriria a porta na rede
e faria o Windows pedir o firewall. Também solta um atalho `.url` na grade. Gera `docs/screenshot-stream.png`.

Plugins do usuário (cenário `plugins`): menu Arquivo `##menu_novo_plugin` e `##menu_importar_plugin`; aba Efeitos `##plugin_novo` e `##plugin_importar`.
Assistente (janela `Novo plugin`): `##plug_nome`, `##plug_tipo_efeito`, `##plug_tipo_gerador`, `##plug_categoria`, `##plug_pasta`, `##plug_autor`, modelos `##plug_modelo_N` (N = 0 a 5), parâmetros
`##plug_p<i>_nome`/`_remover`/`_min`/`_max`/`_def`, `##plug_novo_tipo`, `##plug_adicionar_param`, `##plug_codigo`, `##plug_abrir_depois`, `##plug_criar`, `##plug_cancelar`; depois de criar: `##plug_abrir_editor`,
`##plug_mostrar_pasta`, `##plug_ver_efeitos`, `##plug_outro`, `##plug_fechar`. Botão direito num plugin ISF da aba Efeitos: `##isf_ctx_editor`, `##isf_ctx_pasta`, `##isf_ctx_duplicar`, `##isf_ctx_exportar`
(no cabeçalho do efeito na cadeia: `##C_efeito_N_editar`, `_pasta`, `_duplicar_plugin`). Importar: `##plug_importar_instalar`, `##plug_importar_ver`, `##plug_importar_pasta`, `##plug_importar_fechar`. Exportar: `##plug_exp_nome`, `_autor`, `_versao`,
`_licenca`, `_descricao`, `_exportar`, `_fechar`. No `--qa` os diálogos de arquivo são trocados pelas variáveis `BSLSTAGE_QA_PLUGIN_FILE` (pacote a importar) e `BSLSTAGE_QA_PLUGIN_EXPORT` (destino), e Abrir no editor e Mostrar na pasta só registram a ação.
O snapshot traz `state.plugins` (`wizardOpen`, `created`, `createdFile`, `createdId`, `rows`, `importOpen`, `importValid`, `importName`, `importLicense`, `importAuthor`, `importError`, `installedFolder`, `exportOpen`, `exportedFile`, `lastAction`, `lastPath`).
`tools/uiqa/pluginshots.py` gera `docs/screenshot-novo-plugin.png`.

Pixel Map (modo `Pixel Map###modo_pixelmap`; `ensure_mode(app, "pixelmap")`): janelas `pm_config`, `pm_overview` e `pm_tabela`.
Configuração: `##pm_cadeado`, `##pm_in_w/h`, `##pm_fis_w/h`, chips `##pm_wm_mono/img/none` e `##pm_modo_px/gab`, rascunho do painel
`##pm_nome` `##pm_dw` `##pm_dh` `##pm_dx` `##pm_dy` (gabinetes: `##pm_dcols` `##pm_drows` `##pm_dcabw` `##pm_dcabh`),
`##pm_adicionar`, `##pm_criar_mapeamento` (modal `pm_modal_criar`: `##pm_criar_substituir`, `##pm_criar_adicionar`,
`##pm_criar_cancelar`), `##pm_importar_mapeamento` (modal `pm_modal_importar`), `##pm_exportar_todos/overview/csv`,
`##pm_importar_csv`. Overview: `##pm_ima`, `##pm_grade`, `##pm_ajustar`, `##pm_100`; itens `pixelmap:painel:<i>` (retângulo do painel na
tela), `pixelmap:canvas`, `pixelmap:vista` e `pixelmap:aviso-escala`. Tabela (uma linha por painel, use `nth`): `##pm_cor`, `##pm_nome`,
`##pm_x` `##pm_y` `##pm_w` `##pm_h`, `##pm_gerar`, `##pm_mais`, `##pm_remover`. Os botões que abrem diálogos nativos (Gerar padrão HD,
Exportar…, Importar CSV…, Escolher imagem…) ficam fora do fuzz. O snapshot traz `state.pixelmap`: `input`, `linked`, `phys`, `watermark`,
`panels` (`name`, `phys`, `comp` já arredondado como na tabela, `overlap`, `outside`), `scaleWarn`, `selected`, `snap`, `dragging`, `guides`,
`zoom` e `status`. `tools/uiqa/pmshots.py` gera `docs/screenshot-pixelmap.png`.

Sequências (modo `Sequências###modo_sequencias`; `ensure_mode(app, "sequences")`, ou `--mode sequences` na linha de comando): janelas `seq_lista`,
`seq_canvas`, `seq_passos`, `seq_gerador` e `seq_envelopes` (as duas últimas são abas do mesmo dock). Lista: `##seq_nova`, `##seq_visao_nova`,
`##seq_religar`; por linha `##seq_alca`, `##seq_excluir`, `##visao_olho`, `##visao_excluir`. Canvas: chips `##seq_ferr_ret` e `##seq_ferr_pincel`,
`##seq_fantasma`, `##seq_composicao`, `##seq_nomes`, `##seq_ajustar`, `##seq_100`, `##seq_desfazer`, `##seq_refazer`, `##seq_passo_ant`,
`##seq_passo_prox`; itens `seq:canvas`, `seq:vista`, `seq:slice:<uid>` e `seq:borda:x|y:<i>`. Passos: `##seq_passo_mais`, `##seq_passo_menos`,
`##seq_passos_total`, `##seq_repetir`, `##seq_vaievolta`, `##seq_limpar_passo`, `##seq_limpar_tudo`; células `seq:passo:<i>`. Gerador:
`##ger_ligado`, `##ger_direcao` (itens na janela `Combo`), `##ger_inv_x/y`, `##ger_bloco`, `##ger_modulo`, `##ger_esp_x/y`, `##ger_embaralhar`,
`##ger_semente`, `##ger_manual`, `##ger_limiar`, `##ger_editar_grade`, `##ger_auto`. Envelopes: `##env_novo`, `##env_duplicar`, `##env_excluir`
(modal `##env_excluir_ok`), `##env_desfazer`, `##env_refazer`, `##env_ima`, `##env_nome`, `##env_x/y`; itens `env:grafico`, `env:vista` e
`env:ancora:<i>`. O botão `##env_editar` do parâmetro Envelope dos efeitos leva ao modo Sequências. O snapshot traz `state.sequences`: `list`
(`id`, `name`, `steps`, `lit` = acesos por passo, `generator`, `genDirection`, `genManual`), `views`, `envelopes`, `selected`, `view`, `step`, `tool`,
`undo`/`redo`, `gesture`, `gridEdit`, `zoom`, `fit`, `ghost`, `backdrop`, `status`, `slices` e `envelope` (`id`, `anchors`, `selected`, `undo`,
`redo`, `dragging`, `snap`). O cenário `sequencias` gera `docs/screenshot-sequencias.png`.

Mapeamento: o snapshot traz `state.mapping` com `screen`, `slice`, `corner`, `multi` (slices selecionados), `pointsMode`,
`undo`/`redo` (passos do historico), `snap`, `grid`, `gridStep`, `inputZoom`/`outputZoom`, `dragging` e `guides` (linhas-guia do
ima visiveis agora). Itens `entrada:imagem`/`saida:imagem` = retangulo do conteudo na tela (muda com zoom/pan; em "Ajustar à vista" e
o conteudo inteiro) e `entrada:vista`/`saida:vista` = a area clicavel. `driver.drag(..., mods=("shift","ctrl","alt","space"),
button="middle", probe=fn)`: `probe` roda com o botao ainda apertado (le o estado ou tira screenshot no meio do arrasto).
`tools/uiqa/mapshots.py` gera `docs/screenshot-mapeamento-transformar.png` e `docs/screenshot-mapeamento-ima.png` (este com as
linhas-guia, tirado com o mouse apertado).

Z-Slice: o efeito `bsl:z-slice` nas cadeias da camada e do master mostra o botao `##C_efeito_N_zrotear` ("Rotear zonas para slices…");
o modal tem um combo por zona (`##C_efeito_N_zrota_zona_Z`, itens `##zrota_s<i>` e `##zrota_nenhum_<z>`), `##C_efeito_N_zrota_aplicar` e
`##C_efeito_N_zrota_cancelar`. Mapeamento > Propriedades: combos `##zFilter` (Todas, Frente, Meio, Fundo, Zona 4) e `##zSource`. O estado
dos slices (`state.screens[i].slices[j]`) traz `zFilter` (0 = Todas ... 4 = Zona 4) e `zSource` (-1 = Master, >= 0 = camada).
`tools/uiqa/aishots.py [vhs] [relight] [aura] [zslice]` gera `docs/screenshot-aura.png` (video de teste gerado com o ffmpeg do
third_party a partir de `testmedia/person.jpg`) e `docs/screenshot-zslice.png` (opera o assistente com o mouse e confere o estado).

Numeros arrastaveis (hot text): `click` abre o campo de digitacao (Ctrl+A, texto, Enter/Tab/Esc); `drag(..., mods=("shift",))`
arrasta segurando Shift/Ctrl; botao direito abre "Restaurar padrão" (cenario `hot`).

## Uso

```
cmake --build build --config Release
py -3 tools/uiqa/run.py --config 1.0              # escala fixa 100%
py -3 tools/uiqa/run.py --config 1.5              # escala fixa 150%
py -3 tools/uiqa/run.py --config window           # "Acompanhar a janela" (padrao do app)
py -3 tools/uiqa/run.py --config 1.5 --size 2400x1300 --only grade,fuzz
```

Cenários `previa` (nome da célula só leva para a Prévia, imagem dispara, "Ao vivo", transporte próprio, modo Camada, quadriculado e
encaixe) e `layouts` (layouts prontos + presets do usuário: salvar, aplicar de volta, Alt+1, outra janela com as mesmas proporções,
Gerenciar, renomear e excluir). `py -3 tools/uiqa/docshots.py previa` gera `docs/screenshot-previa.png`.
Cenário `ndi` (NDI e Spout): fontes **NDI** e **Spout** na aba Fontes, o painel Clip (lista de fontes NDI, mesmo vazia, ou o aviso
"NDI Runtime não encontrado" com o link, ids `##ndi_fonte`, `##spout_sender`, `ndi:sem_runtime`, `##ndi_link_copiar`), a janela
Exibir > Compatibilidade de vídeo (`##menu_compat`, itens `compat:ndi_status`, `compat:spout_status`, botão `##compat_fechar`) e o
destino **NDI** da Screen (`##destino`, `##ndi`, `##ndiFormato`). O estado do snapshot ganha `state.ndi` (`available`, `version`,
`error`, `sources`, `outputs` com `name` e `connections`), `state.spout.senders`, `state.screens[i].kind` e `state.selClip.status`.
Sem o runtime do NDI confere o aviso; com `--ndi-lib <dll>` (só para testes) faz o loopback: a fonte NDI recebendo a saída NDI do
próprio app. `py -3 tools/uiqa/run.py --config window --only ndi,fuzz [--ndi-lib <dll>]`. O botão que abre o navegador
(`##ndi_link_abrir`) fica fora do fuzz. `tools/uiqa/docshots.py ndi` gera `docs/screenshot-ndi.png`.

Slice ligado a camada (cenário `mapeamento`, função `layer_slice_checks`): Propriedades > Fonte tem o combo `##sliceFonte` (itens
`##fonte_comp` e `##fonte_camada_N`, N = 1 é a camada de baixo) e o checkbox `##sliceMaster` ("Incluir efeitos do master", só com
camada). Botão direito num slice das vistas Entrada/Saída abre o menu `##map_ctx_vincular` (itens `##map_ctx_comp` e
`##map_ctx_camada_N`) e `##map_ctx_criar` ("Criar camada para este slice"). O cabeçalho da camada na grade tem o link
`##camada_N_slice` ("só nos slices vinculados", com o selo SLICE); o Pixel Map tem `##pm_camada_por_painel`. O snapshot ganha
`state.layers[i].id`/`sliceOnly`, `state.screens[i].slices[j].source`/`layerId`/`includeMaster` e `state.layerViews` (vistas de camada
que o Compositor renderizou: 0 sem slice ligado a camada). `py -3 tools/uiqa/sliceshots.py` gera `docs/screenshot-slice-camada.png`
(sem mouse: abre um projeto montado em `build/qa/sliceshots`).

Cenário `atalhos` (teclado real, `driver.key`, `key_down`/`key_up` para o modo segurar): atribuir tecla pelo botão direito numa célula, na
camada e na coluna e apertá-la; ligar/desligar camada; padrões (1..0 = colunas, Q..P = células da camada selecionada, Espaço, Delete, B
segurando = blackout); digitar num campo de texto não dispara atalho de show; editor (gravar, Esc, conflito cancelar/substituir, modo
Segurar, global, remover, restaurar padrões). Grava `docs/screenshot-atalhos.png` (editor aberto, teclas nas células e na camada):
`py -3 tools/uiqa/run.py --config window --only atalhos,fuzz`.

Os presets também têm teste sem janela: `build\Release\bslstage_uitest.exe` (JSON, preset inválido, troca de trechos do `imgui.ini`, pasta
de presets, exportar/importar).

Screenshots em `docs/qa/<config>_<estado>.png`. Codigo de saida 0 = nenhum passo falhou, sem assert nem conflito de ID.
Python 3 sem dependencias extras (so `ctypes`). Nao mexa no mouse/teclado enquanto roda.

`--ui-scale <f>` e `--ui-mode window|system|fixed` tambem valem no app normal (para testes).

## Execução determinística (foco, prazos e estado)

- Antes de cada tecla, clique, arrasto ou roda o driver confere que a janela do app é a que tem o foco (`GetForegroundWindow`) e, nos
  eventos de mouse, que o cursor está sobre uma janela do processo do app (`WindowFromPoint`). Se não estiver, retoma o foco
  (`AttachThreadInput` + `SetForegroundWindow`, com `SwitchToThisWindow` de reserva) com prazo de 12 s, confirma e só então envia o
  input; se não conseguir, o passo falha com a janela que está na frente e **nada é enviado**. Teclas e cliques nunca vão para janela
  de outro programa (o cenário `segundo_plano` só digita na própria janela de teste, e antes de cada caractere confere o foco dela).
- O clique por rótulo (`click`) relê o snapshot depois de mover o cursor e só clica quando o retângulo do item parou de se mexer
  (o snapshot sai a cada 3 quadros, então o layout pode ter mudado entre a leitura e o clique).
- Prazos com folga: abrir o app 120 s, quadros 30 s, item 6 s, estado 8 s, encerrar 20 s. Esperas por condição (janela maximizada,
  tamanho do cliente, quadros do app) no lugar de `sleep` fixo.
- Se o foco sair do app no meio de um passo e o passo falhar, ele é repetido (até 2 vezes) depois de soltar o mouse, fechar popups e
  voltar ao modo Composição; a saída mostra `repetindo <passo>`.
- No começo e no fim de `run.py` qualquer `BSLStage.exe --qa` deste repositório que tenha ficado aberto (execução anterior que caiu) é
  encerrado; `Ctrl+C` ou erro também encerram o app. O estado do app fica em `build\qa\cfg_*` (apagado a cada execução), nunca no `%APPDATA%`.

## Transform de camada (cenário `transform`)

Botão **Transformar** (ícone `move`) na Prévia (`##transformar_previa`, janela `cmp_previa`) e na Saída (`##transformar_saida`, janela
`cmp_saida`). Ligado, o monitor mostra a composição inteira (o retângulo da imagem é o item `cmp_previa:imagem` / `cmp_saida:imagem`) e
a barra ganha `##transf_recortar_<p>`, `##transf_ima_<p>`, `##transf_presets_<p>` (menu com os itens `##tr_preset_N`, N = 0 Tela cheia, 1
Ajustar, 2 Preencher, 3/4 metades, 5-8 quadrantes, 9 Grade 2×2, 10 Grade 3×3), `##transf_desfazer_<p>` e `##transf_refazer_<p>`
(`<p>` = `previa` ou `saida`). A área clicável é `##transf_area_<p>`. Aba Camada (`cmp_aba_camada`): `##tr_x ##tr_y ##tr_w ##tr_h ##tr_esc
##tr_rot ##tr_ax ##tr_ay ##tr_cl ##tr_ct ##tr_cr ##tr_cb`, `##tr_proporcao`, `Resetar` e `Presets…`; painel Clip:
`##clip_transform_proprio`; Mapeamento > Propriedades (slice ligado a camada): `Aplicar transform da camada`. Estado:
`state.layers[i].transform` (`x y w h rotation anchor crop identity`), `state.layers[i].ownTransform`,
`state.screens[i].slices[j].applyLayerTransform`, `state.layerViews` (vistas de camada renderizadas: slices e Prévia em modo Camada) e
`state.transformEditor` = `output`, `preview`, `crop`, `snap`, `undo`, `redo`, `dragging`, `drag`, `guides`. Ctrl+arrastar mexe na camada
selecionada mesmo coberta por outra; clicar escolhe a camada de cima.

## Salvar sem o diálogo nativo (cenário `projeto`)

A digitação no diálogo nativo "Salvar como" não é confiável com `SendInput`. Com `--qa`, a variável de ambiente
`BSLSTAGE_QA_SAVE_PATH` (definida por `run.py` para `<pasta qa>\proj\teste.bslstage`) faz o "Salvar como" usar esse caminho e pular o
diálogo; fora do `--qa` ela é ignorada. O diálogo de verdade fica no cenário opcional `projeto_dialogo` (`--only projeto_dialogo`, sem a
variável). Nenhum cenário deixa um diálogo nativo aberto quando falha (`close_stray_dialogs`). Cenários que criam camadas (Mapeamento,
Pixel Map, Transform) deixam a grade rolando: o `Tester` rola a grade ao fim e volta à composição inicial (Ctrl+N) antes de `grade`.

## Menu Exibir (cenários `exibir` e `exibir_reinicio`)

Menu `Exibir` (antigo "Ver"). Ids: caixa de cada painel `##exibir_<chave>` (`saida`, `previa`, `composicao`, `clip`, e no submenu `##exibir_abas`:
`arquivos`, `fontes`, `efeitos`, `aba-composicao`, `aba-camada`, `aba-saida`; `status` = barra de status; no Mapeamento `map-lista`, `map-entrada`,
`map-saida`, `map-propriedades`; no Pixel Map `pm-config`, `pm-overview`, `pm-tabela`), `##exibir_mostrar_todos`, submenu `##exibir_soltar` com
`##exibir_soltar_<chave>`, `##exibir_tela_cheia`, `##exibir_bloquear`; em Layout: `##layout_salvar` (Salvar layout atual como…), `##layout_excluir`
(submenu com `##layout_excluir_N`; janela `Excluir layout`: `##layout_excluir_ok`, `##layout_excluir_cancelar`) e `##layout_restaurar` (Restaurar
padrão). Estado: `state.view` = `hidden`, `detached`, `locked`, `fullscreen`, `viewports` (janelas do sistema do ImGui; a principal conta),
`mainPos` e `panels[]` (`key`, `visible`, `detached`, `docked`, `seen`, `rect` = x, y, w, h da janela do painel em coordenadas da janela principal;
some `mainPos` para a tela). O snapshot inteiro está em coordenadas da janela principal (o ImGui trabalha em coordenadas da tela por causa dos
viewports; `app/Qa.cpp` subtrai a origem). `driver.App(..., keep=True)` reabre o app com o appdata da execução anterior (teste de reinício);
o cenário `exibir_reinicio` abre as próprias instâncias. `py -3 tools/uiqa/docshots.py exibir` gera `docs/screenshot-exibir.png`.

## DMX / Art-Net (cenário `dmx`)

Menu `Saída`: `##menu_dmx` abre a janela `DMX / Art-Net` (Esc fecha). Ids: abas `##dmx_aba_monitor`, `##dmx_aba_patch`, `##dmx_aba_rede`; Monitor:
`##dmx_monitor_universo` (combo), `##dmx_grade` (os 512 canais), linhas dos universos `N##dmx_univ_N`; Rede: `##dmx_ligar` (Receber DMX), `##dmx_placa`
(combo; itens `##dmx_placa_<ip>`, o loopback é `##dmx_placa_127.0.0.1`), `##dmx_universos_texto`, `##dmx_artnet`, `##dmx_sacn`, `##dmx_nome`,
`##dmx_artnet_base`, `##dmx_mistura`, `##dmx_timeout`, `##dmx_perda`, `##dmx_prioridade`; Patch: `##dmx_add_camada`, `##dmx_add_master`, `##dmx_auto`,
`##dmx_exportar` (abre o diálogo de pasta: fora do fuzz), por linha `##dmx_on`, `##dmx_camada`, `##dmx_modo`, `##dmx_universo`, `##dmx_endereco`, `##dmx_fx_btn`,
`##dmx_remover`; canais aprendidos `##dmx_m_*`. Barra de status: `##dmx_indicador`. DMX learn: botão direito num campo numérico, numa célula, coluna, camada ou no olho do
efeito: `##dmx_atribuir` (e `##dmx_remover`, `##dmx_atribuir_acao` nos alvos de ação); janela `DMX learn`: `##dmx_learn_ligar`, `##dmx_learn_cancelar`.
Estado: `state.dmx` = `state` (0 desligado, 1 aguardando, 2 recebendo, 3 sem sinal, 4 erro), `running`, `enabled`, `status`, `universes[]` (`universe`, `active`,
`packets`, `pps`), `windowOpen`, `tab`, `learning`, `blocks`, `maps`, `applied`.

O cenário liga o receptor **só no loopback** (nada abre na rede, sem aviso do Firewall), faz o patch automático, manda ArtDmx reais por UDP para `127.0.0.1:6454`
(opacidade pelo patch) e aprende um canal pelo botão direito. `py -3 tools/uiqa/run.py --config window --only dmx`. Screenshots de documentação (com dados reais por
loopback; a aba Monitor não usa SendInput): `py -3 tools/uiqa/dmxshot.py` gera `docs/screenshot-dmx.png` (`--aba patch --saida docs/screenshot-dmx-patch.png` para o Patch).

## Atualizações e reportes (cenário `atualizacao`)

Menu Ajuda: `##menu_atualizar`, `##menu_atualizacao_nova` (só com versão nova), `##menu_reportar`, `##menu_sugerir`. Janela Atualizações: `##atualizacao_aviso` (o ponto na
barra de status), `##atualizacao_baixar`, `##atualizacao_lembrar`, `##atualizacao_pular`, `##atualizacao_auto`, `##atualizacao_beta`, `##atualizacao_procurar`. Janela de reporte:
`##reportar_titulo`, `##reportar_descricao`, `##reportar_passos`, `##reportar_sistema`, `##reportar_log`, `##reportar_abrir`, `##reportar_copiar`, `##reportar_fechar`; Esc fecha as duas.
O snapshot traz `state.update` (`phase`, `hasUpdate`, `version`, `notice`, `noticeText`, `windowOpen`, `autoCheck`, `beta`, `skipped`, `lastCheck`...) e `state.report` (`open`, `kind`, `system`,
`log`, `bodyLength`, `urlLength`, `truncated`, `lastUrl`, `lastClipboard`).

No `--qa` **nunca há rede**: o transporte das atualizações é um `FakeHttp` ("nenhuma versão publicada"), e "Abrir no GitHub" não abre o navegador (grava `state.report.lastUrl`).
`BSLStage.exe --qa <pasta> --fake-release <pasta com release.json>` simula um release (e os assets que existirem na pasta). `py -3 tools/uiqa/updshots.py` usa isso para gerar
`docs/screenshot-atualizacao.png` (aviso na barra de status + janela de notas) e `docs/screenshot-reportar.png`.

## Licenças, privacidade e termos (cenários `licencas` e `boas_vindas`)

Menu Ajuda: `##menu_licencas` (Licenças e avisos…) e "Sobre" (`##sobre_codigo`, `##sobre_licencas`). Janela: abas `##legal_aba_{licenca,terceiros,privacidade,termos}`, `##legal_codigo`,
`##legal_texto_completo`, `##legal_fechar`; na aba Terceiros cada componente é `##tp_<n>` e o texto da licença dele `##tp_<n>_<m>`. O snapshot traz `state.legal` (`windowOpen`, `tab`, `noticeVersion`, `thirdParty`, `licenseFull`, `openedTexts`, `docsFound`, `lastUrl`).

O navegador nunca abre no `--qa` (o link do código-fonte grava `state.legal.lastUrl`). `py -3 tools/uiqa/legalshots.py` gera `docs/screenshot-licencas.png` (aba Terceiros).
