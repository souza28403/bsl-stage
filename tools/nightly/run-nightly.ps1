param(
    [switch]$Force,
    [switch]$Agora,
    [int]$UiqaRuns = 3
)

$ErrorActionPreference = 'Continue'
$raiz = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$pastaRel = Join-Path $env:LOCALAPPDATA 'BSL Stage\testes-noturnos'
$pastaApp = Join-Path $env:APPDATA 'BSL Stage'
$pastaBackup = Join-Path $pastaRel 'backup-config'
$arqEstado = Join-Path $pastaRel 'estado.txt'
$arqUltimo = Join-Path $pastaRel 'ultimo.txt'
$arqPedido = Join-Path $pastaRel 'pedido.txt'
$inicio = Get-Date
$arqRelatorio = Join-Path $pastaRel ($inicio.ToString('yyyy-MM-dd_HHmm') + '.txt')
$arquivosConfig = @('settings.json', 'imgui.ini')
New-Item -ItemType Directory -Force -Path $pastaBackup | Out-Null

$avisosPedido = New-Object System.Collections.ArrayList
if (Test-Path -LiteralPath $arqPedido) {
    $tokens = @((Get-Content -LiteralPath $arqPedido -Raw).Trim() -split '\s+' | Where-Object { $_ })
    $k = 0
    while ($k -lt $tokens.Count) {
        $t = $tokens[$k]
        if ($t -eq '-Force') {
            $Force = $true
        }
        elseif ($t -eq '-UiqaRuns' -and ($k + 1) -lt $tokens.Count -and $tokens[$k + 1] -match '^\d+$') {
            $UiqaRuns = [int]$tokens[$k + 1]
            $k++
        }
        else {
            [void]$avisosPedido.Add('pedido ignorado: ' + $t)
        }
        $k++
    }
    Remove-Item -LiteralPath $arqPedido -Force
}

$passos = New-Object System.Collections.ArrayList
$donos = New-Object System.Collections.ArrayList
$cabecalho = ''
$resumoUiqa = ''
$commit = ''

$codigoOcioso = @"
using System;
using System.Runtime.InteropServices;
public static class BslStageOcioso
{
    [StructLayout(LayoutKind.Sequential)]
    public struct LASTINPUTINFO
    {
        public uint cbSize;
        public uint dwTime;
    }

    [DllImport("user32.dll")]
    public static extern bool GetLastInputInfo(ref LASTINPUTINFO plii);

    [DllImport("user32.dll")]
    public static extern IntPtr OpenInputDesktop(uint dwFlags, bool fInherit, uint dwDesiredAccess);

    [DllImport("user32.dll")]
    public static extern bool CloseDesktop(IntPtr hDesktop);

    [DllImport("kernel32.dll")]
    public static extern uint SetThreadExecutionState(uint esFlags);

    public static void ManterAcordado(bool ligar)
    {
        SetThreadExecutionState(ligar ? 0x80000003u : 0x80000000u);
    }

    public static uint OciosoMs()
    {
        LASTINPUTINFO li = new LASTINPUTINFO();
        li.cbSize = (uint)Marshal.SizeOf(typeof(LASTINPUTINFO));
        GetLastInputInfo(ref li);
        return unchecked((uint)Environment.TickCount - li.dwTime);
    }

    public static bool Bloqueada()
    {
        IntPtr h = OpenInputDesktop(0, false, 0x0100);
        if (h == IntPtr.Zero) return true;
        CloseDesktop(h);
        return false;
    }
}
"@
if (-not ('BslStageOcioso' -as [type])) { Add-Type -TypeDefinition $codigoOcioso }

function Usuario-Ativo {
    [BslStageOcioso]::OciosoMs() -lt 180000
}

function Tela-Bloqueada {
    if (@(Get-Process -Name LogonUI -ErrorAction SilentlyContinue).Count -gt 0) { return $true }
    [BslStageOcioso]::Bloqueada()
}

function Motivo-Visual {
    if (-not $Agora -and (Get-Date).Hour -lt 19) { return 'testes de tela só a partir das 19h' }
    if (Tela-Bloqueada) { return 'tela bloqueada: testes visuais pulados' }
    $null
}

function Passo-Visual($rotulo, $motivo) {
    $estado = 'ADIADO'
    if ($motivo -like 'tela bloqueada*') { $estado = 'PULADO' }
    Adicionar-Passo $rotulo $estado $null $null @() $motivo
}

