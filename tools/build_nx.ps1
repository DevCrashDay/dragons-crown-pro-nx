# Builds the Switch .nro, automatically fetching Box64, gpu/deps.txt, and Mesa 26.2.4 on fresh clones.
param(
    [string]$BuildDir = "build-nx",
    [switch]$Clean,
    [switch]$StockMesa,
    [switch]$SkipDeps
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_common.ps1")
$RepoRoot = $script:RepoRoot
$BuildPath = Join-Path $RepoRoot $BuildDir

$DevkitPro = @($env:DEVKITPRO, "C:\devkitPro", "D:\devkitPro") | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $DevkitPro) {
    Stop-Tool "devkitPro not found. Install devkitPro (https://devkitpro.org) with switch-dev, switch-sdl2, switch-freetype, switch-libpng, switch-glad."
}
$Bash = Join-Path $DevkitPro "msys2\usr\bin\bash.exe"
if (-not (Test-Path $Bash)) {
    Stop-Tool "devkitPro MSYS2 bash not found at $Bash"
}

$Box64Commit = "3e5a0b5988435843a9a28534c08e97a2d34ed698"
$MesaCommit = "0ff5207"

function Ensure-PatchedRepo([string]$Dir, [string]$Url, [string]$Commit, [string]$PatchRel, [string]$ProbeRel) {
    $patchFile = Join-Path $RepoRoot $PatchRel
    $probe = Join-Path $Dir $ProbeRel
    if (Test-Path -LiteralPath $probe) { return }
    if (Test-Path -LiteralPath $Dir) { Remove-Item -LiteralPath $Dir -Recurse -Force }
    Write-Step "Fetching $(Split-Path -Leaf $Dir) ($($Commit.Substring(0, 7)))"
    New-Item -ItemType Directory -Force -Path $Dir | Out-Null
    & git -C $Dir init --quiet
    & git -C $Dir config core.autocrlf false
    & git -C $Dir fetch --quiet --depth 1 $Url $Commit
    if ($LASTEXITCODE -ne 0) { Stop-Tool "git fetch failed for $Url" }
    & git -C $Dir -c advice.detachedHead=false checkout --quiet FETCH_HEAD
    if ($LASTEXITCODE -ne 0) { Stop-Tool "git checkout failed for $Dir" }
    if (Test-Path -LiteralPath $patchFile) {
        & git -C $Dir apply $patchFile
        if ($LASTEXITCODE -ne 0) { Stop-Tool "Failed applying $PatchRel to $Dir" }
    }
}

function Find-MesaSdk {
    foreach ($root in @((Join-Path $RepoRoot "third_party\mesa-switch"), (Join-Path (Split-Path -Parent $RepoRoot) "mesa-switch"))) {
        $sdk = Join-Path $root "mesa-install\opt\devkitpro\portlibs\switch"
        if (Test-Path -LiteralPath (Join-Path $sdk "lib\cmake\OpenGL\OpenGLConfig.cmake")) { return $sdk }
    }
    return $null
}

function Ensure-VenvTools([string[]]$Packages) {
    $venvPy = Get-VenvPython
    if (-not $venvPy) {
        $py = Get-Python
        if (-not $py) { Stop-Tool "Python 3.10+ is required (https://www.python.org/downloads/)." }
        Write-Step "Creating .venv"
        & $py.Exe @($py.Args) -m venv (Join-Path $RepoRoot ".venv")
        if ($LASTEXITCODE -ne 0) { Stop-Tool "Could not create .venv" }
        $venvPy = Get-VenvPython
    }
    & $venvPy -m pip install --quiet --disable-pip-version-check @Packages
    if ($LASTEXITCODE -ne 0) { Stop-Tool "pip install $($Packages -join ' ') failed" }
    return $venvPy
}

