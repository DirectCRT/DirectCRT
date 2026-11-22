# DirectCRT

DirectCRT is a `ddraw.dll` replacement for 2D DirectDraw games from the Windows 98
and XP era. Those games render at 640x480, 800x600 or less, which looks blurry or
tiny on today's screens. DirectCRT scales them properly and shades them like a
period CRT monitor, the way DOSBox Staging's `crt-auto` does for VGA.

It's a fork of [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw) by
FunkyFr3sh and keeps its DirectDraw implementation and its game compatibility
work.

## Status

Early development. Today DirectCRT is cnc-ddraw with a few changes, and the CRT
shading is still to come. [docs/development-plan.md](docs/development-plan.md)
covers the goals, the decisions and the phases.

Changes from cnc-ddraw so far:

- Settings are in `directcrt.ini`, global section `[directcrt]`. The file is
  created next to `ddraw.dll` the first time a game starts.
- `windowed=auto` (the new default) follows the game: a window for games that ask
  for one, fullscreen for the rest. Alt+Enter still switches, but it's no longer
  saved, so every start uses the configured mode.
- Games that run in their own window get the desktop's colour depth (32-bit),
  instead of a fixed 16-bit, and are shown 1:1 at their own size and position.
- The mouse is released when the game releases DirectDraw.
- `unlockcursor_middleclick=true` adds a middle click as a way to release the
  mouse.
- Logs and crash dumps are named `directcrt-*.log` and `directcrt-*.dmp`.
- Windows XP and older aren't supported. DirectCRT targets Windows 10 and 11,
  Wine and Proton.

## Using it

1. Copy `ddraw.dll` into the game's folder, next to its `.exe`.
2. Start the game.

On Wine and Proton, set `ddraw` to "native, builtin" in `winecfg` (Libraries),
or start the game with `WINEDLLOVERRIDES="ddraw=n,b"`.

DirectCRT only handles DirectDraw: Direct3D games aren't supported. A game
with several renderers needs its DirectDraw or "software" renderer selected.

[docs/supported-games.md](docs/supported-games.md) lists the games known to
work.

### Hotkeys

| Keys | Action |
|---|---|
| Alt+Enter | Switch between windowed and fullscreen |
| Ctrl+Tab, or Right Alt+Right Ctrl | Release the mouse |
| Alt+Page Down | Maximise the window |

## Building

You need Visual Studio 2026 with "Desktop development with C++". Open
`DirectCRT.sln`, or build from a Developer Command Prompt:

```
msbuild DirectCRT.sln -m -p:Configuration=Release -p:Platform=x86
```

Everything goes to `bin\<configuration>\`. There are four configurations:

- **Release**: the normal build.
- **DebugLog**: optimised, and writes a detailed log, for bug reports.
- **DebugLogMini**: like DebugLog, with a shorter log.
- **Debug**: unoptimised.

The solution also builds [ddtest](tools/ddtest/README.md), a DirectDraw test
pattern program for checking scaling, colour depth and presentation.

GitHub Actions builds all four configurations on every push to `master` and on
pull requests.

## Licence

DirectCRT as a whole is licensed under the GNU General Public License, version 2
or (at your option) any later version; see [LICENSE](LICENSE). It will include
code ported from DOSBox Staging, which is GPL-2.0-or-later.

The code that comes from cnc-ddraw is also available under its original MIT
licence, in [LICENSE-cnc-ddraw.txt](LICENSE-cnc-ddraw.txt). Bundled third-party
code keeps its own licence:

- Microsoft Detours: MIT ([src/detours/LICENSE.md](src/detours/LICENSE.md))
- LodePNG: zlib (`src/lodepng.c`)
- Khronos OpenGL headers: MIT-style (`inc/glcorearb.h`, `inc/wglext.h`, `inc/KHR/`)
- Shaders in `inc/openglshader.h` and `inc/d3d9shader.h`: MIT, as noted in each one
