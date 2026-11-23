"""Builds a DOS test program that shows an image in a VESA mode, plus the
same image as an RGB PNG for ddtest.

usage: python -I mkdos.py SRC.png MODE OUTDIR
  SRC.png  any image, already at the mode's resolution
  MODE     VESA/VGA mode number, e.g. 0x13, 0x151, 0x101, 0x103

Writes OUTDIR/SHOW.COM, OUTDIR/IMG.BIN and OUTDIR/ref.png.

The palette only uses 6-bit DAC values that both DOSBox conversions
(c*255+31)/63 and (c<<2)|(c>>4) turn into the same 8-bit value, so the
RGB PNG has exactly the colours DOSBox shows.
"""
import struct
import sys

from PIL import Image


def six_to_eight(c):
    return (c * 255 + 31) // 63


SAFE6 = [c for c in range(64) if six_to_eight(c) == ((c << 2) | (c >> 4))]


def nearest_safe6(v8):
    return min(SAFE6, key=lambda c: abs(six_to_eight(c) - v8))


def quantize(img):
    img = img.convert('RGB')
    q = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:768]
    pal += [0] * (768 - len(pal))
    pal6 = [nearest_safe6(v) for v in pal]
    pal8 = [six_to_eight(c) for c in pal6]
    idx = q.tobytes()
    ref = Image.frombytes('P', img.size, idx)
    ref.putpalette(pal8)
    return pal6, idx, ref.convert('RGB')


