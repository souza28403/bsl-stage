param(
    [string]$Version,
    [string]$DistDir,
    [string]$Repo,
    [string]$BuildDir,
    [ValidateSet('none','low','medium','high')][string]$Compression = 'high',
    [switch]$Sign,
    [string]$Pfx,
    [string]$SignTool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe',
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
$WixVersion = '6.0.2'

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $BuildDir) { $BuildDir = Join-Path $Root 'build\Release' }
if (-not $Pfx) { $Pfx = Join-Path $Root 'Certificado BSL Stage\BSL_LIGHT.pfx' }
$Out = Join-Path $Root 'dist'
$Obj = Join-Path $Out 'msi-obj'

function Read-ProjectInfo {
    $info = @{ Version = $null; Repo = $null }
    $header = Join-Path $Root 'build\generated\app\Version.h'
    if (Test-Path $header) {
        $text = Get-Content $header -Raw
        if ($text -match '#define BSLSTAGE_VERSION "([^"]+)"') { $info.Version = $Matches[1] }
        if ($text -match '#define BSLSTAGE_GITHUB_REPO "([^"]+)"') { $info.Repo = $Matches[1] }
    }
    $cmake = Get-Content (Join-Path $Root 'CMakeLists.txt') -Raw
    if (-not $info.Version -and $cmake -match 'project\(BSLStage VERSION (\d+\.\d+\.\d+)') { $info.Version = $Matches[1] }
    if (-not $info.Repo -and $cmake -match 'set\(BSLSTAGE_GITHUB_REPO "([^"]+)"') { $info.Repo = $Matches[1] }
    return $info
}

function Invoke-Native {
    param([string]$Exe, [string[]]$Arguments, [string]$What)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$What falhou (codigo $LASTEXITCODE)" }
}

function Get-SignPassword {
    if ($env:BSL_SIGN_PASSWORD) { return $env:BSL_SIGN_PASSWORD }
    $secure = Read-Host -Prompt 'Senha do certificado de assinatura' -AsSecureString
    $ptr = [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try { return [System.Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr) }
    finally { [System.Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr) }
}

function Find-SignTool {
    if (Test-Path $SignTool) { return $SignTool }
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $found = Get-ChildItem $kits -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $found) { throw "signtool.exe nao encontrado. Informe -SignTool <caminho>." }
    return $found.FullName
}

function Sign-Files {
    param([string[]]$Files, [string]$Password)
    $tool = Find-SignTool
    foreach ($f in $Files) {
        Write-Host "Assinando $(Split-Path $f -Leaf)"
        & $tool sign /f $Pfx /p $Password /fd SHA256 /tr $TimestampUrl /td SHA256 /d 'BSL Stage' $f | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "signtool falhou em $f (codigo $LASTEXITCODE)" }
    }
}

