# SSX 3 GizWipe - one-step Windows build (run through build.bat).
#
#   build.bat                       asks for the Gizmondo SSX 3 zip (or uses the one from last time)
#   build.bat "D:\games\ssx3.zip"   or drag the zip (or the game's folder) onto build.bat
#   build.bat -Rebuild              convert the game files again even if nothing changed
#
# Installs what is missing, for this user only, with winget: Python (+ numpy, Pillow), ffmpeg, MSYS2, and BlocksDS
# (through the Wonderful toolchain, as BlocksDS's own Windows guide does). Then converts the game files and builds
# the ROM, ssx3_gizwipe.nds, next to build.bat.

param([string]$Game = '', [switch]$Rebuild)

# (not 'Stop': Windows PowerShell turns a native program's stderr into errors; exit codes are checked instead)
$ErrorActionPreference = 'Continue'
$Repo  = Split-Path -Parent $PSScriptRoot
$Work  = Join-Path $Repo 'build'
New-Item -ItemType Directory -Force $Work | Out-Null

function Say($m)  { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Note($m) { Write-Host "    $m" }
function Fail($m) { Write-Host "`nBuild stopped: $m" -ForegroundColor Red; exit 1 }
function Have($c) { [bool](Get-Command $c -ErrorAction SilentlyContinue) }

function Winget-Install($id, $what) {
    if (-not (Have 'winget')) {
        Fail "$what is needed and winget (the Windows package manager) is not available to install it.`n    Install $what yourself, or get winget ('App Installer' in the Microsoft Store), then run build.bat again."
    }
    Say "Installing $what (winget $id)"
    & winget install -e --id $id --scope user --accept-package-agreements --accept-source-agreements --silent
    if ($LASTEXITCODE -ne 0) {
        # some packages have no per-user installer: try the machine-wide one (asks for admin)
        & winget install -e --id $id --accept-package-agreements --accept-source-agreements --silent
    }
}

# ---------------------------------------------------------------------------------------------------------------
# 1. the game files
$Remember = Join-Path $Work 'game_path.txt'
if (-not $Game -and (Test-Path $Remember)) {
    $prev = (Get-Content $Remember -Raw).Trim()
    if ($prev -and (Test-Path -LiteralPath $prev)) { $Game = $prev }
}
if (-not $Game) {
    $zips = @(Get-ChildItem -LiteralPath $Repo, (Split-Path -Parent $Repo) -Filter *.zip -ErrorAction SilentlyContinue |
              Where-Object { $_.Name -match 'ssx' -and $_.Name -notmatch 'gizwipe' })
    if ($zips.Count -eq 1) { $Game = $zips[0].FullName }
}
if (-not $Game) {
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title  = 'Choose your Gizmondo SSX 3 zip'
    $dlg.Filter = 'Zip files (*.zip)|*.zip|All files (*.*)|*.*'
    if ($dlg.ShowDialog() -ne 'OK') { Fail 'no game zip chosen. Drag the zip (or the game folder) onto build.bat.' }
    $Game = $dlg.FileName
}
if (-not (Test-Path -LiteralPath $Game)) { Fail "not found: $Game" }
$Game = (Resolve-Path -LiteralPath $Game).Path
Set-Content -LiteralPath $Remember -Value $Game
Say "Game files: $Game"

# ---------------------------------------------------------------------------------------------------------------
# 2. Python 3.9+ with numpy and Pillow - a normal Windows Python, not devkitPro's or MSYS2's (they have no pip)
function Test-Python($cmd) {
    try {
        $exe = $cmd[0]; $pre = @($cmd | Select-Object -Skip 1)
        $out = & $exe @pre -c "import sys; print(sys.version_info >= (3, 9), sys.executable)" 2>$null
        if ($LASTEXITCODE -ne 0 -or -not $out) { return $false }
        $ok, $path = "$out".Split(' ', 2)
        return ($ok -eq 'True' -and $path -notmatch 'msys|devkitpro|cygwin|mingw')
    } catch { return $false }
}
function Find-Python {
    $cands = @()
    if (Have 'py') { $cands += , @('py', '-3') }
    foreach ($n in 'python', 'python3') {
        $c = Get-Command $n -ErrorAction SilentlyContinue
        if ($c -and $c.Source -notmatch 'WindowsApps|msys|devkitpro') { $cands += , @($c.Source) }
    }
    foreach ($root in "$env:LOCALAPPDATA\Programs\Python", "$env:ProgramFiles\Python*", "$env:ProgramFiles") {
        Get-ChildItem -Path "$root\Python3*\python.exe" -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | ForEach-Object { $cands += , @($_.FullName) }
    }
    foreach ($c in $cands) { if (Test-Python $c) { return , $c } }
    return $null
}
Say 'Python'
$Py = Find-Python
if (-not $Py) {
    Winget-Install 'Python.Python.3.12' 'Python 3'
    $Py = Find-Python
    if (-not $Py) { Fail 'Python was installed but could not be found. Close this window and run build.bat again.' }
}
$PyExe = $Py[0]; $PyPre = @($Py | Select-Object -Skip 1)
function Run-Py { & $PyExe @PyPre @args }
Note (Run-Py -c "import sys; print('Python', sys.version.split()[0], sys.executable)")
Run-Py -c "import numpy, PIL" 2>$null
if ($LASTEXITCODE -ne 0) {
    Say 'Installing numpy and Pillow'
    Run-Py -m pip --version 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) { Run-Py -m ensurepip --user }
    Run-Py -m pip install --user --disable-pip-version-check numpy pillow
    Run-Py -c "import numpy, PIL"
    if ($LASTEXITCODE -ne 0) { Fail 'numpy / Pillow could not be installed (see the messages above).' }
}

