#!/usr/bin/env python3
"""
Bakes the bottom-screen wood-grain backdrop into a static raster image,
pixel-for-pixel replicating DrawSpeckleField()/SpeckleHash() from
Graphics_3ds.cpp so it looks identical to the live-drawn version, but as a
one-time offline render instead of ~840 draw calls + ~2520 hash computes
every frame on-device.

Written after the render-to-texture GPU cache for this same
backdrop failed on real hardware three times in a row (see Graphics_3ds.cpp's
"static wood-grain backdrop" comment) -- a plain static asset, loaded once
into linear RAM exactly like any other tile-sheet texture, sidesteps that
whole class of bug entirely.

Outputs (run from the zelda3ds/ folder):
  cia-assets/woodgrain_preview.png -- the backdrop as a PNG.
  romfs/woodgrain.bin -- an older raw format (4-byte header: u16 width,
    u16 height, then row-major RGBA8). The game no longer reads it.

The game loads romfs/woodgrain.t3x, which is the PNG converted with
devkitPro's tex3ds tool. The converted woodgrain.t3x is committed, so this
only needs re-running when the backdrop changes. Both intermediate outputs are ignored by git.

Usage: python3 gen-woodgrain.py
"""

from PIL import Image
import struct

W, H = 320, 240

def speckle_hash(x: int) -> int:
    x &= 0xFFFFFFFF
    x ^= x >> 16
    x = (x * 0x7FEB352D) & 0xFFFFFFFF
    x ^= x >> 15
    x = (x * 0x846CA68B) & 0xFFFFFFFF
    x ^= x >> 16
    return x

def draw_speckle_field(px_grid, x0, y0, w, h, count, seed, rgb, base_alpha):
    for i in range(count):
        h_len   = speckle_hash(seed * 2 + 1 + i * 7919)
        h_pos   = speckle_hash(seed * 3 + 5 + i * 104729)
        h_shade = speckle_hash(seed * 5 + 9 + i * 200003)

        line_w = w * (0.20 + (h_len % 1000) / 1000.0 * 0.55)
        px = x0 + (h_pos % 1000) / 1000.0 * (w - line_w)
        py = y0 + ((h_pos // 1000) % 1000) / 1000.0 * h
        thick = 1.6 + (1.4 if (h_len >> 10) & 1 else 0.0)
        shade_scale = 0.25 + (h_shade % 1000) / 1000.0 * 0.45
        a = min(255, int(base_alpha * shade_scale))

        x_start = max(0, int(px))
        x_end   = min(W, int(px + line_w) + 1)
        y_start = max(0, int(py))
        y_end   = min(H, int(py + thick) + 1)
        for yy in range(y_start, y_end):
            for xx in range(x_start, x_end):
                # Standard alpha-over blend against whatever's already there.
                sr, sg, sb = rgb
                dr, dg, db = px_grid[yy][xx]
                fa = a / 255.0
                px_grid[yy][xx] = (
                    int(sr * fa + dr * (1 - fa)),
                    int(sg * fa + dg * (1 - fa)),
                    int(sb * fa + db * (1 - fa)),
                )

def main():
    base = (0x22, 0x16, 0x0C)
    grid = [[base for _ in range(W)] for _ in range(H)]

    # Same two passes, same seeds/counts/colors as Graphics_3ds.cpp's
    # Video_DrawBottomMap(): dark streaks then lighter tan streaks.
    draw_speckle_field(grid, 0, 0, W, H, 520, 0xB00D, (0x00, 0x00, 0x00), 0x60)
    draw_speckle_field(grid, 0, 0, W, H, 320, 0xC0FF, (0x8A, 0x64, 0x34), 0x48)

    img = Image.new("RGBA", (W, H))
    pixels = img.load()
    for y in range(H):
        for x in range(W):
            r, g, b = grid[y][x]
            pixels[x, y] = (r, g, b, 255)

    png_path = "cia-assets/woodgrain_preview.png"
    img.save(png_path)
    print(f"Preview PNG: {png_path}")

    bin_path = "romfs/woodgrain.bin"
    with open(bin_path, "wb") as f:
        f.write(struct.pack("<HH", W, H))
        for y in range(H):
            for x in range(W):
                r, g, b, a = pixels[x, y]
                f.write(bytes([r, g, b, a]))
    print(f"Raw asset: {bin_path} ({W}x{H}, {4 + W*H*4} bytes)")

if __name__ == "__main__":
    main()
