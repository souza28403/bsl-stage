# Avisos de terceiros (THIRD-PARTY-NOTICES)

O BSL Stage é software livre: © 2026 BSL Light, licenciado sob a GNU GPL versão 3 (arquivo `LICENSE`).
Este documento lista o software de terceiros que **acompanha** o BSL Stage (compilado dentro do programa, copiado para a
pasta do executável ou embutido nos arquivos em `assets/`) e o que o programa **usa sem distribuir** (NDI). Cada componente
continua sob a licença do seu autor. O texto completo de cada licença está na pasta `licenses/`, copiado dos arquivos que os
próprios projetos distribuem (o que não estava disponível está marcado como **PENDENTE**, nunca foi escrito de memória).

Como manter: as versões abaixo são as fixadas no `CMakeLists.txt`. Ao trocar ou adicionar uma dependência, atualize este arquivo
e `licenses/`. O teste `bslstage_uitest` confere que toda biblioteca do CMake aparece aqui e que todo arquivo de `licenses/`
existe e não está vazio. A janela **Ajuda > Licenças e avisos… > Terceiros** lê este arquivo em tempo de execução: mantenha o
formato (uma seção `###` por componente, com as linhas `- **Chave:** valor`).

Este documento não é aconselhamento jurídico. Os itens **PENDENTE** pedem uma decisão ou verificação de quem responde pelo projeto
(veja "Pendências" no fim).

## Compilados dentro do programa

### GLFW
- **Versão:** 3.4
- **Licença:** zlib/libpng
- **Copyright:** Copyright (c) 2002-2006 Marcus Geelnard; Copyright (c) 2006-2019 Camilla Löwy
- **URL:** https://www.glfw.org/ (código: https://github.com/glfw/glfw)
- **Texto da licença:** `licenses/GLFW-zlib.txt`
- **Uso:** janelas, contexto OpenGL, teclado e mouse. Baixado pelo CMake (FetchContent) e compilado como biblioteca estática.

### glad (carregador OpenGL) e cabeçalhos da Khronos
- **Versão:** glad 2.0.8, gerado com `--api gl:core=3.3` (third_party/glad)
- **Licença:** (WTFPL ou CC0-1.0) e Apache-2.0 para o código gerado; Khronos (licença tipo MIT) para `KHR/khrplatform.h`
- **Copyright:** Copyright (c) 2008-2018 The Khronos Group Inc. (khrplatform.h); o glad não declara copyright no código gerado
- **URL:** https://github.com/Dav1dde/glad e https://www.khronos.org/registry/OpenGL/
- **Texto da licença:** `licenses/glad-e-Khronos.txt`, `licenses/Apache-2.0.txt`
- **Uso:** carrega as funções do OpenGL 3.3 core. Incluído no repositório (`third_party/glad`).
- **Situação:** PENDENTE: o texto completo da CC0-1.0 e da WTFPL não estava no disco; colar o texto oficial em `licenses/glad-e-Khronos.txt`.

### Dear ImGui
- **Versão:** 1.93.0 (ramo docking, tag v1.93.0-docking)
- **Licença:** MIT
- **Copyright:** Copyright (c) 2014-2026 Omar Cornut
- **URL:** https://github.com/ocornut/imgui
- **Texto da licença:** `licenses/ImGui-MIT.txt`
- **Uso:** toda a interface gráfica (com os backends GLFW e OpenGL3). Inclui os arquivos `imstb_*.h`, derivados do stb (domínio público ou MIT). Baixado pelo CMake.

### nlohmann/json
- **Versão:** 3.11.3
- **Licença:** MIT
- **Copyright:** Copyright (c) 2013-2022 Niels Lohmann
- **URL:** https://github.com/nlohmann/json
- **Texto da licença:** `licenses/nlohmann-json-MIT.txt`
- **Uso:** projetos `.bslstage`, configurações, atualizações, DMX. Só cabeçalhos; baixado pelo CMake.