function Adicionar-Passo($nome, $estado, $segundos, $codigo, $linhas, $aviso) {
    [void]$passos.Add([pscustomobject]@{ Nome = $nome; Estado = $estado; Segundos = $segundos; Codigo = $codigo; Linhas = @($linhas); Aviso = $aviso })
}

function Hash-Texto($texto) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(('' + $texto)))).Replace('-', '')
}

function Obter-Estado {
    $head = (& git -C $raiz rev-parse HEAD 2>$null | Out-String)
    $status = (& git -C $raiz status --porcelain 2>$null | Out-String)
    $diff = (& git -C $raiz diff 2>$null | Out-String)
    'HEAD=' + (Hash-Texto $head) + '|STATUS=' + (Hash-Texto $status) + '|DIFF=' + (Hash-Texto $diff)
}

function Adicionar-Dono($processo) {
    $c = Get-CimInstance Win32_Process -Filter ('ProcessId=' + $processo.Id) -ErrorAction SilentlyContinue
    if ($c) { [void]$donos.Add([pscustomobject]@{ Pid = [int]$c.ProcessId; Nome = $c.Name; Criado = $c.CreationDate }) }
}

function Registrar-Descendentes {
    $todos = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)
    $novo = $true
    while ($novo) {
        $novo = $false
        $ids = @($donos | ForEach-Object { [int]$_.Pid })
        foreach ($p in $todos) {
            if (($ids -contains [int]$p.ParentProcessId) -and -not ($ids -contains [int]$p.ProcessId)) {
                [void]$donos.Add([pscustomobject]@{ Pid = [int]$p.ProcessId; Nome = $p.Name; Criado = $p.CreationDate })
                $ids += [int]$p.ProcessId
                $novo = $true
            }
        }
    }
}

function Executar($arquivo, $argumentos, $limiteSeg = 5400) {
    $saidaTmp = [IO.Path]::GetTempFileName()
    $erroTmp = [IO.Path]::GetTempFileName()
    $ini = Get-Date
    $parametros = @{
        FilePath = $arquivo
        WorkingDirectory = $raiz
        NoNewWindow = $true
        PassThru = $true
        RedirectStandardOutput = $saidaTmp
        RedirectStandardError = $erroTmp
    }
    if ($argumentos) { $parametros['ArgumentList'] = $argumentos }
    $processo = Start-Process @parametros
    $null = $processo.Handle
    Adicionar-Dono $processo
    $estourou = $false
    while (-not $processo.HasExited) {
        Start-Sleep -Milliseconds 1500
        Registrar-Descendentes
        if (((Get-Date) - $ini).TotalSeconds -gt $limiteSeg) {
            $estourou = $true
            & taskkill.exe /T /F /PID $processo.Id 2>$null | Out-Null
            break
        }
    }
    $processo.WaitForExit(30000) | Out-Null
    Registrar-Descendentes
    $linhas = @(Get-Content -LiteralPath $saidaTmp, $erroTmp -ErrorAction SilentlyContinue)
    if ($estourou) { $linhas += ('tempo esgotado: passou de ' + $limiteSeg + ' s e foi encerrado') }
    Remove-Item -LiteralPath $saidaTmp, $erroTmp -Force -ErrorAction SilentlyContinue
    [pscustomobject]@{
        Codigo = $processo.ExitCode
        Segundos = [math]::Round(((Get-Date) - $ini).TotalSeconds, 1)
        Linhas = $linhas
    }
}

function Encerrar-Donos {
    $encerrados = 0
    foreach ($d in $donos) {
        $atual = Get-CimInstance Win32_Process -Filter ('ProcessId=' + $d.Pid) -ErrorAction SilentlyContinue
        if ($atual -and $atual.CreationDate -eq $d.Criado -and $atual.Name -match '^(BSLStage|bslstage_.+|python[w]?|py)\.exe$') {
            Stop-Process -Id $d.Pid -Force -ErrorAction SilentlyContinue
            $encerrados++
        }
    }
    $donos.Clear()
    $encerrados
}

