# Splits game files that FAT32 cannot hold (> 4 GiB) into <name>.000, <name>.001, ...
# The runtime opens the base name and reads the parts as one file.
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [int]$PartSizeMB = 3900,
    [switch]$KeepSource
)

$ErrorActionPreference = "Stop"
$partSize = [int64]$PartSizeMB * 1MB
if ($partSize -le 0 -or $partSize -ge 4GB) {
    throw "Part size must be between 1 MiB and 4 GiB (got $PartSizeMB MiB)."
}

$targets = if (Test-Path $Path -PathType Container) {
    Get-ChildItem $Path -File | Where-Object { $_.Length -gt $partSize }
} else {
    @(Get-Item $Path)
}
if (-not $targets) {
    Write-Host "Nothing to split (no file larger than $PartSizeMB MiB)."
    return
}

foreach ($file in $targets) {
    if ($file.Length -le $partSize) {
        Write-Host "Skip $($file.Name): $($file.Length) bytes fits one part."
        continue
    }
    if (-not $file.FullName.EndsWith(".CPK", "OrdinalIgnoreCase")) {
        Write-Warning "Splitting $($file.Name), which is not a .CPK; the runtime reads any split file."
    }
    Write-Host ("Splitting {0} ({1:N0} bytes) into {2} MiB parts" -f $file.FullName, $file.Length, $PartSizeMB)
    $parts = [int][math]::Ceiling([double]$file.Length / [double]$partSize)
    $buffer = New-Object byte[] (8MB)
    $stream = [IO.File]::OpenRead($file.FullName)
    [int64]$total = 0
    try {
        for ($i = 0; $i -lt $parts; $i++) {
            $outPath = "{0}.{1:D3}" -f $file.FullName, $i
            $out = [IO.File]::Create($outPath)
            try {
                [int64]$remaining = [math]::Min([double]$partSize, [double]($file.Length - [int64]$i * $partSize))
                while ($remaining -gt 0) {
                    $want = [int][math]::Min([int64]$buffer.Length, $remaining)
                    $n = $stream.Read($buffer, 0, $want)
                    if ($n -le 0) { throw "Unexpected end of $($file.FullName) while writing part $i." }
                    $out.Write($buffer, 0, $n)
                    $remaining -= $n
                    $total += $n
                }
            } finally {
                $out.Close()
            }
            $written = (Get-Item $outPath).Length
            if ($written -ge 4GB) { throw "Part is not FAT32-safe: $outPath ($written bytes)." }
            Write-Host ("  {0}  {1:N0} bytes" -f (Split-Path -Leaf $outPath), $written)
        }
    } finally {
        $stream.Close()
    }
    if ($total -ne $file.Length) {
        throw "Split size mismatch for $($file.FullName): wrote $total of $($file.Length) bytes."
    }
    if (-not $KeepSource) {
        Remove-Item $file.FullName -Force
        Write-Host "  removed the source file"
    }
}
Write-Host "Done. The runtime opens the original name and reads the .000/.001 parts."
