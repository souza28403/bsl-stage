param(
    [Parameter(Mandatory = $true)][string]$Destination,
    [string]$BuildDir,
    [string]$Root
)

$ErrorActionPreference = 'Stop'

if (-not $Root) { $Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path }
if (-not $BuildDir) { $BuildDir = Join-Path $Root 'build\Release' }
if (-not (Test-Path $BuildDir)) { throw "Pasta de build nao encontrada: $BuildDir. Rode: cmake --build --preset release" }

function Find-CrtFolder {
    $candidates = New-Object System.Collections.Generic.List[string]
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        foreach ($p in (& $vswhere -all -products '*' -property installationPath)) {
            if ($p) { $candidates.Add($p) }
        }
    }
    $found = @()
    foreach ($inst in $candidates) {
        $redist = Join-Path $inst 'VC\Redist\MSVC'
        if (-not (Test-Path $redist)) { continue }
        foreach ($ver in (Get-ChildItem $redist -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' })) {
            foreach ($crt in (Get-ChildItem (Join-Path $ver.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue)) {
                if (Test-Path (Join-Path $crt.FullName 'vcruntime140.dll')) {
                    $found += [pscustomobject]@{ Version = [version]$ver.Name; Path = $crt.FullName }
                }
            }
        }
    }
    if (-not $found) { return $null }
    return ($found | Sort-Object Version -Descending | Select-Object -First 1).Path
}

if (Test-Path $Destination) { Remove-Item $Destination -Recurse -Force }
New-Item -ItemType Directory -Force $Destination | Out-Null

foreach ($exe in 'BSLStage.exe', 'bslstage_worker.exe', 'bslstage_updater.exe', 'bslstage_isfcheck.exe') {
    $src = Join-Path $BuildDir $exe
    if (-not (Test-Path $src)) { throw "Faltou $exe em $BuildDir" }
    Copy-Item $src $Destination
}
Copy-Item (Join-Path $BuildDir '*.dll') $Destination
Copy-Item (Join-Path $BuildDir 'assets') (Join-Path $Destination 'assets') -Recurse

$cef = Join-Path $BuildDir 'cef'
if (Test-Path $cef) {
    Copy-Item $cef (Join-Path $Destination 'cef') -Recurse
} else {
    Write-Warning 'Sem a pasta cef: este pacote NAO traz a fonte Web (CEF). Veja docs/RELEASE.md.'
}

foreach ($f in 'LICENSE', 'README.md', 'THIRD-PARTY-NOTICES.md', 'PRIVACIDADE.md', 'TERMOS-DE-USO.md', 'AGENTS.md') {
    Copy-Item (Join-Path $Root $f) $Destination
}
Copy-Item (Join-Path $Root 'licenses') (Join-Path $Destination 'licenses') -Recurse
$cefCredits = Join-Path $BuildDir 'licenses\cef-CREDITS.html'
if (Test-Path $cefCredits) { Copy-Item $cefCredits (Join-Path $Destination 'licenses') -Force }
Copy-Item (Join-Path $Root 'docs\PATENTES-E-LICENCAS.md') $Destination
New-Item -ItemType Directory -Force (Join-Path $Destination 'docs') | Out-Null
Copy-Item (Join-Path $Root 'docs\plugins') (Join-Path $Destination 'docs\plugins') -Recurse
Copy-Item (Join-Path $Root 'skills') (Join-Path $Destination 'skills') -Recurse

$crt = Find-CrtFolder
if (-not $crt) { throw 'Runtime do Visual C++ (Microsoft.VC14x.CRT) nao encontrado. Instale o Visual Studio com o componente C++ (VC Redist).' }
foreach ($dll in 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll', 'concrt140.dll') {
    $src = Join-Path $crt $dll
    if (Test-Path $src) { Copy-Item $src $Destination -Force }
}
Write-Host "Runtime do VC++ (app-local) copiado de $crt"

foreach ($doc in 'LICENSE', 'THIRD-PARTY-NOTICES.md', 'PRIVACIDADE.md', 'TERMOS-DE-USO.md') {
    if (-not (Test-Path (Join-Path $Destination $doc))) { throw "pacote sem $doc" }
}
if (-not (Get-ChildItem (Join-Path $Destination 'licenses') -File)) { throw 'pacote sem os textos de licencas (licenses/)' }
if (-not (Test-Path (Join-Path $Destination 'BSLStage.exe'))) { throw 'pacote sem BSLStage.exe' }
if (-not (Test-Path (Join-Path $Destination 'vcruntime140.dll'))) { throw 'pacote sem o runtime do VC++' }

Write-Host "Pasta de distribuicao pronta: $Destination"
