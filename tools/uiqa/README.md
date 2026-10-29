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
| Presets de layout | menu Ver > Layout: `##layout_salvar`, itens dos presets pelo nome (`Palco A`), `##layout_gerenciar`. Janela `Salvar layout`: `##layout_nome`, `##layout_incluir_escala`, `##layout_salvar_ok`. Janela `Gerenciar layouts`: `##layout_N_aplicar`, `##layout_N_renomear`, `##layout_N_exportar`, `##layout_N_excluir` (N = 1 é o primeiro da lista), `##layout_importar`, `##layout_fechar`. Estado: `state.layoutPresets` (nomes, na ordem), `state.lastLayoutPreset`; o retângulo de um painel vem do item de janela (`app.window_rect("cmp_grade")`, `rect_of` em `run.py`) |
| Menu Arquivo | `##menu_novo`, `##menu_abrir`, `##menu_recentes`, `##menu_salvar`, `##menu_salvar_como`, `##menu_sair` |

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
Ver > Compatibilidade de vídeo (`##menu_compat`, itens `compat:ndi_status`, `compat:spout_status`, botão `##compat_fechar`) e o
destino **NDI** da Screen (`##destino`, `##ndi`, `##ndiFormato`). O estado do snapshot ganha `state.ndi` (`available`, `version`,
`error`, `sources`, `outputs` com `name` e `connections`), `state.spout.senders`, `state.screens[i].kind` e `state.selClip.status`.
Sem o runtime do NDI confere o aviso; com `--ndi-lib <dll>` (só para testes) faz o loopback: a fonte NDI recebendo a saída NDI do
próprio app. `py -3 tools/uiqa/run.py --config window --only ndi,fuzz [--ndi-lib <dll>]`. O botão que abre o navegador
(`##ndi_link_abrir`) fica fora do fuzz. `tools/uiqa/docshots.py ndi` gera `docs/screenshot-ndi.png`.

Os presets também têm teste sem janela: `build\Release\bslstage_uitest.exe` (JSON, preset inválido, troca de trechos do `imgui.ini`, pasta
de presets, exportar/importar).

Screenshots em `docs/qa/<config>_<estado>.png`. Codigo de saida 0 = nenhum passo falhou, sem assert nem conflito de ID.
Python 3 sem dependencias extras (so `ctypes`). Nao mexa no mouse/teclado enquanto roda.

`--ui-scale <f>` e `--ui-mode window|system|fixed` tambem valem no app normal (para testes).
