# Como publicar uma versão do BSL Stage

O programa se atualiza pelo **GitHub Releases** e recebe reportes pelo **GitHub Issues**. Não há servidor próprio, token nem segredo no
programa. Este guia diz como publicar uma versão e o que ainda depende de passo manual.

## 1. Antes da primeira vez

1. **Repositório.** O valor atual é `souza28403/bsl-stage`. Ele vem de uma constante só, `BSLSTAGE_GITHUB_REPO`
   (`CMakeLists.txt`, gerada em `Version.h`). Troque-o nestes lugares quando o repositório de verdade existir:
   - `CMakeLists.txt` (variável de cache `BSLSTAGE_GITHUB_REPO`; o workflow de release já passa `-DBSLSTAGE_GITHUB_REPO=<dono/nome>`
     do repositório que está publicando, então os pacotes publicados apontam para o lugar certo mesmo sem editar);
   - `.github/ISSUE_TEMPLATE/config.yml` (link de Discussões) e o `README.md`.
2. **Rótulos** `bug` e `enhancement` no repositório (os repositórios novos já os têm). São os que "Reportar problema…" e "Sugerir melhoria…"
   usam no endereço do GitHub.
3. **Issues em branco habilitados** (`blank_issues_enabled: true` em `.github/ISSUE_TEMPLATE/config.yml`). O programa abre
   `https://github.com/<repo>/issues/new?title=…&body=…&labels=…` com o texto pronto; com os formulários `bug.yml`/`melhoria.yml` o GitHub ignora
   o `body` da URL, então o texto pré-preenchido só funciona pelo issue em branco. *Esse comportamento não foi testado contra o GitHub de verdade
   (nada foi criado lá); confira uma vez depois de publicar o repositório.*
4. **Permissão do workflow:** Settings > Actions > General > Workflow permissions pode ficar em "Read repository contents"; o próprio
   `release.yml` pede `contents: write`. O token é o `GITHUB_TOKEN` automático: **nenhum segredo precisa ser criado.**

## 2. Publicar uma versão

1. Escolha o número (semver). Edite `project(BSLStage VERSION x.y.z …)` no `CMakeLists.txt` (é o único lugar da versão) e faça commit.
2. Crie e envie a tag: `v1.2.0` (estável) ou `v1.3.0-beta.1` (pré-release: só quem marcou "Receber versões beta" é avisado).
   O workflow recusa tag que não bata com a versão do CMake.
3. O workflow `.github/workflows/release.yml` (`windows-latest`) compila em Release, roda `bslstage_updatetest`, `bslstage_uitest` e
   `bslstage_maptest`, monta `BSL-Stage-<versão>-win64.zip`, gera `SHA256SUMS.txt` e cria o release **como rascunho**.
4. Abra o rascunho em Releases, confira o pacote e as notas (o programa mostra as notas em markdown simples: títulos, listas, negrito, código),
   resolva os itens manuais abaixo e clique em **Publish**. Só depois de publicado o programa dos clientes enxerga a versão.
5. Ensaio sem publicar: Actions > Release > "Run workflow" compila e empacota e guarda o `.zip` como artefato, sem criar release.

Nomes que o programa espera nos arquivos do release:

| Arquivo | Para quê |
|---|---|
| `BSL-Stage-<versão>-win64.zip` | o pacote (escolhido pelo nome; um `.zip` sem "src"/"source" no nome também serve). Pode haver uma pasta de topo (`BSL-Stage-1.2.0-win64/`) ou o `BSLStage.exe` na raiz |
| `BSL-Stage-<versão>-win64.msi` | o instalador (ver `docs/instalador/COMO-GERAR-O-INSTALADOR.md`). É o que o programa baixa quando foi instalado por ele (marca `InstalledBy=msi` no registro); quem usa o `.zip` continua recebendo o `.zip` |
| `SHA256SUMS.txt` | uma linha `<sha256>  <arquivo>` por arquivo. O programa confere o download contra ele; **sem esse arquivo, avisa e pede confirmação** |
| `*Setup*.exe` / `*installer*.exe` | alternativa ao `.zip`: o programa baixa e executa o instalador quando o app fechar (sem backup: quem cuida é o instalador) |

## 3. O que depende de passo manual (não foi inventado aqui)

- **Fonte Web (CEF).** O `CMakeLists.txt` procura o CEF em `BSL_CEF_DIR`, cujo padrão é uma pasta local do desenvolvedor. No runner do GitHub ela não
  existe, então o pacote do workflow sai **sem a fonte Web** (o resto funciona; a aba Fontes avisa). Para incluí-la é preciso baixar a distribuição
  binária do CEF (a mesma versão do desenvolvimento), apontar `-DBSL_CEF_DIR=` no passo "Configurar" e distribuir junto o `CREDITS.html` do CEF
  (ver `docs/PATENTES-E-LICENCAS.md`). Isso não está automatizado porque a URL/versão do CEF não está fixada no repositório.
- **FFmpeg sem H.264/HEVC.** O `CMakeLists.txt` baixa o FFmpeg LGPL da BtbN, que **inclui** os decodificadores `h264` e `hevc`.
  `docs/PATENTES-E-LICENCAS.md` recomenda, antes de distribuir publicamente, usar o decodificador do Windows para esses codecs e distribuir um
  FFmpeg compilado com `--disable-decoder=h264,hevc`. Isso ainda não foi feito, e é por esse motivo (além da revisão) que o release nasce como
  **rascunho** e o workflow imprime um aviso. Não publique para o público até decidir isso.
