---
description: C and C++ code style rules
paths:
  - "src/**/*.{c,cpp,h}"
  - "inc/**/*.h"
  - "tests/**/*.{c,h}"
  - "tools/**/*.{c,cpp,h}"
---

# General principles

- Simplest, most minimal solution wins; don't overengineer or future-proof
- Plain data and functions that work on it; no object hierarchies
- Small, focused functions; pure where they can be (`crt_auto.c` takes its
  inputs and returns its result, so it's unit tested without Windows)
- Look for existing patterns in the codebase first — don't reinvent the wheel
- Self-documenting code; no restating the obvious in comments
- Only add abstractions when justified by current needs
- Clarity first in cold code; in hot code, the data layout and the memory
  traffic come first (see Data-oriented design)
- When in doubt, underengineer

# Scope

These rules apply to our own code. Vendored code follows its own conventions —
do not reformat it: `third_party/`, `src/detours/`, `src/lodepng.c`,
`inc/lodepng.h`, `inc/KHR/`, `inc/glcorearb.h`, `inc/wglext.h`.

Code inherited from cnc-ddraw keeps its style when it's edited: match the code
around the change, and don't restyle what the change doesn't touch.

# After any code change, verify:

1. Build Release x86 with MSBuild (`DirectCRT.sln`); no new warnings
2. `tests/run_tests.cmd` when `crt_auto`, `settings`, `display_settings` or
   `vbeam` changed
3. `tools/ddtest/regress.ps1` when rendering, presenting or the shaders changed
   (it takes over the screen)
4. After changing `settings.c`'s ini text, regenerate the default ini with
   `tools/default-ini.ps1`

# Language

- C, as MSVC compiles it, for everything but the ImGui OSDs
- C++ only where Dear ImGui needs it (`src/overlay*.cpp`), written close to C:
  no exceptions, no STL containers, no classes of our own, no templates;
  C callers reach it through `extern "C"` functions in a header
- 32-bit x86: the games are; pointers and `size_t` are 4 bytes
- Windows 10+ and Wine; Win32, Direct3D 11 and OpenGL 3.3 only
- Bounded string functions only (`_snprintf` / `snprintf` / `strncpy` with the
  buffer's size, and terminate the result); never `strcpy`, `strcat`,
  `sprintf`
- Avoid complex macros; a macro only where a function or a constant can't do it
- Tables of plain data over code that repeats per item (`settings.c`'s
  `SETTING` table drives the ini, the OSD and the defaults)

# Data-oriented design

Hot code is everything that runs per frame, per scanline or per pixel: the
blits (`blt.c`, `ddsurface.c`), palette and format conversion, the renderers'
upload and present (`render_d3d11.c`, `render_ogl.c`), the frame limiter, and
the OSDs' per-frame drawing. Cold code is everything else: the ini, mode
changes, window changes, the settings OSD's reactions to input.

Design hot code around its data:

- Know what the loop reads and writes, how much of it, and in what order;
  shape the data for that access, not for how the concept is named
- Contiguous arrays walked linearly; rows top to bottom, `x` in the inner
  loop, the row stride from the surface's pitch
- Hot fields together, cold fields elsewhere: split a struct when a loop only
  touches part of it; struct of arrays when a loop reads one field of many
  items, array of structs when it reads all fields of each
- Pixel buffers use the format's own element type (`uint8_t` palette indices,
  `uint16_t` 565, `uint32_t` XRGB); counters and arithmetic use `int`
- No allocations in the frame: allocate on a mode or size change and reuse;
  no `malloc`, `HeapAlloc`, GPU resource or view creation per frame
- Decide once, outside the loop: branch on the bit depth, the format, the
  shader or the setting before the loop and run a loop specialised for it;
  no per-pixel branches, function pointers or switches
- Lookup tables when they stay in L1/L2 (a 256-entry palette to XRGB table);
  compute instead when the table wouldn't
- Copy what the loop needs from `g_ddraw` and other globals into locals before
  it: the compiler can't keep globals in registers across calls or past
  aliasing stores. `__restrict` where source and destination never overlap
- Align large buffers to 64 bytes (a cache line; `blt_copy`'s AVX path
  needs it)
- Data written by different threads (the game's and the renderer's) goes on
  different cache lines
- Hold a critical section only for the copy out of the game's surface (the
  upload's Map and copy); never across `Present`, a wait or other GPU work
- SIMD through intrinsics, behind a runtime CPU check with a scalar fallback
  (as `blt.c` does with `g_blt_use_avx`)
- CPU to GPU: one upload per frame with `D3D11_MAP_WRITE_DISCARD` (or the
  OpenGL equivalent); no GPU readback in the frame path (screenshots aside);
  per-pixel work belongs in the shaders, not on the CPU
- Optimise with measurements (ddtest, the status OSD's frame times); keep the
  simpler version when the faster one doesn't measure faster

Cold code doesn't need any of this: there, the clearest code wins.

# Naming

- `lower_snake_case` for functions, variables, arguments and struct members
- Public functions start with their module's name: `crt_auto_calc_layout`,
  `blt_copy`, `ui_button`, `menu_draw`; `static` functions don't need it
- `UPPER_SNAKE_CASE` for typedef'd structs and enums, their values, macros and
  constants: `CRT_AUTO_LAYOUT`, `CRT_SHADER_SHARP`, `MENU_TEXT`
- Globals, and file-level statics, start with `g_`: `g_ddraw`, `g_group`
- DirectDraw's interfaces keep their API's names
- No Hungarian notation in new code
- Unit suffixes where a number has a unit: `delay_ms`, `refresh_hz`

# Types

- `int` for counts and arithmetic; fixed-size types (`uint8_t`, `uint32_t`,
  ...) for pixel data and binary formats
- 64-bit (`long long`, `LARGE_INTEGER`) for timers and for products that can
  overflow 32 bits
- `BOOL` and `DWORD` at the Win32 and COM boundaries, as the code around them
  does; `bool` in C++
- Enums for choices, with a `_COUNT` value when a table is indexed by them
- Small results returned by value (`CRT_AUTO_LAYOUT`) over out-parameters
- Always initialise variables and struct members
- Maintain `const`-correctness throughout

# Includes & headers

- Order: `windows.h` and the system headers, the C library, third-party
  headers, then ours
- Header guards: `HEADERNAME_H`, from the file name (`CRT_AUTO_H`)
- A new file starts with a comment saying what it is; ported code names its
  source, the functions it came from, and their licence (as `crt_auto.h` does)

# Comments

- `/* */` comments, including in C++
- End-of-line comments for struct members, enum values and tabular data
- Comments say why, or what a block means in the domain (a DOSBox function it
  follows, a game it works around); not what the next line does
- Debug logging through `TRACE` / `TRACE_EXT` (`debug.h`); nothing in the hot
  path outside them
