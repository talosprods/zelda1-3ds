#!/usr/bin/env python3
"""
A real 3D triangle for the banner: a tetrahedron.

A 3D triangle is four flat triangles: a base and three sides. That is a
tetrahedron, and every
previous attempt here built something else -- first a tapered cube (a wedge),
then an extruded triangle (a prism). Both have a triangular silhouette from
the front, neither is a 3D triangle.

Four faces is a trivial fit: the banner model has six quads with unshared
vertices, each freely placeable, and folding a quad's fourth corner onto its
third turns it into a triangle. So four quads become the four faces, and the
remaining two collapse to a point and disappear.

  quad 0 -> base
  quad 1 -> side A      (base edge 0-1 up to the apex)
  quad 2 -> side B      (base edge 1-2 up to the apex)
  quad 3 -> side C      (base edge 2-0 up to the apex)
  quads 4,5 -> collapsed

The 346-vertex frame mesh is removed the same way. It cannot be used for
faces: its 268 triangles share only 346 vertices, so moving one vertex drags
several triangles with it.

Counts never change, so no offset in the file moves. FaceCulling=Never on this
model (read back with SPICA), so winding does not matter.
"""
import math
import os
import struct
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(TOOLS, "..", "cia-assets")
IN = os.path.join(ASSETS, "spinning_banner.cgfx")
OUT = os.path.join(ASSETS, "tetra.cgfx")

STREAM_SIDES = (0x3BA0, 400, 20)    # 20 verts = quads 0..4, pos+uv
STREAM_FRONT = (0x3D38, 80, 20)     # 4 verts  = quad 5
EDGE = (0x43D0, 8304, 24)           # 346 verts, FBI's frame

# Sized for the banner camera (at (0,1,44.786), 30 degree vertical FOV): the
# visible half-height at the model is about 12, so a shape roughly 13 tall and
# 14 wide fills the frame with a margin.
R = float(os.environ.get("TETRA_R", "8.6"))      # circumradius of the base
# A regular tetrahedron -- all six edges the same length -- has height
# R*sqrt(2) for base circumradius R. An earlier version was not regular: at
# R=8 the base edges were 13.9 and the slanted edges
# 15.3, which is why it looked stretched. Derived rather than typed, so it
# stays regular whatever R is set to.
BASE_Y = float(os.environ.get("TETRA_BASE_Y", str(-R * math.sqrt(2) / 2)))
APEX_Y = float(os.environ.get("TETRA_APEX_Y", str(R * math.sqrt(2) / 2)))
BASE_TILT = math.radians(float(os.environ.get("TETRA_BASE_TILT", "0")))
# The donor cube spans exactly +/-6.947 on every axis, centred on the origin,
# so that is where the menu camera looks. An earlier CY=1.0 here was not read
# from anywhere -- the file contains no cameras and no animations at all
# (checked with SPICA), so every "banner camera" constant I had been sizing
# against was invented.
CY = 0.0


def rot_x(p, a):
    x, y, z = p
    ca, sa = math.cos(a), math.sin(a)
    return (x, y * ca - z * sa, y * sa + z * ca)


