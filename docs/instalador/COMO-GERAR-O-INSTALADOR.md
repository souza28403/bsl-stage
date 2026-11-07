# Como gerar o instalador (.msi)

O instalador instala o BSL Stage por máquina em `C:\Program Files\BSL Stage` (x64), com a interface em português, atalho no Menu Iniciar, atalho opcional na Área de Trabalho e associação dos arquivos `.bslstage`. Desinstalar apaga só o programa; projetos e configurações ficam.

## 1. Gerar

1. Compile o programa: `cmake --build --preset release` (precisa terminar sem erros).
2. Rode, na raiz do repositório:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\package\build-msi.ps1
   ```

3. O arquivo sai em `dist\BSL-Stage-<versão>-win64.msi` (a versão vem do `CMakeLists.txt`). O primeiro uso instala o WiX 6.0.2 sozinho (`dotnet tool`, sem aceite de termos). A geração leva alguns minutos porque o pacote tem ~300 MB.

O script monta a pasta de distribuição (`tools\package\stage-dist.ps1`, a mesma do `.zip`), copia junto o runtime do Visual C++ (o instalador funciona em PC limpo) e gera o aviso em RTF a partir de `docs\instalador\AVISO-INSTALADOR.md`. Para mudar o texto da tela de licença, edite a seção "Texto da tela do instalador" desse arquivo.

## 2. Assinar

Rode com `-Sign`:

```powershell
powershell -ExecutionPolicy Bypass -File tools\package\build-msi.ps1 -Sign
```

O script pede a senha do certificado (`Certificado BSL Stage\BSL_LIGHT.pfx`) sem mostrá-la na tela. Para não digitar, defina a variável `BSL_SIGN_PASSWORD` antes. Ele assina `BSLStage.exe`, `bslstage_worker.exe` e `bslstage_updater.exe` e depois o `.msi`, com SHA-256 e carimbo de tempo da DigiCert. Nunca commite o certificado nem a senha (a pasta já está no `.gitignore`).

## 3. Publicar

1. Siga `docs\RELEASE.md` (versão no `CMakeLists.txt`, tag `vX.Y.Z`). O workflow compila e já anexa ao rascunho do release o `.zip`, o `.msi` (sem assinatura) e o `SHA256SUMS.txt`.
2. Para publicar o `.msi` **assinado**: gere-o localmente com `-Sign`, abra o rascunho em Releases e troque o `.msi` anexado pelo assinado. Depois atualize a linha do `.msi` no `SHA256SUMS.txt` (`Get-FileHash dist\BSL-Stage-<versão>-win64.msi`) e anexe o arquivo corrigido. Sem essa linha correta o programa recusa a atualização.
3. Clique em Publish.

## 4. Atualização automática

O instalador grava `HKLM\Software\BSL Light\BSL Stage\InstalledBy = msi`. Com essa marca, o programa baixa o `.msi` da release (confere o SHA-256 do `SHA256SUMS.txt`) e, ao fechar, abre o `msiexec /i` com a interface do instalador (o Windows pede a permissão de administrador). Sem a marca (versão em `.zip`), o fluxo antigo continua igual. O arquivo `.msi` da release precisa se chamar `BSL-Stage-<versão>-win64.msi`.

## 5. O que testar depois de gerar

- Instalar em um PC limpo (sem Visual C++ instalado): o programa abre?
- Atalhos (Menu Iniciar e Área de Trabalho) e duplo clique em um `.bslstage`.
- Instalar uma versão maior por cima (substitui) e tentar instalar uma menor (deve bloquear com mensagem em português).
- Desinstalar e conferir que `%APPDATA%\BSL Stage`, `%LOCALAPPDATA%\BSL Stage` e `Documentos\BSL Stage` continuam lá.
- Verificação de ICE (opcional): `wix msi validate -sice ICE43 -sice ICE57 dist\BSL-Stage-<versão>-win64.msi`. ICE43 e ICE57 são falsos positivos em instalações por máquina com atalhos; ICE61 e ICE60 aparecem como avisos e são esperados.
