# Privacidade do BSL Stage

Versão do aviso: 1 (6 de novembro de 2026).

**Aviso:** este texto descreve o que o programa faz, conferido no código. Não é aconselhamento jurídico e deve ser revisado com um advogado antes da versão 1.0.

## Em resumo

- O BSL Stage funciona **offline**. Você pode usá-lo sem internet, sem conta e sem cadastro.
- **Nenhuma telemetria.** O programa não envia estatísticas de uso, relatórios de erro, identificadores nem dados do seu computador.
- A **única conexão automática** é a busca de atualização no GitHub, no máximo uma vez por dia, e dá para desligar.
- Reportar um problema **só acontece pelo navegador**, e você vê e revisa tudo o que será enviado antes.
- Seus projetos, mídias e configurações ficam **no seu computador**.

## A busca de atualização (única conexão automática)

- **O que vai:** uma consulta HTTPS à API pública do GitHub (`api.github.com`, a lista de versões publicadas do projeto). O pedido leva só o nome e a versão do programa (`User-Agent: BSL-Stage/<versão>`) e os cabeçalhos de formato (`Accept`, `X-GitHub-Api-Version`). Não há cookie, conta, token nem nenhum dado seu ou do seu projeto. Como em qualquer conexão pela internet, o GitHub vê o endereço IP de onde o pedido saiu e aplica a política de privacidade dele.
- **Quando:** cerca de 8 segundos depois de abrir o programa, no máximo **uma vez por dia**. Nunca durante um show: não roda com saída ligada, em tela cheia (F11), com o layout bloqueado nem no perfil de desempenho Eco. Se não houver internet, nada aparece na tela; o programa tenta de novo só depois de 6 horas.
- **Como desligar:** Ajuda > Procurar atualizações… e desmarque **Verificar atualizações automaticamente**. A opção fica gravada em `settings.json` (`update.autoCheck`). Com ela desligada, o programa só consulta o GitHub quando você clica em "Procurar atualizações…".
- **Baixar versões:** só acontece quando você clica em **Baixar e instalar**, e só de endereços do GitHub. O arquivo é conferido pelo SHA-256 publicado no release.
- Com **Receber versões beta** ligado (desligado por padrão), a consulta pede a lista de versões em vez só da última estável.

## Reportar problema e sugerir melhoria

- Ajuda > Reportar problema… (ou Sugerir melhoria…) monta o texto no seu computador e mostra uma **prévia exata** do que será enviado.
- As caixas "informações do sistema" (versão do programa, Windows, placa de vídeo, driver OpenGL, perfil de desempenho e memória) e "últimas linhas do log" (até 40 linhas, sem o seu nome de usuário e sem a sua pasta pessoal) vêm **desmarcadas**.
- **Abrir no GitHub** só abre o navegador padrão na página "novo issue" com o texto preenchido. **Você** revisa e confirma lá, com a sua conta do GitHub. O programa não envia nada sozinho e não usa nenhuma credencial.
- Atenção: um issue no GitHub é **público**. Não escreva nada que você não queira tornar público.

## Servidores e portas locais

- **Painel de controle do WebBackgrounds** (botão "Abrir painel de controle" no painel Clip): o servidor local escuta **somente em `127.0.0.1`** (só este computador consegue acessar). Ele só sobe quando você abre o painel.
- **Exceção, desligada por padrão: "Controle remoto pela rede"** (nos efeitos e fontes do WebBackgrounds e do Slice Refiner). Se você ligar essa opção, o servidor passa a escutar em **todas as interfaces de rede** e qualquer aparelho da mesma rede, que souber o endereço e a porta, pode abrir o painel e mexer nos parâmetros, **sem senha**. Use só em rede de confiança. O Firewall do Windows pode perguntar se você autoriza.
- **Receptor DMX (Art-Net e sACN):** vem **desligado**. Ao ligar (Saída > DMX / Art-Net…), o programa escuta nas portas UDP 6454 (Art-Net) e 5568 (sACN), na placa de rede que você escolher (ou em todas), e responde ao ArtPoll com o nome "BSL Stage" para aparecer na lista de nós da mesa. Isso só acontece na rede local.
- **NDI:** só quando você instala o NDI Tools/Runtime da Vizrt (o BSL Stage não o inclui). Ao usar entrada ou saída NDI, o NDI descobre e troca vídeo com outros aparelhos da rede local. Isso é feito pelo runtime do NDI, com a política dele. **Spout** só compartilha imagens entre programas do mesmo computador.

