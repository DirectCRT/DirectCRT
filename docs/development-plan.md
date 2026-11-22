# DirectCRT development plan

Status: phase 1 done, as of 22 November 2026.

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
| Renderer | Direct3D 11. DOSBox's GLSL shaders are translated to HLSL and compiled at build time. GDI stays as an unshaded fallback. The prototype's OpenGL renderer stays too, for crt-auto only. | Best supported API on Windows (low-latency presentation, VRR, HDR later); runs well under Wine and Proton through DXVK. Precompiled shaders avoid Wine's `d3dcompiler` replacement. OpenGL runs DOSBox's GLSL unmodified, so it's the reference for the translated shaders, and a shaded fallback where Direct3D 11 runs badly (Wine without DXVK, broken drivers). It gets the overlay, but not the features that depend on Direct3D 11 (HDR, low-latency presentation). |
| Settings | DOSBox Staging's setting names and meanings (`viewport`, `aspect`, `integer_scaling`, `crt_color_profile`, `color_temperature`, `black_level`, ...). One central settings file plus per-game overrides; a `directcrt.ini` in the game folder takes priority. | DOSBox users already know them; their help texts can be adapted. |
| User interface | No desktop app. All settings are in an on-screen overlay drawn by Dear ImGui inside the DLL. Installing is copying `ddraw.dll` into the game folder (a zip and a README; a small installer later if needed, not Qt). | CRT settings are judged by eye, so they're best changed live on the real game picture. Works wherever the DLL works, including Wine, Proton and a Steam Deck in Game Mode. Saves a second program, Qt and its deployment. Dear ImGui is MIT licensed; DuckStation uses it the same way (DuckStation is CC BY-NC-ND 4.0: take ideas only, no code, icons or text). |

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
2. **Overlay** (Dear ImGui, in the DLL), the only user interface:
   - **Status OSD**, on its own hotkey: game resolution, output size, scale
     factor, active shader tier, FPS, frame times. Configurable size and
     verbosity.
   - **Full OSD**, on another hotkey: every setting, global or for this game,
     with help texts adapted from DOSBox's, applied live (settings that need a
     restart say so). Mouse, keyboard and gamepad (XInput, for a Steam Deck).
     While it's open the game gets no input: the cursor is released and shown,
     and DirectInput, `GetAsyncKeyState` and raw input are held back. The game
     keeps running.
   - **First start** (no central settings file yet, so once per user, not per
     game): a toast for a few seconds naming the hotkeys and offering a tour
     of the OSD. Nothing modal: some games break if they lose input or focus
     while starting.
   - **Safe mode:** holding Shift while the game starts runs it with default
     settings, so a setting that stops the game starting (hooks,
     `nonexclusive`, the renderer) can be fixed in the OSD.
   - Drawn after the CRT shader, at screen resolution, with an embedded
     TrueType font scaled for the DPI. Screenshots with or without it.
   - Hotkeys configurable; defaults that old games rarely use.
   - Direct3D 11 and OpenGL through ImGui's own backends; GDI through a
     software ImGui renderer, or failing that a message pointing to
     `directcrt.ini`.

## cnc-ddraw features

