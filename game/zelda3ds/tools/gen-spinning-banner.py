#!/usr/bin/env python3
"""
Builds the spinning 3D HOME-menu banner by re-texturing a known-good CGFX.

Why this approach. Generating a banner CGFX from scratch with pycgfx was tried
across a dozen structural variants (untextured/textured, static/animated,
one-/two-sided, several naming schemes incl. Nintendo's NW4C convention) and
every single one rendered *nothing at all* on real hardware -- see
DEVELOPMENT.md SS27. Rather than keep guessing at what the HOME Menu's loader
rejects, this takes a CGFX that demonstrably works and changes only its pixels.

The base is FBI's `meta/banner_3ds.cgfx` (Steveice10/FBI, MIT) -- the
rotating box in FBI's own HOME-menu banner. FBI is
by bannertool's own author, so it is a homebrew-made banner proven on real
consoles. NOTE: this is someone else's model asset (MIT); it is fetched at
build time and not stored in this repository, and it is credited in NOTICE.

What makes this safe to patch: both of its textures are 128x128 ETC1, and ETC1
at 128x128 is exactly 8192 bytes, so new texture data is a byte-for-byte
in-place replacement -- no offsets, references or section sizes change. The two
blobs sit at 0x6480 and 0x8480 and end exactly at EOF, verified by resolving
the PixelBasedImage headers rather than assumed.

Run: python3 gen-spinning-banner.py
Needs: the base CGFX at cia-assets/fbi_banner_base.cgfx (fetched separately,
not committed -- same "don't bundle other people's assets" convention as
tools/bin/), and devkitPro's tex3ds for the ETC1 encode.
"""
import os
import struct
import subprocess
import sys

from PIL import Image, ImageDraw

TOOLS = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(TOOLS, "..", "cia-assets")
BASE = os.path.join(ASSETS, "fbi_banner_base.cgfx")
OUT = os.path.join(ASSETS, "spinning_banner.cgfx")
TEX3DS = os.environ.get("TEX3DS", "/opt/devkitpro/tools/bin/tex3ds")

# Offsets verified from the base file's own PixelBasedImage headers.
TEX_SLOTS = [(0x6480, 8192), (0x8480, 8192)]
TEX_W = TEX_H = 128

GOLD = (216, 168, 64)          # same gold as the 2D banner art
GOLD_DARK = (150, 112, 36)     # shading for the lower triangles
BG = (0, 0, 0)


