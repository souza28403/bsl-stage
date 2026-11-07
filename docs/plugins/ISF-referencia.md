# ISF: referência completa do que o BSL Stage entende

Este documento foi conferido contra o código do leitor (`src/fx/IsfDoc.cpp`) e do host (`src/fx/IsfRenderer.cpp`, `src/fx/IsfLibrary.cpp`). Se algo aqui
disser "não suportado", o BSL Stage ignora a entrada (com um aviso) ou mostra um erro: não adianta insistir.

ISF (Interactive Shader Format) é um padrão aberto: um arquivo `.fs` com um cabeçalho JSON dentro de um comentário `/*{ ... }*/` seguido de código GLSL.
O BSL Stage escreveu o próprio leitor a partir da especificação pública; o código roda como GLSL 3.30 core (OpenGL 3.3).

## 1. Estrutura do arquivo

```
/*{
    "DESCRIPTION": "texto curto que aparece na dica do efeito",
    "CREDIT": "autor",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Color Effect"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" }
    ]
}*/

void main()
{
    gl_FragColor = IMG_THIS_NORM_PIXEL(inputImage);
}
```

Além de `INPUTS`, o cabeçalho pode ter `PASSES` e `IMPORTED` (seções 5 e 6).

- O arquivo é lido como UTF-8 (a marca BOM é aceita). Máximo de 1 MB por arquivo.
- O cabeçalho é o primeiro `/* ... */` do arquivo. Dentro dele valem as regras do JSON (aspas duplas, vírgulas entre itens, sem vírgula depois do último). O leitor
  tolera comentários `//` e `/* */` dentro do JSON, mas não escreva: outros programas recusam.
- Um erro de JSON aparece como "Cabeçalho JSON inválido na linha N do arquivo: ..." com a linha certa.
- O nome do plugin na interface é o **nome do arquivo** sem `.fs`. O mesmo vale para o `id` gravado nos projetos (`isf:<caminho relativo>`): renomear o arquivo cria
  um plugin novo, e os projetos antigos mostram o antigo em âmbar ("não encontrado").
- Um arquivo `.vs` com o mesmo nome (opcional) é o vertex shader. Quase ninguém precisa dele.
- **Efeito ou gerador?** Quem declara uma entrada `{ "NAME": "inputImage", "TYPE": "image" }` é **efeito** (recebe a imagem da camada). Sem ela é **gerador**
  (cria a imagem; aparece na aba Fontes, seção ISF).
- Limites: 128 entradas, 32 passes, 5000 shaders no total.

### Chaves do cabeçalho

| Chave | Uso |
|---|---|
| `DESCRIPTION` | Texto da dica na aba Efeitos. |
| `CREDIT` | Autor. Só informativo. |
| `ISFVSN` | Versão do ISF. Ignorada (escreva `"2.0"`). |
| `CATEGORIES` | Lista de textos. Só a **primeira** vale, e define o grupo na aba Efeitos. |
| `INPUTS` | Os controles (seção 2). |
| `PASSES` | Passes de desenho, buffers e persistência (seção 5). |
| `IMPORTED` | Imagens que viajam com o plugin (seção 6). |

Categorias que o BSL Stage traduz (o resto aparece como foi escrito): `Color Effect` e `Color` = Cor; `Distortion Effect` = Distorção; `Geometry Effect` = Geometria;
`Blur` = Desfoque; `Sharpen` = Nitidez; `Stylize` = Estilizar; `Tile Effect` = Mosaico; `Halftone Effect` = Meio-tom; `Glitch`; `Generator` = Geradores;
`Transition` = Transição; `Video` = Vídeo; `Utility` = Utilitários; `Feedback`. Sem `CATEGORIES`: efeitos vão para "Outros" e geradores para "Geradores".
O assistente do programa usa `"Meus plugins"`.

## 2. Tipos de entrada (`INPUTS`)

Cada entrada tem `NAME` (obrigatório) e `TYPE`; `LABEL` é o nome mostrado na interface (sem ele aparece o `NAME`).
`NAME` precisa ser um identificador: letras sem acento, números e `_`, começando por letra ou `_`. Nomes repetidos ou inválidos são ignorados com aviso.
O valor chega ao shader como uma variável com o mesmo nome (não declare nada no código).