### stb
- **Versão:** commit 003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5 (nothings/stb; usa stb_image, stb_image_write e stb_truetype)
- **Licença:** MIT ou domínio público (Unlicense), à escolha
- **Copyright:** Copyright (c) 2017 Sean Barrett
- **URL:** https://github.com/nothings/stb
- **Texto da licença:** `licenses/stb-MIT-ou-dominio-publico.txt`
- **Uso:** leitura de imagens, gravação de PNG/JPEG e rasterização de texto. Baixado pelo CMake.

### SpoutGL (Spout2)
- **Versão:** 2.007.018
- **Licença:** BSD-2-Clause
- **Copyright:** Copyright (c) 2020, Lynn Jarvis
- **URL:** https://github.com/leadedge/Spout2
- **Texto da licença:** `licenses/Spout2-BSD-2-Clause.txt`
- **Uso:** entrada e saída Spout (compartilhamento de texturas entre programas no mesmo PC). Só o código de `SPOUTSDK/SpoutGL`, compilado como biblioteca estática.

### Monocypher
- **Versão:** 4.0.2
- **Licença:** BSD-2-Clause ou CC0-1.0 (domínio público), à escolha
- **Copyright:** Copyright (c) 2017-2023, Loup Vaillant; Copyright (c) 2017-2019, Michael Savage; Copyright (c) 2017-2023, Fabio Scotoni
- **URL:** https://monocypher.org/ (código: https://github.com/LoupVaillant/Monocypher)
- **Texto da licença:** `licenses/Monocypher-BSD-2-ou-CC0.txt`
- **Uso:** verificação offline da chave de ativação do instalador (assinatura Ed25519). Baixado pelo CMake e compilado como biblioteca estática; também linkado na ação personalizada do instalador.

### Native File Dialog Extended
- **Versão:** 1.2.1
- **Licença:** zlib/libpng
- **Copyright:** Bernard Teo, Michael Labbe e colaboradores
- **URL:** https://github.com/btzy/nativefiledialog-extended
- **Texto da licença:** `licenses/NFD-zlib.txt`
- **Uso:** diálogos de abrir e salvar arquivo no Linux e no macOS (não é usado no Windows). Baixado pelo CMake (FetchContent) e compilado como biblioteca estática.

### miniaudio
- **Versão:** 0.11.26
- **Licença:** domínio público (Unlicense) ou MIT-0, à escolha
- **Copyright:** Copyright 2026 David Reid
- **URL:** https://github.com/mackron/miniaudio
- **Texto da licença:** `licenses/miniaudio-dominio-publico-ou-MIT-0.txt`
- **Uso:** captura de áudio (entrada e "som do PC") no Linux. Biblioteca de um único cabeçalho, baixada pelo CMake e compilada só fora do Windows.

### FFGL (cabeçalhos do SDK de plugins)
- **Versão:** FFGL 2.x (`FFGL.h` e `FFGLPlatform.h`, sem modificação; `GL/glew.h` é um arquivo próprio que só redireciona para o glad)
- **Licença:** BSD-3-Clause
- **Copyright:** Copyright 2023 FreeFrame
- **URL:** https://github.com/resolume/ffgl
- **Texto da licença:** `licenses/FFGL-BSD-3-FreeFrame.txt`
- **Uso:** só cabeçalhos, para o host de plugins FFGL; nenhum código do SDK entra no executável. As fontes do SDK em `tools/ffgl-test-plugins/sdk` servem só aos testes e não são distribuídas no pacote.

## Bibliotecas copiadas para a pasta do executável

