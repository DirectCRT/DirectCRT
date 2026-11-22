"""Compares a DirectCRT capture with a DOSBox Staging capture of the same image.

usage: python -I compare.py DIRECTCRT.png DOSBOX.png OUTPREFIX [x y w h]

Finds the picture in each (the bounding box of non-black pixels), lines them
up (DOSBox's forced-borderless fullscreen window can put its picture a pixel
off), reports the per-channel differences, and writes:
  OUTPREFIX_diff.png   |difference| x 8, whole canvas, half size
  OUTPREFIX_crops.png  the same 1:1 crops from both, side by side

Exits with 0 when they're the same: under 0.01% of the pixels differ by more
than 1/255 (a few stray pixels, such as rounded window corners, are allowed).
"""
import sys

import numpy as np
from PIL import Image


def bbox(a):
    m = a.max(axis=2) > 8
    ys = np.where(m.any(axis=1))[0]
    xs = np.where(m.any(axis=0))[0]
    if len(xs) == 0:
        return None
    return int(xs[0]), int(ys[0]), int(xs[-1] - xs[0] + 1), int(ys[-1] - ys[0] + 1)


a_path, b_path, out = sys.argv[1], sys.argv[2], sys.argv[3]
a = np.asarray(Image.open(a_path).convert('RGB')).astype(np.int16)
b = np.asarray(Image.open(b_path).convert('RGB')).astype(np.int16)
assert a.shape == b.shape, (a.shape, b.shape)

print('DirectCRT picture at', bbox(a))
print('DOSBox    picture at', bbox(b))

if len(sys.argv) > 4:
    x, y, w, h = (int(v) for v in sys.argv[4:8])
else:
    x, y, w, h = bbox(b)

# Align: DOSBox's forced-borderless window can put the picture a pixel off
best = None
for dy in range(-2, 3):
    for dx in range(-2, 3):
        sa = a[y + 8:y + h - 8, x + 8:x + w - 8]
        sb = b[y + 8 + dy:y + h - 8 + dy, x + 8 + dx:x + w - 8 + dx]
        m = np.abs(sa - sb).mean()
        if best is None or m < best[0]:
            best = (m, dx, dy)
_, odx, ody = best
print('DOSBox offset from DirectCRT: dx=%d dy=%d' % (odx, ody))
b = np.roll(b, (-ody, -odx), axis=(0, 1))

d = np.abs(a - b)
region = d[y:y + h, x:x + w]
print('region %d,%d %dx%d' % (x, y, w, h))
print('  mean |diff| per channel: %.3f %.3f %.3f' % tuple(region.reshape(-1, 3).mean(axis=0)))
print('  max  |diff|: %d' % region.max())
for t in (0, 1, 2, 4, 8, 16):
    print('  pixels with max channel diff > %2d: %.4f%%' % (t, 100.0 * (region.max(axis=2) > t).mean()))

Image.fromarray(np.clip(d * 8, 0, 255).astype(np.uint8)).resize((a.shape[1] // 2, a.shape[0] // 2), Image.BOX).save(out + '_diff.png')

# 1:1 crops: photo detail, pixel patterns, colour bars
cw, ch = 360, 240
spots = [(x + w // 4, y + h // 3), (x + 3 * w // 4, y + h // 6), (x + w // 8, y + h - ch - 4)]
rows = []
for sx, sy in spots:
    sx, sy = int(sx - cw // 2), int(sy)
    ca = a[sy:sy + ch, sx:sx + cw]
    cb = b[sy:sy + ch, sx:sx + cw]
    rows.append(np.concatenate([ca, np.zeros((ch, 8, 3), np.int16) + 255, cb], axis=1))
sheet = np.concatenate([np.concatenate([r, np.zeros((8, r.shape[1], 3), np.int16) + 255]) for r in rows])
Image.fromarray(sheet.astype(np.uint8)).save(out + '_crops.png')

same = (region.max(axis=2) > 1).mean() < 0.0001
print('  result: %s' % ('same' if same else 'DIFFERENT'))
sys.exit(0 if same else 1)