| Area | Verdict |
|---|---|
| DirectDraw and Direct3D stub objects, IAMMediaStream, WinAPI/GDI/WinG hooks, DirectInput hook | Keep |
| About 280 per-game profiles in `src/config.c` | Keep; show them as per-game defaults in the OSD |
| Compatibility settings (`nonexclusive`, `singlecpu`, `fixchilds`, `guard_lines`, `fake_mode`, `win_version`, `hook`, ...) | Keep, on an advanced page |
| Game-specific hacks (`armadahack`, `tshack`, ...) | Keep, set by the profiles |
| Window modes, Alt+Enter, saved window position, `toggle_borderless`, `toggle_upscaled` | Keep. Done: `windowed=auto` (the new default) follows the game's request, a window for windowed games and fullscreen for fullscreen ones, with `fullscreen` choosing upscaled or a real mode switch; `true`/`false` still force it. Alt+Enter is no longer saved to the ini, so every start uses the configured mode. Windowed games now get the desktop's bit depth (32-bit), as with Windows' own DirectDraw, instead of a fixed 16-bit that cut 24/32-bit pictures down; `windowed_bpp=16` brings the old behaviour back. They're also shown 1:1 where they put their window: a saved window size and position (`savesettings`) no longer stretches or moves them, and none is saved from them |
| Display modes: 8, 16 and 32-bit; `SetDisplayMode` rejects 24-bit | Add 24-bit in the presenter (convert to 32-bit on upload); some games use it |
| Plain "Fullscreen", which switches the monitor to the game's own resolution | Keep, but rename it so users know their monitor will receive the original game resolution and no CRT shading is possible |
| Frame limiter and VSync (`maxfps`, `limiter_type`, `maxgameticks`, `minfps`, `vsync`) | Keep |
| Mouse (`adjmouse`, cursor-unlock hotkeys, cursor fixes) | Keep, driven by the new scaling code |
| Screenshots, debug logs, crash dumps | Keep; add a choice of raw or shaded screenshots |
| `renderer` (gdi, opengl, openglcore, direct3d9, direct3d9on12, auto) | Replace with Direct3D 11, plus OpenGL (crt-auto only, without its libretro shader loading and GL 1.x/2.x paths) and the GDI fallback |
| `shader`, `d3d9_filter`, libretro shader loading | Remove |
| `maintas`, `aspect_ratio`, `boxing` | Replace with `viewport`, `aspect` and `integer_scaling` |
| `inject_resolution`, `vhack` | Remove (they make games render at high resolutions) |
| XP build configurations, MinGW i486 build, old-Windows code paths | Remove. Done: the build configurations (DebugLogXP and DebugLogMiniXP are now DebugLog and DebugLogMini on the current toolset) and the i486 build scripts. The old-Windows code paths are left for phase 3 |
| C++Builder config tool (`config/`) | Remove in phase 3; the overlay replaces it. Until then it's out of date: it still edits `ddraw.ini` |

The `adjmouse-viewport` branch adds an `adjmouse_viewport` setting, a manual way
to map the mouse to a picture a shader draws smaller than the window. The
prototype already moved the scaling into C, so crt-auto doesn't need it.

## Phases

0. **Foundations.** Rename to DirectCRT; move to a standalone GitHub repository
   (`vvekic/cnc-ddraw` is still in upstream's fork network); switch to GPL; CI
   with Visual Studio 2026; remove the XP configurations.
   - Done: the solution, project, version resource, messages, log and dump
     names, and the settings file (`directcrt.ini`, global section
     `[directcrt]`, `DIRECTCRT_CONFIG_FILE` to point elsewhere); `LICENSE` is
     GPL-2.0 with cnc-ddraw's MIT notice in `LICENSE-cnc-ddraw.txt`; the private
     repository `vvekic/DirectCRT`.
   - CI runs on `windows-2025` with the default toolset. GitHub announced on
     20 November 2026 that this image moves to Visual Studio 2027 and that the
     `windows-2025-vs2026` label is deprecated from 1 December, so the build log
     prints which Visual Studio it used.
   - Version numbering restarts at 0.1.0.0.
1. **Prototype.** Run crt-auto in the existing OpenGL renderer
   (`reference/crt-auto.glsl` on the `adjmouse-viewport` branch is a
   single-file version) and check it with ddtest at 320x200, 320x240, 640x480
   and 800x600. Answers whether it looks right and performs well before the
   real work starts.
   - Done, differently from planned: instead of the single-file 86Box port,
     `shader=crt-auto` runs DOSBox Staging's own shader files unmodified
     (`third_party/dosbox-staging/`, built into the DLL as resources) as two
     passes, image adjustments and the main shader, with the same uniforms and
     texture sizes as DOSBox's pipeline. The shader choice, picture size
     (`viewport=fit`, `aspect=auto`, `integer_scaling=auto`), double
     scanning, pixel doubling and VGA colour defaults are in C
     (`src/crt_auto.c`, unit tests in `tests/`), and the picture rectangle is
     the viewport, so the mouse maps correctly without `adjmouse_viewport`.
     The 86Box port turned out to differ from DOSBox: it used vga-1080p for
     fake double scan, and it didn't double the image for crt-hyllian.
   - Looks right: ddtest's photo, pixel patterns and colour bars at the four
     sizes, captured from DirectCRT and from DOSBox Staging 0.84 (same shader
     files) showing the same picture, are identical to within 1/255 on a
     3840x2160 screen (vga-4k and vga-1440p) and in a 1920x1080 window (fake
     double scan, vga-1080p and sharp).
   - Performs well: at most 0.39 ms of GPU time per frame for all passes at
     2880x2160 on an RTX 5080 (vga-4k; vga-1440p 0.24 ms, vga-1080p 0.09 ms,
     sharp 0.17 ms). DebugLog builds log it. Weaker GPUs, such as a Steam
     Deck, are still to be measured.
   - Fixed on the way: `renderer=openglcore` failed on drivers whose
     `wglGetProcAddress` doesn't return `glGetIntegerv` (NVIDIA's), and fell
     back to GDI.