| `TYPE` | Variável GLSL | Chaves | Observações |
|---|---|---|---|
| `float` | `float` | `DEFAULT`, `MIN`, `MAX` | Padrões: `MIN` 0, `MAX` 1, `DEFAULT` = `MIN`. Se `MAX <= MIN`, o `MAX` vira `MIN + 1`. O padrão é limitado à faixa. Controle: número arrastável. |
| `bool` | `bool` | `DEFAULT` | `true`/`false` ou 0/1. Padrão `false`. Controle: caixa. |
| `long` | `int` | `VALUES`, `LABELS`, `DEFAULT` | Lista de opções. `VALUES` são inteiros e `LABELS` os nomes (mesma ordem). Sem `VALUES`, vale a faixa `MIN`..`MAX` (no máximo 64 opções). `DEFAULT` é um dos valores. Controle: menu. |
| `color` | `vec4` | `DEFAULT` | `[r, g, b]` ou `[r, g, b, a]`, de 0 a 1, alfa reto. Sem alfa informado, vale 1. Controle: seletor de cor. |
| `point2D` | `vec2` | `DEFAULT`, `MIN`, `MAX` | Duas coordenadas em **pixels**. O valor não é normalizado: divida por uma resolução fixa (`vec2(1920.0, 1080.0)`) para obter 0..1 que não muda com o modo Eco. Sem `MIN`/`MAX`, a faixa vai de 0 a 4096. |
| `event` | `bool` | nenhuma | Botão. A variável é verdadeira **por um quadro** depois do clique. |
| `image` | `sampler2D` | nenhuma | `inputImage` é a imagem do efeito. Qualquer **outra** entrada `image` fica **vazia** (transparente): o BSL Stage não liga imagens extras. Para ter uma imagem no plugin use `IMPORTED`. |
| `audioFFT` | `sampler2D` | nenhuma | Espectro do áudio (ver seção 4). Suportado. |
| `audio` | `sampler2D` | nenhuma | Forma de onda: **não suportada**. A entrada fica vazia e aparece o aviso. Use `audioFFT`. |
| `cube` | `sampler2D` | nenhuma | **Não suportado.** Vira textura vazia com aviso. |

Qualquer outro `TYPE` é ignorado com aviso.

Exemplo de cada tipo:

```
{ "NAME": "inputImage", "TYPE": "image" },
{ "NAME": "forca", "LABEL": "Força", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 1.0 },
{ "NAME": "ligado", "LABEL": "Ligado", "TYPE": "bool", "DEFAULT": true },
{ "NAME": "modo", "LABEL": "Modo", "TYPE": "long", "DEFAULT": 0, "VALUES": [0, 1, 2], "LABELS": ["Normal", "Invertido", "Espelhado"] },
{ "NAME": "cor", "LABEL": "Cor", "TYPE": "color", "DEFAULT": [1.0, 0.5, 0.0, 1.0] },
{ "NAME": "centro", "LABEL": "Centro (px em 1920x1080)", "TYPE": "point2D", "DEFAULT": [960.0, 540.0], "MIN": [0.0, 0.0], "MAX": [1920.0, 1080.0] },
{ "NAME": "disparo", "LABEL": "Disparar", "TYPE": "event" },
{ "NAME": "espectro", "LABEL": "Espectro do áudio", "TYPE": "audioFFT" }
```

## 3. Variáveis e funções disponíveis no código

O BSL Stage escreve um preâmbulo com estas declarações (não repita):

| Nome | Tipo | Valor |
|---|---|---|
| `isf_FragNormCoord` | `vec2` | Posição do pixel de 0 a 1. **(0, 0) é o canto inferior esquerdo**, (1, 1) o superior direito. Também aceito: `vv_FragNormCoord`. |
| `RENDERSIZE` | `vec2` | Tamanho em pixels da imagem que está sendo desenhada (no passe atual). No perfil Eco, imagens de 640x360 ou mais são desenhadas em **metade** do tamanho. |
| `TIME` | `float` | Segundos desde que o efeito ou gerador começou a rodar. |
| `TIMEDELTA` | `float` | Segundos desde o quadro anterior. |
| `DATE` | `vec4` | Ano, mês, dia e segundos desde a meia-noite (hora local). |
| `FRAMEINDEX` | `int` | Contador de quadros, começa em 0. |
| `PASSINDEX` | `int` | Índice do passe em desenho (0 para shaders de um passe só). |
| uma variável por entrada | conforme a tabela | O mesmo nome do `NAME`. |
| uma variável por buffer | `sampler2D` | Cada `TARGET` de `PASSES` e cada imagem de `IMPORTED` pode ser lida pelo nome. |

