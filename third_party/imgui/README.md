# Dear ImGui

Unmodified files from [Dear ImGui](https://github.com/ocornut/imgui) v1.93.0
(released 14 October 2026), at the same paths as in its repository: the core
and the Direct3D 11 and OpenGL 3 renderer backends. DirectCRT's overlay
(`src/overlay*.cpp`) is built with them.

Taken from `https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0.zip`
(SHA-256 `e21907fd8dd5ea59aa00538632a02ad0a3ededb4ddc533b94702331fbcd6c470`).
To update, copy the same files from a newer release and update the version
and hash here.

`imconfig.h` is left as it is; the build sets the options in it as
preprocessor definitions instead, in `DirectCRT.vcxproj`:

- `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`
- `IMGUI_DISABLE_DEMO_WINDOWS`, `IMGUI_DISABLE_DEBUG_TOOLS`: smaller
- `IMGUI_DISABLE_WIN32_FUNCTIONS`: no clipboard, IME or shell functions, so
  `ddraw.dll` imports nothing new

The Direct3D 11 backend compiles its shaders with `D3DCompile`, so
`d3dcompiler_47.dll` is delay-loaded: only when the overlay is first drawn.
The font is ImGui's built-in ProggyForever (MIT), drawn at any size.

Licence: MIT (`LICENSE.txt`).