2. **Presenter.** Direct3D 11 renderer; build-time shader translation (glslang,
   SPIRV-Cross, then compile to bytecode and embed); the fixed pipeline; the
   scaling, tier and automatic colour logic ported to C; double-scan detection;
   DOSBox-style settings; live reload. Check through DXVK on Windows. Add a
   crt-compare mode that compares the Direct3D 11 and OpenGL renderers
   directly.
3. **Cleanup.** Apply the feature table; translate old ini keys to the new ones.
   Before starting, extend ddtest with DirectDraw versions 1, 2, 4 and 7
   (`DirectDrawCreate` plus `QueryInterface`, `DDSURFACEDESC`), GDI text on
   surfaces, `WaitForVerticalBlank` plus `Blt`, colour-fill and colour-key
   blits, and palette fades, so it works as a regression test. Also remove
   the C++Builder config tool (`config/`).
4. **Overlay**, in this order: the status OSD; the full OSD with input
   capture; safe mode; the first-start toast and the tour.
5. **Later.** HDR output first; deinterlacing and dedithering after it, at a
   lower priority.
   - HDR output, Direct3D 11 only: the extra peak brightness makes up for the
     darkening from the mask and scanlines. It needs at least 10 bits per
     channel, so add 10-bit SDR output with it (an `R10G10B10A2` swap chain and
     matching framebuffers between passes): it keeps the colour adjustments'
     steps even and the scanlines' dark gradients smooth. Both are options;
     the default stays 8-bit like DOSBox, which is what crt-compare checks.

## Testing

- **ddtest** (`tools/ddtest`): test patterns at any resolution and bit depth,
  four presentation methods, scripted runs (`--start --frames N --log FILE`)
  and PNG dumps (`--dump DIR`). See its README.
- **crt-compare** (`tools/crt-compare`): shows the same pictures in DirectCRT
  and DOSBox Staging, captures both and compares them pixel by pixel. The
  Direct3D 11 renderer has to pass it too.
- **DXVK on Windows:** put DXVK's `d3d11.dll` and `dxgi.dll` next to
  `ddtest.exe` to run the Direct3D 11 renderer through Vulkan, as under Proton.
- **Real Linux or a Steam Deck** before releases.

## Open questions

- Which DirectDraw modes were double-scanned on VGA-class monitors. For now
  DirectCRT follows DOSBox Staging's S3 BIOS: up to 384 lines are double
  scanned and up to 512 pixels wide are pixel doubled (320x200, 320x240,
  400x300 and 512x384 become 640x400, 640x480, 800x600 and 1024x768). Real
  Windows 9x drivers may have differed.
- The exact name of the plain fullscreen mode.
- How far to support macOS through CrossOver (best effort for now).
- Whether the `adjmouse-viewport` branch gets merged or is dropped once the
  presenter lands.
