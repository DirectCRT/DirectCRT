# DirectCRT development plan

Status: planning, as of 22 November 2026. The repository is still named
cnc-ddraw; renaming it is part of phase 0.

## Goal

Proper, configurable scaling and CRT shading for DirectDraw games from the
Windows 98 and XP era that render at resolutions too low for today's screens,
typically 640x480 and 800x600. The shading behaves exactly like DOSBox Staging's
`crt-auto` for VGA.

Not goals:

- Supporting other shaders (libretro or otherwise).
- Direct3D games.
- Making games render at high resolutions.
- Running on Windows ME, 2000 or XP.

## Decisions

| Topic | Decision | Reason |
|---|---|---|
| Name | **DirectCRT**. Config file `directcrt.ini`. The DLL stays `ddraw.dll`, because games load it by that name. | |
| Base | Fork of cnc-ddraw. Keep its DirectDraw implementation and its game compatibility work. | Nothing else covers 2D DirectDraw this well on both Windows and Wine. |
| Behaviour | DOSBox Staging's `crt-auto`, VGA path only. The dosbox-staging repository is the source of truth, not the 86Box port. | One well-researched look instead of a shader catalogue. |
| Licence | GPL-2.0-or-later. cnc-ddraw's MIT code keeps its notice. | We port DOSBox Staging's GPL C++ logic and its GPL `sharp` shader. |
| Host systems | Windows 10 and 11, Wine, Proton. | The shaders need a modern GPU; machines that run XP natively already have CRTs. |
| Renderer | Direct3D 11. DOSBox's GLSL shaders are translated to HLSL and compiled at build time. GDI stays as an unshaded fallback. OpenGL is used only for the prototype. | Best supported API on Windows (low-latency presentation, VRR, HDR later); runs well under Wine and Proton through DXVK. Precompiled shaders avoid Wine's `d3dcompiler` replacement. |
| Settings | DOSBox Staging's setting names and meanings (`viewport`, `aspect`, `integer_scaling`, `crt_color_profile`, `color_temperature`, `black_level`, ...). One central settings file plus per-game overrides; a `directcrt.ini` in the game folder takes priority. | DOSBox users already know them; their help texts can be adapted. |
| Desktop app | Qt 6, layout inspired by DuckStation's desktop app. | DuckStation is licensed CC BY-NC-ND 4.0: take ideas only, no code, icons or text. |
| Overlay | Dear ImGui inside the DLL. | MIT licence; DuckStation uses it the same way. |

## What crt-auto does

Source: dosbox-staging, mainly `src/gui/render/auto_shader_switcher.cpp`,
`src/gui/render/auto_image_adjustments.cpp`, `RENDER_CalcDrawRectInPixels` in
`src/gui/render/render.cpp`, `resources/shaders/` and
`resources/shader-presets/crt/crt-hyllian/`.

- **A fixed pipeline**, not one shader: optional dedithering passes, then image
  adjustments (always), then the main shader.
- **Scaling is done in C++.** `viewport` (fit, a size limit, a percentage of the
  desktop, or relative), `aspect` (the mode's own pixel shape, square pixels, or
  stretch) and `integer_scaling` (auto, vertical, horizontal, off) give the image
  rectangle. "Pixels per scanline" is that rectangle's height divided by the
  game's line count.
- **`integer_scaling = auto`** applies only with CRT shaders: whole steps below
  3x, half steps (3.5x, 4.5x) from 3x to 5x, no snapping at 5x and above.
- **The main shader is chosen per video mode and window size:**

  | Screen pixels per game line | Main shader |
  |---|---|
  | 4 or more | crt-hyllian, `vga-4k` preset |
  | 3 to under 4 | crt-hyllian, `vga-1440p` preset |
  | 2 to under 3 | `vga-1080p`, or `vga-1080p-fake-double-scan` for double-scanned modes of up to 216 lines |
  | under 2 | `sharp` |

  Double-scanned modes are measured at their doubled height.
- **Automatic colour for VGA:** P22 phosphors, 7800 K white point, no black
  level boost. User settings on top: colour profile (none, EBU, P22, SMPTE-C,
  Philips, Trinitron), colour temperature and brightness preservation,
  brightness, contrast, gamma, saturation, digital contrast, black level, RGB
  gains, and the output colour space (sRGB, Display P3, DCI-P3, Rec. 2020, ...).
- **Licences:** the crt-hyllian, vga-1080p, fake-double-scan and image
  adjustment shaders are MIT; `sharp.glsl` and all of the C++ are
  GPL-2.0-or-later.

What we take:

- **Now:** the full VGA path, including fake double scan; the output colour space.
- **Later:** deinterlacing (for FMV with alternating black lines).
- **Maybe:** checkerboard dedithering.
- **Never:** CGA, EGA, Hercules, composite and arcade modes.

## Architecture

