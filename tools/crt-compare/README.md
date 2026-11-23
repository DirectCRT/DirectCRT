# crt-compare

Checks that DirectCRT's `shader=crt-auto` looks exactly like DOSBox Staging's
`crt-auto`. It shows the same pictures in both, captures the screen and
compares the captures pixel by pixel.

```
tools/crt-compare/run.sh WORKDIR [CONFIGURATION] [CASE...]
```

For example, from Git Bash in the repository: `tools/crt-compare/run.sh ../crt-work`.
It tests `bin/<CONFIGURATION>` (default `Release`; use a build that doesn't
draw debug text over the picture). Each case is `fs:WxH`, fullscreen on the
primary monitor (`display=fullscreen`), or `win:WxH`, in a 1920x1080 window
(`display=window`, `window_size=1920x1080px`), for 320x200, 320x240,
640x480 and 800x600. Without cases it runs all eight, which takes about five
minutes. Leave the computer alone while it runs: the captures are of the
screen.

Set `SETTINGS` to test other crt-auto settings than DOSBox's defaults. They're
set in both the `directcrt.ini` and DOSBox's `[render]` section, separated by
`;`, for example:

```
SETTINGS='viewport=relative 120% 90%;aspect=stretch' tools/crt-compare/run.sh ../crt-work Release fs:320x200
```

Set `RENDERER=opengl` or `RENDERER=direct3d11` to test one renderer.

Set `REFERENCE=opengl` or `REFERENCE=direct3d11` to compare with DirectCRT
using that renderer instead of DOSBox, for example the two renderers directly
(DOSBox isn't needed then):

```
RENDERER=direct3d11 REFERENCE=opengl tools/crt-compare/run.sh ../crt-work
```

Set `DXVK` to a folder with DXVK's 32-bit `d3d11.dll` and `dxgi.dll` (the
`x32` folder of a [release](https://github.com/doitsujin/dxvk/releases)) to run
the tested DirectCRT's Direct3D 11 through Vulkan, as under Wine and Proton.
The reference doesn't get them, so this compares DXVK with Windows' own
Direct3D 11:

```
DXVK=../dxvk-3.1.1/x32 RENDERER=direct3d11 REFERENCE=direct3d11 tools/crt-compare/run.sh ../crt-work
```

The summary says whether DXVK was used, and its version (DXVK writes
`ddtest_d3d11.log` and `ddtest_dxgi.log` next to the program). DXVK also keeps
a shader cache in `%LOCALAPPDATA%\dxvk`.

Turn off overlays that draw over the screen, such as the NVIDIA app's
performance overlay: they end up in the captures, and the case shows as
different.

At the end it lists each case as `same` or `DIFFERENT`. The captures, a
difference image (`cmp_*_diff.png`, differences x 8) and side-by-side crops
(`cmp_*_crops.png`, DirectCRT on the left) are in `WORKDIR/captures`.

Which DOSBox shader a case tests depends on the screen. On a 3840x2160 screen,
the fullscreen cases use crt-hyllian `vga-4k` (and `vga-1440p` for 800x600);
the windowed cases use the fake double scan shader (320x200), `vga-1080p`
(320x240, 640x480) and `sharp` (800x600). That's all five.

## Needs

- DOSBox Staging, with the same shader files as `third_party/dosbox-staging`.
  Set `DOSBOX` to its `dosbox.exe` if it isn't in
  `%LOCALAPPDATA%\Programs\DOSBox Staging`. Your own DOSBox settings aren't
  used or changed (`--noprimaryconf`); `dosconf.py` writes a config with
  DOSBox's defaults. It needs DOSBox's web server (0.84 or later), which the
  config turns on for 127.0.0.1, port 8086.
- Python 3 with numpy and Pillow.
- Git Bash and Windows PowerShell.

## How it works

1. `ddtest --dump` saves the photo, pixel patterns and colour bars at each
   size, and `compose.py` puts them into one picture.
2. `mkdos.py` reduces it to 256 colours whose 6-bit VGA palette values DOSBox
   turns back into exactly the same 8-bit colours, saves that as `ref.png`
   for ddtest, and writes `IMG.BIN` and `SHOW.COM`, a small DOS program that
   sets the video mode (13h for 320x200, VESA 151h, 101h and 103h for the
   others) and displays it.
3. `capture.ps1` starts ddtest (with `shader=crt-auto` in its
   `directcrt.ini`) and captures the screen or the window's client area.
4. `dosshot.ps1` starts DOSBox and takes its rendered screenshot, the picture
   after the shader exactly as DOSBox shows it, rather than a screen
   capture. SHOW.COM writes a marker to memory once the picture is up, which
   the script reads through DOSBox's web server; it then sends Alt+F5 (the
   only way to ask for that screenshot, so the DOSBox window is brought to
   the front) and shuts DOSBox down through the web server.
5. `compare.py` centres DOSBox's screenshot, which is only the picture, on a
   black canvas the size of DirectCRT's capture, lines them up and compares
   them. "Same" allows differences of 1/255 (rounding) and a few stray
   pixels, such as rounded window corners.

Leave the keyboard and mouse alone during a run: a click that brings another
window to the front ends up in DirectCRT's capture, and a key press can end
SHOW.COM.
