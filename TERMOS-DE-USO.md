# Termos de uso do BSL Stage

Versão do aviso: 1 (6 de novembro de 2026).

**Aviso importante:** este texto é um resumo em linguagem simples, feito pela equipe do projeto. **Não é aconselhamento jurídico** e não substitui a licença. **Revisar com advogado antes da versão 1.0.** Se este resumo e o arquivo `LICENSE` divergirem, vale o `LICENSE`.

## Em resumo

- O BSL Stage é **software livre**, sob a licença **GNU GPL versão 3 (GPL-3.0)**. © 2026 BSL Light.
- Não existe um contrato de instalação (EULA) proprietário: instalar e usar o programa não exige "aceitar" nada além da GPL-3.0, e a GPL só pede algo de quem **distribui** o programa.
- **Software livre, sem garantia.** O uso é por sua conta e risco.
- Os **nomes e o logo** da BSL Light e do BSL Stage não são cobertos pela GPL.

## O que a GPL-3.0 permite (resumo)

- **Usar** o programa para qualquer fim, inclusive comercial: shows, eventos, locação, instalações fixas. Sem pagar licença à BSL Light.
- **Estudar** o código-fonte e **modificá-lo** para as suas necessidades.
- **Copiar e compartilhar** o programa com outras pessoas.
- **Distribuir versões modificadas**, desde que você:
  - mantenha a mesma licença (GPL-3.0) para o que distribuir;
  - mantenha os avisos de copyright e de licença;
  - ofereça o **código-fonte correspondente** a quem receber o programa;
  - indique que modificou o programa.
- Quem contribui com código concede também uma licença das **patentes** que cubram a sua contribuição (GPL-3.0, seção 11).
- Você não pode acrescentar restrições extras à GPL-3.0 sobre o que você distribui.

O código-fonte completo está no repositório do projeto (Ajuda > Sobre mostra o endereço). O texto integral da licença está no arquivo `LICENSE` e em Ajuda > Licenças e avisos… > Licença > Ver texto completo.

## Sem garantia (GPL-3.0, seções 15 e 16)

Em português simples: o programa é oferecido **como está**, sem garantia de que funcione, de que sirva para um fim específico ou de que não tenha erros, e a BSL Light e quem contribuiu **não respondem** por danos causados pelo uso, como perda de dados, falha durante um evento ou prejuízo a terceiros, na medida em que a lei permitir.

O texto original (em inglês) é o que vale:

```
15. Disclaimer of Warranty.

THERE IS NO WARRANTY FOR THE PROGRAM, TO THE EXTENT PERMITTED BY
APPLICABLE LAW.  EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT
HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM "AS IS" WITHOUT WARRANTY
OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE.  THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE PROGRAM
IS WITH YOU.  SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME THE COST OF
ALL NECESSARY SERVICING, REPAIR OR CORRECTION.

16. Limitation of Liability.

IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING
WILL ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO MODIFIES AND/OR CONVEYS
THE PROGRAM AS PERMITTED ABOVE, BE LIABLE TO YOU FOR DAMAGES, INCLUDING ANY
GENERAL, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE
USE OR INABILITY TO USE THE PROGRAM (INCLUDING BUT NOT LIMITED TO LOSS OF
DATA OR DATA BEING RENDERED INACCURATE OR LOSSES SUSTAINED BY YOU OR THIRD
PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE WITH ANY OTHER PROGRAMS),
EVEN IF SUCH HOLDER OR OTHER PARTY HAS BEEN ADVISED OF THE POSSIBILITY OF
SUCH DAMAGES.
```

### Uso em shows e eventos

Um programa de vídeo ao vivo pode falhar, travar ou ter erros. Antes de um evento, **teste o seu projeto, o seu computador e as suas saídas**, e tenha um plano B (outro computador, mídia em outro programa, operador de reserva). Não use o BSL Stage em situações em que uma falha possa colocar pessoas em risco (por exemplo, como único controle de segurança, de iluminação de emergência ou de efeitos pirotécnicos).

## Instalação, atualização e remoção

- O BSL Stage não precisa de instalador: o pacote `.zip` é extraído numa pasta e o `BSLStage.exe` é executado. Os executáveis não são assinados digitalmente, então o Windows SmartScreen pode avisar na primeira execução.
- **Atualizações** são opcionais: o programa só avisa que existe uma versão nova; você decide se baixa e instala (veja `PRIVACIDADE.md`). Cada versão nova também é software livre sob a GPL-3.0. Projetos e configurações não são sobrescritos pela atualização.
- **Remover:** apague a pasta do programa e, se quiser, as pastas de dados descritas em `PRIVACIDADE.md`.

## Marcas e logo

