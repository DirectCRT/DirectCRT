#!/usr/bin/env python3
"""Translate the DOSBox Staging GLSL shaders to Direct3D 11 bytecode.

Each shader goes GLSL -> SPIR-V (glslang) -> HLSL (SPIRV-Cross) -> SM 4.0
bytecode (fxc).  The output is committed to src/d3d11_shaders/ so that
the build needs no shader tools.  Run this script
again after changing a shader or bumping a tool version.

    python tools/shaders/gen_d3d11_shaders.py fetch     # download/build tools
    python tools/shaders/gen_d3d11_shaders.py generate  # write the outputs
    python tools/shaders/gen_d3d11_shaders.py check     # compare a fresh
                                                        # translation with them

Tools live in build-shadertools/ (ignored by git).  fxc comes from the
Windows SDK; FXC_SDK is the version used for the committed outputs.
"""

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOLS = os.path.join(ROOT, "build-shadertools")
OUT = os.path.join(ROOT, "src", "d3d11_shaders")
SHADERS = os.path.join(ROOT, "third_party", "dosbox-staging", "resources", "shaders")
OWN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hlsl")

GLSLANG_VERSION = "16.6.0"
GLSLANG_URL = ("https://github.com/KhronosGroup/glslang/releases/download/"
               f"{GLSLANG_VERSION}/glslang-{GLSLANG_VERSION}-windows-x86_64-release.zip")
GLSLANG_SHA256 = "82bf434e69b9bb4829de7e2b4bc2c5e7a7861e53d66cf75e5cc70f5f694a8d9b"
SPIRV_CROSS_TAG = "vulkan-sdk-1.4.363.0"
SPIRV_CROSS_URL = ("https://github.com/KhronosGroup/SPIRV-Cross/archive/refs/tags/"
                   f"{SPIRV_CROSS_TAG}.tar.gz")
SPIRV_CROSS_SHA256 = "f708982e88b763ef5b0394ed468fc2c3628f68cdab89f2e7a946cd628c04e721"
FXC_SDK = "10.0.26100.0"

GLSLANG = os.path.join(TOOLS, "glslang", "bin", "glslang.exe")
SPIRV_CROSS = os.path.join(TOOLS, "spirv-cross-build", "Release", "spirv-cross.exe")

# (C identifier, path below the DOSBox shader directory, what it draws into;
# see translate_stage)
DOSBOX_SHADERS = [
    ("image_adjustments", "_internal/image-adjustments.glsl", "texture"),
    ("crt_hyllian", "crt/crt-hyllian.glsl", "window"),
    ("vga_1080p", "crt/vga-1080p.glsl", "window"),
    ("vga_1080p_fake_double_scan", "crt/vga-1080p-fake-double-scan.glsl", "window"),
    ("sharp", "interpolation/sharp.glsl", "window"),
    ("nearest", "interpolation/nearest.glsl", "window"),
    ("bilinear", "interpolation/bilinear.glsl", "window"),
]


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        sys.stderr.write(" ".join(cmd) + "\n" + r.stdout + r.stderr)
        sys.exit(1)
    return r.stdout


# ---------------------------------------------------------------- fetch

def find_cmake():
    c = shutil.which("cmake")
    if c:
        return c
    vswhere = os.path.expandvars(
        r"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe")
    if os.path.exists(vswhere):
        path = run([vswhere, "-latest", "-products", "*", "-property",
                    "installationPath"]).strip()
        c = os.path.join(path, r"Common7\IDE\CommonExtensions\Microsoft"
                               r"\CMake\CMake\bin\cmake.exe")
        if os.path.exists(c):
            return c
    sys.exit("cmake not found")


def download(url, dest, sha256):
    print("downloading", url)
    with urllib.request.urlopen(url) as r, open(dest, "wb") as f:
        shutil.copyfileobj(r, f)
    with open(dest, "rb") as f:
        got = hashlib.sha256(f.read()).hexdigest()
    print("  sha256", got)
    if got != sha256:
        os.remove(dest)
        sys.exit(f"  expected sha256 {sha256}; update it when changing the version")