### FFmpeg
- **Versão:** 9.0 (pacote BtbN `ffmpeg-n9.0-latest-win64-lgpl-shared-9.0`, build `n9.0.2-35-g9a9b70bb8c-20261024`; DLLs avcodec-63, avformat-63, avutil-61, avdevice-63, avfilter-12, swscale-10, swresample-7)
- **Licença:** LGPL-3.0 (build "lgpl-shared", `--enable-version3`, sem as partes GPL)
- **Copyright:** Copyright (c) 2007-2026 the FFmpeg developers (cabeçalho do `ffprobe -version` do pacote)
- **URL:** https://ffmpeg.org/ (código-fonte e como obtê-lo); build: https://github.com/BtbN/FFmpeg-Builds
- **Texto da licença:** `licenses/FFmpeg-LGPL-3.0.txt`
- **Uso:** decodificação de vídeo e imagens e captura de dispositivos. Vinculado dinamicamente (DLLs): o usuário pode trocá-las por outra build compatível da LGPL.
- **Linux (AppImage):** FFmpeg 9.0.2 compilado a partir do código-fonte oficial (https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz, SHA-256 e assinatura conferidos por `tools/package/build-ffmpeg-linux.sh`), bibliotecas compartilhadas (libavcodec.so.63, libavformat.so.63, libavutil.so.61, libavdevice.so.63, libswscale.so.10, libswresample.so.7), licença LGPL-2.1-ou-posterior (`licenses/FFmpeg-LGPL-2.1.txt`; sem `--enable-gpl`, `--enable-version3` nem `--enable-nonfree`) e só com as bibliotecas externas dav1d e libvpx, ligadas estaticamente (seções abaixo). O texto de configure usado vai em `usr/share/doc/bsl-stage/FFMPEG-CONFIGURE.txt` dentro do AppImage. Esta build inclui os decodificadores H.264 e HEVC nativos do FFmpeg (patentes: `docs/PATENTES-E-LICENCAS.md`).
- **Situação:** PENDENTE: (1) o CMake baixa a build "latest" da BtbN, que muda com o tempo, então a oferta de código-fonte da LGPL exige fixar a build usada ou compilar o próprio FFmpeg; (2) o build da BtbN incorpora dentro das DLLs bibliotecas de terceiros (libdav1d, libaom, libvpx, libopus, libvorbis, libwebp, libjxl, libopenh264, libass, freetype, harfbuzz, libxml2, entre outras, conforme a linha de configuração do build) e os avisos delas não acompanham o pacote; (3) a build inclui os decodificadores H.264 e HEVC (patentes: `docs/PATENTES-E-LICENCAS.md`). Compilar um FFmpeg mínimo próprio resolve os três.

### dav1d
- **Versão:** 1.5.4 (só no AppImage para Linux, ligada estaticamente nas bibliotecas do FFmpeg)
- **Licença:** BSD-2-Clause
- **Copyright:** Copyright © 2018-2025, VideoLAN and dav1d authors
- **URL:** https://code.videolan.org/videolan/dav1d
- **Texto da licença:** `licenses/dav1d-BSD-2.txt`
- **Uso:** decodificação de vídeo AV1 pelo FFmpeg compilado para Linux.

### libvpx
- **Versão:** 1.17.0 (só no AppImage para Linux, ligada estaticamente nas bibliotecas do FFmpeg)
- **Licença:** BSD-3-Clause, com concessão de patentes do WebM Project
- **Copyright:** Copyright (c) 2010, The WebM Project authors
- **URL:** https://github.com/webmproject/libvpx
- **Texto da licença:** `licenses/libvpx-BSD-3.txt`, `licenses/libvpx-PATENTS.txt`
- **Uso:** decodificação de VP8 e VP9 (inclusive com canal alfa) pelo FFmpeg compilado para Linux.

