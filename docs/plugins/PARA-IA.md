# Para IAs: criar plugins para o BSL Stage a partir do pedido do cliente

Este documento é auto-suficiente. Se você é uma IA (ou uma pessoa com pressa) e precisa transformar um pedido como "quero um efeito que faça a imagem tremer no ritmo da música" em um plugin que funciona, siga este arquivo na ordem.
Detalhes e tabelas completas: `ISF-referencia.md`. O fluxo em forma de skill: `skills/bsl-stage-plugin/SKILL.md`.

## 1. O que é o BSL Stage

Programa de VJ, mapping e painel de LED para Windows (C++20, OpenGL 3.3 core, GPL-3.0). O cliente monta uma grade de clips e camadas, aplica efeitos (nativos, ISF e FFGL) e manda para as saídas. Um **plugin ISF** é um arquivo `.fs` de texto
com um cabeçalho JSON e um shader GLSL; é o caminho certo em 95% dos pedidos. Um **plugin FFGL** é uma DLL em C++ (seção 10) e só se justifica quando o ISF não dá conta.

## 2. Fluxo obrigatório

1. **Entender o pedido.** É um efeito (modifica a imagem de um clip ou camada) ou um gerador (cria imagem do zero)? Quais controles o cliente quer mexer ao vivo? Reage ao áudio? Precisa de memória do quadro anterior (rastro)?
   Se faltar informação, escolha o padrão mais simples e diga o que assumiu; não faça perguntas longas.
2. **Escolher o modelo** (seção 5) mais próximo e copiá-lo.
3. **Escrever o `.fs`** em `Documents\BSL Stage\Plugins\<Nome do plugin>.fs`. O nome do arquivo é o nome do plugin na interface.
4. **Validar** com `bslstage_isfcheck` (seção 6). Corrigir até sair com código 0 e sem avisos que importem.
5. **Conferir a prévia** (`--png`): abra a imagem e confirme que parece com o pedido.
6. **Entregar** o arquivo e uma explicação curta em português: o que faz, quais controles tem, onde aparece no programa.

Nunca entregue um shader que você não validou.

## 3. Contrato do ISF aceito (resumo)

Arquivo `.fs`, UTF-8: um cabeçalho JSON dentro do primeiro `/*{ ... }*/` e depois GLSL.

Cabeçalho:

```
"DESCRIPTION": texto curto           "CREDIT": autor            "ISFVSN": "2.0"
"CATEGORIES": ["Meus plugins"]       (só a primeira categoria vale)
"INPUTS": [ ... ]                    "PASSES": [ ... ] (opcional)       "IMPORTED": { ... } (opcional)
```

Entradas (`NAME` = identificador ASCII, vira variável GLSL; `LABEL` = texto na interface, em português):

| TYPE | GLSL | Chaves |
|---|---|---|
| `image` | sampler | Só `inputImage` recebe imagem. Se existir, o plugin é **efeito**; sem ele é **gerador**. Outras entradas `image` ficam vazias. |
| `float` | `float` | `DEFAULT`, `MIN` (0), `MAX` (1) |
| `bool` | `bool` | `DEFAULT` |
| `long` | `int` | `VALUES` [inteiros], `LABELS` [textos], `DEFAULT` |
| `color` | `vec4` | `DEFAULT` [r,g,b,a] de 0 a 1, alfa reto |
| `point2D` | `vec2` | `DEFAULT`, `MIN`, `MAX` em **pixels de uma tela 1920x1080** (divida por `vec2(1920.0, 1080.0)`) |
| `event` | `bool` | Botão; verdadeiro por um quadro |
| `audioFFT` | sampler 32 x 1 | Espectro: `IMG_NORM_PIXEL(espectro, vec2(x, 0.5)).r`, x de 0 (grave) a 1 (agudo), valor de 0 a 1 |

**Não existem:** `audio` (forma de onda), `cube`, imagens de entrada extras (ficam vazias), transições (`startImage`/`endImage`).

Variáveis prontas (não declare): `isf_FragNormCoord` (`vec2`, 0..1, **y = 0 é a base**), `RENDERSIZE` (`vec2`, pixels), `TIME` (segundos), `TIMEDELTA`, `DATE` (`vec4`), `FRAMEINDEX` (`int`), `PASSINDEX` (`int`).
Macros: `IMG_NORM_PIXEL(img, uv)`, `IMG_PIXEL(img, px)`, `IMG_THIS_NORM_PIXEL(img)`, `IMG_THIS_PIXEL(img)`, `IMG_SIZE(img)`. O primeiro argumento é o nome de uma imagem escrito direto.
Saída: `gl_FragColor` (`vec4`, **alfa reto**; em gerador use alfa 1.0).

