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
| Painel Clip | `##clip_tocar_pausar`, `##clip_reiniciar`, `##loop_loop`, `##loop_uma_vez`, `##loop_pingpong`, `##velocidade`, `##paginaAnterior`, `##paginaSeguinte` |
| Navegador | `##acima` (pasta acima); as linhas continuam com o nome do arquivo (`foto.jpg`, `docs/`) |
| Mapeamento | `##map_add_tela`, `##map_add_slice`, `##map_duplicar`, `##map_remover`, `##map_identificar`; campos `##x ##y ##w ##h ##resW ##resH ##cx ##cy ##g ##brilho ##opacidade` |
| Menu Arquivo | `##menu_novo`, `##menu_abrir`, `##menu_recentes`, `##menu_salvar`, `##menu_salvar_como`, `##menu_sair` |

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

Screenshots em `docs/qa/<config>_<estado>.png`. Codigo de saida 0 = nenhum passo falhou, sem assert nem conflito de ID.
Python 3 sem dependencias extras (so `ctypes`). Nao mexa no mouse/teclado enquanto roda.

`--ui-scale <f>` e `--ui-mode window|system|fixed` tambem valem no app normal (para testes).
