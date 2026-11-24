# Builds the Switch .nro and prepares a ready-to-copy SD card folder from your PS4 .pkg file(s)
# or from an already-prepared PC build tree.
#
# Examples:
#   powershell -ExecutionPolicy Bypass -File tools\prepare_nx.ps1 -Pkg D:\Games\DragonsCrownPro.pkg
#   powershell -ExecutionPolicy Bypass -File tools\prepare_nx.ps1 -Pkg base.pkg,update.pkg -Destination E:\
param(
    [Alias("Out")][string]$Destination = "",
    [string[]]$Pkg = @(),
    [ValidateSet("", "CUSA10454", "CUSA07001", "CUSA10209", "CUSA10487")][string]$Title = "",
    [ValidateSet("", "1.00", "1.01", "1.02", "1.03")][string]$Version = "",
    [string]$Image = "",
    [string]$Prepared = "",
    [switch]$Rebuild,
    [switch]$SkipBuild,
    [switch]$SkipNro,
    [switch]$StockMesa,
    [switch]$SplitCpk
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_common.ps1")
$RepoRoot = $script:RepoRoot
$DownloadRoot = Split-Path -Parent $RepoRoot

function Resolve-PreparedFolder([string]$StdoutText) {
    foreach ($line in ($StdoutText -split "`r?`n")) {
        if ($line -match '^\s*ready\s*:\s*(.+?)\s+\([A-Z0-9]+\s+') { return $Matches[1].Trim() }
    }
    return $null
}

# 1. Build .nro if needed.
$Nro = Join-Path $RepoRoot "build-nx\dragons_crown_pro.nro"
if (-not $SkipNro -and -not $SkipBuild -and ($Rebuild -or -not (Test-Path -LiteralPath $Nro))) {
    Write-Step "Building Switch NRO (build-nx\dragons_crown_pro.nro)"
    $buildArgs = @{}
    if ($StockMesa) { $buildArgs["StockMesa"] = $true }
    & (Join-Path $PSScriptRoot "build_nx.ps1") @buildArgs
    if ($LASTEXITCODE -ne 0) { Stop-Tool "build_nx.ps1 failed ($LASTEXITCODE)" }
}

# 2. Unpack .pkg(s) and build boot.bin + content profile when -Pkg is provided.
if ($Pkg -and $Pkg.Count -gt 0) {
    $pkgItems = @($Pkg | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    foreach ($item in $pkgItems) {
        if (-not (Test-Path -LiteralPath $item)) { Stop-Tool "PKG path not found: $item" }
    }
    if (-not (Test-PythonModule "cryptography")) {
        Write-Step "Setting up Python cryptography for PKG extraction"
        & (Join-Path $PSScriptRoot "setup.ps1") -InstallPython
        if ($LASTEXITCODE -ne 0) { Stop-Tool "Python setup failed" }
    }
    $gameRoot = Join-Path $RepoRoot "game"
    $pyArgs = @("--game-root", $gameRoot, "--scope", "full", "--pkg") + $pkgItems
    if ($Title) { $pyArgs += @("--title", $Title) }
    if ($Version) { $pyArgs += @("--version", $Version) }

    Write-Step "Extracting PKG package(s)"
    $python = Get-Python
    if (-not $python) { Stop-Tool "Python 3.10+ not found." }
    $prepScript = (Join-Path $RepoRoot "scripts\prepare_game.py") -replace '\\', '/'
    $prepLines = @()
    $prevErr = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $python.Exe @($python.Args) "-I" $prepScript @pyArgs 2>&1 | Tee-Object -Variable prepLines | Out-Host
    } finally {
        $ErrorActionPreference = $prevErr
    }
    if ($LASTEXITCODE -ne 0) { Stop-Tool "prepare_game.py failed ($LASTEXITCODE)" }
    $Prepared = Resolve-PreparedFolder ($prepLines -join "`n")
    if (-not $Prepared -or -not (Test-Path -LiteralPath $Prepared)) {
        $latestView = Get-ChildItem (Join-Path $gameRoot "prepared") -Directory -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($latestView) { $Prepared = $latestView.FullName }
    }
    if (-not $Prepared -or -not (Test-Path -LiteralPath (Join-Path $Prepared "eboot.bin"))) {
        Stop-Tool "Prepared game folder not found after extraction."
    }

    $folderName = Split-Path -Leaf $Prepared
    $Image = Join-Path $RepoRoot "build\image\$folderName"
    Write-Step "Building boot image (boot.bin)"
    $rc = Invoke-Script "prepare_image.py" @($Prepared, "--out", $Image)
    if ($rc -ne 0) { Stop-Tool "prepare_image.py failed ($rc)" }
} else {
    $PcGame = Join-Path $DownloadRoot "DragonsCrownProPC-v1.4.0-windows-x64\DragonsCrownProPC-v1.4.0-windows-x64\game"
    if (-not $Image) {
        $localImg = Get-ChildItem (Join-Path $RepoRoot "build\image") -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
        $Image = if ($localImg) { $localImg.FullName } else { Join-Path $PcGame "image\v1.03" }
    }
    if (-not $Prepared) {
        $localPrep = Get-ChildItem (Join-Path $RepoRoot "game\prepared") -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
        $Prepared = if ($localPrep) { $localPrep.FullName } else { Join-Path $PcGame "prepared\v1.03" }
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $Image "boot.bin"))) {
    Stop-Tool "boot.bin not found in $Image. Pass -Pkg <path-to-.pkg> or generate the boot image first."
}
if (-not (Test-Path -LiteralPath (Join-Path $Prepared "eboot.bin"))) {
    Stop-Tool "Prepared game files not found in $Prepared. Pass -Pkg <path-to-.pkg>."
}