Passes: `"PASSES": [ { "TARGET": "nome", "PERSISTENT": true, "FLOAT": true, "WIDTH": "$WIDTH/2", "HEIGHT": "$HEIGHT/2" }, { } ]`. O último passe vai para a saída. `PERSISTENT` mantém o buffer entre quadros (rastro/feedback). Buffers
começam transparentes. `FLOAT` = 32 bits por canal (mais pesado).

Imagens do plugin: `"IMPORTED": { "logo": { "PATH": "logo.png" } }`, arquivo ao lado do `.fs`, lida com `IMG_NORM_PIXEL(logo, uv)`.

Limites: arquivo até 1 MB, 128 entradas, 32 passes.

## 4. Regras que as IAs mais erram

1. **Não escreva `#version`.** O host acrescenta `#version 330 core`.
2. **Não declare `uniform`** para as entradas nem para `TIME`: dá erro de redefinição.
3. **Não use `texture2D`/`sampler2D` direto** com nomes que você inventou. Leia imagens só com as macros `IMG_*`.
4. **Nomes**: o `NAME` do cabeçalho e o nome no código têm que ser idênticos (maiúsculas contam). Sem acento, sem espaço, sem nome de função do GLSL (`mix`, `step`, `length`...) nem das variáveis prontas.
5. **Gerador sem alfa**: `gl_FragColor = vec4(cor, 1.0)`; `vec3` sozinho não compila.
6. **Alfa**: efeito deve devolver `src.a` (ou calcular o alfa de propósito). Não multiplique a cor pelo alfa na saída. Só buffers dos `PASSES` guardam cor pré-multiplicada, e o último passe volta para alfa reto.
7. **Divisão por zero e `NaN`** em feedback: proteja com `max(x, 0.0001)`.
8. **`smoothstep(a, b, x)` com `a >= b`** é indefinido: use `1.0 - smoothstep(menor, maior, x)`.
9. **Laços**: só `for` com limite fixo pequeno. Sem `while`.
10. **Acentos no GLSL** só em comentários ou nem isso; o JSON do cabeçalho pode ter acentos.
11. **JSON**: aspas duplas, vírgulas entre itens, nenhuma vírgula depois do último. Sem comentários no JSON.
12. **Coordenadas**: não inverta `y`. Use `RENDERSIZE.x / RENDERSIZE.y` para manter círculos redondos.
13. **Leia o buffer no mesmo passe** que o grava só se for `PERSISTENT` (você lê o quadro anterior).
14. Não copie shaders da internet: veja `licencas.md`. Escreva o seu.

## 5. Modelos prontos

Todos validados pelo `bslstage_plugintest` (cada bloco abaixo é compilado nos testes). Os mesmos modelos, mais variantes, estão em `skills/bsl-stage-plugin/templates/`.

### Efeito mínimo

```
/*{
    "DESCRIPTION": "Efeito em branco: repete a imagem de entrada.",
    "CREDIT": "Modelo livre (CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" }
    ]
}*/

void main()
{
    gl_FragColor = IMG_THIS_NORM_PIXEL(inputImage);
}
```

### Gerador mínimo

```
/*{
    "DESCRIPTION": "Gerador em branco: um degradê que se move com o tempo.",
    "CREDIT": "Modelo livre (CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": []
}*/

void main()
{
    vec2 uv = isf_FragNormCoord;
    gl_FragColor = vec4(uv.x, uv.y, 0.5 + 0.5 * sin(TIME), 1.0);
}
```

### Efeito com um controle de cada tipo

