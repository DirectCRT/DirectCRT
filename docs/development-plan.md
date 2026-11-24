# DirectCRT development plan

Status: phase 3 started (the display settings are done), as of 23 November 2026.

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
| Renderer | Direct3D 11. DOSBox's GLSL shaders are translated to HLSL and compiled at build time. No GDI renderer: where neither Direct3D 11 nor OpenGL works, DirectCRT steps aside for Windows' own DirectDraw. The prototype's OpenGL renderer stays too, for crt-auto only. | Best supported API on Windows (low-latency presentation, VRR, HDR later); runs well under Wine and Proton through DXVK. Precompiled shaders avoid Wine's `d3dcompiler` replacement. OpenGL runs DOSBox's GLSL unmodified, so it's the reference for the translated shaders, and a shaded fallback where Direct3D 11 runs badly (Wine without DXVK, broken drivers). It gets the overlay, but not the features that depend on Direct3D 11 (HDR, low-latency presentation). |
| Settings | DOSBox Staging's setting names and meanings (`viewport`, `aspect`, `integer_scaling`, `crt_color_profile`, `color_temperature`, `black_level`, ...). One `directcrt.ini` per game, next to its `ddraw.dll`, with one section, `[directcrt]`; no per-game sections. | DOSBox users already know them; their help texts can be adapted. |
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
   - **Full OSD**, on another hotkey: every setting,
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
   - Direct3D 11 and OpenGL through ImGui's own backends. There's no GDI
     renderer: where neither works, DirectCRT steps aside (see phase 4).

## cnc-ddraw features