function Ensure-MesaSwitch {
    if ($StockMesa) { return $null }
    $sdk = Find-MesaSdk
    if ($sdk) { return $sdk }

    $mesaDir = Join-Path $RepoRoot "third_party\mesa-switch"
    Ensure-PatchedRepo $mesaDir "https://github.com/danfromtico/mesa-switch.git" $MesaCommit "patches\mesa-switch.patch" "meson.build"

    Write-Step "Building Mesa 26.2.4 Switch OpenGL/GLES SDK (one-time)"
    $venvPy = Ensure-VenvTools @("meson", "mako", "pyyaml")
    $venvScripts = Split-Path -Parent $venvPy
    $meson = Join-Path $venvScripts "meson.exe"
    if (-not (Test-Path -LiteralPath $meson)) { Stop-Tool "meson.exe not found in $venvScripts" }

    $ninja = @((Get-Command ninja -ErrorAction SilentlyContinue | ForEach-Object Source),
               (Join-Path $RepoRoot ".venv\Scripts\ninja.exe"),
               "C:\msys64\ucrt64\bin\ninja.exe",
               (Join-Path $DevkitPro "msys2\ucrt64\bin\ninja.exe"),
               (Join-Path $DevkitPro "msys2\usr\bin\ninja.exe")) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
    if (-not $ninja) {
        Ensure-VenvTools @("ninja") | Out-Null
        $ninja = Join-Path $venvScripts "ninja.exe"
    }

    $dkpFwd = $DevkitPro -replace "\\", "/"
    $stage = Join-Path $BuildPath "mesa-cross"
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    $pkgWrapper = (Join-Path $stage "switch-pkg-config-wrapper.sh") -replace "\\", "/"
    $crossFile = (Join-Path $stage "switch_cross_file_dkp.txt") -replace "\\", "/"

    $wrapperBody = @"
#!/bin/bash
set -o pipefail
export PKG_CONFIG_DIR=
export PKG_CONFIG_PATH=
export PKG_CONFIG_SYSROOT_DIR=
export PKG_CONFIG_LIBDIR=/opt/devkitpro/portlibs/switch/lib/pkgconfig
/usr/bin/pkg-config --static "`$@" | /usr/bin/sed -e 's|/opt/devkitpro|$dkpFwd|g' -e 's|`$(DEVKITPRO)|$dkpFwd|g'
"@
    [IO.File]::WriteAllText($pkgWrapper, ($wrapperBody -replace "`r`n", "`n"))

    $crossBody = @"
[binaries]
c = '$dkpFwd/devkitA64/bin/aarch64-none-elf-gcc.exe'
cpp = '$dkpFwd/devkitA64/bin/aarch64-none-elf-g++.exe'
ar = '$dkpFwd/devkitA64/bin/aarch64-none-elf-gcc-ar.exe'
nm = '$dkpFwd/devkitA64/bin/aarch64-none-elf-nm.exe'
strip = '$dkpFwd/devkitA64/bin/aarch64-none-elf-strip.exe'
pkg-config = ['$dkpFwd/msys2/usr/bin/bash.exe', '$pkgWrapper']
flex = '$dkpFwd/msys2/usr/bin/flex.exe'
bison = '$dkpFwd/msys2/usr/bin/bison.exe'

[built-in options]
c_args = ['-D__SWITCH__', '-DHAVE_TIMESPEC_GET', '-I$dkpFwd/portlibs/switch/include', '-isystem', '$dkpFwd/libnx/include', '-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-fPIC', '-ftls-model=local-exec', '-ffunction-sections', '-fdata-sections']
c_link_args = ['-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-fPIC', '-ftls-model=local-exec', '-L$dkpFwd/portlibs/switch/lib', '-L$dkpFwd/libnx/lib', '-lnx']
cpp_args = ['-D__SWITCH__', '-DHAVE_TIMESPEC_GET', '-I$dkpFwd/portlibs/switch/include', '-isystem', '$dkpFwd/libnx/include', '-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-fPIC', '-ftls-model=local-exec', '-ffunction-sections', '-fdata-sections']
cpp_link_args = ['-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-fPIC', '-ftls-model=local-exec', '-L$dkpFwd/portlibs/switch/lib', '-L$dkpFwd/libnx/lib', '-lnx']

[host_machine]
system = 'horizon'
cpu_family = 'aarch64'
cpu = 'cortex-a57'
endian = 'little'

[target_machine]
system = 'horizon'
cpu_family = 'aarch64'
cpu = 'cortex-a57'
endian = 'little'
"@
    [IO.File]::WriteAllText($crossFile, ($crossBody -replace "`r`n", "`n"))

    $mesaBuild = Join-Path $mesaDir "builddir-opengl"
    $mesaDest = Join-Path $mesaDir "mesa-install"
    $setupArgs = @("setup", $mesaBuild)
    if (Test-Path -LiteralPath (Join-Path $mesaBuild "meson-private\coredata.dat")) { $setupArgs += "--wipe" }
    $setupArgs += @(
        "--cross-file=$crossFile", "--default-library=static", "--prefix=/opt/devkitpro/portlibs/switch",
        "--libdir=lib", "--buildtype=release", "-Doptimization=2", "-Db_lto=false", "-Db_ndebug=true",
        "-Dvulkan-drivers=", "-Dgallium-drivers=nouveau", "-Dgallium-rusticl=false", "-Dplatforms=switch",
        "-Degl-native-platform=switch", "-Dglx=disabled", "-Degl=enabled", "-Dopengl=true",
        "-Dgles1=enabled", "-Dgles2=enabled", "-Dvideo-codecs=", "-Dshader-cache=enabled",
        "-Dxmlconfig=auto", "-Dexpat=auto", "-Dtools=[]", "-Dllvm=disabled", "-Dshared-llvm=disabled",
        "-Dcpp_rtti=false", "-Dbuild-tests=false"
    )
    $prevPath = $env:PATH
    $env:PATH = "$venvScripts;$DevkitPro\devkitA64\bin;$DevkitPro\msys2\usr\bin;$prevPath"
    $env:NINJA = $ninja
    try {
        Push-Location $mesaDir
        & $meson @setupArgs
        if ($LASTEXITCODE -ne 0) { Stop-Tool "Mesa meson setup failed" }
        & $ninja -C $mesaBuild
        if ($LASTEXITCODE -ne 0) { Stop-Tool "Mesa build failed" }
        & $meson install -C $mesaBuild --destdir $mesaDest
        if ($LASTEXITCODE -ne 0) { Stop-Tool "Mesa install failed" }
    } finally {
        Pop-Location
        $env:PATH = $prevPath
        Remove-Item Env:\NINJA -ErrorAction SilentlyContinue
    }

    $sdkOut = Join-Path $mesaDest "opt\devkitpro\portlibs\switch"
    foreach ($pc in @(Get-ChildItem (Join-Path $sdkOut "lib\pkgconfig\*.pc") -ErrorAction SilentlyContinue)) {
        $txt = (Get-Content -LiteralPath $pc.FullName -Raw) -replace "(?m)^prefix=.*$", 'prefix=${pcfiledir}/../..'
        [IO.File]::WriteAllText($pc.FullName, ($txt -replace "`r`n", "`n"))
    }
    $cmakeOpenGlDir = Join-Path $sdkOut "lib\cmake\OpenGL"
    New-Item -ItemType Directory -Force -Path $cmakeOpenGlDir | Out-Null
    Copy-Item (Join-Path $mesaDir "OpenGLConfig.cmake") (Join-Path $cmakeOpenGlDir "OpenGLConfig.cmake") -Force
    Write-Ok "Mesa Switch SDK staged at $sdkOut"
    return $sdkOut
}