```
/*{
    "DESCRIPTION": "Modelo com um controle de cada tipo: número, liga/desliga, lista, cor, ponto e botão.",
    "CREDIT": "Modelo livre (CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "intensidade", "LABEL": "Intensidade", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 1.0 },
        { "NAME": "ligado", "LABEL": "Ligado", "TYPE": "bool", "DEFAULT": true },
        { "NAME": "modo", "LABEL": "Modo", "TYPE": "long", "DEFAULT": 0, "VALUES": [0, 1, 2], "LABELS": ["Normal", "Invertido", "Preto e branco"] },
        { "NAME": "cor", "LABEL": "Cor do halo", "TYPE": "color", "DEFAULT": [1.0, 0.4, 0.1, 1.0] },
        { "NAME": "centro", "LABEL": "Centro (px em 1920x1080)", "TYPE": "point2D", "DEFAULT": [960.0, 540.0], "MIN": [0.0, 0.0], "MAX": [1920.0, 1080.0] },
        { "NAME": "flash", "LABEL": "Flash", "TYPE": "event" }
    ]
}*/

void main()
{
    vec2 uv = isf_FragNormCoord;
    vec4 src = IMG_NORM_PIXEL(inputImage, uv);
    vec3 c = src.rgb;
    if (modo == 1)
        c = 1.0 - c;
    else if (modo == 2)
        c = vec3(dot(c, vec3(0.2126, 0.7152, 0.0722)));
    float aspect = RENDERSIZE.x / RENDERSIZE.y;
    vec2 d = (uv - centro / vec2(1920.0, 1080.0)) * vec2(aspect, 1.0);
    float halo = 1.0 - smoothstep(0.0, 0.5, length(d));
    c = mix(c, cor.rgb, halo * intensidade);
    if (flash)
        c = vec3(1.0);
    if (!ligado)
        c = src.rgb;
    gl_FragColor = vec4(c, src.a);
}
```

### Gerador reativo ao áudio

```
/*{
    "DESCRIPTION": "Barras que dançam com o som do PC ou do microfone.",
    "CREDIT": "Modelo livre (CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": [
        { "NAME": "espectro", "LABEL": "Espectro do áudio", "TYPE": "audioFFT" },
        { "NAME": "ganho", "LABEL": "Ganho do áudio", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0 },
        { "NAME": "corBaixo", "LABEL": "Cor embaixo", "TYPE": "color", "DEFAULT": [0.1, 0.6, 1.0, 1.0] },
        { "NAME": "corAlto", "LABEL": "Cor em cima", "TYPE": "color", "DEFAULT": [1.0, 0.2, 0.6, 1.0] }
    ]
}*/

float faixa(float x)
{
    return IMG_NORM_PIXEL(espectro, vec2(x, 0.5)).r;
}

void main()
{
    vec2 uv = isf_FragNormCoord;
    float v = faixa((floor(uv.x * 32.0) + 0.5) / 32.0) * ganho;
    v = max(v, 0.04 + 0.03 * sin(TIME * 2.0 + uv.x * 12.0));
    float coluna = fract(uv.x * 32.0);
    float barra = (1.0 - smoothstep(v - 0.01, v + 0.01, uv.y)) * step(0.1, coluna) * step(coluna, 0.9);
    vec3 fundo = vec3(0.05, 0.05, 0.12) + 0.06 * uv.y;
    vec3 cor = mix(corBaixo.rgb, corAlto.rgb, uv.y);
    gl_FragColor = vec4(mix(fundo, cor, barra), 1.0);
}
```

### Efeito com memória (rastro)

```
/*{
    "DESCRIPTION": "Rastro: o quadro anterior fica na tela e vai se apagando.",
    "CREDIT": "Modelo livre (CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "persistencia", "LABEL": "Persistência", "TYPE": "float", "DEFAULT": 0.9, "MIN": 0.0, "MAX": 0.99 },
        { "NAME": "limpar", "LABEL": "Limpar rastro", "TYPE": "event" }
    ],
    "PASSES": [
        { "TARGET": "rastro", "PERSISTENT": true, "FLOAT": true },
        { }
    ]
}*/

void main()
{
    if (PASSINDEX == 0) {
        vec4 src = IMG_THIS_NORM_PIXEL(inputImage);
        vec4 novo = vec4(src.rgb * src.a, src.a);
        vec4 antigo = IMG_THIS_NORM_PIXEL(rastro) * persistencia;
        vec4 saida = novo + antigo * (1.0 - novo.a);
        if (limpar)
            saida = novo;
        gl_FragColor = saida;
    } else {
        vec4 t = IMG_THIS_NORM_PIXEL(rastro);
        float a = min(t.a, 1.0);
        gl_FragColor = a > 0.0 ? vec4(min(t.rgb / a, vec3(1.0)), a) : vec4(0.0);
    }
}
```

Modelos de arquivo em `skills/bsl-stage-plugin/templates/`: `efeito-em-branco`, `efeito-cor-ajuste`, `efeito-distorcao-uv`, `efeito-feedback`, `efeito-audio`, `gerador-em-branco`, `gerador-cor-degrade`, `gerador-padrao`, `gerador-audio`.
Os mesmos que o assistente "Novo plugin…" do programa gera.

