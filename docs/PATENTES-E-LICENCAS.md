# Patentes, licenças e marcas — análise de risco

> Levantamento técnico feito em 2026-10-24 com base em fontes públicas. **Não é parecer jurídico.**
> Antes de distribuir publicamente (principalmente fora do Brasil), vale uma revisão com advogado
> de propriedade intelectual.

## Resumo

| Item | Risco | Situação / ação |
|---|---|---|
| Ideia do programa (clips, camadas, grade, slices) | Baixo | Nenhuma patente da Resolume B.V. encontrada. Conceito de VJ/mixer existe desde os anos 90. |
| Código do Resolume | — | Não copiamos nada (regra clean room em `ENGENHARIA-REVERSA.md`). Isso é direito autoral, não patente. |
| H.264 (AVC) — decodificação | **Médio** | Patentes no pool Via LA; última patente nos EUA prevista para ~nov/2027. Ver mitigação abaixo. |
| H.265 (HEVC) — decodificação | **Médio-alto** | Patentes ativas em vários pools (Access Advance, Via LA). Ver mitigação abaixo. |
| VP8 / VP9 / AV1 (WEBM) | Baixo | Formatos com licença livre do Google/AOMedia. A Sisvel tem pool contestando, mas cobra só **hardware**, não software nem conteúdo. |
| ProRes | Baixo-médio | A Apple tem programa de licença do ProRes; o decoder do FFmpeg é independente. Não usar o nome "Apple ProRes" como selo de certificação. |
| HAP | Nenhum | Formato aberto (Vidvox, BSD). |
| DXV3 | Baixo | Nenhuma patente encontrada; decoder do FFmpeg feito por interoperabilidade. |
| PNG, JPG, GIF, WEBP, TIFF | Nenhum | Patentes do GIF (LZW) e JPEG relevantes já expiraram. |
| PDF (PDFium, Fase 3) | Nenhum | ISO 32000; a Adobe concede licença de patente gratuita para implementações. PDFium é BSD-3 (binários bblanchon/pdfium-binaries: licenças das dependências em `third_party/pdfium/*/licenses`; distribuir junto o `LICENSE`). |
| Web (CEF, Fase 3B) | Baixo | CEF/Chromium: BSD-3, com licenças de terceiros listadas em `CREDITS.html` (distribuir junto). O build padrão do CEF **não** inclui os codecs H.264/AAC proprietários (só VP8/VP9/AV1, Opus, Vorbis…), o que combina com a mitigação dos codecs acima. |
| Office (Fase 3B) | Nenhum | O programa só chama o Microsoft Office / LibreOffice **já instalados pelo usuário** (COM / linha de comando); nada deles é distribuído. |
| Warp de 4 cantos (homografia) | Nenhum | Matemática de livro-texto, décadas de uso. |
| Soft edge / edge blending | Baixo | Técnica antiga; patentes antigas (ex.: US 6,568,816) já expiraram. |
| Warp por malha (mesh) | **Verificar** | Há patentes ativas sobre correção de warp em malha (ex.: US 10,115,226 / 10,403,028). Ler as reivindicações antes de implementar. |
| Calibração automática com câmera | **Evitar** | Área muito patenteada (VIOSO, Christie e outros). Não implementar sem revisão. |
| Spout | Nenhum | BSD-2. |
| NDI® (entrada e saída) | Licença / marca | O **runtime não é distribuído** pelo projeto: o cliente instala o NDI Tools/Runtime oficial (gratuito) e o app o carrega em tempo de execução. O cabeçalho `third_party/ndi-min/ndi_min.h` é **de autoria própria** (não é o do SDK). A marca vai no Sobre. Ver a seção "NDI" abaixo. |
| Spout (entrada) | Nenhum | Mesmo SpoutGL (BSD-2) da saída. |
| Fontes Barlow / JetBrains Mono | Nenhum | SIL OFL, licenças em `assets/fonts/`. |
| Ícones Lucide (`lucide-static` 1.61.0) | Nenhum | ISC (e MIT para os ícones herdados do Feather); licença em `assets/fonts/LICENSE-Lucide.txt`. |
| IA local: ONNX Runtime 1.24.4 | Nenhum | MIT. Pacote NuGet oficial `Microsoft.ML.OnnxRuntime.DirectML`. Ver a seção "IA local" abaixo. |
| IA local: DirectML 1.15.4 (`DirectML.dll`) | Licença própria (permissiva) | NÃO é MIT: "Microsoft Software License Terms – DirectML". Permite copiar e distribuir a DLL em aplicativos para Windows que usam ferramentas de aprendizado de máquina. Ver a seção "IA local". |
| IA local: Depth Anything V2 **Small** | Nenhum | Apache-2.0 (só o Small; os modelos Base/Large/Giant NÃO são Apache e não são usados). |
| IA local: MODNet | Nenhum | Apache-2.0. |
| Nome "BSL Stage" | Verificar | Pesquisar marca no INPI antes do lançamento público. Não usar "Resolume" em nome, ícone ou divulgação. |

