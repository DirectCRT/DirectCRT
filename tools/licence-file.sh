#!/usr/bin/env bash
# Writes DirectCRT-LICENSE.txt for the release zips: DirectCRT's licence and
# the licences of everything built into ddraw.dll, in one file, so a game's
# folder gets one licence file named for DirectCRT. The texts come from the
# repo's own licence files, so they stay as the code they cover has them.
# CI runs it from the repo's root:
#
#   bash tools/licence-file.sh <out>

set -euo pipefail

shaders=third_party/dosbox-staging/resources/shaders

section()
{
    printf '\n\n========================================================================\n'
    printf '%s\n' "$1"
    printf '========================================================================\n\n'
}

{
    cat <<'EOF'
DirectCRT's licences

DirectCRT as a whole is licensed under the GNU General Public License,
version 2 or (at your option) any later version (1). The code that comes
from cnc-ddraw is also available under its original MIT licence (2). The
third-party code and fonts built into ddraw.dll keep their own licences
(3 to 9).

1. DirectCRT: GNU General Public License, version 2 or later
2. cnc-ddraw: MIT
3. DOSBox Staging's shaders: MIT (sharp, nearest and bilinear are
   GPL-2.0-or-later, as in 1)
4. Microsoft Detours: MIT
5. Dear ImGui: MIT
6. LodePNG: zlib
7. Khronos OpenGL headers: MIT-style
8. Source Sans 3, the OSDs' font: SIL Open Font License 1.1
9. Material Icons, the OSDs' icons: Apache License 2.0
EOF

    section "1. DirectCRT: GNU General Public License, version 2 or later"
    cat LICENSE

    section "2. cnc-ddraw: MIT"
    cat LICENSE-cnc-ddraw.txt

    section "3. DOSBox Staging's shaders: MIT"
    echo "crt-hyllian, vga-1080p, vga-1080p-fake-double-scan and image-adjustments."
    echo
    grep -h "SPDX-FileCopyrightText" "$shaders/crt/crt-hyllian.glsl" \
        "$shaders/crt/vga-1080p.glsl" "$shaders/crt/vga-1080p-fake-double-scan.glsl" \
        "$shaders/_internal/image-adjustments.glsl" |
        sed -E 's#^// SPDX-FileCopyrightText: +#Copyright (c) #' | sort -u
    echo
    sed -n '/^Permission/,$p' third_party/imgui/LICENSE.txt

    section "4. Microsoft Detours: MIT"
    cat src/detours/LICENSE.md

    section "5. Dear ImGui: MIT"
    cat third_party/imgui/LICENSE.txt

    section "6. LodePNG: zlib"
    sed -n '/^Copyright (c) 2005/,/distribution\./p' src/lodepng.c

    section "7. Khronos OpenGL headers: MIT-style"
    sed -n '/^\*\* Copyright/,/USE OR OTHER DEALINGS IN THE MATERIALS\./p' inc/glcorearb.h |
        sed -E 's/^\*\* ?//'

    section "8. Source Sans 3, the OSDs' font: SIL Open Font License 1.1"
    cat third_party/fonts/source-sans-3/LICENSE.md

    section "9. Material Icons, the OSDs' icons: Apache License 2.0"
    cat third_party/fonts/material-icons/LICENSE
} > "$1"
