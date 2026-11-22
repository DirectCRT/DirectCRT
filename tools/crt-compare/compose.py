"""Makes the comparison picture for one resolution: the left half from ddtest's
photo, the right half from its pixel patterns, and a strip of its colour bars
along the bottom.

usage: python -I compose.py DUMPDIR WxH OUT.png
"""
import sys

from PIL import Image

dump, size, out = sys.argv[1], sys.argv[2], sys.argv[3]
w, h = (int(v) for v in size.split('x'))

photo = Image.open('%s/testimage-%s-32.png' % (dump, size)).convert('RGB')
pixels = Image.open('%s/pixels-%s-32.png' % (dump, size)).convert('RGB')
bars = Image.open('%s/bars-%s-32.png' % (dump, size)).convert('RGB')

img = Image.new('RGB', (w, h))
img.paste(photo.crop((0, 0, w // 2, h)), (0, 0))
img.paste(pixels.crop((w // 2, 0, w, h)), (w // 2, 0))
strip = h // 5
img.paste(bars.crop((0, h - strip, w, h)), (0, h - strip))
img.save(out)
print(out)