def fetch():
    os.makedirs(TOOLS, exist_ok=True)
    if not os.path.exists(GLSLANG):
        d = os.path.join(TOOLS, "glslang")
        os.makedirs(d, exist_ok=True)
        z = os.path.join(TOOLS, "glslang.zip")
        download(GLSLANG_URL, z, GLSLANG_SHA256)
        with zipfile.ZipFile(z) as zf:
            zf.extractall(d)
    if not os.path.exists(SPIRV_CROSS):
        t = os.path.join(TOOLS, "spirv-cross.tar.gz")
        download(SPIRV_CROSS_URL, t, SPIRV_CROSS_SHA256)
        src_root = os.path.join(TOOLS, "spirv-cross-src")
        os.makedirs(src_root, exist_ok=True)
        with tarfile.open(t) as tf:
            tf.extractall(src_root, filter="data")
        src = os.path.join(src_root, f"SPIRV-Cross-{SPIRV_CROSS_TAG}")
        build = os.path.join(TOOLS, "spirv-cross-build")
        cmake = find_cmake()
        run([cmake, "-S", src, "-B", build, "-DSPIRV_CROSS_ENABLE_TESTS=OFF",
             "-DSPIRV_CROSS_SHARED=OFF", "-DSPIRV_CROSS_ENABLE_C_API=OFF"])
        run([cmake, "--build", build, "--config", "Release",
             "--target", "spirv-cross"])
    print("glslang:", run([GLSLANG, "--version"]).splitlines()[0])
    print("spirv-cross:", SPIRV_CROSS)


# ------------------------------------------------------------- generate

def find_fxc():
    """FXC_SDK's fxc, so the bytecode doesn't change with the SDKs installed;
    otherwise the one on PATH or the newest, with a warning."""
    kits = os.path.expandvars(r"%ProgramFiles(x86)%\Windows Kits\10\bin")
    c = os.path.join(kits, FXC_SDK, "x64", "fxc.exe")
    if os.path.exists(c):
        return c
    print(f"warning: no fxc from Windows SDK {FXC_SDK}, the outputs may differ")
    c = shutil.which("fxc")
    if c:
        return c
    versions = sorted((v for v in os.listdir(kits) if v.startswith("10.")),
                      key=lambda v: [int(x) for x in v.split(".")])
    for v in reversed(versions):
        c = os.path.join(kits, v, "x64", "fxc.exe")
        if os.path.exists(c):
            return c
    sys.exit("fxc not found")


def prepare_glsl(path):
    """Normalise a DOSBox shader so glslang accepts it.

    GLSL 3.30 has no line continuations (they arrived in 4.20), but desktop
    drivers accept them anyway and the DOSBox shaders use them in macros.
    "sampler" is a keyword under Vulkan rules; some shaders use it as a
    parameter name.
    """
    with open(path, "r", encoding="utf-8", newline="") as f:
        text = f.read()
    text = text.replace("\r\n", "\n")
    text = re.sub(r"\\\n", "", text)
    return re.sub(r"\bsampler\b", "sampler_", text)


# What a window pass's pixel shader gets from the renderer, all in OpenGL
# window coordinates (origin at the bottom left)
WINDOW_CBUFFER = """\
cbuffer DirectCRT : register(b1)
{
    float4 DIRECTCRT_VIEWPORT : packoffset(c0);         // glViewport x, y, w, h
    float2 DIRECTCRT_INPUT_SIZE : packoffset(c1);       // INPUT_SIZE_0
    float DIRECTCRT_WINDOW_HEIGHT : packoffset(c1.z);
};

"""
STRUCT_IN = "struct SPIRV_Cross_Input\n{\n"
FRAG_COORD_FIELD = "    float4 gl_FragCoord : SV_Position;\n"
FRAG_COORD_IN = "    gl_FragCoord = stage_input.gl_FragCoord;\n"
TEXCOORD_IN = "    v_texCoord = stage_input.v_texCoord;\n"

# How far below an exact texel boundary NVIDIA's OpenGL puts v_texCoord.y,
# in pixels; see window_pixel_shader
BOUNDARY_BIAS = "(1.0 / 1024.0)"

# The vertex shaders' v_texCoord, as the window pass recomputes it
TEXCOORD_FORMS = {
    "v_texCoord = vec2(a_position.x + 1.0, a_position.y + 1.0) / 2.0;": "",
    "v_texCoord = vec2(a_position.x + 1.0, a_position.y + 1.0) / 2.0 * INPUT_SIZE_0;":
        " * DIRECTCRT_INPUT_SIZE",
}


