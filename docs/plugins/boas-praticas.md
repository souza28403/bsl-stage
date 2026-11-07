# Boas práticas para plugins leves e corretos

O BSL Stage foi feito para rodar também em PC antigo: OpenGL 3.3, 4 GB de RAM e placa de vídeo integrada como a Intel HD 4000. Um plugin que pesa pouco funciona em qualquer lugar.

## 1. Meça antes de achar que está leve

`bslstage_isfcheck` mostra o custo por quadro na sua placa e uma estimativa em 1920x1080:

```
bslstage_isfcheck meu-plugin.fs --size 1920x1080 --time 2
```

A linha "Custo" é medida na placa de vídeo **deste** computador. Uma placa dedicada moderna é de 10 a 30 vezes mais rápida que uma Intel HD 4000. Como regra prática: se a estimativa para 1920x1080 passar de
**0,5 ms** na sua placa, o plugin já está pesado para um PC fraco; acima de **10 ms** o validador avisa. Lembre que o plugin divide a placa com todas as camadas, os outros efeitos e a saída.

## 2. Desempenho em PC fraco

- **Menos leituras de textura.** Cada `IMG_NORM_PIXEL` é uma leitura de memória. Um desfoque de 25 leituras por pixel em 1080p são 50 milhões de leituras por quadro. Prefira raios pequenos e, se precisar de um desfoque grande,
  faça em dois passes (horizontal e vertical) e em metade da resolução (`"WIDTH": "$WIDTH/2"`).
- **Sem laços dinâmicos caros.** Use `for` com um limite **fixo e pequeno** (`for (int i = 0; i < 8; i++)`). Laços cujo limite vem de um controle, `while` e `do` forçam a placa a esperar o pior caso em todos os pixels e
  somem com o desempenho em placas integradas. Se o usuário precisa escolher a quantidade, deixe o laço no máximo fixo e use `if (i >= quantidade) break;`, ou melhor, mude a fórmula para não precisar de laço.
- **Matemática barata.** `pow`, `exp`, `log`, `sin` e `cos` são caros; `sqrt` e multiplicações são baratos. Calcule fora do que varia por pixel quando possível (por exemplo, `cos(a)` e `sin(a)` de um ângulo que não depende do pixel
  só precisam ser calculados uma vez por quadro: o compilador faz isso se vierem só de variáveis de entrada, mas não conte com isso dentro de laços).
- **Buffers.** `"FLOAT": true` quadruplica a memória e a largura de banda em relação ao padrão (RGBA32F contra RGBA8). Use apenas quando precisar de valores acima de 1, negativos ou muita precisão acumulada. Para rastros simples
  de imagem comum, o buffer normal serve. Use `WIDTH`/`HEIGHT` menores quando o resultado não precisa de nitidez.
- **Poucos passes.** Cada passe é uma imagem inteira desenhada e lida de novo. Junte etapas que dão para fazer num passe só.
- **Perfil Eco.** Em Eco, o programa desenha plugins ISF na **metade da resolução** (a partir de 640x360). Se o seu efeito depende de pixels exatos (linhas finas, padrões de 1 pixel), use `RENDERSIZE` em vez de números fixos e teste com
  `--size 960x540`.
- **Resolução do gerador.** Um gerador desenha no tamanho do canvas. Num canvas 4K o custo é 4 vezes o de 1080p; para efeitos caros, considere desenhar em um buffer menor (`WIDTH`/`HEIGHT`) e ampliar no último passe.

## 3. Precisão

- Variáveis `float` do GLSL são de 32 bits no BSL Stage. Mesmo assim, `sin(TIME * 1000.0)` perde precisão depois de horas de show. Para animações que se repetem, use `mod(TIME, periodo)` ou `fract(TIME * frequencia)`.
- Buffers normais (`FLOAT` desligado) têm 8 bits por canal: acumular valores pequenos (`x * 0.99` repetido) trava num valor ou some por arredondamento. Para rastros que precisam apagar suavemente, use `FLOAT: true`.
- Evite dividir por zero (`x / length(v)` com `v = 0`): proteja com `max(length(v), 0.0001)`. Divisão por zero dá `NaN`, que aparece como pixels pretos ou brancos que se espalham em feedback.
- Compare números reais com margem (`abs(a - b) < 0.001`), não com `==`.
- Pontos `point2D` vêm em pixels de uma tela 1920x1080 conceitual: divida por `vec2(1920.0, 1080.0)` e corrija a proporção com `RENDERSIZE.x / RENDERSIZE.y` para círculos redondos.

## 4. Alfa: reto ou pré-multiplicado?

Há duas formas de guardar transparência:

- **Alfa reto**: a cor é a cor "cheia" e o alfa diz quanto dela aparece. Um pixel meio transparente vermelho é `(1, 0, 0, 0.5)`.
- **Alfa pré-multiplicado**: a cor já vem multiplicada pelo alfa. O mesmo pixel é `(0.5, 0, 0, 0.5)`.

Regras do BSL Stage:

| Onde | Convenção |
|---|---|
| ISF: `inputImage`, saída (`gl_FragColor`), imagens `IMPORTED` | **Alfa reto** |
| Compositor (mistura das camadas) | Espera **alfa reto** |
| Efeitos nativos | Alfa reto |
| FFGL: textura de entrada e saída | **Pré-multiplicado** (o host converte nas duas pontas) |
| Dentro de um buffer seu (`PASSES`) | Livre, mas precisa voltar a reto no último passe |

Consequências para o seu ISF:

- Para escurecer só a cor, mexa em `rgb` e deixe o `a` como veio: `vec4(src.rgb * 0.5, src.a)`.
- Para apagar, mexa no `a` (`vec4(src.rgb, src.a * 0.5)`).
- Num gerador, termine com alfa 1 (`vec4(cor, 1.0)`) a não ser que queira transparência de verdade.
- Em rastros e feedback, guarde a cor pré-multiplicada no buffer (isso evita bordas escuras ao esmaecer) e divida pelo alfa no passe de saída, como no exemplo 3 do guia.
- Não multiplique a cor pelo alfa na saída "para garantir": o compositor já faz a mistura pelo alfa e você escureceria as bordas duas vezes.

## 5. Estabilidade

- Não leia um `TARGET` no mesmo passe que o grava, a não ser que ele seja `PERSISTENT` (e então você lê o quadro anterior).
- Evite `discard`: ele deixa o pixel sem valor e pode aparecer como buraco em feedback.
- Teste o plugin com o clip parado (o `TIME` não avança) e com muito tempo ligado (`--time 600`).
- Teste com imagem de entrada diferente (`--input`), inclusive com transparência.
- Valores extremos nos controles (mínimo e máximo): `--set nome=valor`.
- Cada controle com uma faixa `MIN`/`MAX` realista: faixas enormes dificultam o ajuste ao vivo.

## 6. Organização

- Nome do arquivo = nome do plugin na interface. Escolha um nome curto e claro; renomear depois quebra projetos que já usam.
- Sempre preencha `DESCRIPTION`, `CREDIT` e `CATEGORIES`.
- Rótulos (`LABEL`) em português, curtos, com a unidade quando fizer sentido ("Ângulo (graus)").
- Código sem acentos e sem aspas curvas fora de comentários.
- Uma ideia por plugin: dois efeitos simples encadeados pesam o mesmo que um complexo, e são mais fáceis de ajustar ao vivo.