## Brasil × exterior

- No Brasil, "programas de computador em si" **não são patenteáveis** (Lei 9.279/96, art. 10, V);
  software é protegido por direito autoral (Lei 9.609/98). O INPI aceita só invenções implementadas
  por computador com efeito técnico.
- Como o projeto é open source e pode ser baixado de qualquer país, patentes dos **EUA** são o risco
  prático principal — principalmente codecs.

## Mitigação dos codecs H.264 / HEVC (recomendado antes do lançamento)

1. **Usar o decodificador do próprio Windows (Media Foundation / D3D11VA) para H.264 e HEVC.**
   O Windows e o fabricante da GPU já licenciam esses codecs; o HEVC exige a extensão da Microsoft
   Store, que o usuário instala. É o mesmo caminho que o Firefox usa.
2. **Distribuir o FFmpeg compilado sem os decoders `h264` e `hevc`** (`--disable-decoder=h264,hevc`),
   mantendo VP8/VP9/AV1, HAP, DXV, ProRes etc.
3. Se o Windows não tiver o codec, o programa sugere "Otimizar mídia" (converter para HAP ou VP9),
   que já está no roadmap.

Enquanto o projeto não for distribuído publicamente, o FFmpeg completo pode continuar no build de
desenvolvimento.

## NDI

- **Runtime**: `Processing.NDI.Lib.x64.dll` pertence à Vizrt NDI AB e tem licença própria. O BSL Stage **não inclui, não
  copia e não baixa** essa DLL. Ela é procurada nesta ordem: caminho de `--ndi-lib` (só para testes), pasta do executável,
  variáveis `NDI_RUNTIME_DIR_V6` e `NDI_RUNTIME_DIR_V5` (o instalador do NDI Tools/Runtime define essas variáveis). Sem ela, a
  interface mostra "NDI Runtime não encontrado" com o link para instalar (<https://ndi.video/tools/>, só texto e botão para
  abrir a página) e o resto do programa funciona normalmente.