## 6. Validar com `bslstage_isfcheck`

O validador compila o shader com o **mesmo leitor e o mesmo host do aplicativo**, desenha um quadro fora da tela e imprime os erros com a linha. Fica em `build\Release\bslstage_isfcheck.exe` (build do repositório) ou ao lado do
`BSLStage.exe`. Precisa de uma placa de vídeo com OpenGL 3.3.

```
bslstage_isfcheck <arquivo.fs | pasta> [outros ...] [opções]
  --png <saida.png>    grava o quadro final (um arquivo só)       --png-dir <pasta>   um PNG por shader
  --time <segundos>    TIME do quadro final (padrão 0)            --size <LxA>        tamanho do quadro (padrão 640x360)
  --input <imagem>     imagem de entrada dos efeitos (padrão: cartela de teste com quatro quadrantes e grade)
  --set nome=valor     valor de um controle (repita). cor: r,g,b[,a]   ponto: x,y   bool: true/false   lista: o valor (0, 1...)
  --audio              espectro de áudio sintético (para audioFFT)
  --json               relatório em JSON    --allow-blank   saída preta/transparente não é erro
```

Códigos de saída: `0` tudo certo; `1` erro (arquivo ilegível, JSON inválido ou GLSL que não compila); `2` uso errado ou sem OpenGL 3.3; `3` compila mas a saída é toda preta ou transparente.

Saída em texto (resumo):

```
Arquivo: Plasma.fs
Resultado: OK (compilou e desenhou)
Tipo: gerador   Passes: 1   Categorias: Generator
Parâmetros (6):
  corA               color     padrão 0.1,0.2,0.9,1   rótulo: Cor A
  ...
Avisos:
  - ...
Quadro 640x360: média RGBA (164, 51, 183, 255) de 255; 100.0% dos pixels acesos; canal máximo 255
Custo: 0.02 ms por quadro neste tamanho (~0.9 ms estimados em 1920x1080)
```

Erro de shader (formato `arquivo:linha: mensagem`, seguido do texto da linha):

```
ERROS:
  Plasma.fs:21: error C1503: undefined variable "velocidad"
      21 |     float t = TIME * velocidad;
```

A mensagem exata depende do driver da placa (NVIDIA, AMD, Intel). A linha é sempre a do seu arquivo. Com `--json` o mesmo resultado vem em JSON: `ok`, `errors` (`line`, `message`, `source`), `warnings`, `params`, `stats`.

Receita de validação completa:

```
bslstage_isfcheck "Meu Plugin.fs" --png previa.png --time 2 --size 960x540
bslstage_isfcheck "Meu Plugin.fs" --png previa2.png --time 2 --set intensidade=1 --audio
```

Procure no relatório: `Resultado: OK`, parâmetros com os nomes e faixas que você planejou, nenhum aviso de `CATEGORIES`/`DESCRIPTION`/`#version`/`uniform`, "pixels acesos" acima de alguns por cento (a menos que seja de propósito), custo baixo.

## 7. Checklist final

- [ ] O arquivo começa com `/*{` e o JSON fecha com `}*/`; vírgulas conferidas.
- [ ] Efeito tem `inputImage` e **lê** a imagem; gerador não tem `inputImage` e termina com alfa 1.0.
- [ ] Todo `NAME` do cabeçalho é usado no código com a mesma grafia, ou foi retirado.
- [ ] `DESCRIPTION`, `CREDIT`, `CATEGORIES` e `LABEL` em português preenchidos.
- [ ] Faixas `MIN`/`MAX`/`DEFAULT` fazem sentido e o padrão dá um resultado bonito e seguro.
- [ ] Sem `#version`, sem `uniform`, sem `while`, sem acentos fora de comentários, sem `texture2D` direto.
- [ ] `bslstage_isfcheck` saiu com código 0, sem avisos relevantes, e a prévia PNG foi olhada.
- [ ] Testado também com `--time` maior que 0 e (se reativo) `--audio`.
- [ ] Nada copiado de terceiros; `CREDIT` com o autor.
- [ ] Nome do arquivo definitivo (renomear depois quebra projetos).

## 8. Instalar e ver no programa