Macros reescritas pelo host (o que o ISF clássico oferece):

| Macro | Resultado |
|---|---|
| `IMG_PIXEL(img, p)` | Cor no pixel `p` (em pixels, `vec2`). |
| `IMG_NORM_PIXEL(img, uv)` | Cor na posição normalizada `uv` (0 a 1). É a mais usada. |
| `IMG_THIS_PIXEL(img)` | Cor da imagem no pixel que está sendo desenhado. |
| `IMG_THIS_NORM_PIXEL(img)` | Igual, na forma normalizada. |
| `IMG_SIZE(img)` | `vec2` com largura e altura da imagem em pixels. |

Aceitos por compatibilidade com shaders antigos: `gl_FragColor` (a saída), `texture2D`, `texture2DLod` e `varying`. Também vale o GLSL 3.30 normal: `texture`,
`gl_FragCoord`, funções matemáticas, laços `for`, `if`, funções próprias.

Regras importantes:

- **Não escreva `#version`** nem declare `uniform`: o host faz isso. Declarar de novo dá erro de redefinição.
- A saída é `gl_FragColor` (`vec4`) com **alfa reto** (não pré-multiplicado). Veja `boas-praticas.md`.
- O primeiro argumento das macros `IMG_*` precisa ser o nome de uma imagem escrito direto (não uma variável nem expressão).
- Sem acentos no código GLSL fora de comentários (o host troca por espaço; outros programas recusam).
- As linhas do arquivo são preservadas na hora de compilar: o número da linha de um erro é o da linha do **seu arquivo**.

## 4. Áudio (`audioFFT`)

A entrada `audioFFT` vira uma imagem de **32 x 1 pixels** (RGBA 16 bits em ponto flutuante) com o espectro do som que o programa está ouvindo (som do PC ou entrada,
configurável em Exibir > Áudio). Faixa 0 = graves (cerca de 40 Hz), faixa 31 = agudos (cerca de 16 kHz), em escala logarítmica. O valor, de 0 a 1 (de -60 dB a 0 dB), está nos
canais R, G e B (iguais). Sem áudio, tudo é zero.

```
float faixa(float x)
{
    return IMG_NORM_PIXEL(espectro, vec2(x, 0.5)).r;
}
```

`x` vai de 0 (grave) a 1 (agudo). A textura é filtrada de forma linear, então valores entre as faixas são interpolados. O host só atualiza a textura quando o shader declara uma
entrada `audioFFT`. Para testar sem som, use `bslstage_isfcheck --audio` (espectro sintético).

## 5. Passes (`PASSES`)

Sem `PASSES` o shader tem um passe só, direto na saída. Com `PASSES`, cada item é um passe desenhado em ordem; o **último** passe vai para a saída (ou, se tiver `TARGET`, é copiado
para ela). Dentro do código, `PASSINDEX` diz qual passe está rodando.

| Chave | Uso |
|---|---|
| `TARGET` | Nome do buffer onde o passe grava. Vira uma imagem que os passes seguintes (e, se `PERSISTENT`, os quadros seguintes) leem pelo nome. Identificador válido. |
| `PERSISTENT` | `true`: o buffer sobrevive de um quadro para o outro (são dois buffers que se alternam). Serve para rastros e feedback. Com `PERSISTENT`, ler o `TARGET` dentro do passe que o grava devolve o quadro **anterior**. |
| `FLOAT` | `true`: o buffer usa 32 bits por canal (RGBA32F). Necessário quando o valor sai de 0..1 ou precisa de precisão (acumuladores). Pesa mais: use só quando precisar. |
| `WIDTH`, `HEIGHT` | Tamanho do buffer. Número, ou texto com expressão: números, `$WIDTH`, `$HEIGHT` (tamanho da saída), `$entrada` (valor de uma entrada `float`, `long` ou `bool`), `+ - * /`, parênteses e as funções `floor`, `ceil`, `round`, `min`, `max`, `abs`, `sqrt`, `pow`. Exemplo: `"$WIDTH/2"`. Entre 1 e 8192. |

Buffers começam transparentes (zero). Um passe sem `TARGET` que não seja o último desenha num buffer temporário.

## 6. Imagens que viajam com o plugin (`IMPORTED`)

