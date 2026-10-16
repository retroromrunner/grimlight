#!/usr/bin/env python3
"""GRIMLIGHT frame compositor: renders a pixel-exact 240x160 frame from an mGBA
debugger x/ dump of VRAM + palette RAM + OAM. Usage: render.py dump.txt out.png"""
import re, sys, zlib, struct

def parse_dump(path):
    mem = {}
    for line in open(path):
        m = re.match(r"^(0x[0-9A-Fa-f]+):\s+((?:[0-9A-Fa-f]{4,8}\s*)+)$", line.strip())
        if not m: continue
        addr = int(m.group(1), 16)
        vals = m.group(2).split()
        width = len(vals[0])
        for v in vals:
            n = int(v, 16)
            if width == 4:
                mem[addr] = n & 0xFF; mem[addr + 1] = (n >> 8) & 0xFF; addr += 2
            else:
                for i in range(4): mem[addr + i] = (n >> (8 * i)) & 0xFF
                addr += 4
    return mem

def u16(mem, a): return mem.get(a, 0) | (mem.get(a + 1, 0) << 8)

def rgb15(c):
    r = (c & 31) << 3; g = ((c >> 5) & 31) << 3; b = ((c >> 10) & 31) << 3
    return (r | r >> 5, g | g >> 5, b | b >> 5)

def tile_pixel(mem, base, tile, px, py):
    a = base + tile * 32 + py * 4 + (px >> 1)
    byte = mem.get(a, 0)
    return (byte >> 4) & 15 if px & 1 else byte & 15

def render(mem):
    W, H = 240, 160
    bgpal = [rgb15(u16(mem, 0x05000000 + i * 2)) for i in range(256)]
    objpal = [rgb15(u16(mem, 0x05000200 + i * 2)) for i in range(256)]
    img = [[bgpal[0]] * W for _ in range(H)]  # backdrop
    layers = []  # (priority, kind, data)
    # BG0 priority 2, BG1 priority 0
    layers.append((2, "bg", (0x06000000, 0x06008000)))
    layers.append((0, "bg", (0x06004000, 0x06008800)))
    objs = []
    for i in range(128):
        a = 0x07000000 + i * 8
        at0, at1, at2 = u16(mem, a), u16(mem, a + 2), u16(mem, a + 4)
        if (at0 >> 8) & 3 == 2: continue  # hidden
        y = at0 & 0xFF; x = at1 & 0x1FF
        if y >= 160 and y < 256 - 64: continue
        shape = (at0 >> 14) & 3; size = (at1 >> 14) & 3
        dims = {0: [(8, 8), (16, 16), (32, 32), (64, 64)],
                1: [(16, 8), (32, 8), (32, 16), (64, 32)],
                2: [(8, 16), (8, 32), (16, 32), (32, 64)]}[shape][size]
        objs.append((at2 >> 10 & 3, i, x, y, dims, at2 & 0x3FF, (at1 >> 12) & 1, (at1 >> 13) & 1, (at2 >> 12) & 15))
    layers.append((1, "obj", objs))
    for prio in (2, 1, 0):  # draw back to front: BG0(2), OBJ(1), BG1(0)
        for lp, kind, data in layers:
            if lp != prio: continue
            if kind == "bg":
                tiles_base, map_base = data
                for ty in range(20):
                    for tx in range(30):
                        e = u16(mem, map_base + (ty * 32 + tx) * 2)
                        tile, hf, vf, bank = e & 0x3FF, (e >> 10) & 1, (e >> 11) & 1, (e >> 12) & 15
                        for py in range(8):
                            for px in range(8):
                                sx = 7 - px if hf else px; sy = 7 - py if vf else py
                                ci = tile_pixel(mem, tiles_base, tile, sx, sy)
                                if ci == 0: continue
                                img[ty * 8 + py][tx * 8 + px] = bgpal[bank * 16 + ci]
            else:
                for oprio, idx, x, y, (w, h), tile, hf, vf, bank in sorted(data, key=lambda o: -o[1]):
                    if oprio != prio and False: pass
                    tw = w // 8
                    for py in range(h):
                        for px in range(w):
                            gx = x + px if x < 256 else x + px - 512
                            gy = y + py if y < 128 else y + py - 256
                            if not (0 <= gx < W and 0 <= gy < H): continue
                            spx = w - 1 - px if hf else px; spy = h - 1 - py if vf else py
                            t = tile + (spy // 8) * tw + (spx // 8)
                            ci = tile_pixel(mem, 0x06010000, t, spx % 8, spy % 8)
                            if ci == 0: continue
                            img[gy][gx] = objpal[bank * 16 + ci]
    return img

def write_png(img, path):
    H = len(img); W = len(img[0])
    raw = b""
    for row in img:
        raw += b"\x00" + b"".join(bytes(p) for p in row)
    def chunk(typ, data):
        c = struct.pack(">I", len(data)) + typ + data
        return c + struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)

if __name__ == "__main__":
    mem = parse_dump(sys.argv[1])
    img = render(mem)
    write_png(img, sys.argv[2])
    print("rendered", sys.argv[2], f"({len(mem)} bytes parsed)")
