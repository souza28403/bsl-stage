# Chaves de ativação do instalador

O instalador `.msi` do BSL Stage pede **e-mail + chave de ativação**. A conferência é offline: a chave é uma assinatura digital (EdDSA sobre Curve25519, biblioteca Monocypher) do e-mail do cliente. O instalador carrega só a chave **pública** (`src/license/ChavePublica.h`); a chave **privada** fica só com o dono, então ninguém consegue fabricar chaves válidas.

Formato da chave: `BSL1-` seguido de 103 caracteres em base32 (alfabeto Crockford, sem I, L, O e U), em grupos de 5 separados por `-`. Tamanho total: 128 caracteres. O e-mail é conferido sem diferenciar maiúsculas de minúsculas e sem espaços nas pontas. Na digitação da chave, maiúsculas/minúsculas, espaços e hífens são ignorados, e `O` vale `0`, `I` e `L` valem `1`.

## Gerar chaves

Tudo é feito com `build\Release\bslstage_keygen.exe`, na máquina do dono.

- Criar o par de chaves (uma única vez na vida do produto): `bslstage_keygen init`. Grava a privada em `Licencas BSL Stage\chave-privada.bin` (na raiz do projeto) e reescreve `src\license\ChavePublica.h`. Recusa-se a rodar se a privada já existir. Depois do `init`, recompile e gere o `.msi` de novo.
- Gerar a chave de um cliente: `bslstage_keygen gerar cliente@exemplo.com "Nome do Cliente"`. Imprime a chave e acrescenta uma linha (data, e-mail, nome, chave) em `Licencas BSL Stage\clientes.csv`.
- Conferir uma chave: `bslstage_keygen verificar cliente@exemplo.com BSL1-...`. Retorna código 0 se válida.

A pasta `Licencas BSL Stage\` está no `.gitignore` e nunca deve ser publicada.

## Backup da chave privada

Faça backup de `Licencas BSL Stage\chave-privada.bin` (e do `clientes.csv`) em pelo menos dois lugares fora do computador de trabalho, de preferência em mídia criptografada. **Sem esse arquivo não é possível gerar chaves compatíveis** com os instaladores já distribuídos: seria preciso criar um novo par, publicar um novo instalador e reemitir a chave de todos os clientes. Quem tiver o arquivo consegue gerar chaves válidas, então não o compartilhe.

## Instalação silenciosa

Passe o e-mail e a chave como propriedades do `msiexec`:

```
msiexec /i BSL-Stage-0.1.0-win64.msi /qn BSL_EMAIL=cliente@exemplo.com BSL_CHAVE=BSL1-XXXXX-XXXXX-...
```

Se o e-mail ou a chave forem inválidos, a instalação é abortada com a mensagem "E-mail ou chave de ativação inválidos". As duas propriedades são gravadas em `HKLM\Software\BSL Light\BSL Stage` (`LicencaEmail`, `LicencaChave`). Em atualizações e reinstalações o instalador lê esses valores de volta, então a tela de chave já vem preenchida e o modo silencioso (`/qn`) passa sem repetir `BSL_EMAIL` e `BSL_CHAVE`. O atualizador do próprio programa abre o `.msi` com `msiexec /i` e passa pelo mesmo caminho. Uma versão instalada antes da existência da chave de ativação não tem esses valores: a primeira atualização a partir dela pede a chave.
