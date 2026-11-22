#!/bin/bash
# Shows the same pictures in DirectCRT (shader=crt-auto) and in DOSBox Staging,
# captures both and compares them pixel by pixel. See README.md.
#
# usage: tools/crt-compare/run.sh WORKDIR [CONFIGURATION] [CASE...]
#   WORKDIR        where to put the test files and captures (created)
#   CONFIGURATION  the build to test, bin/<CONFIGURATION> (default Release)
#   CASE           fs:WxH (fullscreen) or win:WxH (1920x1080 window), with WxH
#                  one of 320x200 320x240 640x480 800x600 (default: all eight)
#
# Needs Python with numpy and Pillow, and DOSBox Staging (set DOSBOX to its
# dosbox.exe if it isn't in the default place).

set -u

if [ $# -lt 1 ]; then
    sed -n '5,13p' "$0"
    exit 2
fi

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
mkdir -p "$1"
WORK=$(cd "$1" && pwd)
CFG=${2:-Release}
shift $(( $# >= 2 ? 2 : 1 ))

DOSBOX=${DOSBOX:-"$LOCALAPPDATA/Programs/DOSBox Staging/dosbox.exe"}
[ -f "$DOSBOX" ] || { echo "DOSBox Staging not found at $DOSBOX (set DOSBOX)"; exit 1; }
[ -f "$REPO/bin/$CFG/ddraw.dll" ] || { echo "no build in bin/$CFG"; exit 1; }
[ -f "$REPO/bin/$CFG/ddtest.exe" ] || DDTEST_CFG=Release

w() { cygpath -w "$1"; }
PS="powershell -NoProfile -ExecutionPolicy Bypass -File $(w "$HERE/capture.ps1")"

# VGA BIOS mode for each size: 13h, then VESA modes DOSBox's S3 BIOS has
declare -A MODE=([320x200]=0x13 [320x240]=0x151 [640x480]=0x101 [800x600]=0x103)

cases=("$@")
if [ ${#cases[@]} -eq 0 ]; then
    cases=(fs:320x200 fs:320x240 fs:640x480 fs:800x600 win:320x200 win:320x240 win:640x480 win:800x600)
fi

# Sets key=value in the global section of a directcrt.ini (the first match)
set_ini() {
    sed -i "0,/^$2=.*/s//$2=$3/" "$1"
}

# A DirectCRT folder: the DLL, ddtest and a directcrt.ini with crt-auto
setup_dcrt() {
    local dir=$1 windowed=$2
    mkdir -p "$dir"
    cp "$REPO/bin/$CFG/ddraw.dll" "$dir/"
    cp "$REPO/bin/${DDTEST_CFG:-$CFG}/ddtest.exe" "$dir/"
    rm -f "$dir/directcrt.ini"
    # The first run writes the default directcrt.ini
    (cd "$dir" && ./ddtest.exe --size 320x200 --bpp 8 --present flip --start --frames 2 >/dev/null 2>&1)
    set_ini "$dir/directcrt.ini" shader crt-auto
    set_ini "$dir/directcrt.ini" savesettings 0
    if [ "$windowed" = 1 ]; then
        set_ini "$dir/directcrt.ini" windowed true
        set_ini "$dir/directcrt.ini" fullscreen false
        set_ini "$dir/directcrt.ini" width 1920
        set_ini "$dir/directcrt.ini" height 1080
    else
        set_ini "$dir/directcrt.ini" windowed auto
        set_ini "$dir/directcrt.ini" fullscreen true
    fi
}

echo "Setting up in $WORK"
setup_dcrt "$WORK/fs" 0
setup_dcrt "$WORK/win" 1

# The pictures: ddtest's photo, pixel patterns and colour bars, with a palette
# DOSBox shows exactly, and a DOS program that displays each
for res in 320x200 320x240 640x480 800x600; do
    mkdir -p "$WORK/dumps/$res" "$WORK/dos/$res"
    "$WORK/fs/ddtest.exe" --size "$res" --bpp 32 --dump "$(w "$WORK/dumps/$res")"
    python -I "$HERE/compose.py" "$WORK/dumps/$res" "$res" "$WORK/dos/$res/src.png" >/dev/null
    python -I "$HERE/mkdos.py" "$WORK/dos/$res/src.png" "${MODE[$res]}" "$WORK/dos/$res"
done

# DOSBox sizes its window in logical units
DPI=$(powershell -NoProfile -ExecutionPolicy Bypass -File "$(w "$HERE/dpi.ps1")" | tr -d '\r')
WIN_LOGICAL="$(( 1920 * 96 / DPI ))x$(( 1080 * 96 / DPI ))"

mkdir -p "$WORK/captures"
results=()

for c in "${cases[@]}"; do
    kind=${c%%:*}
    res=${c##*:}
    tag="${kind}_${res}"
    ref=$(w "$WORK/dos/$res/ref.png")
    conf=$(w "$WORK/dos/$tag.conf")
    cap="$WORK/captures"
    args="--size $res --bpp 32 --present flip --pattern image --image $ref --info 0 --mouse 0 --start --frames 1000000"

    if [ "$kind" = fs ]; then
        python -I "$HERE/dosconf.py" "$(w "$WORK/dos/$res")" fullscreen "$conf" >/dev/null
        # Long waits: the NVIDIA overlay notice covers part of the screen at first
        $PS -Exe "$(w "$WORK/fs/ddtest.exe")" -ArgList "$args" -Out "$(w "$cap/dcrt_$tag.png")" -Delay 16
        $PS -Exe "$(w "$DOSBOX")" -ArgList "--noprimaryconf --nolocalconf --conf $conf" -Out "$(w "$cap/dosbox_$tag.png")" -Delay 16
    else
        python -I "$HERE/dosconf.py" "$(w "$WORK/dos/$res")" "$WIN_LOGICAL" "$conf" >/dev/null
        $PS -Exe "$(w "$WORK/win/ddtest.exe")" -ArgList "$args" -Out "$(w "$cap/dcrt_$tag.png")" -Delay 8 -Client
        $PS -Exe "$(w "$DOSBOX")" -ArgList "--noprimaryconf --nolocalconf --conf $conf" -Out "$(w "$cap/dosbox_$tag.png")" -Delay 8 -Client -Class SDL_app
    fi

    echo "== $tag"
    if python -I "$HERE/compare.py" "$cap/dcrt_$tag.png" "$cap/dosbox_$tag.png" "$cap/cmp_$tag"; then
        results+=("$tag: same")
    else
        results+=("$tag: DIFFERENT")
    fi
done

echo
echo "Results (captures and diff images in $WORK/captures):"
printf '  %s\n' "${results[@]}"