- **Licenças, privacidade e termos no pacote.** Os documentos vão dentro do `.zip` e do `.msi` (os dois saem da mesma pasta, montada por
  `tools/package/stage-dist.ps1`). A tela de licença do `.msi` vem de `docs/instalador/AVISO-INSTALADOR.md`. O workflow
  inclui `LICENSE`, `THIRD-PARTY-NOTICES.md`, `PRIVACIDADE.md`, `TERMOS-DE-USO.md`, a pasta `licenses/` (textos das licenças de terceiros, inclusive os do DirectML,
  do ONNX Runtime e do PDFium), `README.md` e `docs/PATENTES-E-LICENCAS.md`, e falha se algum faltar. O CMake também os copia para `build\Release`, de onde o programa
  os lê (Ajuda > Licenças e avisos…). O `.msi` instala esses mesmos arquivos e junta o runtime do Visual C++ (app-local).
  Ao mudar uma dependência, atualize `THIRD-PARTY-NOTICES.md` e `licenses/` (o `bslstage_uitest` confere os nomes e as versões do `CMakeLists.txt`) e, se a
  licença, a privacidade ou os termos mudarem, suba `legal::kNoticeVersion` em `src/legal/Notices.h` (aparece no rodapé de Licenças e avisos).
- **Pendências jurídicas.** `THIRD-PARTY-NOTICES.md` (seção "Pendências") e `TERMOS-DE-USO.md` listam o que precisa de decisão ou revisão de advogado antes da
  1.0: DirectML junto de um programa GPL, licença dos componentes do React Bits no bundle `assets/bsl/web_bg`, build do FFmpeg da BtbN (fixar, avisos das
  bibliotecas embutidas, H.264/HEVC), código-fonte do bundle web, política de marca e e-mail de contato. Resolva-as (ou decida publicar assim) antes de publicar o rascunho.
- **NDI.** O runtime do NDI **não** é distribuído (o cliente instala o NDI Tools). Nada a fazer.
- **Assinatura de código.** O CI não assina (o certificado não está no repositório). A assinatura é local: `tools/package/build-msi.ps1 -Sign`
  assina os executáveis e o `.msi`; o `.msi` assinado substitui o do rascunho (e o `SHA256SUMS.txt` deve ser atualizado). Sem assinatura, o SmartScreen pode avisar.
- **Versão do Visual Studio do runner.** O passo "Configurar" tenta o preset `windows-vs2026` e, se o runner não tiver o Visual Studio 2026,
  cai no `windows` (Visual Studio 2022). Uma versão nova do Visual Studio no `windows-latest` pede um preset novo no `CMakePresets.json`.
- **Release feito daqui.** Para incluir a fonte Web, o release pode ser criado com o `.msi` e o `.zip` gerados localmente
  (`tools/package/build-msi.ps1`), o `SHA256SUMS.txt` e `gh release create`. Foi assim na 0.1.2.

## 4. Como o programa atualiza (para suporte)

- Ajuda > **Procurar atualizações…** consulta `GET https://api.github.com/repos/<repo>/releases/latest` (com "Receber versões beta" ligado, a lista
  `/releases?per_page=10`). A verificação automática roda no máximo uma vez por dia, em segundo plano, ~8 s depois de abrir, e **não** roda com saída
  ligada, tela cheia (F11), layout bloqueado ou perfil Eco. Falha de rede é silenciosa.
- Só vai ao GitHub o cabeçalho `User-Agent: BSL-Stage/<versão>` (e `Accept`/`X-GitHub-Api-Version`). Sem cookies, sem credenciais, sem nenhum dado do
  usuário. O texto de um reporte só sai quando o usuário clica em "Abrir no GitHub" e confirma lá.
- O download vai para `%LOCALAPPDATA%\BSL Stage\updates\downloads\<tag>\`, com o SHA-256 conferido. O `bslstage_updater.exe` roda **depois** que o
  programa fecha (na próxima vez que fechar, ou em "Reiniciar agora" se nada estiver tocando), guarda a versão anterior em
  `%LOCALAPPDATA%\BSL Stage\updates\backup\<versão>\`, troca só os arquivos que existem no pacote, **restaura tudo se algo falhar** e reabre o programa.
  Projetos (`.bslstage`), `settings.json`, `imgui.ini` e atalhos nunca são sobrescritos nem apagados. O registro fica em `updates\updater.log` e o
  resultado em `updates\last-result.json` (o programa mostra no log na abertura seguinte).
- Instalado pelo `.msi` (marca `HKLM\Software\BSL Light\BSL Stage\InstalledBy=msi`), o programa **não** troca arquivos: ele baixa o asset `.msi` da release
  (SHA-256 conferido), e quando o programa fecha o `bslstage_updater.exe` roda `msiexec /i <msi>` com a interface do instalador (o próprio Windows pede
  a permissão de administrador) e reabre o programa se a instalação terminar. Se a release não trouxer `.msi`, o programa avisa e não baixa o `.zip`
  (trocar arquivos em `Program Files` quebraria o reparo do MSI).
- Instalado por `.zip` numa pasta protegida (ex.: `C:\Program Files`), o atualizador pede a elevação do Windows (UAC) no momento de instalar.
  Arquivos que existiam na versão antiga e não existem mais no pacote **permanecem** (nada é apagado).
- Testar o fluxo sem GitHub: `BSLStage.exe --qa <pasta> --fake-release <pasta com release.json e os assets>` (ver `tools/uiqa/updshots.py`).
