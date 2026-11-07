# Aviso do instalador

Este é o texto da tela de licença do instalador `.msi`. O `tools/package/build-msi.ps1` converte a seção "Texto da tela do instalador" (a partir da linha abaixo) em RTF. O que vem antes dela não aparece no instalador.

O aviso curto saiu da janela "Bem-vindo ao BSL Stage" da primeira execução do programa. Os links dele abriam as abas Licença, Privacidade e Termos da janela "Licenças e avisos"; no instalador eles viram a lista de onde os documentos ficam.

## Texto da tela do instalador

### Antes de instalar

- Software livre (GPL-3.0): usar, estudar, modificar e compartilhar.
- Funciona offline. A única conexão automática é a busca de atualização no GitHub, que dá para desligar.
- Sem telemetria. Reportes de problema só abrem o navegador, e você revisa o texto antes.

**Software livre, sem garantia.**

### Licença do programa (GPL-3.0)

O BSL Stage é software livre: você pode redistribuí-lo e modificá-lo sob os termos da Licença Pública Geral GNU, versão 3, publicada pela Free Software Foundation.

Ele é distribuído na esperança de ser útil, mas **sem nenhuma garantia**, nem mesmo a garantia implícita de comercialização ou de adequação a um propósito específico.

Você recebe o código-fonte e pode estudá-lo e alterá-lo. Se redistribuir o programa, modificado ou não, precisa manter a mesma licença e entregar o código-fonte correspondente.

### Onde estão os documentos

Depois de instalar, estes arquivos ficam na pasta do programa (por padrão `C:\Program Files\BSL Stage`):

- `LICENSE`: texto completo da GPL-3.0.
- `THIRD-PARTY-NOTICES.md`: avisos e licenças dos componentes de terceiros usados pelo programa.
- `licenses\`: o texto de cada licença de terceiros.
- `PRIVACIDADE.md`: o que o programa faz e não faz com os seus dados.
- `TERMOS-DE-USO.md`: termos de uso.

No programa, o menu Ajuda > Licenças e avisos mostra os mesmos textos.

### Seus dados

Desinstalar remove o programa, mas mantém os seus projetos e configurações (`%APPDATA%\BSL Stage`, `%LOCALAPPDATA%\BSL Stage` e `Documentos\BSL Stage`).
