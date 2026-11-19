# Testes noturnos

Script: `tools/nightly/run-nightly.ps1`. Tarefa agendada: **BSL Stage - testes noturnos**, todo dia às 19:00, só com o usuário logado.

## O que roda

- Só se o projeto mudou desde a última execução bem-sucedida (commit, `git status` e `git diff`).
- Espera enquanto você usa o PC (até 8 esperas de 15 min; sem atividade nos últimos 3 min).
- Build `cmake --build --preset release`, todos os testes de `build\Release` e `tools/uiqa/run.py --config window` (3 rodadas).
- Testes visuais (uiqa, outputstress e saída no segundo monitor) só depois das 19h e com a tela desbloqueada.
- Saída no segundo monitor (`tools/uiqa/saidatest.py`): a saída continua tocando com a janela Abrir aberta e ao arrastar janelas,
  fica por cima de outra janela e da barra de tarefas, e dois slices iguais sobrepostos têm o brilho de um. Com um monitor só, fica PULADO.
- Faz backup e restaura `settings.json` e `imgui.ini` do BSL Stage.

## Ver o último relatório

Abra `%LOCALAPPDATA%\BSL Stage\testes-noturnos\ultimo.txt`. A primeira linha é o resumo. Os 30 relatórios mais recentes ficam na mesma pasta com nome `aaaa-mm-dd_HHmm.txt`.

## Rodar na mão

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\nightly\run-nightly.ps1 -Force -UiqaRuns 3
```

`-Force` roda mesmo sem mudanças. `-UiqaRuns` define o número de rodadas do uiqa.

## Pedido para uma execução

Crie `%LOCALAPPDATA%\BSL Stage\testes-noturnos\pedido.txt` com uma linha, por exemplo `-UiqaRuns 10 -Force`. O script lê, aplica só nessa execução e apaga o arquivo.

## Mudar o horário

```powershell
Set-ScheduledTask -TaskName "BSL Stage - testes noturnos" -Trigger (New-ScheduledTaskTrigger -Daily -At "20:00")
```

## Desativar

```powershell
Disable-ScheduledTask -TaskName "BSL Stage - testes noturnos"
```

Para remover de vez: `Unregister-ScheduledTask -TaskName "BSL Stage - testes noturnos" -Confirm:$false`.