# ---------------------------------------------------------------------------------------------------------------
# 3. ffmpeg (the sound effects and music)
function Find-Ffmpeg {
    $c = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    $link = "$env:LOCALAPPDATA\Microsoft\WinGet\Links\ffmpeg.exe"
    if (Test-Path $link) { return $link }
    $f = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Gyan.FFmpeg*\*\bin\ffmpeg.exe", "$env:ProgramFiles\ffmpeg\bin\ffmpeg.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($f) { return $f.FullName }
    return $null
}
Say 'ffmpeg'
$Ff = Find-Ffmpeg
if (-not $Ff) {
    Winget-Install 'Gyan.FFmpeg' 'ffmpeg'
    $Ff = Find-Ffmpeg
    if (-not $Ff) { Fail 'ffmpeg was installed but could not be found. Close this window and run build.bat again.' }
}
$env:PATH = (Split-Path -Parent $Ff) + ';' + $env:PATH
Note $Ff

# ---------------------------------------------------------------------------------------------------------------
# 4. BlocksDS (the DS SDK) inside an MSYS2
function Find-BlocksDS {
    $roots = @('C:\msys64', "$env:SystemDrive\msys64", "$env:LOCALAPPDATA\msys64", 'C:\devkitPro\msys2') | Select-Object -Unique
    foreach ($r in $roots) {
        if (-not (Test-Path "$r\usr\bin\bash.exe")) { continue }
        foreach ($core in 'opt\wonderful\thirdparty\blocksds\core', 'opt\blocksds\core') {
            if (Test-Path "$r\$core\sys\default_makefiles\rom_arm9\Makefile") {
                return @{ Root = $r; Core = '/' + ($core -replace '\\', '/') }
            }
        }
    }
    return $null
}
function Msys($root, $script) {
    # a login shell (so a fresh MSYS2 sets itself up), in the MSYS environment, starting in the repository
    $env:MSYSTEM = 'MSYS'; $env:CHERE_INVOKING = '1'; $env:SSX_REPO = $Repo
    $script = $script -replace "`r", ''           # (this file may have been checked out with CRLF line ends)
    & "$root\usr\bin\bash.exe" -lc $script | Out-Host
    return $LASTEXITCODE
}
Say 'BlocksDS'
$Bds = Find-BlocksDS
if (-not $Bds) {
    $root = @('C:\msys64', "$env:SystemDrive\msys64") | Where-Object { Test-Path "$_\usr\bin\bash.exe" } | Select-Object -First 1
    if (-not $root) {
        Winget-Install 'MSYS2.MSYS2' 'MSYS2'
        $root = @('C:\msys64', "$env:SystemDrive\msys64", "$env:LOCALAPPDATA\msys64") | Where-Object { Test-Path "$_\usr\bin\bash.exe" } | Select-Object -First 1
        if (-not $root) { Fail 'MSYS2 was installed but could not be found (expected C:\msys64).' }
    }
    Say "Installing BlocksDS into $root (Wonderful toolchain) - this takes a few minutes, once"
    # updating MSYS2's core can end the shell; the second pass finishes the update
    Msys $root 'pacman -Syu --noconfirm' | Out-Null
    $rc = Msys $root @'
set -e
pacman -Syu --noconfirm
pacman -S --needed --noconfirm make tar curl
if [ ! -x /opt/wonderful/bin/wf-pacman ]; then
    mkdir -p /opt/wonderful
    curl -fL https://wonderful.asie.pl/bootstrap/wf-bootstrap-windows-x86_64.tar.gz | tar xz -C /opt/wonderful
fi
/opt/wonderful/bin/wf-pacman -Syu --noconfirm wf-tools
/opt/wonderful/bin/wf-config repo enable blocksds
/opt/wonderful/bin/wf-pacman -Syu --noconfirm
/opt/wonderful/bin/wf-pacman -S --needed --noconfirm blocksds-toolchain
'@
    $Bds = Find-BlocksDS
    if ($rc -ne 0 -or -not $Bds) {
        Fail "BlocksDS could not be installed automatically. Follow https://blocksds.skylyrac.net/docs/setup/options/windows/`n    (MSYS2 + Wonderful toolchain), then run build.bat again."
    }
}
Note "$($Bds.Root)  $($Bds.Core)"

