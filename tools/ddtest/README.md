# ddtest

A small DirectDraw program that shows test patterns at any resolution and bit
depth. It's for checking scaling, CRT shading, aspect ratio, colour, frame pacing
and mouse mapping without needing a real game. Its blits pattern checks its own
result, which makes it a regression test for DirectDraw itself (see
[Regression test](#regression-test)).

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
- switching between windowed and fullscreen, a new windowed size or another
  interface version starts over as a fresh run (DirectCRT takes a windowed
  program's window size as its mode once, when the program sets its
  cooperative level)

Windowed, the test window's size is the resolution: resizing it changes the
resolution in the panel (and starts over at the new size, where the window is),
and a new resolution in the panel resizes it. Through Windows' own DirectDraw
the picture is 1:1. DirectCRT scales it like any game in its own window: the
window it shows is `window_size`, while ddtest still sees a window of its own
size, so resizing DirectCRT's window doesn't change the resolution.

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
DirectDraw for. DirectCRT follows the request with `display=auto` (the default):
a window for the windowed method, fullscreen at the desktop resolution for the
others. `display=original` switches the monitor to the resolution instead.
`display=window` and `display=fullscreen` force one or the other whatever is
asked.

- **Exclusive fullscreen, page flipping** - a primary surface with one back buffer, `Flip`
- **Exclusive fullscreen, Blt to the primary surface** - draw into an off-screen surface, `Blt` it to the primary
- **Exclusive fullscreen, WaitForVerticalBlank, then Blt to the primary** - the same, waiting for the vertical blank first, as many games did instead of flipping
- **Exclusive fullscreen, write the primary surface** - write straight into the primary surface
- **Windowed** - normal cooperative level, a clipper, and `Blt` into the window

**Interface** picks the DirectDraw interface the program uses, as games of
different years did: `IDirectDraw` (DirectX 1 to 3, from `DirectDrawCreate`),
`IDirectDraw2` (DirectX 5) or `IDirectDraw4` (DirectX 6), asked for with
`QueryInterface`, or `IDirectDraw7` (DirectX 7, from `DirectDrawCreateEx`).
The surfaces, surface descriptions and calls are that version's: version 1
sets the display mode without a refresh rate, and versions 1 and 2 restore
lost surfaces one by one, having no `RestoreAllSurfaces`.

DirectCRT offers 24-bit display modes. Windows' own DirectDraw on current
graphics cards doesn't, and can't `Blt` a 24-bit surface to the desktop
either; ddtest says so when it happens.

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
| Palette fade | colour bars fading to black and back over three seconds; in 8-bit modes with `SetEntries` on all 256 entries every frame, as games faded between screens, otherwise drawn |
| DirectDraw blits and GDI text | ddtest draws on the surface with DirectDraw and GDI, then reads it back and checks it: `Blt` colour fills, a sprite blitted with a source colour key by `Blt` and by `BltFast` (its own off-screen surface, drawn with `Lock` every frame), and text drawn with `TextOut` through the surface's `GetDC`. The fills and sprites must be exact, the colour key letting the background through, and the text white on its black background. The info text and the status line show the result; see [Regression test](#regression-test) |
| White field | full white, for mask strength and brightness |
| Test image | a built-in 4500 x 3000 landscape photo (`testimage.png`, embedded in `ddtest.exe`), 1:1, centred and cropped; Ctrl+arrows move it. Fine natural detail, and a large smooth sky (Ctrl+Up to see it) that's smooth at 24 and 32-bit and bands at 16-bit |
| Image file | a PNG/BMP/JPEG/GIF/TIFF of your own, 1:1, centred and cropped; Ctrl+arrows move it |

In 8-bit modes the patterns use a fixed palette (a 6x6x6 colour cube plus 40 greys),
so gradients show 8-bit banding on purpose.

## Command line

```
--size WxH, --width N, --height N, --bpp 8|16|24|32, --refresh HZ, --fps N
--ddraw 1|2|4|7      the IDirectDraw interface version (7)
--present flip|blt|vblank|lock|windowed, --pattern NAME|NUMBER, --image FILE
--info 0|1           the info text in the picture (same as I)
--mouse 0|1          the mouse crosshair (same as M)
--start              run straight away
--dump DIR           save the patterns (or just --pattern) as PNGs and exit, no DirectDraw
--frame N            animation frame for --dump
--frames N           with --start: exit after N frames (exit code 0; 3 if DirectDraw
                     failed; 4 if the blits pattern's check failed)
--log FILE           with --frames: write the result to FILE
```

Pattern names: `bars`, `ramps`, `grid`, `pixels`, `geometry`, `dither`, `text`,
`motion`, `interlaced`, `palette`, `fade`, `blits`, `white`, `testimage`,
`image`. `ddtest.ini` keeps the presentation method and the pattern by name;
the numbers older versions saved still work.

Example: run 320x240 8-bit with page flipping for 300 frames and log the result:

```
ddtest --size 320x240 --bpp 8 --present flip --pattern grid --start --frames 300 --log result.txt
```

## Regression test

`regress.ps1` runs the blits pattern, 20 frames a run, and prints a table.
IDirectDraw7 gets every presentation method at every bit depth (8, 16, 24,
32; windowed once, at the desktop's). IDirectDraw, 2 and 4 get each method
once, the depth rotating so each version still meets every depth: the
versions share everything but their interfaces' wrappers. That's 32 runs;
`-Full` runs every combination (68 runs).

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\ddtest\regress.ps1
```

It tests `bin\Release\ddraw.dll` with `bin\Release\ddtest.exe` by default,
copied to a new folder under `%TEMP%` with a default `directcrt.ini`, so your
own settings don't matter. `-Dll` and `-Exe` pick others, `-System` tests
Windows' own DirectDraw, and `-Versions 1,7`, `-Methods flip,blt`,
`-Depths 8,32`, `-Size 800x600` and `-Frames 120` narrow or change the runs;
naming versions, methods or depths runs every combination of them.
It exits with 1 if a run failed. A display mode the DLL doesn't offer counts
as skipped: Windows' own DirectDraw sometimes refuses 8 and 16-bit modes on
Windows 10 and 11.

The fullscreen runs take over the screen, about 40 seconds for the whole
table (three minutes with `-Full`); leave the mouse and keyboard alone. A frame whose surfaces were lost
(for example just after a mode switch) isn't checked, so a run can check a
few frames fewer than it ran.