## Conteúdo que carrega da internet por escolha sua

- **Fonte "Web (URL)":** o navegador embutido (CEF/Chromium) abre o endereço que você digitar, como qualquer navegador. Esse site vê o seu IP e pode gravar cookies e cache na pasta de dados do programa (veja abaixo). O BSL Stage não escolhe nem acrescenta nenhum endereço.
- **Fundo "Grid Scan" do WebBackgrounds:** pelo código-fonte desse fundo, ele tenta baixar modelos de reconhecimento facial do `cdn.jsdelivr.net` (o CDN jsDelivr), mesmo com a webcam desligada. A conexão só ocorre se você escolher esse fundo e houver internet; sem internet falha em silêncio e o fundo funciona. Os demais fundos e as fontes embutidas funcionam sem rede. (Verificado no código, não medido em execução.) Esse ponto está registrado para ser corrigido no projeto do WebBackgrounds.
- **Office e PDF:** converter uma apresentação ou documento usa o Microsoft Office ou o LibreOffice **já instalados** no seu computador. O que eles fazem na rede é com o programa deles.

## IA local

Os efeitos marcados com "IA" rodam **só neste computador** (ONNX Runtime e, se houver, a GPU pela DirectML). Nenhuma imagem sai do computador. Os modelos acompanham o programa (ou são baixados na compilação), nunca durante o uso. A build oficial do ONNX Runtime para Windows traz telemetria da plataforma ligada por padrão; o BSL Stage **a desliga** ao iniciar o ONNX Runtime.

## O que o programa guarda no seu computador

- `%APPDATA%\BSL Stage\` (pasta do usuário, Roaming):
  - `settings.json`: as suas preferências (janela, layout, atalhos, atualizações, DMX, projetos recentes...).
  - `imgui.ini` (disposição das janelas), `layouts\` (seus layouts salvos), `web-logos\` (logos enviados ao painel do WebBackgrounds).
  - `bslstage.log`: o registro da sessão atual (é recomeçado a cada abertura). Pode conter caminhos de arquivos e nomes de clips.
  - `autosave\` e `session.lock`: cópias de segurança do projeto aberto, para recuperar depois de uma queda. São apagadas quando o programa fecha normalmente.
- `%LOCALAPPDATA%\BSL Stage\` (dados locais):
  - `updates\`: versão baixada, cópia de segurança da versão anterior, `updater.log` e o resultado da última atualização.
  - `cache\`: PDFs convertidos de documentos do Office e o perfil temporário do LibreOffice. `ffgl-cache.json`: lista de plugins FFGL.
  - `cef\`: cache e cookies do navegador embutido.
- `Documents\BSL Stage\`: as **suas** pastas de efeitos ISF (`ISF\`) e de plugins (`Plugins\`).
- **Projetos** (`.bslstage`) ficam onde você os salvar. O projeto guarda os **caminhos** das suas mídias; as mídias não são copiadas.

Para apagar tudo o que o programa guardou, feche o BSL Stage e apague as pastas `BSL Stage` em `%APPDATA%` e `%LOCALAPPDATA%` (e, se quiser, as suas pastas em `Documents\BSL Stage`).

## Quem recebe os seus dados

A BSL Light **não recebe nenhum dado** do programa: não existe servidor, conta nem painel de análise. As únicas partes de fora que podem receber algo são as que você aciona: o **GitHub** (atualização, e o reporte que você mesmo envia), os sites que você carrega na fonte Web, e o **jsDelivr** no caso do fundo Grid Scan descrito acima.

## Mudanças deste aviso

Quando este texto mudar de forma que importe para você, o número da versão do aviso sobe e o instalador da nova versão mostra o texto atualizado. Ajuda > Licenças e avisos… mostra sempre a versão em vigor.

## Contato

Dúvidas e pedidos sobre privacidade: abra um issue no repositório do projeto (Ajuda > Sobre mostra o endereço). PENDENTE: definir um e-mail de contato da BSL Light para assuntos de privacidade.
