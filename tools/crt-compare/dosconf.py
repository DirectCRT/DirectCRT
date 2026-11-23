"""Writes a DOSBox Staging config that shows DIR/SHOW.COM with crt-auto at
DOSBox's default settings, or others given as NAME=VALUE.

usage: python -I dosconf.py DIR fullscreen|WxH OUT.conf CAPTUREDIR [NAME=VALUE...]
  WxH is the window size in logical units (pixels at 100% scaling)
  CAPTUREDIR is where DOSBox saves its screenshots (dosshot.ps1 takes one)
  NAME=VALUE sets a [render] setting, e.g. 'viewport=relative 120% 90%'
"""
import re
import sys

d, display, out, capture_dir = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
settings = [a.split('=', 1) for a in sys.argv[5:]]
fullscreen = display == 'fullscreen'

conf = """[sdl]
output = opengl
display = 0
fullscreen = {fs}
fullscreen_mode = standard
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

[capture]
capture_dir = {cap}

[webserver]
webserver_enabled = on
webserver_bind_address = 127.0.0.1
webserver_port = 8086

[autoexec]
mount c "{d}"
c:
show
""".format(fs='on' if fullscreen else 'off', ws='default' if fullscreen else display, d=d, cap=capture_dir)

for name, value in settings:
    conf, n = re.subn(r'(?m)^' + re.escape(name.strip()) + r' = .*$', lambda m: name.strip() + ' = ' + value.strip(), conf, count=1)
    if n != 1:
        sys.exit('dosconf.py: no [render] setting ' + name)

open(out, 'w').write(conf)
print(out)
