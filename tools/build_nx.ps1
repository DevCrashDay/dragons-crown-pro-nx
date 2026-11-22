param(
    [string]$BuildDir = "build-nx",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildPath = Join-Path $RepoRoot $BuildDir

if ($Clean -and (Test-Path $BuildPath)) {
    Remove-Item -Recurse -Force $BuildPath
}

$RepoWin = $RepoRoot -replace '\\', '/'
$cmd = "export DEVKITPRO=/opt/devkitpro PKG_CONFIG_LIBDIR=/opt/devkitpro/portlibs/switch/lib/pkgconfig PATH=/opt/devkitpro/portlibs/switch/bin:/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:/usr/bin:`$PATH; " +
       "REPO=`$(cygpath -u '$RepoWin'); BUILD=`"`$REPO/$BuildDir`"; " +
       "aarch64-none-elf-cmake -S `"`$REPO`" -B `"`$BUILD`" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_DEPENDS_USE_COMPILER=FALSE && " +
       "cmake --build `"`$BUILD`" -j`$(nproc)"

& "C:\devkitPro\msys2\usr\bin\bash.exe" -lc $cmd
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