| Area | Verdict |
|---|---|
| DirectDraw and Direct3D stub objects, IAMMediaStream, WinAPI/GDI/WinG hooks, DirectInput hook | Keep |
| About 280 per-game profiles in `src/config.c` | Removed in phase 3: they filled `directcrt.ini` with sections nobody reads. Sections named after a game's executable were kept until the settings OSD, then dropped: the ini is already the game's own |
| Compatibility settings (`nonexclusive`, `singlecpu`, `fixchilds`, `guard_lines`, `fake_mode`, `win_version`, `hook`, ...) | Keep, on an advanced page |
| Game-specific hacks (`armadahack`, `tshack`, ...) | Removed in phase 3 with the profiles, which were the only thing that set them. The fixes cnc-ddraw applies by itself (Battle.net dialogs, StarCraft's and Red Alert's cursor, Age of Empires II's text boxes, Claw) stay |
| Window modes, Alt+Enter, saved window position, `toggle_borderless`, `toggle_upscaled` | Done in phase 3: replaced with `display` and the `window_*` settings (see [Display settings](#display-settings)). Windowed games keep the desktop's bit depth (32-bit) from phase 1; `windowed_bpp=16` brings the old 16-bit back |
| Display modes: 8, 16, 24 and 32-bit | Done: 24-bit added (Direct3D 11 converts to 32-bit on upload, OpenGL and GDI take it as is); some games use it |
| Plain "Fullscreen", which switches the monitor to the game's own resolution | Keep as `display=original`, opt-in, with help text saying the monitor gets the game's resolution and there's no CRT shading |
| Frame limiter and VSync (`maxfps`, `limiter_type`, `maxgameticks`, `minfps`, `vsync`) | Keep |
| Mouse (`adjmouse`, cursor-unlock hotkeys, cursor fixes) | Keep, driven by the new scaling code |
| Screenshots, debug logs, crash dumps | Keep. Done: the screenshot key saves both the raw and the shaded picture |
| `renderer` (gdi, opengl, openglcore, direct3d9, direct3d9on12, auto) | Done: Direct3D 11, OpenGL (crt-auto's pipeline only, in a 3.3 core context, without the libretro shader loading and GL 1.x/2.x paths). Direct3D 9 and GDI are removed; `openglcore`, `direct3d9`, `direct3d9on12`, `gdi` and `software` read as `auto` |
| `shader`, `d3d9_filter`, libretro shader loading | Done: `shader` has DOSBox's values `crt-auto`, `sharp`, `nearest` and `bilinear`, all through crt-auto's pipeline; the rest is removed |
| `width`, `height`, `maintas`, `aspect_ratio`, `boxing` | Done: replaced with `window_size`, `viewport`, `aspect` and `integer_scaling`, for every shader |
| `inject_resolution`, `vhack` | Done: removed (they make games render at high resolutions) |
| XP build configurations, MinGW i486 build, old-Windows code paths | Done: removed. DebugLogXP and DebugLogMiniXP are now DebugLog and DebugLogMini on the current toolset; the last old-Windows path, a Windows 7 workaround in the frame limiter, went in phase 3. The MinGW Makefile and the code only it compiled (the `__GNUC__` branches) went in phase 4: MSBuild is the only build |
| C++Builder config tool (`config/`) | Done: removed in phase 3; the overlay replaces it |
| `min_font_size` | Done: removed in phase 3. It made text readable at the high resolutions `inject_resolution` gave games |

The `adjmouse-viewport` branch adds an `adjmouse_viewport` setting, a manual way
to map the mouse to a picture a shader draws smaller than the window. The
prototype already moved the scaling into C, so crt-auto doesn't need it.

## Display settings

Phase 3 replaces cnc-ddraw's window and scaling settings with fewer ones that
answer three questions in order: fullscreen or a window, how big, and what the
picture looks like. Where DOSBox Staging has a setting for the same thing, it
has its name.

Why:

- `windowed` and `fullscreen` give six combinations. `fullscreen` means
  "upscaled or a real mode switch" in some and "borderless" in another.
- The default, `fullscreen=false`, switches the monitor to the game's
  resolution. That leaves crt-auto one screen pixel per line, so with
  `shader=crt-auto` there's silently no CRT effect, only `sharp`. Games in
  their own window are shown 1:1, with the same result.
- `width`, `height`, `maintas`, `aspect_ratio`, `boxing` and `d3d9_filter`
  overlap with `viewport`, `aspect` and `integer_scaling`, or crt-auto ignores
  them. `toggle_borderless` and `toggle_upscaled` change what Alt+Enter does.
  The window settings are scattered (`posX`, `posY`, `center_window`,
  `border`, `resizable`, `savesettings=0/1/2`).

### The settings

```ini
display=auto            ; auto, window, fullscreen, original
window_size=default     ; default (= medium), small, medium, large, unscaled, WxH, WxHpx
window_position=auto    ; auto, X,Y, X,Ypx, X,Y%
window_decorations=on
window_resizable=on
remember_window=off     ; off, on
shader=crt-auto         ; crt-auto, sharp, nearest, bilinear
aspect=auto
integer_scaling=auto
viewport=fit
image_adjustments=on    ; and the other colour settings, unchanged
renderer=auto           ; auto (Direct3D 11, then OpenGL), direct3d11, opengl
```

### `display`

| Value | Fullscreen games | Games in their own window | Alt+Enter goes to |
|---|---|---|---|
| `auto` (default) | Fullscreen at the desktop resolution | A window | The other one |
| `window` | A window | A window | Fullscreen at the desktop resolution |
| `fullscreen` | Fullscreen at the desktop resolution | Fullscreen at the desktop resolution (rarely wanted; kept for compatibility) | A window |
| `original` | The monitor switches to the game's resolution | A window | The other one, with fullscreen switching the monitor's mode |

- **Fullscreen at the desktop resolution** scales the picture to the screen.
  It's what `windowed=auto` with `fullscreen=true` does now.
- **`original`** is what Windows' own DirectDraw did, and DirectCRT's default
  until now. The monitor, or the graphics card's scaler as set in its driver's
  control panel, stretches the picture to the panel. It's for real CRTs,
  scalers such as a RetroTINK or OSSC, people who prefer their monitor's
  scaling, and the lowest latency.
  - With one screen pixel per line, crt-auto shows `sharp`, which at 1:1
    changes nothing. The colour adjustments still apply, because they run
    before the shader; the help text says so.
  - When the monitor doesn't offer the mode (320x200 usually isn't), cnc-ddraw's
    fallback stays: 2x, then the smallest mode with the game's shape, then the
    smallest with the desktop's shape, then the desktop's own. So the help
    text says "or the closest mode the monitor has". At 2x, crt-auto gets two
    pixels per line and shows `vga-1080p`.
  - Where a mode change fails without an error (Wayland under Wine), it falls
    back to fullscreen at the desktop resolution, as now.
  - The refresh rate is the game's, or `refresh_rate`.
  - The name comes from old DOSBox's `fullresolution`, whose `original` and
    `desktop` made the same choice.
- **One setting decides two things,** how the game starts and which fullscreen
  Alt+Enter goes to. The combination it can't give is "start in a window, with
  Alt+Enter going to the game's resolution", which is rare.

### Windows

One rule for every window, whether the game asked for fullscreen or a window:
the window's client area is `window_size`, and `viewport`, `aspect` and
`integer_scaling` place the picture in it, as in fullscreen. So there can be
black borders in the window, as in DOSBox.

- **`window_size`** has DOSBox's values: `small`, `medium` and `large` are 4:3
  sizes relative to the desktop height (66.67x50%, 98.67x74% and 120x90%);
  `WxH` is in logical units and `WxHpx` in pixels. `default` is `medium`: on a
  3840x2160 screen about 2131x1598, which gives 640x480 3.3 pixels per line
  and `vga-1440p`. One value is new: **`unscaled`**, the game's own resolution
  at 1:1, for games that misbehave when scaled.
- **Games in their own window** are scaled too. cnc-ddraw's hooks
  (`GetClientRect`, `GetWindowRect`, `ClientToScreen`, `ScreenToClient`,
  `SetWindowPos`, `MoveWindow` and the cursor functions) keep telling the game
  its window is its own size and map the mouse back. That's how cnc-ddraw has
  always shown them, but stretched to the saved window size and at 16-bit; on
  22 November they became 1:1 at the desktop's bit depth to fix that. The bit
  depth stays, and the scaling comes back through `window_size` instead of a
  saved size.
  - The window is centred where the game put its own, kept on the screen,
    unless `window_position` says otherwise.
  - When the game resizes its own window, for example from a zoom option in
    its menu, the new size isn't treated as a new resolution. That isn't
    planned until a game needs it (see phase 3).
  - `fixchilds` still turns scaling off when it finds GDI child windows, so
    those games are shown 1:1, as now.
- **The user can resize the window,** smaller than the game too, as now.
  `remember_window` saves its size and position when the game exits, for both
  kinds of game. Alt+Enter's state still isn't saved.
- **`window_position`:** `auto` centres the window, on the game's own window
  or on the screen. It replaces `posX`, `posY` and `center_window`; when the
  game changes resolution, the window keeps its centre.

### Picture

- **`shader`** takes DOSBox's values `crt-auto`, `sharp`, `nearest` and
  `bilinear`. They all use the layout code in `src/crt_auto.c`, with
  `viewport`, `aspect` and `integer_scaling`, which is why the old scaling
  settings can go. As in DOSBox, `integer_scaling=auto` only snaps with
  crt-auto, and the colour adjustments apply with every shader.
- **No `renderer=gdi`**: it read as `auto` (see phase 4).
- **Live reload:** `shader` joins the settings reloaded while the game runs.
  `display`, the `window_*` settings and `renderer` apply at the next start.

### Removed

`windowed`, `fullscreen`, `width`, `height`, `maintas`, `aspect_ratio`,
`boxing`, `d3d9_filter`, libretro shader paths, `toggle_borderless`,
`toggle_upscaled`, `posX`, `posY`, `center_window`, `border`, `resizable`,
`savesettings`, and the renderers `openglcore`, `direct3d9` and
`direct3d9on12`. (`inject_resolution` and `vhack` go too; see the feature
table.)

### Old settings

An old key is read when its new one isn't set, with a line in the log. The
generated `directcrt.ini` only has the new keys.

| Old | New |
|---|---|
| `windowed=auto` (with either `fullscreen`) | `display=auto` |
| `windowed=true` | `display=window` |
| `windowed=true` with `fullscreen=true` (borderless) | `display=fullscreen` |
| `windowed=false` (with either `fullscreen`) | `display=fullscreen` |
| `width`, `height` (not 0) | `window_size=WxHpx` |
| `boxing=true` | `integer_scaling=vertical` |
| `maintas`, `aspect_ratio`, `d3d9_filter`, `toggle_borderless`, `toggle_upscaled`, `center_window` | Dropped |
| `shader=Nearest neighbor`, `Bilinear` | `shader=nearest`, `bilinear` |
| Other `shader` values (Bicubic, Lanczos, xBR, shader files) | `shader=crt-auto` |
| `border`, `resizable` | `window_decorations`, `window_resizable` |
| `posX`, `posY` | `window_position=X,Ypx` (`-32000`: `auto`) |
| `savesettings=0`, `1`, `2` | `remember_window=off`, `on`, `on` |
| `renderer=openglcore`, `direct3d9`, `direct3d9on12` | `renderer=auto` |

Old keys never give `original`. `windowed=false` with `fullscreen=false` was a
mode switch, and it's cnc-ddraw's default, so a copied cnc-ddraw config would
otherwise keep the no-shading trap. `original` has to be chosen.

### Testing

- Unit tests for parsing `display` and `window_size`, the window size
  computation, and the translation of old keys.
- ddtest: its windowed presentation methods open scaled, and its README and
  help text talk about `display` instead of `windowed`.
- crt-compare: the windowed cases set `display=window` and
  `window_size=1920x1080px`, the fullscreen cases `display=fullscreen`. All
  eight cases, on both renderers, again.
- By hand: a game in its own window; Alt+Enter with each `display` value;
  `original` at 320x200 and 640x480; `remember_window`.

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
   - Done: the Direct3D 11 renderer (`renderer=direct3d11`, and the default
     for crt-auto with `renderer=auto`) matches DOSBox in all eight
     crt-compare cases.
   - Done: shader translation. `tools/shaders/gen_d3d11_shaders.py` turns
     DOSBox's GLSL into HLSL and bytecode in `src/d3d11_shaders/`, which is
     committed, so a normal build needs neither the tools nor a shader
     compiler. fxc needs `/Gis`: without it, it simplifies float maths such as
     `(x / y) * y` to `x`, but the OpenGL driver keeps the round trip, and
     its rounding decides which side of a texel boundary some pixels fall
     (the fake double scan shader).
   - Done: DOSBox-style settings. `aspect`, `integer_scaling`, `viewport`
     (all forms: `fit`, `WxH`, `WxHpx`, `N%`, `relative H% V%`),
     `image_adjustments`, `crt_color_profile`, `brightness`, `contrast`,
     `gamma`, `digital_contrast`, `black_level`, `saturation`,
     `color_temperature`, `color_temperature_luma_preserve` and the RGB gains,
     with DOSBox's values, defaults and invalid-value handling, in
     `src/crt_auto.c` with unit tests. crt-compare's `SETTINGS` sets them in
     both programs; non-default sets match DOSBox. One known difference is
     noise: the `sharp` shader at exactly 1x samples on texel centres, and
     each GPU path's filter rounding leaks up to 4/255 (OpenGL) or 9/255
     (Direct3D 11) of a neighbouring line into fine 1-pixel patterns. DOSBox
     itself has more of it than DirectCRT's OpenGL renderer.
   - A deliberate difference from DOSBox: the defaults are what DOSBox's
     `auto` picks for VGA, written out: `crt_color_profile=p22`,
     `black_level=0` and `color_temperature=7800`. `auto` is still read,
     and means the same. That makes the colour temperature and the black
     level plain sliders in the settings OSD.
   - A deliberate difference from DOSBox: `aspect=auto` shows only the low
     resolution VGA modes (up to 720 wide, below 480 lines, no wider than
     16:10, such as 320x200 and 640x400) at 4:3. Every other mode has square
     pixels, so 1280x1024 is shown at 5:4 (DOSBox stretches it to 4:3), as are
     in-between shapes such as 1280x800. ddtest's geometry pattern uses the
     same rule.
   - Done: live reload of crt-auto's settings. A timer on the game's window
     checks `directcrt.ini` every half second; once a change has settled, it
     reads those settings again and, if they differ, sets the picture up again
     (as Alt+Enter does), so the shader tier, picture size, mouse mapping and
     colours follow. Other settings (`shader`, `renderer`, ...) still need a
     restart; the overlay's full OSD will apply them live.
   - Done: a CI job (`shaders` in `build.yml`) checks that the committed
     shaders match a fresh translation (`gen_d3d11_shaders.py check`). The
     tool downloads are pinned by SHA-256, and fxc by Windows SDK version
     (`FXC_SDK`), so the bytecode doesn't change with the SDKs installed.
   - Done: crt-compare compares DirectCRT's renderers with each other
     (`REFERENCE=opengl` or `direct3d11`, no DOSBox needed), and runs the
     tested one through DXVK (`DXVK=<x32 folder>`). Direct3D 11 vs OpenGL:
     the same in all eight cases. One windowed 800x600 run (`sharp`) was off
     by up to 3/255 in 1-pixel horizontal patterns; three reruns matched, so
     it's the `sharp` noise described above. DXVK 3.1.1 vs Windows' Direct3D
     11 (RTX 5080): the same in all eight cases. The NVIDIA app's performance
     overlay, when it's on, draws over the bottom of the first fullscreen
     case's capture; that case then shows as different.
3. **Cleanup.** First the new display settings (see
   [Display settings](#display-settings)). Then apply the rest of the feature
   table. Before that part, extend ddtest with DirectDraw versions 1, 2, 4 and 7
   (`DirectDrawCreate` plus `QueryInterface`, `DDSURFACEDESC`), GDI text on
   surfaces, `WaitForVerticalBlank` plus `Blt`, colour-fill and colour-key
   blits, and palette fades, so it works as a regression test. Also remove
   the C++Builder config tool (`config/`).
   - Done: the display settings, as described above, with unit tests
     (`tests/test_display_settings.c`) and the old keys translated. Every
     renderer uses crt-auto's layout and pipeline; `renderer=gdi` scales
     with nearest neighbour into the same viewport. A mode switch that fails
     falls back to borderless fullscreen. The game profiles for ctp2, Jazz2,
     Jazz2_NonPlus and KKNDgame only set removed keys and are gone.
     Battle.net's dialogs are shown 1:1. Alt+Page Down makes the window fill
     the work area, with the picture placed in it like in any window, instead
     of a size with the game's shape. Windows are at least 320x200.
   - Done: crt-compare takes DOSBox's own rendered screenshot instead of a
     screen capture, using DOSBox's web server to see when the picture is up
     and to shut it down (`dosshot.ps1`). With the new settings
     (`display=fullscreen`; `display=window` with `window_size=1920x1080px`)
     all eight cases are the same as DOSBox on both renderers, now with no
     offset between the pictures.
   - Done: the sweep of features that don't serve DirectCRT's goal. The
     built-in game profiles, the game-specific hacks they set,
     `min_font_size`, the Windows 7 workaround, the C++Builder config tool
     and the list of games with profiles are gone. A game's own section was
     only `[game]` then (cnc-ddraw also read `[game/wine]`, `[game/2]` to
     `[game/9]` and `checkfile`, which only the profiles used); with the
     settings OSD, game sections went too (see phase 4).
   - Done: ddtest as a regression test. `--ddraw 1|2|4|7` picks the
     interface (`DirectDrawCreate` plus `QueryInterface`, `DDSURFACEDESC`
     before version 4), `--present vblank` waits for the vertical blank and
     `Blt`s, the `fade` pattern fades the palette, and the `blits` pattern
     does colour fills, colour-keyed `Blt` and `BltFast`, and GDI text on the
     surface, then reads the surface back and checks it (exit code 4 when it
     fails). `tools/ddtest/regress.ps1` runs it across versions, methods and
     depths: all 52 runs pass on DirectCRT, and on Windows' own DirectDraw
     in the modes it offers. Deliberately wrong colour-key and fill values
     make the check fail.
   - Done: 24-bit display modes. `SetDisplayMode` accepts 24, and
     `EnumDisplayModes` lists 24-bit when the game asks for it, is already
     in a 24-bit mode, or with `resolutions=full`, so other games' mode
     lists don't change. Blits, colour fills and colour keys work on
     3-byte pixels; Direct3D 11 converts to BGRA on upload, OpenGL uploads
     `GL_BGR`, GDI draws the DIB as is, and screenshots save it. The picture
     matches 32-bit on all three renderers, and `regress.ps1` now covers
     24-bit too (68 runs).
   - Done: screenshots. The key saves two PNGs of the same frame:
     `<game>_<time>_raw.png`, the game's own surface (8-bit with its
     palette), and `<game>_<time>_crt.png`, the finished frame as shown,
     at the window's or screen's size. The render thread takes both, reading
     the frame back before it's presented (a staging texture, `glReadPixels`,
     or a copy of the window for GDI), and a worker thread encodes them so
     a 4K PNG doesn't stall the game. The key wakes the render thread, so it
     works while the game isn't drawing. The shaded shots from Direct3D 11
     and OpenGL are the same to within 1 in 99.99% of pixels; the rest is
     ddtest's frame counter. The shaded shot is taken without the overlay.
   - Not planned: when a game resizes its own window, making the new size
     its new resolution. Only games in their own window can do it, mostly
     windowed-only games with a zoom option, and none we know of needs it.
     It waits for a real game to design against. Games in their own window
     stay as they are: scaled by `window_size`, a window with `display=auto`
     (the default) and fullscreen only with `display=fullscreen` or
     Alt+Enter.
     A "virtual desktop" (a fixed 1024x768 screen, shaded as a whole, with
     the game's windows in it) was considered and rejected: the title bar,
     menus and dialogs are real windows drawn by Windows, so it would mean
     capturing them and routing input back, a window manager of its own. A
     PC emulator running Windows 98 gives that experience.
4. **Overlay**, in this order: the status OSD; the full OSD with input
   capture; safe mode; the first-start toast and the tour.
   - Done: Dear ImGui 1.93.0 in `third_party/imgui`, unmodified, with its
     Direct3D 11 and OpenGL 3 backends and no platform backend. The overlay
     is drawn after the screenshot is read back and before Present, at the
     window's or screen's size. `src/overlay_status.c` (C) keeps the state
     and the timings and writes the rows; `src/overlay.cpp` (C++) draws them.
     The ImGui backend is set up on first use and shut down with the
     renderer's resources, so Alt+Enter and device loss rebuild it.
     `d3dcompiler_47.dll`, which ImGui's Direct3D 11 backend needs, is
     delay-loaded and checked first; without it there's no OSD, not a crash.
   - Done: the status OSD, on Alt+F11 (`keystatusosd`). Basic: three lines,
     the game's mode and refresh rate and the picture's size and scale, the
     shader and renderer, the frame rate and frame time. Full: a table (game,
     screen, picture, scale with double scanning and pixel doubling, shader
     and its pixels per line, renderer and vsync, the emulated refresh rate
     and where it comes from next to the monitor's, what the vertical blank
     follows (`maxgameticks`), frame rate of the game's pictures
     and of presents, average and slowest frame time, the crt-auto passes'
     GPU time) and a graph of the last 240 frame times. The hotkey saves
     whether it's shown (`status_osd`), so it's
     back the next time the game starts. The numbers cover
     half a second each; the frame times are between the game's new
     pictures, not presents. `status_osd_size` (13, 16 or 20 px, times the
     display scale, at most a 22nd of the picture's height),
     `status_osd_detail`, `status_osd_position` and `status_osd_opacity`
     (the background's, 75% by default; the text stays solid). While it's shown the
     render thread wakes at least 4 times a second, so it updates while the
     game is waiting; GPU timing queries run only while it's shown (always
     in debug builds).
   - Done: the settings OSD, on Alt+F12 (`keysettingsosd`). One table of
     settings (`src/settings.c`: name, tab, kind, values, default, help,
     whether it applies live) now writes the default `directcrt.ini`, its
     comments included, and the OSD's tabs (Display, Picture, Colour,
     Timing, Overlay, Other, Compatibility, Hotkeys). A row per setting:
     a checkbox, radio buttons for a fixed set of values, a slider and a
     box to type the number in (with "a number" next to named values), a
     text field (with "other" next to named values) or a hotkey button that
     takes the next key pressed. A slider can move in steps (100 K for the
     colour temperature, 5% for the status OSD's opacity: the nearest step
     while it's dragged, the next one with the keyboard); a number typed in
     needn't be on one. Settings can share a row (the red, green and blue
     gain, each slider in its colour), and headings split a tab into groups
     (the Colour tab: Monitor, Colour balance, the game's colours), with
     a tooltip of their own. With `image_adjustments` off, the rest of the
     Colour tab is greyed out. A
     tooltip under the mouse or keyboard focus gives the help: on a radio
     button, that value's own line of the help (the help is written one
     value per line, `value = text`); on the row's label or a slider, the
     whole help. Under it, the ini line, the default and whether it applies
     now or after a restart (also marked `*` in the row).
     It writes to `[directcrt]`, the only section: the ini is the game's
     own, next to its `ddraw.dll`, so there's no choice between all games
     and this one (sections named after a game, as in cnc-ddraw, aren't
     read any more: a value there hid a change made in the OSD).
     Every row has a reset button (a circular arrow; dim when it's already
     the default) that writes the default; its tooltip says which.
     Each change is written to `directcrt.ini` at once and applied as when
     the file is saved (`WM_OVERLAY_MENU`, then `dd_reload_settings`).
     While it's open the game keeps running and drawing but gets no keyboard
     or mouse input: the window procedure and the keyboard hook queue it for
     the OSD instead, `GetKeyState`, `GetAsyncKeyState` and DirectInput's
     keyboard and mouse report nothing, the cursor is unlocked and shown,
     and on closing it's locked again and the game gets the releases of keys
     it saw go down. Esc or the hotkey closes it. Both OSDs are in Source
     Sans 3 (`third_party/fonts`, built in as a resource; Adobe's unmodified
     TTF, so its Reserved Font Name stays), rasterised 1.2 times the asked
     size, since its size counts accents and descenders. Its text is 16 px times
     the display scale (13 to a 30th of the picture's height); the status
     OSD keeps its own size, and stays visible, while it's open.
     Not yet: it shows only the settings' own names, not the legacy keys
     some older ini files use.
   - Done: no GDI renderer. GDI can't draw the shaders or the overlay, and
     a picture without them isn't what DirectCRT is for. At the first
     `DirectDrawCreate` DirectCRT probes the renderers in order (Direct3D
     11 then OpenGL, or OpenGL first with `renderer=opengl`): a Direct3D 11
     device on the graphics card, else on WARP (the OSD then says
     "Direct3D 11 (WARP)"), and an OpenGL 3 context on a hidden window.
     About 75 ms when Direct3D 11 works. When neither does, DirectCRT steps
     aside: its hooks are undone, a message box says so once, and
     `DirectDrawCreate(Ex)`, `CoCreateInstance`, `DirectDrawCreateClipper`
     and `DirectDrawEnumerate*` all go to Windows' own `ddraw.dll` from the
     system folder. The DPI awareness and timer resolution set at load
     stay. A renderer that fails later, on the render thread, switches to
     the other one for the rest of the run; if both fail, the window is
     black with a message saying why. Tested by faking `d3d11.dll` and
     `opengl32.dll` away in the game's folder, and with forced setup
     failures. (Windows' own DirectDraw draws black with those fakes too,
     since it uses Direct3D itself.)
   - Done, outside the plan: the virtual beam (`src/vbeam.c`), the emulated
     display's refresh on one fixed timeline. Each refresh is the active lines
     and VGA's share of vertical blank (45 of 525 lines).
     `WaitForVerticalBlank`, `GetVerticalBlankStatus`, `GetScanLine` (was
     unsupported) and `GetFlipStatus` read it. `Flip` with `DDFLIP_WAIT`
     behaves as on hardware: it waits only while the previous flip is still
     pending, and drawing with a wait flag into the buffer still on screen
     waits until the flip is shown. Triple buffering doesn't wait, and a
     double-buffered game with slow frames gets 30 or 20 fps, as on a real
     card. BG2, which busy-polls, went from 98.5% to 74% of a core (Windows'
     own DirectDraw: 87%), with no visible difference. `emulated_refresh`:
     `auto`, the rate the game asks for in `SetDisplayMode` (60 if it
     doesn't ask), or a fixed rate from 24 to 240 Hz. The game gets that
     rate wherever it used to get 60. Your monitor's rate doesn't change.
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
  five presentation methods, DirectDraw interface versions 1, 2, 4 and 7,
  scripted runs (`--start --frames N --log FILE`) and PNG dumps
  (`--dump DIR`). Its blits pattern checks DirectDraw's and GDI's drawing on
  the surface, and `tools/ddtest/regress.ps1` runs that across every
  combination as a regression test. See its README.
- **crt-compare** (`tools/crt-compare`): shows the same pictures in DirectCRT
  and DOSBox Staging, captures both and compares them pixel by pixel. Both
  renderers have to pass it, and match each other (`REFERENCE=opengl`).
- **DXVK on Windows:** put DXVK's `d3d11.dll` and `dxgi.dll` next to
  `ddtest.exe` to run the Direct3D 11 renderer through Vulkan, as under Proton.
  crt-compare does this with `DXVK=<x32 folder>` and compares the result with
  Windows' own Direct3D 11.
- **Real Linux or a Steam Deck** before releases.

## Open questions

- Which DirectDraw modes were double-scanned on VGA-class monitors. For now
  DirectCRT follows DOSBox Staging's S3 BIOS: up to 384 lines are double
  scanned and up to 512 pixels wide are pixel doubled (320x200, 320x240,
  400x300 and 512x384 become 640x400, 640x480, 800x600 and 1024x768). Real
  Windows 9x drivers may have differed.
- How far to support macOS through CrossOver (best effort for now).
- Whether the `adjmouse-viewport` branch gets merged or is dropped once the
  presenter lands.