def assemble():
    """A tiny two-pass assembler for the 16-bit code below (COM, org 100h)."""
    code = []   # items: bytes, ('label', name), ('rel8', name), ('rel16', name), ('abs16', name, add)

    def b(*xs):
        code.append(bytes(xs))

    def lab(n):
        code.append(('label', n))

    def rel8(n):
        code.append(('rel8', n))

    def rel16(n):
        code.append(('rel16', n))

    def abs16(n, add=0):
        code.append(('abs16', n, add))

    # open IMG.BIN
    b(0xB8, 0x00, 0x3D)                 # mov ax,3D00h
    b(0xBA); abs16('fname')             # mov dx,fname
    b(0xCD, 0x21)                       # int 21h
    b(0x73, 0x03)                       # jnc +3
    b(0xE9); rel16('fail')              # jmp fail
    b(0xA3); abs16('handle')            # mov [handle],ax
    # read the header: mode, width, height, 768 palette bytes
    b(0x89, 0xC3)                       # mov bx,ax
    b(0xB4, 0x3F)                       # mov ah,3Fh
    b(0xB9, 0x06, 0x03)                 # mov cx,774
    b(0xBA); abs16('hdr')               # mov dx,hdr
    b(0xCD, 0x21)                       # int 21h
    # set the mode with VBE
    b(0xB8, 0x02, 0x4F)                 # mov ax,4F02h
    b(0x8B, 0x1E); abs16('hdr')         # mov bx,[hdr]
    b(0xCD, 0x10)                       # int 10h
    # bytes per line from the VBE mode info, else the width
    b(0xB8, 0x01, 0x4F)                 # mov ax,4F01h
    b(0x8B, 0x0E); abs16('hdr')         # mov cx,[hdr]
    b(0xBF); abs16('modeinfo')          # mov di,modeinfo
    b(0xCD, 0x10)                       # int 10h
    b(0x3D, 0x4F, 0x00)                 # cmp ax,004Fh
    b(0x75); rel8('nopitch')            # jne nopitch
    b(0xA1); abs16('modeinfo', 0x10)    # mov ax,[modeinfo+10h]
    b(0xA3); abs16('pitch')             # mov [pitch],ax
    b(0xEB); rel8('pitchok')            # jmp pitchok
    lab('nopitch')
    b(0xA1); abs16('hdr', 2)            # mov ax,[hdr+2]
    b(0xA3); abs16('pitch')             # mov [pitch],ax
    lab('pitchok')
    # palette
    b(0xBA, 0xC8, 0x03)                 # mov dx,3C8h
    b(0x30, 0xC0)                       # xor al,al
    b(0xEE)                             # out dx,al
    b(0x42)                             # inc dx
    b(0xBE); abs16('hdr', 6)            # mov si,hdr+6
    b(0xB9, 0x00, 0x03)                 # mov cx,768
    b(0xF3, 0x6E)                       # rep outsb
    # bank 0, es:di = A000:0000
    b(0x31, 0xD2)                       # xor dx,dx
    b(0xE8); rel16('setbank')           # call setbank
    b(0xB8, 0x00, 0xA0)                 # mov ax,0A000h
    b(0x8E, 0xC0)                       # mov es,ax
    b(0x31, 0xFF)                       # xor di,di
    b(0x8B, 0x2E); abs16('hdr', 4)      # mov bp,[hdr+4]
    lab('rowloop')
    b(0x57)                             # push di
    b(0xB4, 0x3F)                       # mov ah,3Fh
    b(0x8B, 0x1E); abs16('handle')      # mov bx,[handle]
    b(0x8B, 0x0E); abs16('hdr', 2)      # mov cx,[hdr+2]
    b(0xBA); abs16('rowbuf')            # mov dx,rowbuf
    b(0xCD, 0x21)                       # int 21h
    b(0x5F)                             # pop di
    b(0xBE); abs16('rowbuf')            # mov si,rowbuf
    b(0x8B, 0x0E); abs16('hdr', 2)      # mov cx,[hdr+2]
    lab('copyloop')
    b(0xA4)                             # movsb
    b(0x85, 0xFF)                       # test di,di
    b(0x75); rel8('nowrap')             # jnz nowrap
    b(0xE8); rel16('nextbank')          # call nextbank
    lab('nowrap')
    b(0xE2); rel8('copyloop')           # loop copyloop
    b(0xA1); abs16('pitch')             # mov ax,[pitch]
    b(0x2B, 0x06); abs16('hdr', 2)      # sub ax,[hdr+2]
    b(0x01, 0xC7)                       # add di,ax
    b(0x73); rel8('noskipwrap')         # jnc noskipwrap
    b(0xE8); rel16('nextbank')          # call nextbank
    lab('noskipwrap')
    b(0x4D)                             # dec bp
    b(0x75); rel8('rowloop')            # jnz rowloop
    # 'DCRT' at 0040:00F0 (the BIOS inter-application area): the picture is
    # up, for dosshot.ps1, which reads it through DOSBox's web server
    b(0x31, 0xC0)                       # xor ax,ax
    b(0x8E, 0xC0)                       # mov es,ax
    b(0x26, 0xC7, 0x06, 0xF0, 0x04, 0x44, 0x43)  # mov word [es:4F0h],'DC'
    b(0x26, 0xC7, 0x06, 0xF2, 0x04, 0x52, 0x54)  # mov word [es:4F2h],'RT'
    # wait for a key, back to text mode
    b(0x30, 0xE4)                       # xor ah,ah
    b(0xCD, 0x16)                       # int 16h
    b(0xB8, 0x03, 0x00)                 # mov ax,0003h
    b(0xCD, 0x10)                       # int 10h
    lab('fail')
    b(0xB8, 0x00, 0x4C)                 # mov ax,4C00h
    b(0xCD, 0x21)                       # int 21h

    lab('nextbank')
    b(0x8B, 0x16); abs16('bank')        # mov dx,[bank]
    b(0x42)                             # inc dx
    lab('setbank')
    b(0x89, 0x16); abs16('bank')        # mov [bank],dx
    b(0x50, 0x53, 0x51, 0x52)           # push ax, bx, cx, dx
    b(0xB8, 0x05, 0x4F)                 # mov ax,4F05h
    b(0x31, 0xDB)                       # xor bx,bx
    b(0xCD, 0x10)                       # int 10h
    b(0x5A, 0x59, 0x5B, 0x58)           # pop dx, cx, bx, ax
    b(0xC3)                             # ret

    lab('fname'); code.append(b'IMG.BIN\x00')
    lab('handle'); code.append(b'\x00\x00')
    lab('pitch'); code.append(b'\x00\x00')
    lab('bank'); code.append(b'\x00\x00')
    lab('end')

    def size(item):
        if isinstance(item, bytes):
            return len(item)
        return {'label': 0, 'rel8': 1, 'rel16': 2, 'abs16': 2}[item[0]]

    labels = {}
    pc = 0x100
    for item in code:
        if not isinstance(item, bytes) and item[0] == 'label':
            labels[item[1]] = pc
        pc += size(item)

    # Uninitialised data after the program
    labels['hdr'] = labels['end']
    labels['modeinfo'] = labels['hdr'] + 774
    labels['rowbuf'] = labels['modeinfo'] + 256

    out = bytearray()
    pc = 0x100
    for item in code:
        n = size(item)
        if isinstance(item, bytes):
            out += item
        elif item[0] == 'rel8':
            d = labels[item[1]] - (pc + 1)
            assert -128 <= d <= 127, item
            out += struct.pack('<b', d)
        elif item[0] == 'rel16':
            out += struct.pack('<h', labels[item[1]] - (pc + 2))
        elif item[0] == 'abs16':
            out += struct.pack('<H', labels[item[1]] + item[2])
        pc += n

    assert labels['rowbuf'] + 2048 < 0xF000
    return bytes(out)


def main():
    src, mode, outdir = sys.argv[1], int(sys.argv[2], 0), sys.argv[3]
    img = Image.open(src)
    w, h = img.size
    assert w <= 2048
    pal6, idx, ref = quantize(img)
    with open(outdir + '/IMG.BIN', 'wb') as f:
        f.write(struct.pack('<HHH', mode, w, h))
        f.write(bytes(pal6))
        f.write(idx)
    with open(outdir + '/SHOW.COM', 'wb') as f:
        f.write(assemble())
    ref.save(outdir + '/ref.png')
    print('%s: %dx%d, mode 0x%X, %d palette entries' % (outdir, w, h, mode, len(set(idx))))


if __name__ == '__main__':
    main()