# 3. Resolve Switch SD layout: either .../switch/dragons_crown_pro or SD root (defaults to .\dist-nx).
if (-not $Destination) { $Destination = Join-Path $RepoRoot "dist-nx" }
$destNorm = $Destination.TrimEnd('\', '/')
if ((Split-Path -Leaf $destNorm) -ieq "dragons_crown_pro") {
    $AppDir = $destNorm
    $SwitchRoot = Split-Path -Parent $destNorm
} elseif ((Split-Path -Leaf $destNorm) -ieq "switch") {
    $SwitchRoot = $destNorm
    $AppDir = Join-Path $SwitchRoot "dragons_crown_pro"
} else {
    $SwitchRoot = Join-Path $destNorm "switch"
    $AppDir = Join-Path $SwitchRoot "dragons_crown_pro"
}

$GameDir = Join-Path $AppDir "game"
foreach ($dir in @($SwitchRoot, $AppDir, $GameDir, (Join-Path $AppDir "user"), (Join-Path $AppDir "logs"), (Join-Path $AppDir "cache"))) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

$destFs = $null
try {
    $rootDrive = ([IO.Path]::GetPathRoot((Resolve-Path $AppDir).Path)).TrimEnd('\', ':')
    if ($rootDrive.Length -eq 1) { $destFs = (Get-Volume -DriveLetter $rootDrive -ErrorAction SilentlyContinue).FileSystem }
} catch { }
$largeFiles = @(Get-ChildItem -LiteralPath $Prepared -File | Where-Object { $_.Length -ge 4GB })
if ($largeFiles.Count -gt 0 -and $destFs -eq "FAT32" -and -not $SplitCpk) {
    Write-Step "FAT32 target detected with >4 GiB file; enabling -SplitCpk automatically"
    $SplitCpk = $true
}

Write-Step "Laying out Switch folder ($AppDir)"
Copy-Item (Join-Path $Image "*") $GameDir -Recurse -Force
if ($SplitCpk -and $largeFiles.Count -gt 0) {
    $largeNames = @($largeFiles | ForEach-Object Name)
    Get-ChildItem -LiteralPath $Prepared | Where-Object { $largeNames -notcontains $_.Name } |
        ForEach-Object { Copy-Item $_.FullName $GameDir -Recurse -Force }
    foreach ($lf in $largeFiles) {
        $stageDir = Join-Path $RepoRoot "build\split-stage"
        if (Test-Path -LiteralPath $stageDir) { Remove-Item -LiteralPath $stageDir -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
        $staged = Join-Path $stageDir $lf.Name
        Copy-Item $lf.FullName $staged -Force
        & (Join-Path $PSScriptRoot "split_cpk.ps1") -Path $staged
        Move-Item (Join-Path $stageDir "$($lf.Name).*") $GameDir -Force
        Remove-Item -LiteralPath $stageDir -Recurse -Force
    }
} else {
    Copy-Item (Join-Path $Prepared "*") $GameDir -Recurse -Force
    if ($SplitCpk) {
        & (Join-Path $PSScriptRoot "split_cpk.ps1") -Path $GameDir
    }
}

if (-not $SkipNro) {
    if (Test-Path -LiteralPath $Nro) {
        Copy-Item $Nro (Join-Path $SwitchRoot "dragons_crown_pro.nro") -Force
        Write-Ok "Copied dragons_crown_pro.nro -> $SwitchRoot"
    } else {
        Write-Warn "$Nro is missing; run tools\build_nx.ps1 first."
    }
}

$Ini = Join-Path $AppDir "nx.ini"
if (-not (Test-Path -LiteralPath $Ini)) {
    @"
# Dragon's Crown Pro (Nintendo Switch)
# Press L3 + R3 (click both sticks) in-game to edit these settings.
NX_SAFE=0
GL_MODE=gles
VSYNC=0
NX_BOOST=1
NX_SCHED=spread
NX_PERSISTENT_RING=1
# Optional:
# PAD_MAP=cross=b,circle=a
# AUDIO=none
# NX_GPU_THREAD=44:2
# FRAME_DUMP=300
"@ | Set-Content -Encoding ASCII $Ini
    Write-Ok "Wrote $Ini"
}

Write-Ok "Ready! Copy 'switch/' to the root of your Switch SD card and launch dragons_crown_pro.nro via Title Takeover (hold R on a game)."
