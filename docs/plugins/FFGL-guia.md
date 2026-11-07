# Guia FFGL: plugins em C++

FFGL (FreeFrame GL) é um padrão aberto para plugins de vídeo que rodam na placa de vídeo. Um plugin FFGL é uma **DLL de 64 bits**. O BSL Stage carrega DLLs FFGL 2.x de efeito e de fonte (gerador).
Escolha FFGL quando o ISF não basta: você precisa de C++, de bibliotecas externas, de vários objetos de OpenGL, de leitura de arquivos ou de cálculo pesado no processador.
Para a grande maioria dos efeitos visuais, o ISF (`ISF-guia.md`) é mais simples e já cobre o que o cliente precisa.

## Compatibilidade

- **Qualquer plugin FFGL 2.x de 64 bits** (efeito ou fonte) que respeite o padrão funciona, seja escrito por você ou por terceiros. O programa fala o protocolo aberto FFGL.
- Os plugins de terceiros **não vêm com o BSL Stage** e não são redistribuídos por ele. Você os instala por conta própria, respeitando a licença de cada um.
- Mixers FFGL (plugins de mistura entre duas fontes) ainda não são suportados e aparecem como "não suportado".
- Plugins que carregam um navegador próprio dentro da DLL podem falhar ou duplicar processos pesados; para páginas da web use a fonte Web nativa.

## Onde instalar

Copie a `.dll` (e as pastas de apoio que ela precisar, ao lado dela) para:

```
Documents\BSL Stage\Plugins
```

ou para uma pasta que você adicionou em **Exibir > Plugins**. A janela **Plugins** também tem **Importar plugins…**, que copia uma DLL (ou pasta) para a sua pasta de plugins e atualiza a lista. Depois de copiar, a
varredura roda sozinha; o plugin aparece na aba **Efeitos** (seção FFGL) ou na aba **Fontes** (geradores).

## Como o BSL Stage protege o show

- Antes de carregar, o programa lê só o cabeçalho do arquivo e ignora o que não é uma DLL x64 com `plugMain`.
- Cada DLL nova é examinada num **processo separado**, com limite de 10 segundos. Se travar ou cair, vai para a lista de bloqueados (Exibir > Plugins, "Tentar de novo") e o programa continua.
- Em execução, toda chamada ao plugin é protegida. Se um plugin falhar, ele passa a deixar a imagem passar direto e o painel do efeito mostra "Plugin falhou: ..." em vermelho; o show não para.
- O resultado da varredura fica em cache (`%LOCALAPPDATA%\BSL Stage\ffgl-cache.json`), por caminho, tamanho e data. Compilou de novo: a DLL é examinada de novo.

## Regras do FFGL que importam

O programa chama a DLL assim, a cada quadro: informação de batida (BPM e fase do compasso), tempo, e `FF_PROCESS_OPENGL` com a imagem de entrada.

- **Alfa pré-multiplicado.** O host entrega a textura de entrada com o alfa já multiplicado nas cores, e espera a saída também pré-multiplicada. No shader: divida a cor pelo alfa para trabalhar com cor reta, e multiplique de volta no fim
  (o exemplo faz isso). Diferente do ISF, que usa alfa reto.
- **Linha 0 na base.** A imagem do FFGL tem a origem embaixo. O host inverte por você quando necessário.
- **Estado do OpenGL.** Antes de cada chamada o estado é o "padrão do FFGL" (sem blend, sem depth, sem programa, sem VAO). Devolva-o como encontrou: use os `Scoped...Binding` do SDK, que desfazem sozinhos.
- **Tamanho da textura.** A textura de entrada pode ser maior que a área usada. Use `GetMaxGLTexCoords` e multiplique as coordenadas de textura por esse valor (o exemplo usa `MaxUV`).
- **GLSL.** O host cria um contexto OpenGL 3.3 core ou superior. Escreva `#version 330 core` para rodar também em placa de vídeo integrada antiga. `#version 410` só se precisar mesmo.
- **Threads.** O plugin é chamado sempre da thread de render.

### Parâmetros

| Tipo FFGL | Controle no BSL Stage |
|---|---|
| `FF_TYPE_STANDARD` | Número com a faixa do plugin |
| `FF_TYPE_BOOLEAN` | Liga/desliga |
| `FF_TYPE_EVENT` | Botão (vale 1 por um quadro) |
| `FF_TYPE_INTEGER` | Número inteiro |
| `FF_TYPE_OPTION` | Menu com os nomes dos elementos |
| `FF_TYPE_RED`, `GREEN`, `BLUE` (e `ALPHA`) em sequência | Uma cor |
| `FF_TYPE_HUE`, `SATURATION`, `BRIGHTNESS` (e `ALPHA`) em sequência | Uma cor HSB |
| `FF_TYPE_XPOS`, `YPOS` | Ponto 2D |
| `FF_TYPE_TEXT`, `FF_TYPE_FILE` | Texto e arquivo |

Nomes de parâmetro: use ASCII (o nome vira o rótulo na interface). Agrupe parâmetros com grupos do FFGL se o plugin tiver muitos.

## O modelo pronto: `sdk/ffgl-template`

A pasta `sdk/ffgl-template/` tem um plugin completo de exemplo: **BSL Exemplo**, um efeito com dois parâmetros (Brilho e Saturação). Com os valores padrão, a imagem passa igual.
Arquivos: `src/BslExample.h`, `src/BslExample.cpp` e `CMakeLists.txt`. Passo a passo de compilar e instalar: `sdk/ffgl-template/README.md`.

Para fazer o seu plugin a partir dele:

1. Copie `src/BslExample.*` para `src/MeuPlugin.*` e renomeie a classe.
2. Em `CFFGLPluginInfo`, troque o **id de 4 letras** (precisa ser único; o programa usa o id para reencontrar o plugin num projeto), o nome, a descrição e o autor. `FF_EFFECT` altera uma
   imagem de entrada; `FF_SOURCE` gera uma imagem (nesse caso use `SetMinInputs(0)` e `SetMaxInputs(0)` e não leia `inputTextures`).
3. Troque o fragment shader e os parâmetros (`SetParamInfof`, `SetFloatParameter`, `GetFloatParameter`).
4. No `CMakeLists.txt`, troque os nomes do alvo e do arquivo `.cpp`.
5. Compile em Release x64 e copie a DLL para `Documents\BSL Stage\Plugins`.

## Licença do SDK

As classes `CFFGLPlugin`, `FFGLShader` e as demais vêm do FFGL SDK, licenciado sob BSD de 3 cláusulas. A cópia usada pelo repositório está em `tools/ffgl-test-plugins/sdk` com o texto da licença em
`tools/ffgl-test-plugins/LICENSE-FFGL-SDK.md`. Se você distribuir uma DLL compilada com ele, mantenha o aviso de copyright e a licença junto do código-fonte e da documentação. O restante do plugin é seu e
você escolhe a licença (veja `licencas.md`).

## Testar

- O `bslstage_ffgltest` carrega a DLL de exemplo, confere os parâmetros e os pixels de saída (valores padrão passam igual; brilho no máximo vira branco; saturação 0 vira cinza).
- No programa: Exibir > Plugins mostra as pastas, os plugins bloqueados e o motivo. O log (`%APPDATA%\BSL Stage\bslstage.log`) registra erros de carregamento.