- **"BSL Light", "BSL Stage" e o logo** (monograma e logotipos da pasta `assets/brand`) **não são cobertos pela GPL-3.0**. A licença livre vale para o código e para o que o texto dela cobre; os nomes e a identidade visual continuam sendo da BSL Light.
- Você **pode** usar o programa, redistribuí-lo sem alterações com o nome original e dizer, de forma verdadeira, que o seu trabalho foi feito "com o BSL Stage" ou que algo é "compatível com o BSL Stage".
- Você **não pode** usar os nomes ou o logo de um jeito que sugira que a sua versão modificada, o seu produto ou o seu serviço é oficial ou aprovado pela BSL Light. Quem distribuir uma versão **modificada** deve trocar o nome e retirar o logo e as imagens da marca da BSL Light (pasta `assets/brand`).
- Outras marcas citadas (NDI, Apple ProRes, HAP, Microsoft e demais) pertencem aos seus donos e aparecem só para indicar compatibilidade; não há vínculo, patrocínio nem endosso.
- PENDENTE (decisão jurídica): a política formal de marca (o que é permitido e o que exige permissão) e a pesquisa/registro da marca "BSL Stage" no INPI (`docs/PATENTES-E-LICENCAS.md`).

## Patentes e formatos de vídeo (H.264 e HEVC)

- Os formatos de vídeo **H.264 (AVC)** e **H.265 (HEVC)** são cobertos por patentes administradas por consórcios (pools de licença, como Via LA e Access Advance). Em alguns países, **decodificar ou distribuir** software que decodifica esses formatos pode exigir licença.
- O BSL Stage decodifica vídeo com o **FFmpeg** (biblioteca livre, LGPL-3.0) e, na versão atual, a build usada **inclui** os decodificadores H.264 e HEVC. **A BSL Light não oferece licença de patente desses codecs.** O que o projeto pretende fazer antes do lançamento público (usar o decodificador do próprio Windows, que já é licenciado pela Microsoft e pelo fabricante da placa de vídeo, e distribuir um FFmpeg sem esses decodificadores) está descrito em `docs/PATENTES-E-LICENCAS.md`.
- Até lá, **você** é responsável por ter os direitos necessários para o uso e para a distribuição que fizer, no seu país. Formatos como VP8/VP9/AV1 (WEBM), HAP, PNG, JPEG, GIF e WEBP têm risco de patente baixo ou nulo, segundo o levantamento do projeto.
- A GPL-3.0 não dá a você licença de patentes de terceiros, só das patentes de quem contribuiu com o código do projeto.
- PENDENTE (decisão jurídica): decidir se a primeira versão pública sai com os decodificadores H.264/HEVC desativados no FFmpeg distribuído.

## NDI e outras tecnologias de terceiros

- **NDI®** é marca registrada da **Vizrt NDI AB**. O BSL Stage **não inclui** o runtime do NDI: quem usa instala o NDI Tools/Runtime oficial (gratuito) e aceita a licença da Vizrt diretamente com ela. O BSL Stage só carrega o runtime quando ele já está instalado.
- O BSL Stage traz software de terceiros, cada um sob a **sua própria licença**, listado em `THIRD-PARTY-NOTICES.md` (e em Ajuda > Licenças e avisos… > Terceiros), com os textos completos em `licenses/`. Em especial, a `DirectML.dll` é da Microsoft e tem licença própria.

## Responsabilidade sobre o conteúdo e os plugins

- Os **vídeos, imagens, músicas, fontes, páginas web, PDFs** e outros conteúdos que você usa no BSL Stage são de sua responsabilidade: o programa não verifica se você tem os direitos para exibi-los.
- **Plugins** (FFGL, ISF) e outros arquivos de terceiros que você carregar têm a licença e os riscos deles. O BSL Stage protege as chamadas aos plugins FFGL contra falhas e faz a varredura deles num processo separado, mas não analisa o que eles fazem.
- O **controle remoto pela rede** e o **receptor DMX** (desligados por padrão) abrem portas na sua rede local; cuide de proteger essa rede.

## Mudanças e contato

Quando este texto mudar de forma que importe, o número da versão do aviso sobe e o instalador da nova versão mostra o texto atualizado. Dúvidas, erros neste texto ou pedidos de permissão (por exemplo, uso da marca): abra um issue no repositório do projeto. PENDENTE: definir um e-mail de contato da BSL Light.

## Pendências jurídicas (para quem responde pelo projeto)

Antes da versão 1.0, estes pontos precisam de decisão ou revisão de um advogado. Nenhum foi decidido neste texto:

- razão social e ano do copyright de "BSL Light" no aviso do programa;
- política de marca e registro de "BSL Stage" (INPI);
- decisão sobre H.264/HEVC e a distribuição do FFmpeg (patentes);
- DirectML (licença da Microsoft junto de um programa GPL-3.0);
- licença dos componentes do React Bits e publicação do código-fonte do bundle `assets/bsl/web_bg`;
- lei aplicável e foro (a GPL-3.0 não define), e como tratar pedidos de privacidade (LGPD, GDPR);
- e-mail de contato.

A lista completa dos itens de terceiros pendentes está no fim de `THIRD-PARTY-NOTICES.md`.