1. Salve em `Documents\BSL Stage\Plugins` (a pasta é criada pelo programa na primeira vez). Subpastas funcionam.
2. Com o BSL Stage aberto, o plugin aparece sozinho em cerca de um segundo: efeitos na aba **Efeitos > ISF**, geradores na aba **Fontes > ISF**, no grupo da categoria.
3. Salvar de novo o arquivo recompila o efeito em uso sem perder os valores. Se houver erro, o painel do efeito mostra a linha e a versão anterior continua funcionando.
4. Para entregar a outra pessoa: aba Efeitos, botão direito sobre o plugin, **Exportar .bslplugin…** (cria um pacote com `.fs`, imagens e `manifest.json` com nome, autor, versão e licença).

## 9. Erros comuns e como corrigir

| Mensagem ou sintoma | Causa | Correção |
|---|---|---|
| `Cabeçalho JSON inválido na linha N` | Vírgula, aspa ou colchete errado | Corrigir na linha N do arquivo |
| `Sem o cabeçalho JSON do ISF` | Faltou `/*{ ... }*/` no início | Adicionar o cabeçalho |
| `Linha N: undefined variable "x"` | Nome diferente do `NAME` ou variável não declarada | Igualar os nomes |
| `Linha N: redefinition` ou `already defined` | Declarou algo que o host já declara | Apagar a declaração |
| `Linha N: ... cannot convert from 'vec3' to 'vec4'` ou semelhante | Tipos de GLSL misturados (`gl_FragColor = vec3`) | `vec4(cor, 1.0)` |
| `Linha N: ... no matching overloaded function` | Argumentos errados em `mix`, `step`, `smoothstep`... | Conferir tipos (`float` e `vec3` não se misturam sem conversão); `2` vira `2.0` |
| `Linha N: ... '#version' ...` | Escreveu `#version` | Remover |
| `Falha ao ligar o shader` | Faltou o `void main()` ou há função repetida | Conferir |
| Saída toda preta (código 3) | Não escreve em `gl_FragColor`, alfa 0, efeito ignora a entrada ou cor sempre 0 | Rever a conta; testar `gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0)` para separar o problema |
| Imagem esticada ou círculo oval | Faltou correção de proporção | Multiplicar `x` por `RENDERSIZE.x / RENDERSIZE.y` |
| Efeito sem reação ao áudio no teste | Esqueceu `--audio`, ou lê a faixa errada | Conferir o `audioFFT` no cabeçalho e rodar com `--audio` |
| Controle não aparece no programa | `TYPE` inválido ou `NAME` inválido (aviso no relatório) | Corrigir o cabeçalho |
| Rastro que "queima" a imagem em branco ou preto | Feedback sem `FLOAT` ou ganho acima de 1 por quadro | `FLOAT: true`, `persistencia` < 1 |

## 10. Quando o ISF não basta: plugin FFGL em C++

Use somente se o cliente precisa de C++ (bibliotecas, geometria, leitura de arquivos, processamento no CPU). A pasta `sdk/ffgl-template/` tem um modelo completo (efeito com dois parâmetros), `CMakeLists.txt` e `README.md` com o passo a passo.
Resumo: copie o modelo, troque o id de 4 letras, o nome e o shader (GLSL 330), compile em Release x64 e copie a DLL para `Documents\BSL Stage\Plugins`. No FFGL as imagens são **alfa pré-multiplicado**. Detalhes: `FFGL-guia.md`.
Valide com `bslstage_ffgltest` (para o modelo) e abrindo o programa (Exibir > Plugins mostra bloqueios e motivos).

## 11. Regras de conduta

- Não copie shaders, código, imagens ou plugins de terceiros sem permissão (`licencas.md`). Escreva a sua versão.
- Não instale nada fora de `Documents\BSL Stage\Plugins` e não altere pastas de outros programas.
- Um plugin ISF não executa código no computador; mesmo assim, não inclua arquivos que não sejam `.fs`, `.vs`, imagens e texto no pacote.
- Ao entregar, informe o nome do arquivo, os controles (nome, faixa, padrão), o que assumiu e o resultado do `bslstage_isfcheck`.

## 12. Formato sugerido da resposta ao cliente

```
Criei o plugin "Nome" (efeito|gerador) em Documents\BSL Stage\Plugins\Nome.fs.
Controles: Intensidade (0 a 1, padrão 0,5), Cor (...).
Como usar: aba Efeitos > ISF > Meus plugins > Nome (arraste para o clip).
Validação: bslstage_isfcheck OK, prévia em previa.png, custo ~X ms em 1080p.
Para mudar: abra o arquivo no editor e salve; o efeito atualiza sozinho.
```
