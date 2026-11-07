# Skills para assistentes de IA

Esta pasta guarda skills (instruções reutilizáveis para assistentes de IA) do BSL Stage. Elas ficam **só no repositório**: o programa não instala nada no seu perfil.

| Skill | Para quê |
|---|---|
| `bsl-stage-plugin/` | Criar, corrigir e validar plugins (efeitos e geradores ISF, e modelo FFGL) a partir do pedido do cliente. |

## Como instalar uma skill

Uma skill é uma pasta com um `SKILL.md` (cabeçalho com `name` e `description`, depois as instruções) e arquivos de apoio.

1. Copie a pasta inteira (`bsl-stage-plugin`, com `SKILL.md` e `templates/`) para a pasta de skills do seu assistente (a documentação dele diz onde fica):
   - **para todos os projetos:** a pasta de skills do seu usuário;
   - **só para este repositório:** a pasta de skills do projeto, dentro da raiz do repositório.
2. Reinicie o assistente (ou abra uma conversa nova). Ao pedir "crie um efeito para o BSL Stage…", ele usa a skill sozinho pela `description`.
3. A skill aponta para `docs/plugins/` e `sdk/ffgl-template/`: para a IA achar esses arquivos, trabalhe com a pasta do repositório aberta (ou copie a pasta `docs/plugins` junto).
   O validador `bslstage_isfcheck.exe` está em `build\Release` do repositório compilado ou ao lado do `BSLStage.exe`.

Assistentes que não usam o formato de skill podem ler `AGENTS.md` (na raiz) e `docs/plugins/PARA-IA.md`, que dizem a mesma coisa.

Os modelos de `bsl-stage-plugin/templates/` são os mesmos que o assistente "Novo plugin…" do programa gera, de uso livre (CC0). Para regenerá-los depois de mudar os modelos do código:
`build\Release\bslstage_plugintest.exe --write-templates skills\bsl-stage-plugin\templates`.
