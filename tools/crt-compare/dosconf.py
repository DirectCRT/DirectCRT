"""Writes a DOSBox Staging config that shows DIR/SHOW.COM with crt-auto at
DOSBox's default settings, or others given as NAME=VALUE.

usage: python -I dosconf.py DIR fullscreen|WxH OUT.conf [NAME=VALUE...]
  WxH is the window size in logical units (pixels at 100% scaling)
  NAME=VALUE sets a [render] setting, e.g. 'viewport=relative 120% 90%'
"""
import re
import sys

d, display, out = sys.argv[1], sys.argv[2], sys.argv[3]
settings = [a.split('=', 1) for a in sys.argv[4:]]
fullscreen = display == 'fullscreen'

conf = """[sdl]
output = opengl
display = 0
fullscreen = {fs}
fullscreen_mode = forced-borderless
window_size = {ws}
window_position = 0,0
vsync = off

[dosbox]
machine = svga_s3
startup_verbosity = quiet

[render]
shader = crt-auto
aspect = auto
integer_scaling = auto
viewport = fit
color_space = srgb
image_adjustments = on
crt_color_profile = auto
brightness = 45
contrast = 65
gamma = 0
digital_contrast = 0
black_level = auto
saturation = 0
color_temperature = auto
color_temperature_luma_preserve = 0
red_gain = 100
green_gain = 100
blue_gain = 100
deinterlacing = off
dedithering = off

[cpu]
cpu_cycles = max

[autoexec]
mount c "{d}"
c:
show
""".format(fs='on' if fullscreen else 'off', ws='default' if fullscreen else display, d=d)

for name, value in settings:
    conf, n = re.subn(r'(?m)^' + re.escape(name.strip()) + r' = .*$', lambda m: name.strip() + ' = ' + value.strip(), conf, count=1)
    if n != 1:
        sys.exit('dosconf.py: no [render] setting ' + name)

open(out, 'w').write(conf)
print(out)
