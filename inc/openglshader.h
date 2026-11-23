#ifndef OPENGLSHADER_H
#define OPENGLSHADER_H

/* The game's surface to RGB, before the shaders (render_ogl.c) */

static char PASSTHROUGH_VERT_SHADER[] =
    "#version 130\n"
    "in vec4 VertexCoord;\n"
    "in vec4 COLOR;\n"
    "in vec4 TexCoord;\n"
    "out vec4 COL0;\n"
    "out vec4 TEX0;\n"
    "uniform mat4 MVPMatrix;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    gl_Position = MVPMatrix * VertexCoord;\n"
    "    COL0 = COLOR;\n"
    "    TEX0.xy = TexCoord.xy;\n"
    "}\n";


static char PALETTE_FRAG_SHADER[] =
    "#version 130\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D Texture;\n"
    "uniform sampler2D PaletteTexture;\n"
    "in vec4 TEX0;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec4 pIndex = texture(Texture, TEX0.xy);\n"
    "    FragColor = texture(PaletteTexture, vec2(pIndex.r * (255.0/256.0) + (0.5/256.0), 0));\n"
    "}\n";


static char PASSTHROUGH_FRAG_SHADER[] =
    "#version 130\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D Texture;\n"
    "in vec4 TEX0;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec4 texel = texture(Texture, TEX0.xy);\n"
    "    FragColor = texel;\n"
    "}\n";


static char RGB555_FRAG_SHADER[] =
    "#version 130\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D Texture;\n"
    "in vec4 TEX0;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec4 texel = texture(Texture, TEX0.xy);\n"
    "    int bytes = int(texel.r * 255.0 + 0.5) | int(texel.g * 255.0 + 0.5) << 8;\n"
    "    vec4 color;\n"
    "    color.r = float((bytes >> 10) & 31) / 31.0;\n"
    "    color.g = float((bytes >> 5) & 31) / 31.0;\n"
    "    color.b = float(bytes & 31) / 31.0;\n"
    "    color.a = 1.0;\n"
    "    FragColor = color;\n"
    "}\n";

#endif
