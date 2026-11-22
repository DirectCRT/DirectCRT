# DOSBox Staging shaders

Unmodified copies of the files DirectCRT needs for `crt-auto`, at the same paths
as in the [DOSBox Staging](https://github.com/dosbox-staging/dosbox-staging)
repository. They're built into `ddraw.dll` as resources (see `res.rc`).

Copied from commit `55c25ba8fb9ef4ff4b9bd98cb8d5bd3137ebe54d` (20 November
2026). To update, copy the same files from a newer commit and update the hash
here.

| File | Used for | Licence |
|---|---|---|
| `resources/shaders/_internal/image-adjustments.glsl` | colour emulation, the first pass | MIT |
| `resources/shaders/crt/crt-hyllian.glsl` | 3 or more screen pixels per game line | MIT |
| `resources/shaders/crt/vga-1080p.glsl` | 2 to 3 pixels per line | MIT |
| `resources/shaders/crt/vga-1080p-fake-double-scan.glsl` | 2 to 3 pixels per line, double-scanned modes of up to 216 lines | MIT |
| `resources/shaders/interpolation/sharp.glsl` | under 2 pixels per line | GPL-2.0-or-later |
| `resources/shader-presets/crt/crt-hyllian/vga-1440p.preset` | crt-hyllian settings for 3 to 4 pixels per line | (data) |
| `resources/shader-presets/crt/crt-hyllian/vga-4k.preset` | crt-hyllian settings for 4 or more pixels per line | (data) |

Each shader names its authors and licence in its header. The logic that picks
the shader, the image size and the colour settings is ported to C in
`src/crt_auto.c`, from DOSBox Staging's `src/gui/render/auto_shader_switcher.cpp`,
`auto_image_adjustments.cpp` and `render.cpp` (GPL-2.0-or-later).

The files are marked `-text` in `.gitattributes`, so Git keeps them byte for
byte as they are upstream.