# ---------------------------------------------------------------------------------------------------------------
# 5. convert the game files (skipped when the game and the converters are unchanged since the last build)
$gi = Get-Item -LiteralPath $Game
$tools = Get-ChildItem (Join-Path $Repo 'tools') -Filter *.py | Sort-Object Name | ForEach-Object { "$($_.Name):$($_.Length)" }
$stampText = "$Game|$($gi.Length)|$($gi.LastWriteTimeUtc.Ticks)|" + ($tools -join ',')
$stamp = Join-Path $Work 'assets.stamp'
$haveAssets = Test-Path (Join-Path $Repo 'ds_runtime\nitrofs')
if ($Rebuild -or -not $haveAssets -or -not (Test-Path $stamp) -or (Get-Content $stamp -Raw).Trim() -ne $stampText) {
    Say 'Converting the game files (about 5 minutes)'
    Run-Py (Join-Path $Repo 'tools\build_assets.py') $Game
    if ($LASTEXITCODE -ne 0) { Fail 'the game files could not be converted (see the messages above).' }
    Set-Content -LiteralPath $stamp -Value $stampText
} else {
    Note 'game files already converted (build.bat -Rebuild to convert them again)'
}

# ---------------------------------------------------------------------------------------------------------------
# 6. compile the ROM
Say 'Compiling the DS program'
$core = $Bds.Core
$rc = Msys $Bds.Root @"
set -e
cd "`$(cygpath -u "`$SSX_REPO")/ds_runtime"
export BLOCKSDS=$core
export BLOCKSDSEXT=`$(dirname $core)/external
export WONDERFUL_TOOLCHAIN=/opt/wonderful
if [ ! -d /opt/wonderful/toolchain/gcc-arm-none-eabi/bin ] && [ -d /opt/devkitpro/devkitARM/bin ]; then
    export ARM_NONE_EABI_PATH=/opt/devkitpro/devkitARM/bin/
fi
command -v make >/dev/null || pacman -S --needed --noconfirm make
make -j`$(nproc)
"@
if ($rc -ne 0) { Fail 'the DS program did not compile (see the messages above).' }

$rom = Join-Path $Repo 'ds_runtime\ssx3_gizwipe.nds'
if (-not (Test-Path $rom)) { Fail 'the build finished but ssx3_gizwipe.nds is missing.' }
Copy-Item $rom (Join-Path $Repo 'ssx3_gizwipe.nds') -Force
Say 'Done'
Note (Join-Path $Repo 'ssx3_gizwipe.nds')
Write-Host ''