### PDFium
- **Versão:** chromium/8111 (PDFium 158.0.8111.0), binários bblanchon/pdfium-binaries
- **Licença:** BSD-3-Clause e Apache-2.0 (PDFium); MIT (empacotamento da bblanchon); mais as licenças das dependências embutidas na DLL
- **Copyright:** Copyright 2014 The PDFium Authors; empacotamento: Copyright 2014-2025 Benoit Blanchon
- **URL:** https://github.com/bblanchon/pdfium-binaries (PDFium: https://pdfium.googlesource.com/pdfium/)
- **Texto da licença:** `licenses/PDFium-BSD-3-e-Apache-2.0.txt`, `licenses/pdfium-binaries-MIT.txt`, `licenses/pdfium-dep-agg23.txt`, `licenses/pdfium-dep-dragonbox-Apache2-LLVM.txt`, `licenses/pdfium-dep-dragonbox-Boost.txt`, `licenses/pdfium-dep-fast_float.txt`, `licenses/pdfium-dep-freetype.txt`, `licenses/pdfium-dep-harfbuzz.txt`, `licenses/pdfium-dep-highway.txt`, `licenses/pdfium-dep-icu.txt`, `licenses/pdfium-dep-lcms.txt`, `licenses/pdfium-dep-libjpeg_turbo-ijg.txt`, `licenses/pdfium-dep-libjpeg_turbo-md.txt`, `licenses/pdfium-dep-libopenjpeg.txt`, `licenses/pdfium-dep-libpng.txt`, `licenses/pdfium-dep-llvm-libc.txt`, `licenses/pdfium-dep-simdutf.txt`, `licenses/pdfium-dep-zlib.txt`, `licenses/Apache-2.0.txt`
- **Uso:** renderizar PDF, só no processo auxiliar `bslstage_worker.exe` (`pdfium.dll`). Baixado pelo CMake. Os textos de abseil estão em `licenses/Apache-2.0.txt`.

### ONNX Runtime
- **Versão:** 1.24.4 (pacote NuGet Microsoft.ML.OnnxRuntime.DirectML; `onnxruntime.dll` e `onnxruntime_providers_shared.dll`)
- **Licença:** MIT (mais os avisos do `ThirdPartyNotices.txt` do pacote)
- **Copyright:** Copyright (c) Microsoft Corporation
- **URL:** https://github.com/microsoft/onnxruntime
- **Texto da licença:** `licenses/ONNXRuntime-MIT.txt`, `licenses/ONNXRuntime-ThirdPartyNotices.txt`
- **Uso:** executa localmente os modelos de IA (efeitos "IA"). Carregado em tempo de execução. A telemetria da plataforma, ligada por padrão nas builds oficiais para Windows, é desligada pelo BSL Stage ao iniciar o ONNX Runtime (veja `PRIVACIDADE.md`).

### DirectML
- **Versão:** 1.15.4 (pacote NuGet Microsoft.AI.DirectML; `DirectML.dll`)
- **Licença:** Microsoft Software License Terms (DirectML), licença própria da Microsoft, NÃO é de código aberto; os cabeçalhos/código de exemplo do pacote são MIT
- **Copyright:** © Microsoft Corporation. All rights reserved.
- **URL:** https://aka.ms/DirectML (pacote: https://www.nuget.org/packages/Microsoft.AI.DirectML/1.15.4)
- **Texto da licença:** `licenses/DirectML-Microsoft-Software-License-Terms.txt`, `licenses/DirectML-codigo-MIT.txt`, `licenses/DirectML-ThirdPartyNotices.txt`
- **Uso:** aceleração por GPU dos modelos de IA. O programa a copia para ao lado do exe só para garantir uma versão recente; se faltar, usa a `DirectML.dll` do Windows.
- **Situação:** PENDENTE (decisão jurídica): a licença da Microsoft (seção 1a) permite copiar e distribuir a DLL em aplicativos desenvolvidos com ferramentas de aprendizado de máquina, para Windows e Xbox, mas é uma licença proprietária (com coleta de dados e arbitragem nos EUA) distribuída junto de um programa GPL-3.0. Decidir com um advogado se a DLL continua no pacote; a alternativa sem distribuir nada é usar só a `DirectML.dll` do Windows.

