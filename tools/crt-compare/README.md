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
primary monitor, or `win:WxH`, in a 1920x1080 window, for 320x200, 320x240,
640x480 and 800x600. Without cases it runs all eight, which takes about five
minutes. Leave the computer alone while it runs: the captures are of the
screen.

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
  DOSBox's defaults.
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
   `directcrt.ini`) and then DOSBox, each showing the picture, and captures
   the screen or the window's client area.
4. `compare.py` lines the captures up and compares them. "Same" allows
   differences of 1/255 (rounding) and a few stray pixels, such as rounded
   window corners.

Fullscreen DOSBox runs with `fullscreen_mode = forced-borderless`, because a
GDI screen capture of its normal fullscreen window comes out black. That
window is one pixel larger than the screen, so DOSBox's picture is one pixel
to the right of DirectCRT's; `compare.py` allows for that.