if (-not $SkipDeps) {
    Sync-GpuDependencies
    Ensure-PatchedRepo (Join-Path $RepoRoot "third_party\box64") "https://github.com/ptitSeb/box64.git" $Box64Commit "patches\box64-switch.patch" "src\emu\x64emu.c"
}

$MesaSdk = Ensure-MesaSwitch

if ($Clean -and (Test-Path $BuildPath)) {
    Remove-Item -Recurse -Force $BuildPath
}

function ConvertTo-MsysPath([string]$WinPath) {
    $fwd = $WinPath -replace '\\', '/'
    if ($fwd -match '^([A-Za-z]):(.*)$') {
        return '/' + $Matches[1].ToLowerInvariant() + $Matches[2]
    }
    return $fwd
}

$RepoWin = $RepoRoot -replace '\\', '/'
$DkpUnix = ConvertTo-MsysPath $DevkitPro
$CmakeExtra = ""
if ($StockMesa) {
    $CmakeExtra = " -DDCP_USE_MESA_SWITCH=OFF"
} elseif ($MesaSdk) {
    $MesaUnix = ConvertTo-MsysPath $MesaSdk
    $CmakeExtra = " -DDCP_USE_MESA_SWITCH=ON -DDCP_MESA_SWITCH_SDK=`"$MesaUnix`""
}

$cmd = "export DEVKITPRO='$DkpUnix' PKG_CONFIG_LIBDIR='$DkpUnix/portlibs/switch/lib/pkgconfig' PATH='$DkpUnix/portlibs/switch/bin:$DkpUnix/devkitA64/bin:$DkpUnix/tools/bin:/usr/bin:`$PATH'; " +
       "REPO=`$(cygpath -u '$RepoWin'); BUILD=`"`$REPO/$BuildDir`"; " +
       "aarch64-none-elf-cmake -S `"`$REPO`" -B `"`$BUILD`" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_DEPENDS_USE_COMPILER=FALSE$CmakeExtra && " +
       "cmake --build `"`$BUILD`" -j`$(nproc)"

& $Bash -lc $cmd
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
