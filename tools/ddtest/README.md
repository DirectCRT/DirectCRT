# ddtest

A small DirectDraw program that shows test patterns at any resolution and bit
depth. It's for checking scaling, CRT shading, aspect ratio, colour, frame pacing
and mouse mapping without needing a real game.

It loads whichever `ddraw.dll` Windows finds first: the one next to `ddtest.exe`,
otherwise Windows' own. The status line and the in-picture info text show which
one is in use. Building the solution puts `ddtest.exe` in `bin\<Configuration>\`
next to the freshly built `ddraw.dll`. To compare with Windows' own DirectDraw,
copy `ddtest.exe` to a folder on its own.

## Using it

Set the width, height, bit depth and presentation method in the control panel and
press **Apply**. The pattern, the overlays and "Scale image to fit" change
immediately; everything else waits for Apply. **Query** lists the display modes
the loaded DirectDraw reports; picking one fills in the fields.

In the test window: **Esc** stops (and returns to the panel, which matters in
exclusive fullscreen), **Left/Right** change the pattern, **I** and **M** toggle the
info text and the mouse crosshair, left click marks a point, right click clears
the marks.

Presentation methods, matching what games do:

- **Fullscreen: page flipping** - a primary surface with one back buffer, `Flip`
- **Fullscreen: Blt to primary** - draw into an off-screen surface, `Blt` it to the primary
- **Fullscreen: lock the primary** - write straight into the primary surface
- **Windowed** - normal cooperative level, a clipper, and `Blt` into the window

## Patterns

All patterns are generated for the current size.

| Pattern | Checks |
|---|---|
| Colour bars | colour pipeline; near-black steps (0, 8, 16, 24) for black level |
| Grey and colour ramps | gamma, banding, near-black detail |
| Grid (16 x 12 cells) | geometry and cropping: the outermost pixels are a red border; cells are square on a 4:3 screen |
| Pixel patterns | single-pixel checkerboards and lines: scanlines, phosphor masks, moire, integer scaling |
| Geometry / aspect ratio | circles that are round on a 4:3 screen; a blue square-pixel circle when the pixels aren't square |
| Dither patterns | checkerboard and ordered dithers next to the colour they should blend to |
| Text | small to large text, coloured text, 80 and 40 columns across the screen |
| Motion | sweeping bar and line, bouncing square, frame counter: tearing, VSync, frame pacing |
| Interlaced | every other line black, for deinterlacing |
| Palette cycling | in 8-bit modes, `SetEntries` every frame (the greys stay fixed) |
| White field | full white, for mask strength and brightness |
| Image file | a PNG/BMP/JPEG/GIF/TIFF, 1:1 or scaled to fit |

In 8-bit modes the patterns use a fixed palette (a 6x6x6 colour cube plus 40 greys),
so gradients show 8-bit banding on purpose.

## Command line

```
--size WxH, --width N, --height N, --bpp 8|16|24|32, --refresh HZ, --fps N
--present flip|blt|lock|windowed, --pattern NAME|NUMBER, --image FILE, --fit
--start              apply the settings straight away
--dump DIR           save the patterns (or just --pattern) as PNGs and exit, no DirectDraw
--frame N            animation frame for --dump
--frames N           with --start: exit after N frames (exit code 0, or 3 if DirectDraw failed)
--log FILE           with --frames: write the result to FILE
```

Pattern names: `bars`, `ramps`, `grid`, `pixels`, `geometry`, `dither`, `text`,
`motion`, `interlaced`, `palette`, `white`, `image`.

Example: run 320x240 8-bit with page flipping for 300 frames and log the result:

```
ddtest --size 320x240 --bpp 8 --present flip --pattern grid --start --frames 300 --log result.txt
```
