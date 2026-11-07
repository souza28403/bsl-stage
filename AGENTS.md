# AGENTS.md: instruções para IAs que trabalham neste repositório

## O projeto

BSL Stage: software de VJ, mapping e painel de LED, open source (GPL-3.0), C++20, GLFW, OpenGL 3.3 core, Dear ImGui, nlohmann/json. Alvo: Windows 11, inclusive PC antigo (Intel HD 4000, 4 GB de RAM).
Arquitetura: `docs/ARQUITETURA.md`. Roadmap: `docs/ROADMAP.md`. Licenças: `docs/PATENTES-E-LICENCAS.md`, `THIRD-PARTY-NOTICES.md`.

## Se o pedido é criar um plugin (efeito ou gerador) para o cliente

Leia **`docs/plugins/PARA-IA.md`**: ele tem o contrato do ISF aceito pelo programa, modelos de código prontos, o checklist, os erros comuns e o passo a passo de teste. Em resumo:

1. Escolha efeito (tem `inputImage`) ou gerador (não tem). O caminho padrão é um shader **ISF** (`.fs`).
2. Parta de um modelo em `skills/bsl-stage-plugin/templates/`.
3. Salve em `Documents\BSL Stage\Plugins\<Nome>.fs`.
4. Valide: `build\Release\bslstage_isfcheck.exe "<arquivo>.fs" --png previa.png --time 2` (código de saída 0 = ok; mostra erros com a linha). Corrija até passar e olhe o PNG.
5. Entregue o arquivo com a lista de controles e o resultado da validação.

Só use C++ (plugin FFGL) quando o ISF não bastar: `sdk/ffgl-template/` e `docs/plugins/FFGL-guia.md`.

Regras para plugins: sem `#version`, sem `uniform` declarado, saída `gl_FragColor` com alfa reto, gerador termina com alfa 1.0, nada copiado de terceiros (`docs/plugins/licencas.md`).
Mais detalhes (tipos de entrada, variáveis, passes, o que não é suportado): `docs/plugins/ISF-referencia.md`.

A skill com o mesmo fluxo está em `skills/bsl-stage-plugin/SKILL.md`.

## Se o pedido é mexer no código do programa

- Compilar: `cmake --preset windows` e `cmake --build --preset release` na raiz. O build usa `/W4` e precisa terminar sem avisos novos.
- Testes (executáveis em `build\Release`, prefixo `bslstage_`): `mediatest testmedia`, `spouttest`, `projecttest`, `livetest`, `doctest`, `ffgltest`, `bsltest`, `maptest`, `uitest`, `pixelmaptest`, `aitest`, `keystest`,
  `fxtest`, `transformtest`, `dmxtest`, `updatetest`, `plugintest` e `ndittest --ndi-lib <dll>`. Interface: `python tools/uiqa/run.py --config window` (precisa da estação desbloqueada; não mexa no mouse enquanto roda).
- Plugins do usuário: `src/plugin/` (modelos do assistente, arquivos, pacote `.bslplugin`, validador), `src/fx/Isf*.{h,cpp}` (leitor e host ISF), `src/ui/panels/PluginWizard.*` (janelas), `tools/isfcheck.cpp`, `tools/plugintest.cpp`.
- Arquivos de texto em UTF-8 com quebras de linha CRLF. Depois de editar arquivos com acento, confira se não sobrou caractere corrompido.
- Não faça commit, não use a rede e não altere pastas de outros programas nem o perfil do usuário sem o pedido explícito.
- Não copie código nem arquivos de outros programas; plugins de terceiros não são redistribuídos.
