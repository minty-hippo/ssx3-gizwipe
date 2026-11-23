#!/usr/bin/env bash
# SSX 3 GizWipe - build the ROM on Linux / macOS (or WSL):  ./build.sh /path/to/ssx3_gizmondo.zip
# Checks for Python 3.9+ with numpy and Pillow, ffmpeg and BlocksDS, installs numpy/Pillow if missing, converts the
# game files (skipped when nothing changed; ./build.sh --rebuild <zip> to force) and compiles ssx3_gizwipe.nds.
set -e
cd "$(dirname "$0")"
REPO=$(pwd)
mkdir -p build

REBUILD=0
if [ "$1" = "--rebuild" ]; then REBUILD=1; shift; fi
GAME=${1:-}
if [ -z "$GAME" ] && [ -f build/game_path.txt ]; then GAME=$(cat build/game_path.txt); fi
if [ -z "$GAME" ] || [ ! -e "$GAME" ]; then
    echo "usage: ./build.sh /path/to/ssx3_gizmondo.zip   (or the game's folder)"; exit 1
fi
GAME=$(cd "$(dirname "$GAME")" && pwd)/$(basename "$GAME")
echo "$GAME" > build/game_path.txt
say() { printf '\n==> %s\n' "$1"; }
fail() { printf '\nBuild stopped: %s\n' "$1" >&2; exit 1; }

say "Python"
PY=$(command -v python3 || command -v python || true)
[ -n "$PY" ] || fail "Python 3.9+ is needed (install python3 with your package manager)"
"$PY" -c 'import sys; sys.exit(sys.version_info < (3, 9))' || fail "Python 3.9 or newer is needed"
if ! "$PY" -c 'import numpy, PIL' 2>/dev/null; then
    say "Installing numpy and Pillow"
    "$PY" -m pip install --user numpy pillow 2>/dev/null \
      || "$PY" -m pip install --user --break-system-packages numpy pillow 2>/dev/null \
      || fail "could not install numpy / Pillow with pip. Install them with your package manager:
    Debian/Ubuntu: sudo apt install python3-numpy python3-pil
    Fedora:        sudo dnf install python3-numpy python3-pillow
    Arch:          sudo pacman -S python-numpy python-pillow
    macOS:         brew install numpy pillow"
fi

say "ffmpeg"
command -v ffmpeg >/dev/null || fail "ffmpeg is needed (sudo apt install ffmpeg / sudo dnf install ffmpeg / brew install ffmpeg)"

say "BlocksDS"
if [ -z "${BLOCKSDS:-}" ]; then
    for d in /opt/wonderful/thirdparty/blocksds/core /opt/blocksds/core; do
        if [ -f "$d/sys/default_makefiles/rom_arm9/Makefile" ]; then export BLOCKSDS=$d; break; fi
    done
fi
[ -n "${BLOCKSDS:-}" ] || fail "BlocksDS not found. Install it: https://blocksds.skylyrac.net/docs/setup/"
echo "    $BLOCKSDS"

STAMP="$GAME|$(wc -c < "$GAME" 2>/dev/null || echo dir)|$(cd tools && ls -l *.py | awk '{print $5,$9}' | tr '\n' ',')"
if [ $REBUILD = 1 ] || [ ! -d ds_runtime/nitrofs ] || [ "$(cat build/assets.stamp 2>/dev/null)" != "$STAMP" ]; then
    say "Converting the game files (about 5 minutes)"
    "$PY" tools/build_assets.py "$GAME"
    echo "$STAMP" > build/assets.stamp
else
    echo "    game files already converted (./build.sh --rebuild to convert them again)"
fi

say "Compiling the DS program"
make -C ds_runtime -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
cp ds_runtime/ssx3_gizwipe.nds .
say "Done"
echo "    $REPO/ssx3_gizwipe.nds"