- **Cabeçalho**: `third_party/ndi-min/ndi_min.h` foi escrito do zero para este projeto, declarando só o subconjunto da API C que
  usamos (descoberta, receptor e emissor de vídeo), a partir da documentação pública do NDI 5/6 (<https://docs.ndi.video>). Não
  copia o `Processing.NDI.Lib.h` do SDK nem seu texto. A ordem da tabela de funções (`NDIlib_v5_load`) foi conferida contra os
  símbolos exportados pelo runtime oficial (o `bslstage_ndittest` repete essa conferência).
- **Marca**: "NDI® is a registered trademark of Vizrt NDI AB" aparece em Ajuda > Sobre. Não usar o logotipo do NDI sem seguir as
  diretrizes de marca da NDI.
- **Teste**: `bslstage_ndittest --ndi-lib <dll>` carrega o runtime de onde ele já estiver instalado (nesta máquina, a pasta do
  Resolume Arena); a DLL nunca é copiada para o projeto nem para a pasta de build.
- **Distribuição**: se um dia o instalador do BSL Stage incluir o runtime, será preciso aceitar a licença de redistribuição do NDI
  SDK e seguir o guia de marca. Hoje o instalador não o inclui.

## IA local (ONNX Runtime, DirectML e modelos)

Os efeitos "BSL Relight 2.5D", "BSL Cybernetic Body-Parts Glitch", "BSL Performer Aura & Fluid" e "BSL Z-Slice LED Mapper" (selo "IA" na lista de efeitos) usam IA que roda **só neste PC**:
nenhuma imagem é enviada para fora. É opcional e fica desligada no perfil Eco. Os arquivos são baixados no `cmake` (configure) para
`third_party/onnxruntime/` e `assets/models/` (ambos no `.gitignore`), com versão e SHA256 fixos no `CMakeLists.txt`.

| Componente | Versão | Licença | Origem |
|---|---|---|---|
| ONNX Runtime (`onnxruntime.dll`, `onnxruntime_providers_shared.dll`, cabeçalhos) | 1.24.4 | MIT | <https://www.nuget.org/packages/Microsoft.ML.OnnxRuntime.DirectML/1.24.4> · <https://github.com/microsoft/onnxruntime> |
| DirectML (`DirectML.dll`, `DirectML.h`) | 1.15.4 | Microsoft Software License Terms – DirectML (`LICENSE.txt` do pacote); o código de exemplo/cabeçalhos do pacote são MIT (`LICENSE-CODE.txt`) | <https://www.nuget.org/packages/Microsoft.AI.DirectML/1.15.4> |
| Depth Anything V2 **Small**, ONNX fp16 (`depth_anything_v2_small_fp16.onnx`, 49,6 MB) | revisão `4472b73` | Apache-2.0 | <https://huggingface.co/onnx-community/depth-anything-v2-small> (conversão do modelo <https://huggingface.co/depth-anything/Depth-Anything-V2-Small>, <https://github.com/DepthAnything/Depth-Anything-V2>) |
| MODNet, ONNX fp32 (`modnet_fp32.onnx`, 25,9 MB) | revisão `fa2fa54` | Apache-2.0 | <https://huggingface.co/Xenova/modnet> (conversão do modelo oficial <https://github.com/ZHKKKe/MODNet>) |

- **DirectML.dll não é MIT.** A licença do pacote (seção 1a) permite copiar e distribuir o software dentro de aplicativos e serviços
  que você desenvolve com ferramentas e frameworks de aprendizado de máquina, para Windows e Xbox. O BSL Stage a copia ao lado do exe
  só para garantir uma versão recente do DirectML (a do `System32` pode ser antiga). Se o instalador distribuir a DLL, incluir o
  `LICENSE.txt` e o `ThirdPartyNotices.txt` do pacote `Microsoft.AI.DirectML` e o `ThirdPartyNotices.txt` do ONNX Runtime. Alternativa
  sem redistribuir nada: usar a `DirectML.dll` do Windows (o app procura primeiro ao lado do exe e, se não achar, a do `System32`).
  Antes de distribuir publicamente, ler a licença completa (inclui cláusulas de coleta de dados e de arbitragem nos EUA).
- **Só o Depth Anything V2 *Small* é Apache-2.0**; os tamanhos Base, Large e Giant têm licença CC-BY-NC-4.0 e **não** podem entrar no
  produto. Se um dia trocar o modelo, conferir a licença do tamanho escolhido.
- **Por que MODNet** (e não o MediaPipe Selfie Segmentation): o MediaPipe só distribui TFLite, e as conversões para ONNX são de
  terceiros (PINTO Model Zoo). O MODNet tem repositório oficial Apache-2.0 e um ONNX pronto e conferido no Hugging Face (Xenova, mesma
  licença), com entrada de tamanho livre (múltiplo de 32), que permite a resolução pequena de 384x224 usada aqui.
- **Imagem de teste**: `testmedia/person.jpg` (pasta ignorada pelo git, só para o `bslstage_aitest`) é a foto "NASA astronaut
  Leland D. Melvin with his dogs Jake and Scout" (NASA, domínio público),
  <https://commons.wikimedia.org/wiki/File:NASA_astronaut_Leland_D._Melvin_with_his_dogs_Jake_and_Scout.jpg>. Sem ela o teste valida
  só o formato de saída da máscara.
- **Privacidade**: o app não abre conexão de rede para a IA. Os downloads acontecem só no `cmake`, em tempo de compilação.

## Fontes

- Resolume B.V. — dados da empresa: <https://resolume.com/company>
- Advanced Video Coding (patentes e pool): <https://en.wikipedia.org/wiki/Advanced_Video_Coding>
- Via LA: <https://en.wikipedia.org/wiki/Via-LA>
- Prazo das patentes AVC nos EUA: <https://phpboyscout.uk/an-encoder-on-borrowed-time/>
- HEVC e pools: <https://www.forasoft.com/learn/video-encoding/articles/h265-hevc-explained>
- FFmpeg — licença e patentes: <https://ffbox0-bg.ffmpeg.org/legal.html>
- Sisvel VP9/AV1 (só hardware, sem cobrança de conteúdo):
  <https://www.streamingmedia.com/Articles/News/Online-Video-News/Sisvel-Launches-Patent-Pools-for-VP9-and-AV1-130840.aspx>,
  <https://www.streamingmedia.com/Articles/News/Online-Video-News/No-Content-Royalties-in-Sisvel-VP9-AV1-Patent-Pools-130849.aspx>
- Edge blending (expirada): <https://patents.google.com/patent/US6568816>
- Warp em malha: <https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/10115226>,
  <https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/10403028>
- VIOSO (calibração patenteada): <https://vioso.com/vioso-anyblend/>
- Lei 9.279/96 art. 10: <https://wiki.endsoftwarepatents.org/wiki/Brazil>
- NDI (documentação pública e ferramentas): <https://docs.ndi.video>, <https://ndi.video/tools/>
