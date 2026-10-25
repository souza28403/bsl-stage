# Engenharia reversa — regras do projeto

Usamos o Resolume (e ferramentas como Ghidra/REA) **só para entender comportamento**, nunca
como fonte de código.

## Pode
- Observar o programa rodando: o que acontece ao clicar, a ordem de eventos, o timing.
- Ler arquivos que o próprio usuário cria (XML de Advanced Output, presets) para garantir
  **compatibilidade de importação**.
- Observar como o Resolume chama plugins FFGL (ordem de chamadas, parâmetros) — o protocolo
  FFGL é aberto, isso é interoperabilidade.
- Anotar descobertas em linguagem própria em `docs/notas/` (comportamento, formato, protocolo).

## Não pode
- Copiar, traduzir ou "reescrever linha a linha" código decompilado.
- Colar pseudocódigo do Ghidra/REA no repositório.
- Usar nome, logo, ícones, sons ou layout idêntico ao do Resolume.
- Distribuir qualquer arquivo do Resolume (DLLs, shaders, presets, mídias de exemplo).

## Fluxo (clean room)
1. Análise → nota em `docs/notas/<tema>.md` descrevendo **o quê** acontece, sem código.
2. Implementação → feita a partir da nota e da documentação pública, sem abrir o decompilado.