def window_pixel_shader(name, glsl, hlsl):
    """A pass that draws into the window gets OpenGL's gl_FragCoord, y
    turned round, and v_texCoord computed from where the pixel is rather
    than interpolated.

    Where a pixel's texture coordinate lands exactly on a texel boundary
    (crt-hyllian at 4K: every 27th row) a shader's floor() and fract() jump,
    and which side the interpolated value lands on decides the picture.
    NVIDIA's OpenGL lands just below it in y, every time; Direct3D 11's
    interpolation lands on either side, whichever way up the pass is drawn.
    So the exact value goes 1/1024 of a pixel lower: well above float
    rounding, well below a visible change. In x the exact value is already
    what OpenGL gets."""
    forms = [f for f in TEXCOORD_FORMS if f in glsl]
    if len(forms) != 1 or hlsl.count(TEXCOORD_IN) != 1:
        sys.exit(f"{name}: v_texCoord isn't computed the way DirectCRT expects")
    scale = TEXCOORD_FORMS[forms[0]]

    if "gl_FragCoord" in hlsl:
        if hlsl.count(FRAG_COORD_IN) != 1:
            sys.exit(f"{name}: can't find where gl_FragCoord is set")
        hlsl = hlsl.replace(
            FRAG_COORD_IN,
            FRAG_COORD_IN + "    gl_FragCoord.y = DIRECTCRT_WINDOW_HEIGHT - gl_FragCoord.y;\n")
    else:
        # Last, where SPIRV-Cross puts it: the inputs before it have to stay
        # in the registers the vertex shader writes
        m = re.search(re.escape(STRUCT_IN) + r"(?:    [^\n]*\n)*?(};\n)", hlsl)
        if hlsl.count(STRUCT_IN) != 1 or not m:
            sys.exit(f"{name}: can't find the pixel shader's inputs")
        hlsl = hlsl[:m.start(1)] + FRAG_COORD_FIELD + hlsl[m.start(1):]

    return WINDOW_CBUFFER + hlsl.replace(
        TEXCOORD_IN,
        "    float2 directcrt_pos = float2(stage_input.gl_FragCoord.x,\n"
        "        DIRECTCRT_WINDOW_HEIGHT - stage_input.gl_FragCoord.y - " + BOUNDARY_BIAS + ");\n"
        "    v_texCoord = (directcrt_pos - DIRECTCRT_VIEWPORT.xy) / DIRECTCRT_VIEWPORT.zw" + scale + ";\n")


def translate_stage(tmp, name, glsl, stage, target):
    """target "texture": the pass draws into a texture, as into an OpenGL
    framebuffer object. Its vertex shader negates y, which puts the rows in
    the same order in memory as OpenGL does, and makes SV_Position the same
    as gl_FragCoord.

    target "window": the pass draws straight into the back buffer, the right
    way up; see window_pixel_shader."""
    src = os.path.join(tmp, f"{name}.glsl")
    with open(src, "w", encoding="utf-8", newline="\n") as f:
        f.write(glsl)
    spv = os.path.join(tmp, f"{name}.{stage}.spv")
    define = "-DVERTEX" if stage == "vert" else "-DFRAGMENT"
    run([GLSLANG, "-V", "-R", "-S", stage, define, "--aml", "--amb",
         "--sdub", "Globals", "0", "0", "-o", spv, src])
    hlsl_args = [SPIRV_CROSS, spv, "--hlsl", "--shader-model", "40"]
    if stage == "vert" and target == "texture":
        hlsl_args.append("--flip-vert-y")
    hlsl = run(hlsl_args)
    if stage == "frag" and target == "window":
        hlsl = window_pixel_shader(name, glsl, hlsl)
    refl = json.loads(run([SPIRV_CROSS, spv, "--reflect"]))
    return hlsl, refl


TYPES = {"float": "D3D11_UNIFORM_FLOAT", "vec2": "D3D11_UNIFORM_VEC2",
         "vec3": "D3D11_UNIFORM_VEC3", "vec4": "D3D11_UNIFORM_VEC4",
         "int": "D3D11_UNIFORM_INT"}


def uniforms_of(refl, hlsl):
    """The members of the Globals cbuffer: name -> (type, byte offset), and
    the cbuffer size. Checked against the packoffsets fxc will see."""
    members, size = {}, 0
    for ubo in refl.get("ubos", []):
        if ubo["name"] != "Globals" or ubo.get("binding", 0) != 0:
            sys.exit(f"unexpected uniform block {ubo}")
        size = ubo["block_size"]
        for m in refl["types"][ubo["type"]]["members"]:
            if m["type"] not in TYPES or "array" in m:
                sys.exit(f"unsupported uniform type {m}")
            members[m["name"]] = (m["type"], m["offset"])
    for typ, name, reg, comp in re.findall(
            r"^\s*(\w+) _\d+_(\w+) : packoffset\(c(\d+)(?:\.([yzw]))?\);", hlsl, re.M):
        off = int(reg) * 16 + "xyzw".index(comp or "x") * 4
        if members.get(name, (None, None))[1] != off:
            sys.exit(f"packoffset of {name} ({off}) disagrees with reflection")
    return members, size