### Chromium Embedded Framework (CEF) e Chromium
- **Versão:** CEF 154.0.23+g062ebe4+chromium-154.0.8037.17 (só no pacote que inclui a fonte Web; pasta `cef/`)
- **Licença:** BSD-3-Clause (CEF e Chromium) e as licenças de centenas de dependências listadas no `CREDITS.html` do CEF
- **Copyright:** Copyright (c) 2008-2020 Marshall A. Greenblatt; Portions Copyright (c) 2006-2009 Google Inc.; Copyright The Chromium Authors
- **URL:** https://bitbucket.org/chromiumembedded/cef e https://www.chromium.org/
- **Texto da licença:** `licenses/CEF-BSD-3.txt` (o arquivo licenses/cef-CREDITS.html, com Chromium e as dependências, é copiado para a pasta de saída pelo CMake quando o CEF é compilado, por ter ~9 MB)
- **Uso:** páginas Web (HTML) como fonte, no processo auxiliar. A pasta `cef/` traz também `d3dcompiler_47.dll`, `dxcompiler.dll`, `dxil.dll`, `vulkan-1.dll`, `libEGL.dll`, `libGLESv2.dll` e `vk_swiftshader.dll`, que fazem parte da distribuição do CEF.
- **Situação:** PENDENTE: o CMake procura o CEF num caminho local do desenvolvedor (`BSL_CEF_DIR`), que não é baixado nem fixado no repositório; conferir no `CREDITS.html` a licença de cada DLL acima (em especial `d3dcompiler_47.dll`, da Microsoft) antes de distribuir.

## IA local: modelos

### Depth Anything V2 Small
- **Versão:** conversão ONNX fp16, revisão 4472b73 (onnx-community/depth-anything-v2-small); só o tamanho Small
- **Licença:** Apache-2.0
- **Copyright:** PENDENTE (o arquivo de licença do modelo não estava no disco; conferir no repositório do modelo)
- **URL:** https://huggingface.co/onnx-community/depth-anything-v2-small (modelo original: https://github.com/DepthAnything/Depth-Anything-V2)
- **Texto da licença:** `licenses/Apache-2.0.txt`
- **Uso:** mapa de profundidade para os efeitos de IA. Baixado pelo CMake para `assets/models/`. Os tamanhos Base, Large e Giant NÃO são Apache-2.0 e não são usados.
- **Situação:** PENDENTE: o texto acima é o texto padrão da Apache-2.0; falta conferir a linha de copyright do repositório do modelo.

### MODNet
- **Versão:** conversão ONNX fp32, revisão fa2fa54 (Xenova/modnet)
- **Licença:** Apache-2.0
- **Copyright:** PENDENTE (o arquivo de licença do modelo não estava no disco; conferir no repositório do modelo)
- **URL:** https://huggingface.co/Xenova/modnet (modelo original: https://github.com/ZHKKKe/MODNet)
- **Texto da licença:** `licenses/Apache-2.0.txt`
- **Uso:** recorte de pessoa (matting) para os efeitos de IA. Baixado pelo CMake para `assets/models/`.
- **Situação:** PENDENTE: o texto acima é o texto padrão da Apache-2.0; falta conferir a linha de copyright do repositório do modelo.

## Fontes e ícones (assets/fonts)

### Barlow
- **Versão:** arquivos Barlow-Medium, Barlow-SemiBold e Barlow-Bold (versão do pacote não registrada)
- **Licença:** SIL Open Font License 1.1
- **Copyright:** Copyright 2017 The Barlow Project Authors
- **URL:** https://github.com/jpt/barlow
- **Texto da licença:** `licenses/Barlow-OFL-1.1.txt`
- **Uso:** fonte da interface (design system da BSL Light).

### JetBrains Mono
- **Versão:** JetBrainsMono.ttf (versão do pacote não registrada)
- **Licença:** SIL Open Font License 1.1
- **Copyright:** Copyright 2020 The JetBrains Mono Project Authors
- **URL:** https://github.com/JetBrains/JetBrainsMono
- **Texto da licença:** `licenses/JetBrainsMono-OFL-1.1.txt`
- **Uso:** fonte dos números e dos textos técnicos.

