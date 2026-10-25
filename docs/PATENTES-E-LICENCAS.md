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
| NDI (Fase 7) | Licença | SDK proprietário com termos próprios: módulo opcional, sem distribuir junto. |
| Fontes Barlow / JetBrains Mono | Nenhum | SIL OFL, licenças em `assets/fonts/`. |
| Ícones Lucide (`lucide-static` 1.61.0) | Nenhum | ISC (e MIT para os ícones herdados do Feather); licença em `assets/fonts/LICENSE-Lucide.txt`. |
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