function New-CleanMark {
    param([string]$Source, [string]$Target)
    Add-Type -AssemblyName System.Drawing
    $src = New-Object System.Drawing.Bitmap $Source
    try {
        $bmp = New-Object System.Drawing.Bitmap $src.Width, $src.Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.Clear([System.Drawing.Color]::Transparent)
        $g.DrawImage($src, 0, 0, $src.Width, $src.Height)
        $g.Dispose()
        for ($x = 0; $x -lt 7; $x++) {
            for ($y = 0; $y -lt $bmp.Height; $y++) { $bmp.SetPixel($x, $y, [System.Drawing.Color]::Transparent) }
        }
        $bmp.Save($Target, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
    } finally { $src.Dispose() }
}

function New-BrandImages {
    param([string]$Mark, [string]$BannerPath, [string]$DialogPath)
    Add-Type -AssemblyName System.Drawing
    $img = [System.Drawing.Image]::FromFile($Mark)
    try {
        $red = [System.Drawing.Color]::FromArgb(237, 51, 56)
        $dark = [System.Drawing.Color]::FromArgb(20, 20, 24)

        $banner = New-Object System.Drawing.Bitmap 493, 58, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
        $g = [System.Drawing.Graphics]::FromImage($banner)
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.Clear([System.Drawing.Color]::White)
        $brush = New-Object System.Drawing.SolidBrush $red
        $g.FillRectangle($brush, 0, 0, 6, 58)
        $brush.Dispose()
        $side = 50
        $g.DrawImage($img, 493 - $side - 12, [int]((58 - $side) / 2), $side, $side)
        $g.Dispose()
        $banner.Save($BannerPath, [System.Drawing.Imaging.ImageFormat]::Bmp)
        $banner.Dispose()

        $dlg = New-Object System.Drawing.Bitmap 493, 312, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
        $g = [System.Drawing.Graphics]::FromImage($dlg)
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $g.Clear([System.Drawing.Color]::White)
        $panel = New-Object System.Drawing.SolidBrush $dark
        $g.FillRectangle($panel, 0, 0, 164, 312)
        $panel.Dispose()
        $accent = New-Object System.Drawing.SolidBrush $red
        $g.FillRectangle($accent, 164, 0, 3, 312)
        $accent.Dispose()
        $markSize = 120
        $g.DrawImage($img, [int]((164 - $markSize) / 2), 62, $markSize, $markSize)
        $center = New-Object System.Drawing.StringFormat
        $center.Alignment = [System.Drawing.StringAlignment]::Center
        $titleFont = New-Object System.Drawing.Font 'Segoe UI Semibold', 17, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
        $subFont = New-Object System.Drawing.Font 'Segoe UI', 12, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
        $white = [System.Drawing.Brushes]::White
        $gray = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(170, 170, 178))
        $g.DrawString('BSL STAGE', $titleFont, $white, (New-Object System.Drawing.RectangleF 0, 196, 164, 28), $center)
        $g.DrawString('BSL Light', $subFont, $gray, (New-Object System.Drawing.RectangleF 0, 224, 164, 20), $center)
        $gray.Dispose(); $titleFont.Dispose(); $subFont.Dispose(); $center.Dispose()
        $g.Dispose()
        $dlg.Save($DialogPath, [System.Drawing.Imaging.ImageFormat]::Bmp)
        $dlg.Dispose()
    } finally { $img.Dispose() }
}