def texture_slot(refl):
    tex = refl.get("textures", [])
    if len(tex) != 1 or tex[0]["name"] != "INPUT_TEXTURE_0":
        sys.exit(f"expected one INPUT_TEXTURE_0, got {tex}")
    return tex[0]["binding"]


def fxc_compile(fxc, tmp, hlsl_path, profile, entry):
    """/Gis: without it fxc rewrites float maths, e.g. (x / y) * y becomes x.
    The OpenGL driver keeps the round trip, and its rounding error decides
    which side of a texel boundary a pixel falls (fake double scan's
    tex2D_linear gets uv = mod_texel / INPUT_SIZE_0 and multiplies it back)."""
    out = os.path.join(tmp, f"{os.path.basename(hlsl_path)}.{entry}.cso")
    run([fxc, "/nologo", "/O3", "/Gis", "/T", profile, "/E", entry, "/Fo", out, hlsl_path])
    with open(out, "rb") as f:
        return f.read()


def spdx_lines(glsl_path):
    """The shader's copyright and licence lines: a translation is a copy, and
    both its MIT and its GPL ask for them to go with it."""
    with open(glsl_path, encoding="utf-8") as f:
        return [line for line in f if line.startswith("// SPDX-")]


def c_bytes(ident, data):
    lines = [f"static const unsigned char {ident}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        lines.append("    " + ",".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)


HEADER_TOP = """\
/* Generated by tools/shaders/gen_d3d11_shaders.py. Do not edit.
 *
 * Direct3D 11 (SM 4.0) bytecode for DOSBox Staging's shaders, translated
 * from third_party/dosbox-staging/resources/shaders, and for DirectCRT's
 * own passes (tools/shaders/hlsl/directcrt.hlsl). Each of DOSBox's keeps
 * its licence: image-adjustments, crt-hyllian, vga-1080p and
 * vga-1080p-fake-double-scan are MIT, sharp, nearest and bilinear
 * GPL-2.0-or-later. Their .hlsl files here, and the GLSL, name the authors.
 *
 * glslang {glslang}, SPIRV-Cross {spirv_cross}, {fxc}
 */

#ifndef D3D11_SHADERS_H
#define D3D11_SHADERS_H

typedef enum D3D11_UNIFORM_TYPE
{{
    D3D11_UNIFORM_FLOAT,
    D3D11_UNIFORM_VEC2,
    D3D11_UNIFORM_VEC3,
    D3D11_UNIFORM_VEC4,
    D3D11_UNIFORM_INT,
}} D3D11_UNIFORM_TYPE;

/* A uniform's place in the vertex and pixel shader cbuffers, -1 if the
 * stage doesn't have it */
typedef struct D3D11_UNIFORM
{{
    const char* name;
    D3D11_UNIFORM_TYPE type;
    int vs_offset;
    int ps_offset;
}} D3D11_UNIFORM;

typedef struct D3D11_SHADER
{{
    const char* name;
    const unsigned char* vs;
    unsigned int vs_size;
    const unsigned char* ps;
    unsigned int ps_size;
    unsigned int vs_cbuffer_size; /* bytes, 0 if none */
    unsigned int ps_cbuffer_size;
    unsigned int texture_slot;    /* INPUT_TEXTURE_0, with its sampler */
    unsigned int window_pass;     /* draws into the window, wants the
                                     DirectCRT cbuffer in b1 */
    const D3D11_UNIFORM* uniforms;
    unsigned int uniform_count;
}} D3D11_SHADER;
"""


def generate(out_dir):
    fxc = find_fxc()
    out = []
    with tempfile.TemporaryDirectory() as tmp:
        for name, rel, target in DOSBOX_SHADERS:
            glsl = prepare_glsl(os.path.join(SHADERS, rel))
            stages = {}
            for stage in ("vert", "frag"):
                hlsl, refl = translate_stage(tmp, name, glsl, stage, target)
                suffix = "vs" if stage == "vert" else "ps"
                path = os.path.join(out_dir, f"{name}.{suffix}.hlsl")
                with open(path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(f"// Generated from {rel} by gen_d3d11_shaders.py\n")
                    f.write("".join(spdx_lines(os.path.join(SHADERS, rel))) + "\n")
                    f.write(hlsl)
                code = fxc_compile(fxc, tmp, path, f"{suffix}_4_0", "main")
                stages[stage] = (code, *uniforms_of(refl, hlsl), refl)

            vs_code, vs_members, vs_size, _ = stages["vert"]
            ps_code, ps_members, ps_size, ps_refl = stages["frag"]
            out.append(c_bytes(f"d3d11_{name}_vs", vs_code))
            out.append(c_bytes(f"d3d11_{name}_ps", ps_code))

            rows = []
            for u in sorted(set(vs_members) | set(ps_members)):
                types = {m[u][0] for m in (vs_members, ps_members) if u in m}
                if len(types) != 1:
                    sys.exit(f"{name}: {u} has different types per stage")
                rows.append('    {{ "{}", {}, {}, {} }},'.format(
                    u, TYPES[types.pop()],
                    vs_members[u][1] if u in vs_members else -1,
                    ps_members[u][1] if u in ps_members else -1))
            # C has no empty initialiser: no uniforms is NULL, 0
            if rows:
                out.append(f"static const D3D11_UNIFORM d3d11_{name}_uniforms[] = {{\n"
                           + "\n".join(rows) + "\n};")
                uniforms = (f"d3d11_{name}_uniforms,"
                            f" sizeof(d3d11_{name}_uniforms) / sizeof(d3d11_{name}_uniforms[0])")
            else:
                uniforms = "NULL, 0"
            out.append(
                f"static const D3D11_SHADER d3d11_{name} = {{\n"
                f'    "{rel}",\n'
                f"    d3d11_{name}_vs, sizeof(d3d11_{name}_vs),\n"
                f"    d3d11_{name}_ps, sizeof(d3d11_{name}_ps),\n"
                f"    {vs_size}, {ps_size}, {texture_slot(ps_refl)}, {int(target == 'window')},\n"
                f"    {uniforms},\n"
                "};")

        own = os.path.join(OWN, "directcrt.hlsl")
        for entry, profile in (("vs_quad", "vs_4_0"), ("ps_convert_pal8", "ps_4_0"),
                               ("ps_convert_rgb565", "ps_4_0"),
                               ("ps_convert_rgb555", "ps_4_0"),
                               ("ps_convert_bgra32", "ps_4_0")):
            out.append(c_bytes(f"d3d11_{entry}", fxc_compile(fxc, tmp, own, profile, entry)))

    glslang_ver = run([GLSLANG, "--version"]).splitlines()[0].split(":")[-1].strip()
    fxc_ver = "fxc " + os.path.basename(os.path.dirname(os.path.dirname(fxc)))
    text = HEADER_TOP.format(glslang=glslang_ver, spirv_cross=SPIRV_CROSS_TAG, fxc=fxc_ver)
    text += "\n" + "\n\n".join(out) + "\n\n#endif\n"
    with open(os.path.join(out_dir, "d3d11_shaders.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def check():
    """Translates the shaders again into a temporary folder and compares the
    result with the committed outputs (line endings aside: git may have
    checked them out with CRLF)."""
    def read(path):
        with open(path, "rb") as f:
            return f.read().replace(b"\r\n", b"\n")

    with tempfile.TemporaryDirectory() as fresh:
        generate(fresh)
        names = sorted(set(os.listdir(fresh)) | set(os.listdir(OUT)))
        differ = []
        for n in names:
            a, b = os.path.join(OUT, n), os.path.join(fresh, n)
            if not os.path.exists(a):
                differ.append(f"{n}: not committed")
            elif not os.path.exists(b):
                differ.append(f"{n}: not generated any more")
            elif read(a) != read(b):
                differ.append(f"{n}: differs")
    rel = os.path.relpath(OUT, ROOT)
    if differ:
        print(f"{rel} doesn't match a fresh translation:")
        print("\n".join("  " + d for d in differ))
        print("Run 'gen_d3d11_shaders.py generate' and commit the result.")
        sys.exit(1)
    print(f"{rel} matches a fresh translation ({len(names)} files)")


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ("fetch", "generate", "check"):
        sys.exit(__doc__)
    if sys.argv[1] == "fetch":
        fetch()
    elif sys.argv[1] == "check":
        check()
    else:
        os.makedirs(OUT, exist_ok=True)
        generate(OUT)
        print("wrote", os.path.relpath(OUT, ROOT))


if __name__ == "__main__":
    main()