1. **`ddraw.dll`** (32-bit, C): cnc-ddraw's DirectDraw implementation with a new
   presenter.
   - Scaling, aspect ratio and shader tier are computed in C, the way DOSBox
     does it. The result is shared with mouse mapping: cnc-ddraw's hooks use
     `g_ddraw.render.viewport` throughout, so an image that only the shader
     shrank would put every click in the wrong place.
   - The shaders only do the CRT effect.
   - Double scanning is decided from the game's display mode, which the DLL
     knows (the 86Box port can't).
   - Settings reload while the game runs.
2. **Desktop app** (Qt 6, 64-bit): game list, global and per-game settings,
   installing and removing the DLL, launching games, later a preview pane that
   runs the same shaders on a still image.
3. **Overlay**: information first (game resolution, output size, scale factor,
   active shader tier, FPS), interactive settings later.

## cnc-ddraw features

| Area | Verdict |
|---|---|
| DirectDraw and Direct3D stub objects, IAMMediaStream, WinAPI/GDI/WinG hooks, DirectInput hook | Keep |
| About 280 per-game profiles in `src/config.c` | Keep; show them as per-game defaults in the app |
| Compatibility settings (`nonexclusive`, `singlecpu`, `fixchilds`, `guard_lines`, `fake_mode`, `win_version`, `hook`, ...) | Keep, on an advanced page |
| Game-specific hacks (`armadahack`, `tshack`, ...) | Keep, set by the profiles |
| Window modes, Alt+Enter, saved window position, `toggle_borderless`, `toggle_upscaled` | Keep |
| Plain "Fullscreen", which switches the monitor to the game's own resolution | Keep, but rename it so users know their monitor will receive the original game resolution and no CRT shading is possible |
| Frame limiter and VSync (`maxfps`, `limiter_type`, `maxgameticks`, `minfps`, `vsync`) | Keep |
| Mouse (`adjmouse`, cursor-unlock hotkeys, cursor fixes) | Keep, driven by the new scaling code |
| Screenshots, debug logs, crash dumps | Keep; add a choice of raw or shaded screenshots |
| `renderer` (gdi, opengl, openglcore, direct3d9, direct3d9on12, auto) | Replace with Direct3D 11 plus the GDI fallback |
| `shader`, `d3d9_filter`, libretro shader loading | Remove |
| `maintas`, `aspect_ratio`, `boxing` | Replace with `viewport`, `aspect` and `integer_scaling` |
| `inject_resolution`, `vhack` | Remove (they make games render at high resolutions) |
| XP build configurations, MinGW i486 build, old-Windows code paths | Remove |
| C++Builder config tool (`config/`) | Replace with the Qt app |

The `adjmouse-viewport` branch adds an `adjmouse_viewport` setting, a manual way
to map the mouse to a picture a shader draws smaller than the window. Moving the
scaling into C makes it unnecessary, but it's useful during the prototype.

## Phases

0. **Foundations.** Rename to DirectCRT; move to a standalone GitHub repository
   (`vvekic/cnc-ddraw` is still in upstream's fork network); switch to GPL; CI
   with Visual Studio 2026; remove the XP configurations.
1. **Prototype.** Run crt-auto in the existing OpenGL renderer
   (`reference/crt-auto.glsl` on the `adjmouse-viewport` branch is a
   single-file version) and check it with ddtest at 320x200, 320x240, 640x480
   and 800x600. Answers whether it looks right and performs well before the
   real work starts.
2. **Presenter.** Direct3D 11 renderer; build-time shader translation (glslang,
   SPIRV-Cross, then compile to bytecode and embed); the fixed pipeline; the
   scaling, tier and automatic colour logic ported to C; double-scan detection;
   DOSBox-style settings; live reload. Check through DXVK on Windows.
3. **Cleanup.** Apply the feature table; translate old ini keys to the new ones.
   Before starting, extend ddtest with DirectDraw versions 1, 2, 4 and 7
   (`DirectDrawCreate` plus `QueryInterface`, `DDSURFACEDESC`), GDI text on
   surfaces, `WaitForVerticalBlank` plus `Blt`, colour-fill and colour-key
   blits, and palette fades, so it works as a regression test.
4. **Desktop app.**
5. **Overlay.** Information first, then interactive.
6. **Later.** Deinterlacing, dedithering, HDR output.

## Testing

- **ddtest** (`tools/ddtest`): test patterns at any resolution and bit depth,
  four presentation methods, scripted runs (`--start --frames N --log FILE`)
  and PNG dumps (`--dump DIR`). See its README.
- **DXVK on Windows:** put DXVK's `d3d11.dll` and `dxgi.dll` next to
  `ddtest.exe` to run the Direct3D 11 renderer through Vulkan, as under Proton.
- **Real Linux or a Steam Deck** before releases.

## Open questions

- Which DirectDraw modes were double-scanned on VGA-class monitors: 320x200 and
  320x240 certainly; 400x300 and 512x384 need checking (probably everything
  under about 350 lines).
- The exact name of the plain fullscreen mode.
- How far to support macOS through CrossOver (best effort for now).
- Whether the `adjmouse-viewport` branch gets merged or is dropped once the
  presenter lands.