function ConvertTo-RtfText {
    param([string]$Text)
    $sb = New-Object System.Text.StringBuilder
    foreach ($ch in $Text.ToCharArray()) {
        $code = [int]$ch
        if ($ch -eq '\') { [void]$sb.Append('\\') }
        elseif ($ch -eq '{') { [void]$sb.Append('\{') }
        elseif ($ch -eq '}') { [void]$sb.Append('\}') }
        elseif ($code -gt 126) {
            if ($code -gt 32767) { $code -= 65536 }
            [void]$sb.Append('\u' + $code + '?')
        }
        else { [void]$sb.Append($ch) }
    }
    return $sb.ToString()
}

function ConvertTo-RtfInline {
    param([string]$Text)
    $Text = [regex]::Replace($Text, '\[([^\]]+)\]\([^)]+\)', '$1')
    $parts = [regex]::Split($Text, '(\*\*.+?\*\*|`.+?`)')
    $sb = New-Object System.Text.StringBuilder
    foreach ($p in $parts) {
        if ($p.Length -ge 4 -and $p.StartsWith('**') -and $p.EndsWith('**')) {
            [void]$sb.Append('{\b ' + (ConvertTo-RtfText $p.Substring(2, $p.Length - 4)) + '}')
        } elseif ($p.Length -ge 2 -and $p.StartsWith('`') -and $p.EndsWith('`')) {
            [void]$sb.Append('{\f1\fs18 ' + (ConvertTo-RtfText $p.Substring(1, $p.Length - 2)) + '}')
        } else {
            [void]$sb.Append((ConvertTo-RtfText $p))
        }
    }
    return $sb.ToString()
}

function New-LicenseRtf {
    param([string]$Markdown, [string]$Target, [string]$VersionText)
    $raw = [System.IO.File]::ReadAllText($Markdown, (New-Object System.Text.UTF8Encoding $false))
    $lines = $raw -split "`r?`n"
    $start = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i].Trim() -eq '## Texto da tela do instalador') { $start = $i + 1; break }
    }
    if ($start -lt 0) { throw 'Secao "## Texto da tela do instalador" nao encontrada em AVISO-INSTALADOR.md' }

    $rtf = New-Object System.Text.StringBuilder
    [void]$rtf.Append('{\rtf1\ansi\ansicpg1252\deff0\deflang1046{\fonttbl{\f0\fswiss\fcharset0 Segoe UI;}{\f1\fmodern\fcharset0 Consolas;}}\viewkind4\uc1 ')
    [void]$rtf.Append("`r`n")
    [void]$rtf.Append('\pard\sa160\f0\b\fs28 ' + (ConvertTo-RtfText "BSL Stage $VersionText") + '\b0\fs19\par' + "`r`n")

    $para = New-Object System.Collections.Generic.List[string]
    $flush = {
        if ($para.Count -gt 0) {
            [void]$rtf.Append('\pard\sa140\sl264\slmult1\f0\fs19 ' + (ConvertTo-RtfInline ($para -join ' ')) + '\par' + "`r`n")
            $para.Clear()
        }
    }
    for ($i = $start; $i -lt $lines.Count; $i++) {
        $line = $lines[$i].TrimEnd()
        if ($line -eq '') { & $flush; continue }
        if ($line -match '^###\s+(.*)$') {
            & $flush
            [void]$rtf.Append('\pard\sb200\sa100\keepn\f0\b\fs23 ' + (ConvertTo-RtfInline $Matches[1]) + '\b0\fs19\par' + "`r`n")
        } elseif ($line -match '^-\s+(.*)$') {
            & $flush
            [void]$rtf.Append('\pard\tx420\fi-280\li420\sa80\sl264\slmult1\f0\fs19 \u8226?\tab ' + (ConvertTo-RtfInline $Matches[1]) + '\par' + "`r`n")
        } else {
            $para.Add($line.Trim())
        }
    }
    & $flush
    [void]$rtf.Append('}')
    [System.IO.File]::WriteAllText($Target, $rtf.ToString(), [System.Text.Encoding]::ASCII)
}

function Ensure-Wix {
    $toolsDir = Join-Path $env:USERPROFILE '.dotnet\tools'
    if ((Test-Path $toolsDir) -and ($env:PATH -notlike "*$toolsDir*")) { $env:PATH = "$env:PATH;$toolsDir" }
    $have = $null
    if (Get-Command wix -ErrorAction SilentlyContinue) { $have = (& wix --version) }
    if (-not $have -or -not $have.StartsWith($WixVersion)) {
        Write-Host "Instalando o WiX $WixVersion (dotnet tool, nuget.org)"
        if (Get-Command wix -ErrorAction SilentlyContinue) {
            Invoke-Native 'dotnet' @('tool', 'update', '--global', 'wix', '--version', $WixVersion) 'dotnet tool update wix'
        } else {
            Invoke-Native 'dotnet' @('tool', 'install', '--global', 'wix', '--version', $WixVersion) 'dotnet tool install wix'
        }
        if ((Test-Path $toolsDir) -and ($env:PATH -notlike "*$toolsDir*")) { $env:PATH = "$env:PATH;$toolsDir" }
    }
    $installed = (& wix extension list -g) -join "`n"
    foreach ($ext in 'WixToolset.UI.wixext') {
        if ($installed -notmatch [regex]::Escape("$ext $WixVersion")) {
            Invoke-Native 'wix' @('extension', 'add', '-g', "$ext/$WixVersion") "wix extension add $ext"
        }
    }
}

