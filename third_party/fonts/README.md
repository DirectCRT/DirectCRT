# Fonts

The fonts of the OSDs (the status OSD and the settings OSD), built into the DLL
as resources (`OVERLAY_FONT` and `OVERLAY_ICONS` in `res.rc`) and loaded by
`src/overlay.cpp`. The files are marked `-text` in `.gitattributes`, so Git
keeps them byte for byte.

| File | Font | Licence |
|---|---|---|
| `source-sans-3/SourceSans3-Regular.ttf` | Source Sans 3, version 3.052, by Adobe | SIL Open Font License 1.1 (`source-sans-3/LICENSE.md`) |
| `material-icons/MaterialIcons-Subset.ttf` | Material Icons, by Google, cut down | Apache License 2.0 (`material-icons/LICENSE`) |

## Source Sans 3

An unmodified copy of `TTF/SourceSans3-Regular.ttf` from Adobe's release
[3.052R](https://github.com/adobe-fonts/source-sans/releases/tag/3.052R)
(`TTF-source-sans-3.052R.zip`). It has to stay unmodified: 'Source' is a
Reserved Font Name, so a subset or other changed copy couldn't keep the name.
Its digits are all the same width, so the numbers in the status OSD don't
shift as they change.

## Material Icons

The settings OSD's icons. Made from `font/MaterialIcons-Regular.ttf` and
`font/MaterialIcons-Regular.codepoints` in
[google/material-design-icons](https://github.com/google/material-design-icons)
at commit `f7bd4f25f3764883717c09a1fd867f560c9a9581` (2022-09-19).

Modified: `tools/subset-icons.py` cuts it down to the icons the OSD uses, and
writes `inc/overlay_icons.h` with their code points. To add an icon, add its
name there and run the script again on the two files above.