### Lucide
- **Versão:** lucide-static 1.61.0 (`assets/fonts/lucide.ttf`)
- **Licença:** ISC; MIT para os ícones herdados do Feather
- **Copyright:** Copyright (c) 2026 Lucide Icons and Contributors; Copyright (c) 2013-present Cole Bemis (Feather)
- **URL:** https://lucide.dev/
- **Texto da licença:** `licenses/Lucide-ISC-e-Feather-MIT.txt`
- **Uso:** ícones da interface.

## Fonte Web "WebBackgrounds" (assets/bsl/web_bg)

Os arquivos `assets/bsl/web_bg/index.html`, `control.html` e `studio.json` são o aplicativo web "BSL Background Studio" da BSL Light,
compilado (minificado) num único arquivo com as bibliotecas abaixo embutidas. As versões vêm do `package.json` do projeto de origem
(`BSL_Background_Studio`, fora deste repositório). As dependências transitivas do `@react-three/drei` que entram no bundle
não foram inventariadas (veja "Pendências").

### React e React DOM (WebBackgrounds)
- **Versão:** 19.3.0
- **Licença:** MIT
- **Copyright:** Copyright (c) Meta Platforms, Inc. and affiliates
- **URL:** https://react.dev/
- **Texto da licença:** `licenses/webbg-react-MIT.txt`, `licenses/webbg-react-dom-MIT.txt`
- **Uso:** interface dos fundos animados, embutida em `assets/bsl/web_bg`.

### Scheduler (WebBackgrounds)
- **Versão:** 0.28.0
- **Licença:** MIT
- **Copyright:** Copyright (c) Meta Platforms, Inc. and affiliates
- **URL:** https://github.com/facebook/react
- **Texto da licença:** `licenses/webbg-scheduler-MIT.txt`
- **Uso:** agendador do React, embutido em `assets/bsl/web_bg`.

### three.js (WebBackgrounds)
- **Versão:** 0.180.0
- **Licença:** MIT
- **Copyright:** Copyright © 2010-2025 three.js authors
- **URL:** https://threejs.org/
- **Texto da licença:** `licenses/webbg-three-MIT.txt`
- **Uso:** gráficos 3D/WebGL dos fundos animados.

### @react-three/fiber (WebBackgrounds)
- **Versão:** 9.8.1
- **Licença:** MIT (conforme o `package.json`)
- **Copyright:** PENDENTE (o arquivo de licença não estava no disco)
- **URL:** https://github.com/pmndrs/react-three-fiber
- **Uso:** liga o React ao three.js, embutido em `assets/bsl/web_bg`.
- **Situação:** PENDENTE: copiar o `LICENSE` do pacote para `licenses/` e registrar o copyright.

### @react-three/drei (WebBackgrounds)
- **Versão:** 10.7.9
- **Licença:** MIT
- **Copyright:** Copyright (c) 2020 react-spring
- **URL:** https://github.com/pmndrs/drei
- **Texto da licença:** `licenses/webbg-react-three-drei-MIT.txt`
- **Uso:** utilitários do three.js, embutidos em `assets/bsl/web_bg`.

### @react-three/postprocessing (WebBackgrounds)
- **Versão:** 3.1.3
- **Licença:** MIT
- **Copyright:** Copyright (c) 2020 react-spring
- **URL:** https://github.com/pmndrs/react-postprocessing
- **Texto da licença:** `licenses/webbg-react-three-postprocessing-MIT.txt`
- **Uso:** efeitos de pós-processamento dos fundos animados.

### postprocessing (WebBackgrounds)
- **Versão:** 6.39.5
- **Licença:** zlib
- **Copyright:** Copyright © 2015 Raoul van Rüschen
- **URL:** https://github.com/pmndrs/postprocessing
- **Texto da licença:** `licenses/webbg-postprocessing-Zlib.txt`
- **Uso:** biblioteca de efeitos de pós-processamento para o three.js.

