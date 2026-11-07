---
name: bsl-stage-plugin
description: Use quando o usuário pedir para criar, editar, corrigir ou validar um plugin para o BSL Stage, por exemplo "faça um efeito que trema no ritmo da música", "crie um gerador de ondas", "esse shader ISF dá erro", "quero um plugin FFGL". Cria efeitos e geradores como shaders ISF (.fs), valida com bslstage_isfcheck e instala em Documents\BSL Stage\Plugins.
---

# Criar plugins para o BSL Stage

O BSL Stage carrega dois tipos de plugin, ambos padrões abertos:

- **ISF** (`.fs`): um shader GLSL com cabeçalho JSON. É o caminho padrão (simples, leve, recompila sozinho quando o arquivo é salvo).
- **FFGL 2.x** (`.dll`): C++ para quem precisa de mais. Só use quando o ISF não bastar.

Documentação (leia antes de escrever): `docs/plugins/PARA-IA.md` (contrato, modelos, checklist, erros comuns), `docs/plugins/ISF-referencia.md` (tudo o que o host suporta), `docs/plugins/boas-praticas.md`, `docs/plugins/licencas.md`.
Para FFGL: `docs/plugins/FFGL-guia.md` e `sdk/ffgl-template/`. Dentro desta pasta de skill, os modelos prontos estão em `templates/`.

## Passo a passo

1. **Entender o pedido.** Defina: efeito (altera a imagem de um clip ou camada) ou gerador (cria a imagem)? Quais controles o cliente quer ajustar ao vivo (número, cor, liga/desliga, lista, ponto, botão)? Reage ao áudio? Precisa lembrar do quadro anterior (rastro)?
   Não faça interrogatório: se faltar detalhe, assuma o mais simples e informe o que assumiu.
2. **Escolher o modelo** em `templates/` e copiar:
   - `efeito-em-branco.fs`, `efeito-cor-ajuste.fs`, `efeito-distorcao-uv.fs`, `efeito-feedback.fs`, `efeito-audio.fs`
   - `gerador-em-branco.fs`, `gerador-cor-degrade.fs`, `gerador-padrao.fs`, `gerador-audio.fs`
3. **Escrever o `.fs`.** Nome do arquivo = nome do plugin (curto, claro, em português). Cabeçalho completo (`DESCRIPTION`, `CREDIT`, `CATEGORIES`, `INPUTS` com `LABEL` em português). Regras: sem `#version`, sem `uniform`, saída em `gl_FragColor` com alfa reto, gerador com alfa 1.0,
   sem `while`, `NAME` igual ao nome usado no código. Nunca copie shaders de terceiros.
4. **Validar** com o validador do programa (mesmo leitor e host do aplicativo):

   ```
   build\Release\bslstage_isfcheck.exe "Meu Plugin.fs" --png previa.png --time 2 --size 960x540
   ```

   Código de saída 0 = compilou e desenhou. 1 = erro (o relatório mostra `arquivo:linha: mensagem` e o texto da linha). 3 = compilou, mas a saída é toda preta/transparente. 2 = uso errado ou sem OpenGL 3.3.
   Outras opções: `--set nome=valor`, `--input imagem.png`, `--audio`, `--json`, `--png-dir pasta`.
5. **Corrigir** até passar sem erros e sem avisos relevantes. Abra o PNG e confirme que se parece com o pedido. Teste também com `--time` maior, com valores extremos (`--set`) e, se for reativo, com `--audio`.
6. **Instalar**: salve em `Documents\BSL Stage\Plugins` (subpastas valem). O programa percebe o arquivo em cerca de um segundo; efeitos aparecem em Efeitos > ISF e geradores em Fontes > ISF, no grupo da categoria.
   Se o plugin já estava em uso, salvar de novo recompila sem perder os valores; em caso de erro a versão anterior continua e o painel do efeito mostra a linha.
7. **Entregar** ao cliente: caminho do arquivo, lista de controles (nome, faixa, padrão), onde achar no programa, o que foi assumido e o resultado da validação. Para compartilhar: botão direito sobre o plugin na aba Efeitos > Exportar .bslplugin.

## Referências rápidas

- Tipos de entrada: `float`, `bool`, `long` (lista), `color`, `point2D` (em pixels de 1920x1080), `event` (botão), `image` (só `inputImage`), `audioFFT` (espectro 32 x 1).
- Não suportados: `audio` (forma de onda), `cube`, imagens extras de entrada.
- Variáveis prontas: `isf_FragNormCoord` (y = 0 é a base), `RENDERSIZE`, `TIME`, `TIMEDELTA`, `DATE`, `FRAMEINDEX`, `PASSINDEX`. Macros: `IMG_NORM_PIXEL`, `IMG_THIS_NORM_PIXEL`, `IMG_PIXEL`, `IMG_THIS_PIXEL`, `IMG_SIZE`.
- Memória entre quadros: `PASSES` com `PERSISTENT: true` (modelo `efeito-feedback.fs`).
- Para PC fraco: poucas leituras de textura, laços `for` com limite fixo, `FLOAT` só quando preciso, custo estimado em 1080p abaixo de 0,5 ms na placa de desenvolvimento.

## Plugin FFGL (C++)

Somente se o cliente exigir. Copie `sdk/ffgl-template/`, troque o id de 4 letras, o nome e o shader, compile (`README.md` da pasta) e instale a DLL em `Documents\BSL Stage\Plugins`. Nesse caminho a imagem é alfa pré-multiplicado.

## Conduta

- Não use código, shaders, imagens ou plugins de terceiros sem permissão; escreva a sua versão e registre o autor em `CREDIT`.
- Não altere nada fora de `Documents\BSL Stage\Plugins` (e da pasta do projeto, se estiver desenvolvendo).
- Sem rede e sem commit, a não ser que o usuário peça.
