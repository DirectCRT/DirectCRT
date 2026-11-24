# DirectCRT

DirectCRT is a `ddraw.dll` replacement for 2D DirectDraw games from the Windows 98
and XP era. Those games render at 640x480, 800x600 or less, which looks blurry or
tiny on today's screens. DirectCRT scales them properly and shades them like a
period CRT monitor, the way DOSBox Staging's `crt-auto` does for VGA.

It's a fork of [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw) by
FunkyFr3sh and keeps its DirectDraw implementation and its game compatibility
work.

## Status

Early development. DirectCRT is cnc-ddraw with new display settings and
DOSBox Staging's CRT shading.
[docs/development-plan.md](docs/development-plan.md) covers the goals, the
decisions and the phases.

Changes from cnc-ddraw so far:

- `shader=crt-auto`, the default, shades the picture with DOSBox Staging's
  `crt-auto` for VGA: its own shaders, its choice of shader for the screen
  size, its picture size and its colours, with the same result pixel for
  pixel. `sharp`, `nearest` and `bilinear` are the other shaders. `aspect`,
  `integer_scaling`, `viewport` and the colour settings work as in DOSBox,
  with every shader, and changes to them apply while the game runs.
- Renderers: Direct3D 11 (the default, on WARP if the graphics card can't)
  and OpenGL 3.3, each the other's fallback. Where neither works DirectCRT
  steps aside for Windows' own DirectDraw and says so. Direct3D 9, GDI and
  the libretro shader files are gone.
- `display` decides fullscreen or a window: `auto` (the default) follows the
  game, `window` and `fullscreen` force one, and `original` switches the
  monitor to the game's resolution as Windows' own DirectDraw did. Fullscreen
  is at the desktop resolution, so the shading has room to work.
- Windows are `window_size` (DOSBox's `small`, `medium`, `large`, `WxH`, or
  `unscaled`), with the picture placed in them as in fullscreen. Games that
  run in their own window are scaled the same way and get the desktop's colour
  depth instead of a fixed 16-bit. `window_position`, `window_decorations`,
  `window_resizable` and `remember_window` replace `posX`, `posY`, `border`,
  `resizable` and `savesettings`. Old keys are still read.
- Settings are in `directcrt.ini`, section `[directcrt]`. The file is
  created next to `ddraw.dll` the first time a game starts, so each game has
  its own. Sections named after a game's executable aren't read.
- cnc-ddraw's 280 built-in game profiles and the game-specific hacks they
  turned on (`tshack`, `armadahack`, ...) are gone, and so are
  `min_font_size` and the C++Builder config tool.
- The mouse is released when the game releases DirectDraw.
  `unlockcursor_middleclick=true` adds a middle click as a way to release it.
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

A compatibility setting a game needs goes in its own `directcrt.ini`, next to
its `ddraw.dll`. cnc-ddraw's
[wiki](https://github.com/FunkyFr3sh/cnc-ddraw/wiki) lists the settings many
games need (the section is `[directcrt]` here, not `[ddraw]` or one named after
the game).

### Hotkeys

| Keys | Action |
|---|---|
| Alt+Enter | Switch between a window and fullscreen |
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
pattern program for checking scaling, colour depth and presentation. Its
regression test, `tools\ddtest\regress.ps1`, checks DirectDraw's blits and
GDI text with every interface version, presentation method and bit depth; it
takes over the screen for about two minutes.

Unit tests: `tests\run_tests.cmd` from a Developer Command Prompt.

A fresh default `directcrt.ini`, after a change to the settings:
`powershell -NoProfile -ExecutionPolicy Bypass -File tools\default-ini.ps1`
replaces the one in `bin\Release\` (`-Dll` and `-Out` pick another DLL or
file). It loads the DLL without running a game.

GitHub Actions builds all four configurations and runs the unit tests on every
push to `master` and on pull requests.

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
- DOSBox Staging's shaders in `third_party/dosbox-staging/`: MIT, except
  `sharp.glsl` (GPL-2.0-or-later); see its README
- Dear ImGui, for the OSDs: MIT (`third_party/imgui/LICENSE.txt`)
- Source Sans 3, the OSDs' font: SIL Open Font License 1.1
  (`third_party/fonts/source-sans-3/LICENSE.md`)