def make_diag_faces(size=256):
    """Four flat, unmistakable colours -- one per face. A measurement build.

    Every report so far has been ambiguous in the same way: the Triforce notch
    is black, and black against the HOME Menu background is indistinguishable
    from looking straight through a missing face. With no black anywhere and a
    different colour per face, one photo says exactly which faces the console
    draws and which it does not.
    """
    from PIL import Image as _I, ImageDraw as _D
    img = _I.new("RGB", (size, size), (0, 0, 0))
    d = _D.Draw(img)
    h = size // 2
    # quadrant order matches quad_uv(): 0=top-left 1=top-right 2=bottom-left 3=bottom-right
    for qi, col in enumerate([(255, 40, 40), (40, 220, 60), (60, 120, 255), (255, 0, 220)]):
        d.rectangle([(qi % 2) * h, (qi // 2) * h, (qi % 2) * h + h - 1, (qi // 2) * h + h - 1], fill=col)
    return img


def make_facet_triforce(size=128):
    """A Triforce in each of the four quadrants, each at a different brightness.

    On hardware the plain tetrahedron worked, but its underside showed no
    texture.

    The catch is that the six quads this banner is built from carry no normals,
    so the console cannot shade them: paint the same artwork on all four faces
    and the solid collapses back into a flat silhouette. Drawing the artwork
    once per quadrant, each a step darker, gives both -- every face shows the
    Triforce AND every face has its own tone, which is what makes the shape
    read as three-dimensional.
    """
    from PIL import Image as _I, ImageDraw as _D
    ss = 4
    S = size * ss
    h = S // 2
    img = _I.new("RGB", (S, S), (0, 0, 0))
    d = _D.Draw(img)
    # Gold steps light-to-dark per face (that is what stands in for the
    # normals this mesh does not have); the notch is black on every face --
    # (black rather than brown). Keeping the notch a
    # constant black also makes it read as a hole rather than as another
    # shade of the same material.
    NOTCH = (0, 0, 0)
    tones = [
        ((242, 196, 92), NOTCH),
        ((214, 168, 70), NOTCH),
        ((182, 140, 54), NOTCH),
        ((152, 114, 40), NOTCH),
    ]
    for qi, (gold, dark) in enumerate(tones):
        x0 = (qi % 2) * h
        y0 = (qi // 2) * h
        # Fill the WHOLE quadrant with the gold tone, then cut the notch. An
        # earlier version drew the triangle on a black field, which left black
        # around it -- and a face's UVs land right on that boundary, so the
        # edges of the solid picked up black seams where the faces should meet
        # cleanly. With the quadrant filled, any
        # sampling slop at a face edge reads gold instead of black.
        d.rectangle([x0, y0, x0 + h - 1, y0 + h - 1], fill=gold)
        if qi == 3:
            # The base: solid gold, no notch. A notch here is a black triangle
            # on the one face you only ever glimpse edge-on from below, and it
            # reads as a hole straight through the solid rather than as part of
            # the artwork.
            continue
        top = (x0 + h / 2.0, y0)
        bl = (x0, y0 + h)
        br = (x0 + h, y0 + h)
        mid = lambda p, q: ((p[0] + q[0]) / 2.0, (p[1] + q[1]) / 2.0)
        ml, mr, mb = mid(top, bl), mid(top, br), mid(bl, br)
        d.polygon([ml, mr, mb], fill=dark)       # the Triforce notch
    return img.resize((size, size), _I.LANCZOS)


def make_facet_shades(size=128):
    """Four flat gold tones, one per quadrant.

    The six quads this banner is built from carry positions and UVs but NO
    normals (stride 20), so the console cannot light them per face. A
    single-colour tetrahedron would therefore render as one flat silhouette --
    the opposite of looking three-dimensional. Baking a few brightnesses into
    the texture and pointing each face at a different quadrant gives the
    faceted shading that normals would have provided.
    """
    from PIL import Image as _I
    img = _I.new("RGB", (size, size))
    tones = [(236, 190, 86), (208, 162, 66), (176, 134, 50), (148, 110, 38)]
    h = size // 2
    for qi, t in enumerate(tones):
        x0 = (qi % 2) * h
        y0 = (qi // 2) * h
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + h):
                img.putpixel((x, y), t)
    return img


def make_triforce(size=128):
    """Full-bleed gold with only the inverted centre notch cut out.

    The earlier version drew a whole Triforce (outer triangle plus
    notch) on a black field, which was right for a cube face but wrong once
    gen-triforce-prism.py tapers the geometry into a triangle: the geometry
    already provides the outer silhouette, so the texture's black corners just
    came along for the ride and the banner read as a black wedge with gold
    lines on hardware.

    So: no black background and no outer triangle here. Gold everywhere, with
    only the inverted middle triangle dark. Combined with the tapered mesh that
    gives a gold triangle with a notch -- a Triforce -- and it stays right even
    though the UVs are still the cube's and get squeezed toward the apex.
    """
    ss = 4                      # supersample, then downscale for smooth edges
    S = size * ss
    img = Image.new("RGB", (S, S), GOLD)      # gold everywhere, no black field
    d = ImageDraw.Draw(img)
    # The notch: an inverted triangle occupying the middle quarter, matching
    # where it sits in a real Triforce.
    top = (S / 2, 0.0)
    bl = (0.0, S)
    br = (S, S)

    def mid(p, q):
        return ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)

    ml, mr, mb = mid(top, bl), mid(top, br), mid(bl, br)
    if not os.environ.get("TRIFORCE_SOLID_GOLD"):
        d.polygon([ml, mr, mb], fill=BG)
    return img.resize((size, size), Image.LANCZOS)


def encode_etc1(img, tmp_png, tmp_bin):
    img.save(tmp_png)
    subprocess.run(
        [TEX3DS, "-f", "etc1", "-z", "none", "-r", "-o", tmp_bin, tmp_png],
        check=True, capture_output=True,
    )
    raw = open(tmp_bin, "rb").read()
    # tex3ds always emits its 4-byte compression header, even with -z none:
    # one type byte (0x00 = uncompressed) plus a 3-byte little-endian length.
    # Verify it says what we expect rather than blindly slicing.
    if len(raw) >= 4 and raw[0] == 0x00:
        declared = int.from_bytes(raw[1:4], "little")
        if declared == len(raw) - 4:
            return raw[4:]
    return raw


# Layout of the base file, all verified by reading it rather than assumed.
# COMMON1 (the texture our faces actually use, via material BOX_SIDES):
TX_TXOB_H = 0x2694        # height field of the TXOB
TX_TXOB_W = 0x2698        # width field
TX_IMG_HDR = 0x26B8       # PixelBasedImage: height, width, size, ref
IMAG_SEC = 0x3B78         # IMAG section header; its size field is at +4
HDR_FILESIZE = 0x0C       # file size field in the CGFX header


def upgrade_texture_256(data, etc1):
    """Repoint COMMON1 at a 256x256 texture appended to the end of the file.

    The 128x128 slot cannot simply be widened in place: ETC1 at 256x256 is
    32768 bytes against 8192, and the blob sits mid-file with another texture
    right behind it. Appending avoids moving anything that already exists --
    only this texture's own offset changes, plus three length fields that
    describe the file's extent. (A higher-resolution texture removes visible
    pixels along the black notch.)
    """
    import struct as _s
    new_off = len(data)
    data.extend(etc1)

    # PixelBasedImage: height, width, data size, and the (relative) data ref.
    _s.pack_into("<i", data, TX_IMG_HDR + 0, 256)          # height
    _s.pack_into("<i", data, TX_IMG_HDR + 4, 256)          # width
    _s.pack_into("<i", data, TX_IMG_HDR + 8, len(etc1))    # size
    _s.pack_into("<i", data, TX_IMG_HDR + 12, new_off - (TX_IMG_HDR + 12))

    # The TXOB carries its own copy of the dimensions.
    _s.pack_into("<i", data, TX_TXOB_H, 256)
    _s.pack_into("<i", data, TX_TXOB_W, 256)

    # The IMAG section runs to the end of the file, so it grew too.
    old_sec = _s.unpack_from("<I", data, IMAG_SEC + 4)[0]
    _s.pack_into("<I", data, IMAG_SEC + 4, old_sec + len(etc1))

    # And the header's total file size.
    _s.pack_into("<I", data, HDR_FILESIZE, len(data))
    return data


def main():
    if not os.path.exists(BASE):
        sys.exit(
            "ERROR: %s is missing.\n"
            "Fetch it with:\n"
            "  curl -fsSL -o %s "
            "https://raw.githubusercontent.com/Steveice10/FBI/master/meta/banner_3ds.cgfx" % (BASE, BASE)
        )

    data = bytearray(open(BASE, "rb").read())

    # Re-verify the slots against the file's own headers instead of trusting
    # the constants -- if the base file is ever updated, fail loudly rather
    # than corrupting it.
    for hdr, (want_off, want_size) in zip((0x26B8, 0x2714), TEX_SLOTS):
        h, w, size, ref = struct.unpack_from("<4i", data, hdr)
        off = hdr + 12 + ref
        if (h, w, size, off) != (TEX_H, TEX_W, want_size, want_off):
            sys.exit("ERROR: base CGFX does not look as expected "
                     "(header 0x%04X: %dx%d size=%d off=0x%X)" % (hdr, h, w, size, off))

    if os.environ.get("BANNER_DIAG"):
        tex = make_diag_faces(TEX_W)
    elif os.environ.get("BANNER_FACET_TRIFORCE"):
        tex = make_facet_triforce(TEX_W)
    elif os.environ.get("BANNER_FACET_SHADES"):
        tex = make_facet_shades(TEX_W)
    else:
        tex = make_triforce(TEX_W)
    etc1 = encode_etc1(tex, os.path.join(ASSETS, "_tf_tex.png"),
                       os.path.join(ASSETS, "_tf_tex.bin"))
    if len(etc1) != 8192:
        sys.exit("ERROR: ETC1 encode gave %d bytes, expected 8192" % len(etc1))

    if os.environ.get("BANNER_HIRES"):
        if os.environ.get("BANNER_DIAG"):
            big = make_diag_faces(256)
        elif os.environ.get("BANNER_FACET_TRIFORCE"):
            big = make_facet_triforce(256)
        else:
            big = tex.resize((256, 256), Image.LANCZOS)
        etc1_big = encode_etc1(big, os.path.join(ASSETS, "_tf_big.png"),
                               os.path.join(ASSETS, "_tf_big.bin"))
        if len(etc1_big) != 32768:
            sys.exit("ERROR: 256x256 ETC1 gave %d bytes, expected 32768" % len(etc1_big))
        # Write the artwork into EVERY texture slot, not just the ones we
        # think are live. COMMON1 gets repointed to a 256x256 blob appended at
        # EOF, which left its original 128x128 bytes at 0x6480 untouched -- so
        # if the console resolves COMMON1 to its original location for any
        # reason, it shows FBI's stock texture and no colour change we make can
        # ever appear. That is exactly what happened: a banner whose faces were
        # bright magenta and green looked completely unchanged on hardware.
        for off, size in TEX_SLOTS:
            data[off:off + size] = etc1
        upgrade_texture_256(data, etc1_big)
        for f in ("_tf_big.png", "_tf_big.bin"):
            q = os.path.join(ASSETS, f)
            if os.path.exists(q):
                os.remove(q)
    else:
        for off, size in TEX_SLOTS:
            data[off:off + size] = etc1

    open(OUT, "wb").write(bytes(data))
    for f in ("_tf_tex.png", "_tf_tex.bin"):
        p = os.path.join(ASSETS, f)
        if os.path.exists(p):
            os.remove(p)
    print("Wrote %s (%d bytes, %d texture slots replaced)"
          % (os.path.relpath(OUT, TOOLS), len(data), len(TEX_SLOTS)))


if __name__ == "__main__":
    main()
