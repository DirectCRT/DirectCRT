# Draws ddtest.ico: a monitor showing colour bars, ddtest's first pattern.
# Each size is drawn on its own pixel grid, so the small ones stay sharp; the
# large ones are drawn four times over and scaled down for smooth corners.
#
#   python make_icon.py            writes ddtest.ico next to this script
#   python make_icon.py --preview FILE.png  also writes every size, enlarged, to FILE.png

import os
import sys
from PIL import Image, ImageDraw

SIZES = [16, 20, 24, 32, 40, 48, 64, 256]

BARS = [(235, 235, 235), (235, 235, 16), (16, 235, 235), (16, 235, 16),
        (235, 16, 235), (235, 16, 16), (16, 16, 235)]
# The strip under the bars, as in SMPTE colour bars: blue, black, magenta, black, cyan, black, white
STRIP = [(16, 16, 235), (16, 16, 16), (235, 16, 235), (16, 16, 16),
         (16, 235, 235), (16, 16, 16), (235, 235, 235)]

BEZEL = (58, 60, 66)
BEZEL_EDGE = (28, 29, 33)
BEZEL_LIGHT = (96, 99, 108)
STAND = (44, 46, 51)


def draw(size):
    smooth = size >= 48
    k = 4 if smooth else 1
    n = size * k
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def px(f):
        return int(round(f * size)) * k

    # Bezel, the screen inside it, the neck and the foot
    bx0, by0, bx1, by1 = px(0.0), px(0.06), px(1.0), px(0.80)
    if size <= 16:
        bx0, by0, bx1, by1 = 0, 1, 16, 13
    sx0, sy0, sx1, sy1 = bx0 + px(0.09), by0 + px(0.09), bx1 - px(0.09), by1 - px(0.09)
    if size <= 24:
        sx0, sy0, sx1, sy1 = bx0 + 2 * k, by0 + 2 * k, bx1 - 2 * k, by1 - 2 * k
    if size <= 16:
        # Seven bars two pixels wide, the sides of the bezel just its edge
        sx0, sy0, sx1, sy1 = 1, 3, 15, 11

    radius = px(0.08) if smooth else 0
    if smooth:
        d.rounded_rectangle([bx0, by0, bx1 - 1, by1 - 1], radius, fill=BEZEL_EDGE)
        e = max(k, px(0.012))
        d.rounded_rectangle([bx0 + e, by0 + e, bx1 - 1 - e, by1 - 1 - e], radius - e, fill=BEZEL_LIGHT)
        d.rounded_rectangle([bx0 + e, by0 + 2 * e, bx1 - 1 - e, by1 - 1 - e], radius - e, fill=BEZEL)
    else:
        d.rectangle([bx0, by0, bx1 - 1, by1 - 1], fill=BEZEL_EDGE)
        d.rectangle([bx0 + 1, by0 + 1, bx1 - 2, by1 - 2], fill=BEZEL)
        d.line([bx0 + 1, by0 + 1, bx1 - 2, by0 + 1], fill=BEZEL_LIGHT)

    # Neck and foot
    ny1 = px(0.90) if size > 16 else 15
    nx0, nx1 = px(0.40), px(0.60)
    if size <= 16:
        nx0, nx1 = 6, 10
    fy0, fy1 = ny1, px(0.96) if size > 16 else 16
    fx0, fx1 = px(0.22), px(0.78)
    if size <= 16:
        fx0, fx1, fy0, fy1 = 3, 13, 15, 16
    d.rectangle([nx0, by1, nx1 - 1, ny1 - 1], fill=STAND)
    if smooth:
        d.rounded_rectangle([fx0, fy0, fx1 - 1, fy1 - 1], (fy1 - fy0) // 2, fill=STAND)
    else:
        d.rectangle([fx0, fy0, fx1 - 1, fy1 - 1], fill=STAND)

    # Colour bars, with the strip under them from 32 pixels up
    w, h = sx1 - sx0, sy1 - sy0
    bars_h = h if size < 32 else h * 3 // 4
    for i, c in enumerate(BARS):
        x0 = sx0 + (w * i // 7) // k * k
        x1 = sx0 + (w * (i + 1) // 7) // k * k if i < 6 else sx1
        d.rectangle([x0, sy0, x1 - 1, sy0 + bars_h - 1], fill=c)
        if bars_h < h:
            d.rectangle([x0, sy0 + bars_h, x1 - 1, sy1 - 1], fill=STRIP[i])

    if smooth:
        img = img.resize((size, size), Image.LANCZOS)
    return img


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    images = [draw(s) for s in SIZES]
    out = os.path.join(here, "ddtest.ico")
    images[-1].save(out, format="ICO", sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    print("wrote", out)

    if "--preview" in sys.argv:
        scale = 4
        width = sum(max(s, 64) * scale // (4 if s == 256 else 1) + 16 for s in SIZES)
        sheet = Image.new("RGBA", (width, 64 * scale + 16), (240, 240, 240, 255))
        x = 8
        for s, im in zip(SIZES, images):
            f = 1 if s == 256 else scale
            big = im.resize((s * f, s * f), Image.NEAREST)
            sheet.alpha_composite(big, (x, 8))
            x += max(s, 64) * scale // (4 if s == 256 else 1) + 16
        path = sys.argv[sys.argv.index("--preview") + 1]
        sheet.save(path)
        print("wrote", path)


main()
