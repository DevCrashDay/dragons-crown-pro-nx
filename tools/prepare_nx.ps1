# Lays out the Switch SD card for dragons-crown-pro-nx from the PC build's game folders.
# Run from anywhere:  powershell -ExecutionPolicy Bypass -File tools\prepare_nx.ps1 -Destination E:\switch\dragons_crown_pro
param(
    [string]$Image,
    [string]$Prepared,
    [string]$Destination = "E:\switch\dragons_crown_pro",
    [switch]$SkipNro,
    [switch]$SplitCpk
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$DownloadRoot = Split-Path -Parent $RepoRoot
$PcGame = Join-Path $DownloadRoot "DragonsCrownProPC-v1.4.0-windows-x64\DragonsCrownProPC-v1.4.0-windows-x64\game"
if (-not $Image) { $Image = Join-Path $PcGame "image\v1.03" }
if (-not $Prepared) { $Prepared = Join-Path $PcGame "prepared\v1.03" }

if (-not (Test-Path (Join-Path $Image "boot.bin"))) {
    throw "boot.bin not found in $Image. Build the boot image with the PC launcher first."
}
if (-not (Test-Path (Join-Path $Prepared "eboot.bin"))) {
    throw "prepared game files not found in $Prepared."
}

$GameDir = Join-Path $Destination "game"
$SwitchRoot = Split-Path -Parent $Destination
foreach ($dir in @($GameDir, (Join-Path $Destination "user"), (Join-Path $Destination "logs"))) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

Write-Host "Copying image files to $GameDir"
Copy-Item (Join-Path $Image "*") $GameDir -Recurse -Force
Write-Host "Copying prepared game files to $GameDir"
Copy-Item (Join-Path $Prepared "*") $GameDir -Recurse -Force

if ($SplitCpk) {
    & (Join-Path $PSScriptRoot "split_cpk.ps1") -Path $GameDir
}

if (-not $SkipNro) {
    $Nro = Join-Path $RepoRoot "build-nx\dragons_crown_pro.nro"
    if (Test-Path $Nro) {
        Copy-Item $Nro (Join-Path $SwitchRoot "dragons_crown_pro.nro") -Force
        Write-Host "Copied $(Split-Path -Leaf $Nro) to $SwitchRoot"
    } else {
        Write-Warning "$Nro is missing; run tools\build_nx.ps1 first."
    }
}

$Ini = Join-Path $Destination "nx.ini"
if (-not (Test-Path $Ini)) {
    @"
# Dragons Crown Pro (Switch). Each KEY=VALUE becomes DCP_KEY, the same knob the PC build reads.
# NX_SAFE=0 turns the port's guest-hook features back on (cheats, boss battle, online, ...).
NX_SAFE=1
# Defaults: 2176 MiB direct + 32 MiB flexible, 800 MiB large arena, BOX64_DYNAREC_STRONGMEM=1, libnx audout.
# NX_DMEM_MB=2176
# NX_FMEM_MB=32
# NX_LARGE_ARENA_MB=800
# BOX64_DYNAREC_STRONGMEM=1
# AUDIO=none
# PAD_MAP=cross=b,circle=a
# GPU_PROBE=1
# X64_SELFTEST=1
"@ | Set-Content -Encoding ASCII $Ini
    Write-Host "Wrote $Ini"
}

$Cpk = Join-Path $GameDir "DRACRO.CPK"
if (Test-Path $Cpk) {
    $size = (Get-Item $Cpk).Length
    $drive = (Get-Item $Destination).PSDrive
    $fs = $null
    try { $fs = (Get-Volume -DriveLetter $drive.Name).FileSystem } catch { }
    if ($size -gt 4GB -and $fs -eq "FAT32") {
        Write-Warning "DRACRO.CPK is $([math]::Round($size / 1GB, 2)) GiB and this volume is FAT32. Use exFAT, or run tools\split_cpk.ps1 (or prepare_nx.ps1 -SplitCpk) on a local folder first."
    }
}
Write-Host "Done. Launch /switch/dragons_crown_pro.nro in Title Takeover mode."
