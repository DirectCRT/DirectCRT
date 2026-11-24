# Fonts

The font of the OSDs (the status OSD and the settings OSD), built into the DLL
as a resource (`OVERLAY_FONT` in `res.rc`) and loaded by `src/overlay.cpp`.

| File | Font | Licence |
|---|---|---|
| `source-sans-3/SourceSans3-Regular.ttf` | Source Sans 3, version 3.052, by Adobe | SIL Open Font License 1.1 (`source-sans-3/LICENSE.md`) |

An unmodified copy of `TTF/SourceSans3-Regular.ttf` from Adobe's release
[3.052R](https://github.com/adobe-fonts/source-sans/releases/tag/3.052R)
(`TTF-source-sans-3.052R.zip`). It has to stay unmodified: 'Source' is a
Reserved Font Name, so a subset or other changed copy couldn't keep the name.
Its digits are all the same width, so the numbers in the status OSD don't
shift as they change.

The files are marked `-text` in `.gitattributes`, so Git keeps them byte for
byte.
