# One scripted Ryujinx run of the Switch build: build, deploy, run for a fixed time (or until the
# success marker), then collect every log into runs\<timestamp>-<label>\ and print a summary.
#
#   powershell -ExecutionPolicy Bypass -File tools\ryu_run.ps1 -Label sched -Ini 'NX_SCHED=spread'
param(
    [string]$Label = "run",
    [int]$Seconds = 240,
    [string[]]$Ini = @(),
    [switch]$NoBuild,
    [string]$Ryujinx = "$env:USERPROFILE\Downloads\ryujinx-canary-1.3.354-win_x64\publish\Ryujinx.exe",
    [string]$StopMarker = "Font: system font set",
    # Scripted pad 1 input: "seconds:buttons,..." (PS4 names, "+" joins: "70:options,80:cross+r1").
    # Each press is held 200 ms through PAD_FILE, which the game polls every 20 ms.
    [string]$Pad = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Nro = Join-Path $RepoRoot "build-nx\dragons_crown_pro.nro"
$GameDir = Join-Path $env:APPDATA "Ryujinx\sdcard\switch\dragons_crown_pro"
$LogDir = Join-Path $GameDir "logs"
$Bringup = Join-Path $LogDir "bringup.log"
$RyuLogs = Join-Path (Split-Path -Parent $Ryujinx) "Logs"

if (-not $NoBuild) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build_nx.ps1")
    if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
}

$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$RunDir = Join-Path $RepoRoot "runs\$Stamp-$Label"
New-Item -ItemType Directory -Force -Path $RunDir | Out-Null

# Deploy: .nro, nx.ini (defaults + overrides), fresh bringup.log.
Copy-Item $Nro (Join-Path $GameDir "dragons_crown_pro.nro") -Force
$settings = [ordered]@{ NX_SAFE = "1" }
# "powershell -File" passes -Ini a,b as one string: split on commas too.
foreach ($kv in (($Ini -join ",") -split "," | Where-Object { $_ -match "=" })) {
    $k, $v = $kv -split "=", 2
    $settings[$k.Trim()] = $v.Trim()
}
$PadFile = Join-Path $GameDir "pad.txt"
$presses = @()
if ($Pad) {
    $settings["PAD_FILE"] = "sdmc:/switch/dragons_crown_pro/pad.txt"
    [IO.File]::WriteAllText($PadFile, "")
    $presses = @($Pad -split "," | ForEach-Object {
        $t, $b = $_ -split ":", 2
        $hold = 200
        if ($b -match "^(.+)@(\d+)$") { $b = $Matches[1]; $hold = [int]$Matches[2] }
        [pscustomobject]@{ At = [double]$t; Buttons = ($b -replace "\+", " "); HoldMs = $hold; Done = $false }
    } | Sort-Object At)
}
$iniText = ($settings.Keys | ForEach-Object { "$_=$($settings[$_])" }) -join "`n"
[IO.File]::WriteAllText((Join-Path $GameDir "nx.ini"), "$iniText`n")
Copy-Item (Join-Path $GameDir "nx.ini") $RunDir
if (Test-Path $Bringup) { Remove-Item $Bringup -Force }
Remove-Item (Join-Path $LogDir "frame_*.bmp") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $LogDir "draw_trace.txt"), (Join-Path $LogDir "draw_shaders.txt") -Force -ErrorAction SilentlyContinue
$runtimeBefore = @(Get-ChildItem $LogDir -Filter "dragons_crown_pro_*.log" -ErrorAction SilentlyContinue | ForEach-Object Name)
$ryuBefore = @(Get-ChildItem $RyuLogs -Filter "*.log" -ErrorAction SilentlyContinue | ForEach-Object Name)

Write-Host "ryu_run: $Label for ${Seconds}s, nx.ini: $($iniText -replace "`n", ' ')"
$proc = Start-Process -FilePath $Ryujinx -ArgumentList "`"$(Join-Path $GameDir 'dragons_crown_pro.nro')`"" `
    -PassThru -RedirectStandardOutput (Join-Path $RunDir "ryujinx_stdout.txt") `
    -RedirectStandardError (Join-Path $RunDir "ryujinx_stderr.txt")

function Get-NewRuntimeLog {
    Get-ChildItem $LogDir -Filter "dragons_crown_pro_*.log" -ErrorAction SilentlyContinue |
        Where-Object { $runtimeBefore -notcontains $_.Name } | Sort-Object LastWriteTime | Select-Object -Last 1
}

$launched = Get-Date
$deadline = $launched.AddSeconds($Seconds)
$stopped = "timeout"
$nextCheck = $launched.AddSeconds(5)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 100
    $elapsed = ((Get-Date) - $launched).TotalSeconds
    foreach ($p in $presses) {
        if (-not $p.Done -and $elapsed -ge $p.At) {
            [IO.File]::WriteAllText($PadFile, $p.Buttons)
            Start-Sleep -Milliseconds $p.HoldMs
            [IO.File]::WriteAllText($PadFile, "")
            $p.Done = $true
            Write-Host ("ryu_run: {0,6:N1}s pressed {1} ({2}ms)" -f $elapsed, $p.Buttons, $p.HoldMs)
        }
    }
    if ((Get-Date) -lt $nextCheck) { continue }
    $nextCheck = (Get-Date).AddSeconds(5)
    if ($proc.HasExited) { $stopped = "ryujinx exited ($($proc.ExitCode))"; break }
    $rt = Get-NewRuntimeLog
    if ($rt -and (Select-String -Path $rt.FullName -SimpleMatch $StopMarker -Quiet)) { $stopped = "marker"; Start-Sleep -Seconds 10; break }
}
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }

# Collect.
if (Test-Path $Bringup) { Copy-Item $Bringup $RunDir }
Move-Item (Join-Path $LogDir "frame_*.bmp") $RunDir -Force -ErrorAction SilentlyContinue
Move-Item (Join-Path $LogDir "draw_trace.txt"), (Join-Path $LogDir "draw_shaders.txt") $RunDir -Force -ErrorAction SilentlyContinue
$rt = Get-NewRuntimeLog
if ($rt) { Copy-Item $rt.FullName (Join-Path $RunDir "runtime.log") }
Get-ChildItem $RyuLogs -Filter "*.log" -ErrorAction SilentlyContinue |
    Where-Object { $ryuBefore -notcontains $_.Name } | ForEach-Object { Copy-Item $_.FullName (Join-Path $RunDir "ryujinx.log") }

# Summary.
$all = @()
foreach ($f in @("bringup.log", "runtime.log")) {
    $p = Join-Path $RunDir $f
    if (Test-Path $p) { $all += Get-Content $p }
}
$last = $all | Where-Object { $_ -match "Presenter: show" } | Select-Object -Last 1
$marks = foreach ($m in @("Entry: entering guest", "Font: system font set", "SAVEDATA00", "STOP:", "FAIL:")) {
    "$m=" + [bool]($all | Where-Object { $_ -like "*$m*" } | Select-Object -First 1)
}
$drawn = [bool]($all | Where-Object { $_ -match "max=[1-9]" } | Select-Object -First 1)
$summary = "[$Label] stop=$stopped | $($marks -join ' ') max>0=$drawn`n  last: $last"
$summary | Set-Content (Join-Path $RunDir "summary.txt")
Write-Host $summary
Write-Host "logs: $RunDir"
