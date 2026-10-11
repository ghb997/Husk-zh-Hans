#!/usr/bin/env python3 -I
"""compare.py <shot.png> [<reference.png>] -- is the screenshot a picture, and does it look like the reference?

Prints "black", or "ok <difference>" / "differs <difference>", where the difference is the mean per-channel distance
(0-255) of the two pictures shrunk to 64x36. Games animate, so the bar is loose: it catches a black screen, a crash
screen, a stuck loading screen or a screen from somewhere else, not a moved sprite.
"""
import os, struct, subprocess, sys, tempfile

LIMIT = 38

def pixels(png):
    with tempfile.TemporaryDirectory() as d:
        bmp = os.path.join(d, "s.bmp")
        subprocess.run(["sips", "-s", "format", "bmp", "-z", "36", "64", png, "--out", bmp],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        b = open(bmp, "rb").read()
    off, = struct.unpack_from("<I", b, 10)
    w, h = struct.unpack_from("<ii", b, 18)
    bpp, = struct.unpack_from("<H", b, 28)
    step = bpp // 8
    row = (w * step + 3) & ~3
    out = []
    for y in range(abs(h)):
        for x in range(w):
            p = off + y * row + x * step
            out.append(b[p:p + 3])
    return out

def main():
    shot = pixels(sys.argv[1])
    bright = sum(sum(p) for p in shot) / (3 * len(shot))
    if bright < 6:
        print("black"); return 1
    if len(sys.argv) < 3 or not os.path.exists(sys.argv[2]):
        print("ok -"); return 0
    ref = pixels(sys.argv[2])
    n = min(len(ref), len(shot))
    diff = sum(abs(a - b) for i in range(n) for a, b in zip(shot[i], ref[i])) / (3 * n)
    print(("ok" if diff <= LIMIT else "differs") + " %.1f" % diff)
    return 0 if diff <= LIMIT else 1

sys.exit(main())
