# Modelo de plugin FFGL 2.x (C++)

Um efeito completo com dois parâmetros (**Brilho** e **Saturação**) para você copiar e transformar no seu plugin. Com os valores padrão, a imagem passa igual. Conceitos e regras do FFGL: `docs/plugins/FFGL-guia.md`.

Antes de escolher este caminho, veja se um shader ISF resolve (`docs/plugins/ISF-guia.md`): é bem mais simples.

## Arquivos

| Arquivo | Conteúdo |
|---|---|
| `src/BslExample.h` e `src/BslExample.cpp` | A classe do plugin: identidade (`CFFGLPluginInfo`), os dois parâmetros, os shaders GLSL 3.30 e o desenho |
| `CMakeLists.txt` | Compila a DLL `BSLExample.dll` |

## De onde vem o FFGL SDK

As classes `CFFGLPlugin`, `FFGLShader` e as demais são do FFGL SDK 2.x (licença BSD de 3 cláusulas). Este repositório já tem uma cópia sem alterações em `tools/ffgl-test-plugins/sdk`, com o texto da licença em
`tools/ffgl-test-plugins/LICENSE-FFGL-SDK.md`, e um pequeno adaptador (`tools/ffgl-test-plugins/shim`) que troca o GLEW pelo carregador de OpenGL do projeto (glad, em `third_party/glad`). O modelo usa essas cópias; nada precisa ser baixado.

Se você copiar esta pasta para outro lugar, aponte para o repositório: `-DBSL_REPO_DIR=C:/caminho/BSL-Stage`. Para usar outro SDK FFGL 2.x seu, passe `-DFFGL_SDK_DIR=<pasta com sdk/ e shim/>`. A versão oficial do SDK pode ser obtida no repositório
público do FFGL SDK (licença BSD).

## Compilar

Requisitos: Visual Studio 2022 (carga de trabalho C++) e CMake 3.25 ou mais novo.

```
cmake -S sdk/ffgl-template -B build-plugin -G "Visual Studio 17 2022" -A x64
cmake --build build-plugin --config Release
```

A DLL sai em `build-plugin\Release\BSLExample.dll`. O build principal do BSL Stage (`cmake --build --preset release`) também compila este alvo (`bsl_ffgl_example`) em `build\Release\ffgl-test-plugins\`, e o `bslstage_ffgltest` o carrega e confere os pixels.

## Instalar

Copie `BSLExample.dll` para:

```
Documents\BSL Stage\Plugins
```

O programa examina a DLL num processo separado e, se estiver tudo certo, **BSL Exemplo** aparece na aba **Efeitos**, seção **FFGL**. Se ele não aparecer, abra **Exibir > Plugins**: a lista de bloqueados mostra o motivo.
Para trocar uma DLL que já foi carregada, feche o BSL Stage antes de copiar a nova.

## Transformar no seu plugin

1. Copie `src/BslExample.*` para `src/MeuPlugin.*` e renomeie a classe.
2. Em `CFFGLPluginInfo`, troque o **id de 4 letras** (único), o nome, a descrição e o autor.
3. Mude os parâmetros (`SetParamInfof`, `SetFloatParameter`, `GetFloatParameter`) e o fragment shader.
4. No `CMakeLists.txt`, troque `BslExample.cpp` e `OUTPUT_NAME`.
5. Compile e instale como acima.

Lembretes: a imagem de entrada e a de saída são **alfa pré-multiplicado** (o shader do modelo desfaz e refaz); use os `Scoped...Binding` do SDK para devolver o estado do OpenGL como estava; use `#version 330 core` para rodar em placa integrada antiga.

## Licença deste modelo

Os arquivos de `src/` são de uso livre (CC0). O FFGL SDK mantém a licença BSD dele: ao distribuir uma DLL compilada com o SDK, mantenha o aviso de copyright e o texto da licença junto do código-fonte e da documentação.