### zustand (WebBackgrounds)
- **Versão:** 4.5.7
- **Licença:** MIT
- **Copyright:** Copyright (c) 2019 Paul Henschel
- **URL:** https://github.com/pmndrs/zustand
- **Texto da licença:** `licenses/webbg-zustand-MIT.txt`
- **Uso:** estado interno do @react-three/fiber, embutido em `assets/bsl/web_bg`.

### react-use-measure (WebBackgrounds)
- **Versão:** 2.1.7
- **Licença:** MIT
- **Copyright:** Copyright (c) 2019-2025 Poimandres
- **URL:** https://github.com/pmndrs/react-use-measure
- **Texto da licença:** `licenses/webbg-react-use-measure-MIT.txt`
- **Uso:** medição de elementos, embutido em `assets/bsl/web_bg`.

### face-api.js (WebBackgrounds)
- **Versão:** 0.22.2
- **Licença:** MIT
- **Copyright:** Copyright (c) 2018 Vincent Mühler
- **URL:** https://github.com/justadudewhohacks/face-api.js
- **Texto da licença:** `licenses/webbg-face-api-MIT.txt`
- **Uso:** reconhecimento facial usado só pelo fundo "Grid Scan". Atenção à privacidade: veja a seção sobre o Grid Scan em `PRIVACIDADE.md`.

### TensorFlow.js core (WebBackgrounds)
- **Versão:** 1.7.0 (dependência do face-api.js)
- **Licença:** Apache-2.0
- **Copyright:** PENDENTE (o arquivo de licença do pacote não estava no disco)
- **URL:** https://github.com/tensorflow/tfjs
- **Texto da licença:** `licenses/Apache-2.0.txt`
- **Uso:** execução dos modelos do face-api.js.
- **Situação:** PENDENTE: o texto acima é o texto padrão da Apache-2.0; falta registrar o copyright e o aviso NOTICE do pacote, se houver.

### OGL (WebBackgrounds)
- **Versão:** 1.0.11
- **Licença:** Unlicense (conforme o `package.json`)
- **Copyright:** domínio público (Unlicense); autor do pacote: PENDENTE
- **URL:** https://github.com/oframe/ogl
- **Uso:** biblioteca WebGL usada por vários fundos animados.
- **Situação:** PENDENTE: o arquivo de licença do pacote não estava no disco; copiar para `licenses/`.

### Fontes do WebBackgrounds (via Fontsource 5.3.0)
- **Versão:** Fontsource 5.3.0 (Anton, Archivo Black, Bebas Neue, Great Vibes, Montserrat, Orbitron, Oswald, Pacifico, Permanent Marker, Playfair Display)
- **Licença:** SIL Open Font License 1.1 (todas, exceto Permanent Marker: Apache-2.0)
- **Copyright:** Anton 2020 The Anton Project Authors; Archivo Black 2017 The Archivo Black Project Authors; Bebas Neue 2019 The Bebas Neue Project Authors; Great Vibes 2010 The Great Vibes Pro Project Authors; Montserrat 2011 The Montserrat Project Authors; Orbitron 2018 The Orbitron Project Authors; Oswald 2016 The Oswald Project Authors; Pacifico 2018 The Pacifico Project Authors; Playfair Display 2017 The Playfair Display Project Authors; Permanent Marker: ver o texto da licença
- **URL:** https://fontsource.org/
- **Texto da licença:** `licenses/webbg-fonte-anton-OFL-1.1.txt`, `licenses/webbg-fonte-archivo-black-OFL-1.1.txt`, `licenses/webbg-fonte-bebas-neue-OFL-1.1.txt`, `licenses/webbg-fonte-great-vibes-OFL-1.1.txt`, `licenses/webbg-fonte-montserrat-OFL-1.1.txt`, `licenses/webbg-fonte-orbitron-OFL-1.1.txt`, `licenses/webbg-fonte-oswald-OFL-1.1.txt`, `licenses/webbg-fonte-pacifico-OFL-1.1.txt`, `licenses/webbg-fonte-playfair-display-OFL-1.1.txt`, `licenses/webbg-fonte-permanent-marker-Apache-2.0.txt`
- **Uso:** fontes embutidas no bundle para os textos dos fundos animados.