```
"IMPORTED": {
    "logo": { "PATH": "logo.png" }
}
```

Também vale a lista `[ { "NAME": "logo", "PATH": "logo.png" } ]`. O caminho é relativo ao `.fs` (subpastas permitidas). PNG, JPG, BMP e TGA. Lida com
`IMG_NORM_PIXEL(logo, uv)`; **uv (0, 0) é o canto inferior esquerdo da imagem**, como no resto. Se o arquivo não existir aparece o aviso "Imagem importada não encontrada" e a
imagem fica vazia. Tipo `cube` em `IMPORTED` não é suportado.

## 7. O que NÃO é suportado

- Entradas `audio` (forma de onda) e `cube`.
- Imagens de entrada além de `inputImage` (ficam vazias). Transições do tipo `startImage`/`endImage` não existem.
- Vários vertex shaders por passe; `#include`; arrays de uniforms.
- Valores de entrada em tempo real vindos de fora (o que existe: controles na interface, atalhos, DMX e o áudio).
- Mais de 128 entradas, 32 passes, arquivo maior que 1 MB.

## 8. Orientação, alfa e tamanho

- `isf_FragNormCoord.y = 0` é a **base** da imagem em qualquer cadeia (clip, camada ou master). O host cuida da inversão.
- `inputImage` e a saída usam **alfa reto**. Imagens importadas também. O compositor do BSL Stage espera alfa reto (`cor final = mistura(fundo, cor, alfa)`).
- O efeito roda no tamanho da imagem de entrada; o gerador, no tamanho do canvas da composição. No perfil Eco, a partir de 640x360 o desenho é feito na metade e ampliado.

## 9. Erros e avisos

| Mensagem | Significado |
|---|---|
| `Sem o cabeçalho JSON do ISF (comentário /* ... */ no começo do arquivo).` | Faltou o `/*{ ... }*/`. |
| `Comentário JSON do ISF não foi fechado (falta */).` | O cabeçalho não termina. |
| `Cabeçalho JSON inválido na linha N do arquivo: ...` | Vírgula faltando ou sobrando, aspas, chave sem fechar. A linha é do arquivo. |
| `O cabeçalho JSON do ISF precisa ser um objeto { ... }.` | Escreveu uma lista ou valor solto. |
| `INPUTS precisa ser uma lista.` | `INPUTS` é `[ ... ]`. |
| `Erro no shader ISF (fragment):` seguido de `Linha N: ...` | Erro de GLSL; N é a linha do arquivo. Mesma mensagem que o driver da placa de vídeo dá (varia por fabricante). |
| `Falha ao ligar o shader ISF: ...` | O código compilou mas não liga (por exemplo, variável de saída faltando). |
| `Entrada com NAME inválido ignorada` / `Entrada repetida ignorada` / `Entrada "x" com TYPE desconhecido ignorada` | Aviso: a entrada não existe. |
| `Entrada de áudio "x" (forma de onda) não é suportada: use audioFFT.` | Troque o tipo. |
| `Imagem importada não encontrada: arquivo.png` | O `PATH` não aponta para um arquivo. |

Quando você salva um arquivo com erro, o efeito que já estava rodando **continua com a versão anterior** e o painel do efeito mostra "O arquivo foi alterado e tem erro" com
a linha. Corrigiu e salvou de novo: troca sozinho. Os valores dos parâmetros são mantidos.

## 10. Onde os arquivos ficam e quando são relidos

- `Documents\BSL Stage\Plugins`: pasta "Meus plugins", onde "Novo plugin..." e "Importar .bslplugin" gravam. Procura em subpastas.
- `Documents\BSL Stage\ISF`: exemplos que acompanham o programa.
- Pastas extras: aba Efeitos > ícone de pastas ISF > Adicionar pasta.
- O programa confere as pastas cerca de uma vez por segundo. Arquivo novo, salvo ou apagado: o catálogo atualiza sozinho e os efeitos em uso recompilam (só os que mudaram).
  O botão Recarregar da aba Efeitos faz a leitura completa na hora.

## 11. Validar pela linha de comando

```
bslstage_isfcheck meu-plugin.fs
bslstage_isfcheck meu-plugin.fs --png previa.png --time 1.5 --size 960x540
bslstage_isfcheck meu-plugin.fs --set intensidade=0.8 --set cor=1,0,0,1 --audio --json
```

Veja `PARA-IA.md` para os códigos de saída e o formato do relatório.
