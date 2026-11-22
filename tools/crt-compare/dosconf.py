"""Writes a DOSBox Staging config that shows DIR/SHOW.COM with crt-auto at
DOSBox's default settings.

usage: python -I dosconf.py DIR fullscreen|WxH OUT.conf
  WxH is the window size in logical units (pixels at 100% scaling)
"""
import sys

d, display, out = sys.argv[1], sys.argv[2], sys.argv[3]
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

open(out, 'w').write(conf)
print(out)
