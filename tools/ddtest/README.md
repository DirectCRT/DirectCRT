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

Set the resolution, bit depth and presentation method in the control panel and
press **Run**. The resolution is the width and height boxes; picking from the list
next to them fills them in, and the list shows **Custom** for any other size. **Stop**,
Esc in the test window, or closing the test window ends the run. Only one of Run
and Stop is enabled at a time. (DirectCRT normally ends a program whose window is
closed, because many games ignore the close; it leaves the close to `ddtest.exe`,
so a renamed copy quits instead.)

During a run, changes in the panel apply straight away; values typed into a box
apply when you press Enter or leave the box. Like a game changing its settings:

- the pattern, overlays, frame limit and image just change
- fullscreen, a new resolution, bit depth or refresh rate, or another fullscreen
  presentation method, is a display mode change on the same DirectDraw object
- windowed, a new bit depth recreates the surfaces in the same window
- switching between windowed and fullscreen, or a new windowed size, starts over
  as a fresh run (DirectCRT takes a windowed program's window size as its mode
  once, when the program sets its cooperative level)

Windowed, the test window shows the picture 1:1, so its size is the resolution:
resizing the window changes the resolution in the panel (and starts over at the
new size, where the window is), and a new resolution in the panel resizes the
window.

**DirectDraw modes** lists the display modes the loaded `ddraw.dll` reports, after
you press **List**; picking one fills in the resolution, bit depth and refresh rate.
DirectCRT reports its own list of resolutions, all at 60 Hz; Windows' own
DirectDraw reports the graphics card's real modes.

Every run starts like a new game: a new test window, and `ddraw.dll` is loaded
again (it's unloaded when a run stops), so it reads its settings again and
forgets anything changed at runtime, such as Alt+Enter. Edits to `directcrt.ini`
take effect on the next Run, except crt-auto's settings (`aspect`, `viewport`,
`integer_scaling` and the colour settings), which apply during the run, within
a second of saving the file.

In the test window: **Esc** stops (and returns to the panel, which matters in
exclusive fullscreen), **Left/Right** change the pattern, **Ctrl+arrows** move an
image 32 pixels at a time (with Shift, one pixel; Run starts it centred again), **I** and **M** toggle the
info text and the mouse crosshair, left click marks a point, right click clears
the marks.

Presentation methods, matching what games do. They're what the program asks
DirectDraw for. DirectCRT follows the request with `windowed=auto` (the default):
a window for the windowed method, fullscreen for the others, upscaled with
`fullscreen=true` or as a real mode switch with `fullscreen=false`.
`windowed=true` or `windowed=false` force one or the other whatever is asked.

- **Exclusive fullscreen, page flipping** - a primary surface with one back buffer, `Flip`
- **Exclusive fullscreen, Blt to the primary surface** - draw into an off-screen surface, `Blt` it to the primary
- **Exclusive fullscreen, write the primary surface** - write straight into the primary surface
- **Windowed** - normal cooperative level, a clipper, and `Blt` into the window

24-bit works only windowed through DirectCRT: neither DirectCRT nor Windows' own
DirectDraw on current graphics cards offers 24-bit display modes, and Windows'
DirectDraw can't `Blt` a 24-bit surface to the desktop either. ddtest says so
when it happens.

## Patterns

All patterns are generated for the current size.

| Pattern | Checks |
|---|---|
| Colour bars | colour pipeline; near-black steps (0, 8, 16, 24) for black level |
| Grey and colour ramps | gamma, banding, near-black detail |
| Grid (16 x 12 cells) | geometry and cropping: the outermost pixels are a red border; cells are square on a 4:3 screen |
| Pixel patterns | single-pixel checkerboards and lines: scanlines, phosphor masks, moire, integer scaling |
| Geometry / aspect ratio | circles that should look round. The pixels are square (1:1), 1280 x 1024 included, except for fullscreen low resolution VGA modes (up to 720 wide, below 480 lines, no wider than 16:10, such as 320 x 200 and 640 x 400), which are drawn for a 4:3 screen with tall pixels, as games of the time were. That's crt-auto's `aspect=auto` rule. A blue square-pixel circle shows when the pixels aren't square. If they still look oval, the picture is being stretched (in DirectCRT, `maintas=false`) |
| Dither patterns | checkerboard and ordered dithers next to the colour they should blend to |
| Text | small to large text, coloured text, 80 and 40 columns across the screen |
| Motion | sweeping bar and line, bouncing square, frame counter: tearing, VSync, frame pacing |
| Interlaced | every other line black, for deinterlacing |
| Palette cycling | in 8-bit modes, `SetEntries` every frame (the greys stay fixed) |
| White field | full white, for mask strength and brightness |
| Test image | a built-in 4500 x 3000 landscape photo (`testimage.png`, embedded in `ddtest.exe`), 1:1, centred and cropped; Ctrl+arrows move it. Fine natural detail, and a large smooth sky (Ctrl+Up to see it) that's smooth at 24 and 32-bit and bands at 16-bit |
| Image file | a PNG/BMP/JPEG/GIF/TIFF of your own, 1:1, centred and cropped; Ctrl+arrows move it |

In 8-bit modes the patterns use a fixed palette (a 6x6x6 colour cube plus 40 greys),
so gradients show 8-bit banding on purpose.

## Command line

```
--size WxH, --width N, --height N, --bpp 8|16|24|32, --refresh HZ, --fps N
--present flip|blt|lock|windowed, --pattern NAME|NUMBER, --image FILE
--info 0|1           the info text in the picture (same as I)
--mouse 0|1          the mouse crosshair (same as M)
--start              run straight away
--dump DIR           save the patterns (or just --pattern) as PNGs and exit, no DirectDraw
--frame N            animation frame for --dump
--frames N           with --start: exit after N frames (exit code 0, or 3 if DirectDraw failed)
--log FILE           with --frames: write the result to FILE
```

Pattern names: `bars`, `ramps`, `grid`, `pixels`, `geometry`, `dither`, `text`,
`motion`, `interlaced`, `palette`, `white`, `testimage`, `image`.

Example: run 320x240 8-bit with page flipping for 300 frames and log the result:

```
ddtest --size 320x240 --bpp 8 --present flip --pattern grid --start --frames 300 --log result.txt
```