def main():
    # Build the base ourselves with exactly the texture this shape needs,
    # instead of relying on the caller to set the right environment variables.
    # Twice now a change was tested by hand with an env var and then shipped
    # without it because build-cia.sh had not been updated to match -- two
    # CIAs went out that silently lacked the change. One entry point, no
    # flags to forget.
    env = dict(os.environ)
    env["BANNER_FACET_TRIFORCE"] = "1"
    env["BANNER_HIRES"] = "1"
    if os.environ.get("TETRA_DIAG"):
        env["BANNER_DIAG"] = "1"          # flat colours, measurement build
    subprocess.run([sys.executable,
                    os.path.join(TOOLS, "gen-spinning-banner.py")],
                   check=True, env=env, stdout=subprocess.DEVNULL)

    if not os.path.exists(IN):
        sys.exit("ERROR: %s is missing -- run gen-spinning-banner.py first" % IN)
    d = bytearray(open(IN, "rb").read())
    original_len = len(d)

    # The base is NOT horizontal, and that is the whole point.
    #
    # With the base flat and pointing straight down it faces away from the
    # menu camera permanently, so it is never drawn -- there was never a hole,
    # the face simply never turns toward you. (For any convex solid seen from
    # roughly eye level you only ever see side faces; FBI's cube never shows
    # its top either.) Raising the solid until the base clears the camera is
    # not an option: the apex then leaves the frame.
    #
    # So tilt the base plane instead. As the menu spins the model about Y, a
    # tilted base sweeps its normal around a cone, and for part of every turn
    # that normal points at the viewer -- the gold underside comes into view
    # once per rotation, whatever height the camera actually sits at (which
    # the file does not tell us).
    #
    # The apex is pinned at (0, APEX_Y, 0), exactly on the rotation axis, so
    # the tip stays dead centre and does not swing (an earlier whole-body tilt
    # made it look like it was lying on its side). Only the base rocks.
    base = []
    for k in range(3):
        a = math.radians(90 + k * 120)
        x, z = R * math.cos(a), R * math.sin(a)
        base.append((x, BASE_Y - z * math.sin(BASE_TILT), z * math.cos(BASE_TILT)))
    apex = (0.0, APEX_Y, 0.0)
    pts = base + [apex]
    b0, b1, b2, ap = pts

    # The texture is four flat gold tones, one per quadrant (see
    # make_facet_shades). Each face samples the middle of its own quadrant, so
    # every face is a single, slightly different shade -- that is what makes
    # the solid read as three-dimensional without normals. One flat colour per
    # face is enough; the shape is what has to work.
    # Tone per face, darkest first: the base points away from the viewer and
    # should read as shadow, not as a bright wedge sticking out (which is what
    # it did when it got the lightest tone). The three sides then step from
    # light to dark so adjacent faces always contrast and the solid reads.
    # Each face maps onto the Triforce drawn in its own quadrant of the
    # texture (see make_facet_triforce). The quadrants are ordered light to
    # dark; the base gets the darkest because it faces away from the viewer.
    # V is flipped: the encoder stores the image upside down, so image row y
    # corresponds to v = 1 - y/size.
    def quad_uv(qi):
        x0 = (qi % 2) * 0.5
        y0 = (qi // 2) * 0.5
        # No inset: the quadrant is filled edge to edge with the face's gold
        # tone, so mapping the face exactly onto it keeps the seams clean.
        apex = (x0 + 0.25, 1.0 - y0)
        bl = (x0, 1.0 - (y0 + 0.5))
        br = (x0 + 0.5, 1.0 - (y0 + 0.5))
        return [apex, bl, br]

    FACE_QUAD = [3, 0, 1, 2]        # base -> darkest, then the three sides

    faces = [
        [b0, b1, b2],      # base
        [b0, b1, ap],      # side A
        [b1, b2, ap],      # side B
        [b2, b0, ap],      # side C
    ]

    # Wind every face so its normal points AWAY from the solid's centre.
    # Backface culling is on (the material's rasterization state culls back
    # faces regardless of the model-level FaceCulling=Never I had been going
    # by), so a face wound the wrong way is simply not drawn -- you look
    # straight through it. That is what happened to the base: on the console
    # you see the tetrahedron slightly from below, exactly where the base's
    # only visible side is, and it was wound facing up into the solid.
    # On hardware you looked straight through it where a texture should be.
    # Offline previews missed it because they render without culling and
    # from slightly above.
    cx = sum(p[0] for p in (b0, b1, b2, ap)) / 4.0
    cy_ = sum(p[1] for p in (b0, b1, b2, ap)) / 4.0
    cz = sum(p[2] for p in (b0, b1, b2, ap)) / 4.0
    for fi, tri in enumerate(faces):
        (ax, ay, az), (bx, by, bz), (ccx, ccy, ccz) = tri
        ux, uy, uz = bx - ax, by - ay, bz - az
        vx, vy, vz = ccx - ax, ccy - ay, ccz - az
        nx = uy * vz - uz * vy
        ny = uz * vx - ux * vz
        nz = ux * vy - uy * vx
        mx, my, mz = (ax + bx + ccx) / 3.0, (ay + by + ccy) / 3.0, (az + bz + ccz) / 3.0
        if nx * (mx - cx) + ny * (my - cy_) + nz * (mz - cz) < 0:
            faces[fi] = [tri[0], tri[2], tri[1]]     # flip to face outward

    # A second copy of the base, wound the other way, placed EARLY in the
    # draw order. Which winding survives the console's backface culling could
    # not be determined from the file (the material's own render state cannot
    # be the one in effect -- read literally it disables colour writes, which
    # would draw nothing at all), so stop guessing and supply both. Exactly one
    # copy passes culling whatever the convention is, so the base area is
    # always filled.
    #
    # Early, not last: this same duplicate as the FINAL quad was a disaster.
    # Drawn after everything else it paints its whole triangle over the
    # finished image. Drawn second, the three side faces come after it and
    # cover it up whenever the base should not be visible.
    faces.insert(1, [faces[0][0], faces[0][2], faces[0][1]])
    FACE_QUAD.insert(1, FACE_QUAD[0])

    # Each quad is two triangles, and -- checked, not assumed -- they do NOT
    # all use the same index pattern:
    #   quad 0,5: [0,1,2] + [2,1,3]
    #   quad 1:   [0,1,2] + [3,1,0]
    #   quad 2,3,4: [0,1,2] + [0,3,1]
    # Folding corner 3 onto corner 2 only degenerates the second triangle for
    # quads 0 and 5. For the others it produces a second, reverse-wound copy of
    # the same face lying exactly on top of the first -- coplanar duplicates
    # that z-fight and show up as bright slivers while rotating. So the fold
    # target is computed per quad: corner 3 is placed on whichever corner also
    # appears in that quad's second triangle, which collapses it properly.
    QUAD_TRIS = {
        0: [[0, 1, 2], [2, 1, 3]],
        1: [[0, 1, 2], [3, 1, 0]],
        2: [[0, 1, 2], [0, 3, 1]],
        3: [[0, 1, 2], [0, 3, 1]],
        4: [[0, 1, 2], [0, 3, 1]],
        5: [[0, 1, 2], [2, 1, 3]],
    }

    # No second, reverse-wound copy of the base. That was an attempted fix and
    # it was the bug: read back from the donor with SPICA, all three materials
    # have DepthWrite=False, so nothing ever writes the depth buffer and the
    # depth test can never reject anything. Visibility is decided purely by
    # backface culling (BOX_SIDES has FaceCulling=BackFace) and draw order.
    # Under those rules a reversed copy of the base is front-facing exactly
    # when the real base is not -- so it always draws, and sitting in the last
    # quad it paints its whole triangle over the finished image with no depth
    # test to stop it. That is the hole that showed on hardware.
    #
    # It is also unnecessary: for a CLOSED CONVEX solid, backface culling alone
    # is exactly sufficient. The front-facing faces tile the silhouette once,
    # with no overlaps and no gaps, so draw order cannot matter and no depth
    # buffer is needed. That is why FBI's cube renders correctly. Keep the
    # solid convex and correctly wound and the render is right by construction.

    slots = [(STREAM_SIDES, i * 4) for i in range(5)] + [(STREAM_FRONT, 0)]
    for i, ((at, size, stride), first) in enumerate(slots):
        if i < len(faces):
            tri = faces[i]
            second = QUAD_TRIS[i][1]
            # Corner 3 goes onto another corner of the second triangle, so that
            # triangle ends up with a repeated vertex and covers no area.
            partner = next(c for c in second if c != 3)
            pos = [tri[0], tri[1], tri[2], None]
            pos[3] = pos[partner]
            # The apex of the drawn Triforce lines up with the face's own apex
            # for the three sides; the base has no apex so any consistent
            # corner order does.
            quv = quad_uv(FACE_QUAD[i])
            uv = [quv[0], quv[1], quv[2], None]
            uv[3] = uv[partner]
        else:
            pos = [(0.0, 0.0, 0.0)] * 4              # unused, draws nothing
            uv = [(0.06, 0.06)] * 4
        for k in range(4):
            o = at + (first + k) * stride
            struct.pack_into("<3f", d, o, *pos[k])
            struct.pack_into("<2f", d, o + 12, *uv[k])

    at, size, stride = EDGE
    for k in range(size // stride):
        struct.pack_into("<3f", d, at + k * stride, 0.0, 0.0, 0.0)

    if len(d) != original_len:
        sys.exit("ERROR: file size changed")
    open(OUT, "wb").write(bytes(d))
    print("Wrote %s (%d bytes, tetrahedron: 4 faces from 4 quads)"
          % (os.path.relpath(OUT, TOOLS), len(d)))


if __name__ == "__main__":
    main()