function Executar-Passos {
    $build = Executar 'cmake' '--build --preset release'
    $avisos = @($build.Linhas | Where-Object { $_ -match '\bwarning [A-Z]{1,4}\d+' }).Count
    $erros = @($build.Linhas | Where-Object { $_ -match '\berror [A-Z]{1,4}\d+' }).Count
    $construiu = ($build.Codigo -eq 0 -and $erros -eq 0)
    $estadoBuild = 'FALHA'
    if ($construiu) { $estadoBuild = 'OK' }
    Adicionar-Passo 'build (cmake --build --preset release)' $estadoBuild $build.Segundos $build.Codigo $build.Linhas ('avisos: ' + $avisos + ', erros: ' + $erros)

    $testes = @(
        @{ Nome = 'mediatest'; Argumentos = 'testmedia'; Ui = $false },
        @{ Nome = 'spouttest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'projecttest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'livetest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'doctest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'ffgltest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'bsltest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'maptest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'uitest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'pixelmaptest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'aitest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'keystest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'fxtest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'transformtest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'dmxtest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'updatetest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'licencatest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'plugintest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'streamtest'; Argumentos = ''; Ui = $false },
        @{ Nome = 'outputstress'; Argumentos = '--seconds 8 --toggles 4'; Ui = $true }
    )
    foreach ($t in $testes) {
        $rotulo = 'teste ' + $t.Nome
        $exe = Join-Path $raiz ('build\Release\bslstage_' + $t.Nome + '.exe')
        if (-not $construiu) {
            Adicionar-Passo $rotulo 'PULADO' $null $null @() 'build falhou'
            continue
        }
        if ($t.Ui) {
            $motivo = Motivo-Visual
            if ($motivo) {
                Passo-Visual $rotulo $motivo
                continue
            }
        }
        if (-not (Test-Path -LiteralPath $exe)) {
            Adicionar-Passo $rotulo 'PULADO' $null $null @() 'exe não encontrado'
            continue
        }
        $r = Executar $exe $t.Argumentos
        $estado = 'FALHA'
        if ($r.Codigo -eq 0) { $estado = 'OK' }
        Adicionar-Passo $rotulo $estado $r.Segundos $r.Codigo $r.Linhas ''
    }

    if ($construiu) {
        $motivoSaida = Motivo-Visual
        if ($motivoSaida) {
            Passo-Visual 'saída no segundo monitor' $motivoSaida
        } else {
            $r = Executar 'python' 'tools\uiqa\saidatest.py'
            $estado = 'FALHA'
            if ($r.Codigo -eq 0) { $estado = 'OK' } elseif ($r.Codigo -eq 2) { $estado = 'PULADO' }
            $falhas = @($r.Linhas | Where-Object { $_ -match '^FALHA' })
            Adicionar-Passo 'saída no segundo monitor' $estado $r.Segundos $r.Codigo $r.Linhas ($falhas -join '; ')
        }
    }

    if (-not $construiu) {
        Adicionar-Passo 'uiqa' 'PULADO' $null $null @() 'build falhou'
        return
    }
    $motivoInicial = Motivo-Visual
    if ($motivoInicial) {
        Passo-Visual 'uiqa' $motivoInicial
        return
    }
    $ok = 0
    for ($i = 1; $i -le $UiqaRuns; $i++) {
        $motivo = Motivo-Visual
        if ($motivo) {
            Passo-Visual ('uiqa rodada ' + $i + ' de ' + $UiqaRuns) $motivo
            break
        }
        $r = Executar 'python' 'tools\uiqa\run.py --config window'
        $falhas = @($r.Linhas | ForEach-Object { if ($_ -match '^\s*FALHA\s+(\S+)') { $Matches[1] } })
        $estado = 'FALHA'
        if ($r.Codigo -eq 0) { $estado = 'OK'; $ok++ }
        $aviso = 'passos com falha: ' + $(if ($falhas.Count) { $falhas -join ', ' } else { 'nenhum' })
        Adicionar-Passo ('uiqa rodada ' + $i + ' de ' + $UiqaRuns) $estado $r.Segundos $r.Codigo $r.Linhas $aviso
    }
    $script:resumoUiqa = 'uiqa: ' + $ok + ' de ' + $UiqaRuns + ' rodadas passaram'
}

try {
    $commit = (& git -C $raiz rev-parse HEAD 2>$null | Out-String).Trim()
    $estadoAtual = Obter-Estado
    $anterior = ''
    if (Test-Path -LiteralPath $arqEstado) { $anterior = (Get-Content -LiteralPath $arqEstado -Raw).Trim() }
    if (-not $Force -and $estadoAtual -eq $anterior) {
        $cabecalho = 'sem mudanças'
    }
    else {
        $esperas = 0
        while (-not $Agora -and (Usuario-Ativo) -and $esperas -lt 8) {
            $esperas++
            Start-Sleep -Seconds 900
        }
        if (-not $Agora -and (Usuario-Ativo)) {
            $cabecalho = 'ADIADO: usuário ativo no PC (desistiu após 8 esperas de 15 min)'
        }
        else {
            $existia = @{}
            foreach ($arq in $arquivosConfig) {
                $origem = Join-Path $pastaApp $arq
                $existia[$arq] = Test-Path -LiteralPath $origem
                if ($existia[$arq]) { Copy-Item -LiteralPath $origem -Destination (Join-Path $pastaBackup $arq) -Force }
            }
            try {
                [BslStageOcioso]::ManterAcordado($true)
                Executar-Passos
            }
            finally {
                [BslStageOcioso]::ManterAcordado($false)
                Encerrar-Donos | Out-Null
                foreach ($arq in $arquivosConfig) {
                    $destino = Join-Path $pastaApp $arq
                    if ($existia[$arq]) { Copy-Item -LiteralPath (Join-Path $pastaBackup $arq) -Destination $destino -Force }
                    elseif (Test-Path -LiteralPath $destino) { Remove-Item -LiteralPath $destino -Force }
                }
            }
            $falhas = @($passos | Where-Object { $_.Estado -eq 'FALHA' }).Count
            $pendentes = @($passos | Where-Object { $_.Estado -eq 'PULADO' -or $_.Estado -eq 'ADIADO' }).Count
            if ($falhas -gt 0) { $cabecalho = 'FALHAS: ' + $falhas }
            elseif ($pendentes -gt 0) { $cabecalho = 'PARCIAL: ' + $pendentes + ' passo(s) pulado(s) ou adiado(s)' }
            else { $cabecalho = 'TUDO OK' }
            if ($cabecalho -eq 'TUDO OK') { Obter-Estado | Set-Content -LiteralPath $arqEstado -Encoding UTF8 }
        }
    }
}
catch {
    Adicionar-Passo 'script' 'FALHA' $null $null @($_.Exception.Message) ''
    $cabecalho = 'FALHAS: ' + @($passos | Where-Object { $_.Estado -eq 'FALHA' }).Count
}

$linhas = New-Object System.Collections.ArrayList
[void]$linhas.Add($cabecalho)
[void]$linhas.Add('Início: ' + $inicio.ToString('yyyy-MM-dd HH:mm:ss') + ' | commit: ' + $commit)
[void]$linhas.Add('Parâmetros: UiqaRuns=' + $UiqaRuns + ' Force=' + [bool]$Force)
foreach ($a in $avisosPedido) { [void]$linhas.Add($a) }
foreach ($s in $passos) { [void]$linhas.Add('- ' + $s.Nome + ': ' + $s.Estado) }
if ($resumoUiqa) { [void]$linhas.Add($resumoUiqa) }
foreach ($s in $passos) {
    [void]$linhas.Add('')
    $cab = '[' + $s.Estado + '] ' + $s.Nome
    if ($null -ne $s.Segundos) { $cab += ' | tempo ' + $s.Segundos + ' s | código ' + $s.Codigo }
    if ($s.Aviso) { $cab += ' | ' + $s.Aviso }
    [void]$linhas.Add($cab)
    if ($s.Estado -eq 'FALHA') {
        $falhas = @($s.Linhas | Where-Object { $_ -cmatch '\bFALH(A|OU)\b|\bFAIL\b|QaFailure' } | Select-Object -First 30)
        if ($falhas.Count -gt 0) {
            [void]$linhas.Add('  linhas com falha:')
            foreach ($l in $falhas) { [void]$linhas.Add('    ' + $l) }
        }
    }
    $ultimas = @($s.Linhas | Select-Object -Last 15)
    if ($ultimas.Count -gt 0) {
        [void]$linhas.Add('  últimas 15 linhas:')
        foreach ($l in $ultimas) { [void]$linhas.Add('    ' + $l) }
    }
}
Set-Content -LiteralPath $arqRelatorio -Value $linhas -Encoding UTF8
Copy-Item -LiteralPath $arqRelatorio -Destination $arqUltimo -Force
Get-ChildItem -LiteralPath $pastaRel -Filter '*.txt' -File |
    Where-Object { $_.Name -match '^\d{4}-\d{2}-\d{2}_\d{4}\.txt$' } |
    Sort-Object Name -Descending |
    Select-Object -Skip 30 |
    Remove-Item -Force

$textoBalao = $cabecalho
if ($resumoUiqa) { $textoBalao += ' | ' + $resumoUiqa }
try {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $icone = New-Object System.Windows.Forms.NotifyIcon
    $icone.Icon = [System.Drawing.SystemIcons]::Information
    $icone.Visible = $true
    $icone.ShowBalloonTip(6000, 'BSL Stage - testes noturnos', $textoBalao, [System.Windows.Forms.ToolTipIcon]::Info)
    Start-Sleep -Seconds 7
    $icone.Dispose()
}
catch { }