### React Bits (componentes dos fundos animados)
- **Versão:** não registrada (componentes copiados para o projeto de origem, como GridScan, Aurora, Galaxy, Silk...)
- **Licença:** PENDENTE
- **Copyright:** PENDENTE
- **URL:** https://github.com/DavidHDev/react-bits
- **Uso:** vários fundos animados do WebBackgrounds são componentes do React Bits adaptados.
- **Situação:** PENDENTE (decisão jurídica): não há arquivo de licença desses componentes no disco. O projeto de origem é conhecido por usar uma licença MIT com cláusula adicional (a verificar; não confirmado aqui). Se houver restrição de uso comercial ou de venda, ela pode ser incompatível com a GPL-3.0 e com o uso do BSL Stage em locação e eventos. Conferir a licença do repositório e dos componentes usados antes de publicar `assets/bsl/web_bg`.

## Usado sem ser distribuído

### NDI®
- **Versão:** API NDI 5/6 (cabeçalho próprio `third_party/ndi-min/ndi_min.h`)
- **Licença:** o runtime (`Processing.NDI.Lib.x64.dll`) pertence à Vizrt NDI AB e tem licença própria; o BSL Stage NÃO o inclui, não o copia e não o baixa
- **Copyright:** NDI® is a registered trademark of Vizrt NDI AB. O cabeçalho `ndi_min.h` é de autoria própria (BSL Light), escrito a partir da documentação pública, e não contém código nem texto do NDI SDK
- **URL:** https://ndi.video/ (documentação: https://docs.ndi.video; instalação do runtime: https://ndi.video/tools/)
- **Uso:** entrada e saída NDI quando o cliente instala o NDI Tools/Runtime oficial (gratuito). O programa o carrega em tempo de execução; sem ele, a interface avisa e o resto funciona.

### Microsoft Office / LibreOffice
- **Versão:** a que o cliente já tiver instalada
- **Licença:** licenças dos próprios programas (nada deles é distribuído)
- **Copyright:** Microsoft Corporation; The Document Foundation
- **URL:** https://www.microsoft.com/microsoft-365 e https://www.libreoffice.org/
- **Uso:** o BSL Stage só chama o Office ou o LibreOffice já instalados (COM ou linha de comando) para converter apresentações e documentos para PDF.

## Pendências

Itens que dependem de decisão ou verificação de quem responde pelo projeto (não foram decididos nem inventados aqui):

- Licença do DirectML (Microsoft) junto de um programa GPL-3.0: manter a DLL no pacote ou usar só a do Windows.
- Licença dos componentes do React Bits em `assets/bsl/web_bg`.
- FFmpeg da BtbN (só Windows): fixar a build, oferecer o código-fonte correspondente e incluir os avisos das bibliotecas incorporadas (ou compilar um FFmpeg próprio, sem H.264/HEVC). No Linux o FFmpeg é próprio (`tools/package/build-ffmpeg-linux.sh`), mas ainda com H.264/HEVC.
- Código-fonte do `assets/bsl/web_bg`: o arquivo é um bundle minificado; a GPL pede a forma preferida para modificação. O projeto `BSL_Background_Studio` precisa ser publicado (neste repositório ou em outro).
- Inventário das dependências transitivas do `@react-three/drei` e dos demais pacotes npm que entram no bundle (por exemplo com `pnpm licenses list` no projeto de origem).
- Textos que faltam em `licenses/`: CC0-1.0 e WTFPL (glad), @react-three/fiber, OGL, TensorFlow.js core, e a linha de copyright dos modelos Depth Anything V2 Small e MODNet.
- Distribuição do CEF: decidir de onde vem a build (hoje uma pasta local) e conferir o `CREDITS.html` e a licença das DLLs da pasta `cef/` (inclusive `d3dcompiler_47.dll`).
- Razão social e ano do copyright de "BSL Light" para o aviso do programa (hoje: © 2026 BSL Light).