$info = Read-ProjectInfo
if (-not $Version) { $Version = $info.Version }
if (-not $Repo) { $Repo = $info.Repo }
if (-not $Version) { throw 'Nao consegui ler a versao (build\generated\app\Version.h ou CMakeLists.txt). Informe -Version.' }
if ($Version -notmatch '^(\d+)\.(\d+)\.(\d+)') { throw "Versao invalida: $Version" }
$MsiVersion = "$([int]$Matches[1]).$([int]$Matches[2]).$([int]$Matches[3])"
if ([int]$Matches[1] -gt 255 -or [int]$Matches[2] -gt 255 -or [int]$Matches[3] -gt 65535) { throw "Versao fora do limite do MSI: $MsiVersion" }
$RepoUrl = if ($Repo) { "https://github.com/$Repo" } else { 'https://github.com' }

$Name = "BSL-Stage-$Version-win64"
$MsiPath = Join-Path $Out "$Name.msi"
Write-Host "Versao do pacote: $Version (MSI $MsiVersion); repositorio: $Repo"

$password = $null
if ($Sign) { $password = Get-SignPassword }

New-Item -ItemType Directory -Force $Out | Out-Null
if (-not $DistDir) {
    $DistDir = Join-Path $Out $Name
    & (Join-Path $PSScriptRoot 'stage-dist.ps1') -Destination $DistDir -BuildDir $BuildDir -Root $Root
}
if (-not (Test-Path (Join-Path $DistDir 'BSLStage.exe'))) { throw "Pasta de distribuicao invalida: $DistDir" }
$DistDir = (Resolve-Path $DistDir).Path

if ($Sign) {
    Sign-Files -Files @((Join-Path $DistDir 'BSLStage.exe'), (Join-Path $DistDir 'bslstage_worker.exe'), (Join-Path $DistDir 'bslstage_updater.exe')) -Password $password
}

if (Test-Path $Obj) { Remove-Item $Obj -Recurse -Force }
New-Item -ItemType Directory -Force $Obj | Out-Null
$IconFile = Join-Path $Root 'assets\icons\BSLStage.ico'
$BannerBmp = Join-Path $Obj 'banner.bmp'
$DialogBmp = Join-Path $Obj 'dialog.bmp'
$LicenseRtf = Join-Path $Obj 'aviso.rtf'
$MarkFile = Join-Path $Obj 'mark.png'
New-CleanMark -Source (Join-Path $Root 'assets\brand\bsl-mark.png') -Target $MarkFile
New-BrandImages -Mark $MarkFile -BannerPath $BannerBmp -DialogPath $DialogBmp
New-LicenseRtf -Markdown (Join-Path $Root 'docs\instalador\AVISO-INSTALADOR.md') -Target $LicenseRtf -VersionText $Version

Ensure-Wix

if (Test-Path $MsiPath) { Remove-Item $MsiPath -Force }
$wixArgs = @(
    'build',
    '-arch', 'x64',
    '-culture', 'pt-BR',
    '-ext', 'WixToolset.UI.wixext',
    '-loc', (Join-Path $PSScriptRoot 'Strings.pt-BR.wxl'),
    '-d', "MsiVersion=$MsiVersion",
    '-d', "DistDir=$DistDir",
    '-d', "IconFile=$IconFile",
    '-d', "BannerBmp=$BannerBmp",
    '-d', "DialogBmp=$DialogBmp",
    '-d', "LicenseRtf=$LicenseRtf",
    '-d', "RepoUrl=$RepoUrl",
    '-d', "Compression=$Compression",
    '-pdbtype', 'none',
    '-o', $MsiPath,
    (Join-Path $PSScriptRoot 'Package.wxs')
)
Invoke-Native 'wix' $wixArgs 'wix build'

if ($Sign) { Sign-Files -Files @($MsiPath) -Password $password }

$size = [Math]::Round((Get-Item $MsiPath).Length / 1MB, 1)
$hash = (Get-FileHash $MsiPath -Algorithm SHA256).Hash.ToLower()
Write-Host "MSI pronto: $MsiPath ($size MB)"
Write-Host "SHA-256: $hash"
