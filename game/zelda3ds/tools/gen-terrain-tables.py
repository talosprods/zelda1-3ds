#!/usr/bin/env python3
"""
Single source of truth for how overworld tiles are classified on the
bottom-screen live map, and which tiles sit in the stereoscopic Mid layer.

Writes two C++ headers (both checked in):
  patcher/src/extract/TerrainClasses.gen.h  tile -> terrain class + colours,
                                       used by RomExtract::BuildTerrain() and
                                       Graphics_3ds.cpp BuildTerrainTexture()
  ../Loz/Game/Loz/OverworldMidLayerTiles.gen.h   used by World.cpp

History: this used to bake a terrain.t3x atlas from the extracted room data.
The public build ships no ROM data, so the console now builds that atlas
itself from the player's ROM; only the classification lives here.

Usage:
  python3 tools/gen-terrain-tables.py              regenerate the headers
  python3 tools/gen-terrain-tables.py <assetdir>   ...and check the half-tile
        rule against real room layouts, where <assetdir> is the output of
        `patcher/romextract <rom.nes> <assetdir>`
"""
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

ROWS, COLS = 11, 16
NUM_ROOMS = 128
MAX_COL_START_OFFSET = (COLS - 1) * ROWS   # =165, matches World.cpp's constant


def load_room_grids(asset_dir):
    """Decode every overworld room's 11x16 tileRef grid from extracted
    assets, replicating WorldImpl::LoadLayout() (World.cpp)."""
    rd = lambda n: open(os.path.join(asset_dir, n), "rb").read()
    attr_data = rd("overworldRoomAttr.dat")
    roomcols_data = rd("overworldRoomCols.dat")
    tab_data = rd("overworldCols.tab")
    tab_length = struct.unpack_from("<H", tab_data, 0)[0]
    tab_buf = tab_data[2:]
    tab_offsets = [struct.unpack_from("<H", tab_buf, i*2)[0] for i in range(tab_length)]
    heap_base = tab_length * 2

    def decode(unique_id):
        grid = [[0]*COLS for _ in range(ROWS)]
        column_desc = roomcols_data[unique_id*16: unique_id*16+16]
        for i in range(COLS):
            desc = column_desc[i]
            table = tab_buf[heap_base + tab_offsets[(desc & 0xF0) >> 4]:]
            column_index = desc & 0x0F
            j = k = 0
            while j <= MAX_COL_START_OFFSET:
                if table[j] & 0x80:
                    if k == column_index:
                        break
                    k += 1
                j += 1
            assert j <= MAX_COL_START_OFFSET
            r = 0
            while r < ROWS:
                t = table[j]
                grid[r][i] = t & 0x3F
                r += 1
                if (t & 0x40) and r < ROWS:
                    grid[r][i] = t & 0x3F
                    r += 1
                j += 1
        return grid

    uids = {attr_data[r*7] & 0x7F for r in range(NUM_ROOMS)}
    return [decode(u) for u in sorted(uids)], rd("overworldTileAttrs.dat")


# --- Terrain classification ---------------------------------------------
# Colours deliberately match BiomeColor()'s existing palette (Graphics_3ds.cpp)
# so this reads as "the same style, now per-tile" rather than a new look.
# Ref ranges cross-checked against World.h's Tile_* enum + TileAttr::GetAction
# (via overworldTileAttrs.dat), not guessed.
COLOR_WATER    = (0x3E, 0x6F, 0xA6, 255)   # Ladder-action tiles: real water
COLOR_FOREST   = (0x3F, 0x7A, 0x3C, 255)   # tree/bush/forest-wall/forest-edge
COLOR_MOUNTAIN = (0x8A, 0x6E, 0x52, 255)   # solid rock / mountain-ridge tiles
# A note that applies to every non-green colour here, learned the hard way on
# a small light speck surrounded by saturated green does not read
# as its own colour, it reads as pink/mauve, because green's complement is
# magenta and simultaneous contrast pulls a neutral or warm speck that way.
# That is why repeated "purple/brown on the map" reports pointed at values that
# are nominally brown or grey. Two consequences, both applied below: prefer
# folding small scattered obstacles into the surrounding terrain colour instead
# of giving them their own, and where a colour must stay distinct, lean it
# slightly toward green so the contrast pulls it back to neutral. Check any new
# colour by rendering it as specks against the real COLOR_GROUND, not by eye in
# isolation.
#
# Graveyard/armos stone. The old value here was (0x8A,0x8A,0x92) -- a literally
# lilac grey, the single most purple-reading colour on the map, and scattered
# armos tiles put specks of it in ordinary green clearings. Same treatment as
# above: leaned slightly toward green so the surrounding green pulls it back to
# neutral stone instead of pushing it to mauve. The graveyard region still
# reads as its own distinct place, it just no longer flecks purple.
COLOR_GRAVE    = (0x86, 0x8C, 0x84, 255)   # headstone / armos statue tiles
COLOR_SAND     = (0xC7, 0xB0, 0x78, 255)   # Tile_Sand (0x37)
COLOR_GROUND   = (0x58, 0x94, 0x4E, 255)   # open ground/plains/path/cave-mouth

# --- Terrain families ----------------------------------------------------
# ONE list per terrain, holding *every* tile of that terrain: the full tiles
# and the half/corner/edge variants together, deliberately not separated.
#
# This grouping is the whole point, and it is what three rounds of bugs kept
# getting wrong: corner and half tiles were treated as something different
# from whole tiles at every level, while a diagonal forest edge is simply
# bushes, half a tile of them. The map must not distinguish a
# half bush from a whole one, because that difference means nothing to
# someone reading the map.
#
# Why the mistakes kept happening, so the next person doesn't repeat them:
# a tile's *graphic* cannot tell you its terrain here. The same tileRef is
# drawn with whichever palette row the room specifies, so one tile is green
# foliage in a forest room and brown rock in a mountain room. Judging from a
# raw sheet render (or a single screenshot) put the forest wall's diagonal
# corners 0x32-0x35 in with the mountains, which is exactly the brown/purple
# flecking along every forest edge.
# Classification is therefore settled from the *room layouts*: across all 128
# overworld rooms, 0x32-0x35 border forest wall (0x1A/0x1B) and/or open
# ground and never once a mountain tile. verify_half_tiles() below turns that
# reasoning into an automatic check so it cannot silently rot again.
TERRAIN_FAMILIES = {
    # name        colour           tiles (full + half/corner variants together)
    "water":    (COLOR_WATER,    {0x05, 0x06, 0x07, 0x08, 0x09, 0x17, 0x18,
                                  0x15, 0x16}),                 # 15/16 = half shore
    # Everything you simply cannot walk through on green terrain is one colour.
    # That includes the lone boulders (0x13, Tile_Rock): they are not bushes,
    # but on a 1-pixel-per-tile map the distinction buys the player nothing and
    # a pale speck scattered through a green clearing just reads as noise.
    # Same principle as the half
    # tiles: don't draw a difference that doesn't matter to someone reading the
    # map. Route information is preserved either way -- blocked is blocked.
    "forest":   (COLOR_FOREST,   {0x19, 0x1A, 0x1B, 0x28,       # wall + burnable bush
                                  0x1C, 0x1D, 0x20,            # freestanding tree body
                                  0x1E, 0x1F,                  # ...its half quarters
                                  0x32, 0x33, 0x34, 0x35,      # wall's diagonal corners
                                  0x13, 0x26,                  # lone boulders (0x26 = pushable)
                                  0x27}),                      # bombable secret, sits IN the wall
    "mountain": (COLOR_MOUNTAIN, {0x02, 0x03, 0x22, 0x24, 0x25, 0x2D, 0x36,
                                  0x21, 0x23}),                 # 21/23 = half facade
    "grave":    (COLOR_GRAVE,    {0x14, 0x29, 0x2A, 0x2B, 0x2C}),
    "sand":     (COLOR_SAND,     {0x37}),
    "ground":   (COLOR_GROUND,   {0x00, 0x01, 0x0A, 0x0B, 0x0D, 0x0E, 0x10,
                                  0x11, 0x12, 0x2E, 0x2F, 0x30,
                                  0x31,
                                  0x0C, 0x0F}),                 # 0C/0F = cave mouths
}

# --- Stereoscopic depth layer --------------------------------------------
# Which overworld tiles are drawn into the Mid (screen-plane) 3D layer instead
# of Ground, i.e. everything that visually *stands on* the terrain rather than
# being the terrain: trees, bushes, the forest wall and its corners, boulders,
# headstones, armos statues. Emitted as a C++ header consumed by World.cpp's
# IsOverworldTreeTile(), so the depth layer and the map colour are decided by
# this one table instead of two hand-kept lists that drift apart.
#
# The whole forest family qualifies by definition. The extras below are the
# standing objects that are their own terrain colour but are still objects:
#   0x14 headstone, 0x2C armos statue (both "grave" coloured).
# Secret tiles are deliberately NOT special-cased: 0x26 (push), 0x27 (bomb) and
# 0x28 (burn) are ordinary bushes/boulders that merely happen to hide
# something, and they live in the forest family with everything else. 0x27 was
# the one bush on the screen above the start room that sat in the wrong 3D
# layer, and it is a secret spot. It is embedded
# in the forest wall (72 of its 78 neighbour samples are 0x1B), so leaving it
# on Ground made exactly one bush in the wall sit at a different depth, which
# also quietly advertised the secret.
MID_LAYER_EXTRA = {0x14, 0x2C}

# Tiles allowed to differ from the terrain they border, with the reason.
# Keep this list tiny -- every entry is a hole in the check below.
HALF_TILE_EXEMPT = {
    # Cave/dungeon mouths: half-blocked (you enter from below), but they are
    # openings in the terrain rather than terrain, and the map marks them with
    # its own POI badges. Ground-coloured on purpose so the entrance reads as
    # somewhere you can walk into.
    0x0C, 0x0F,
}

_REF_TO_FAMILY = {}
for _name, (_colour, _refs) in TERRAIN_FAMILIES.items():
    for _ref in _refs:
        assert _ref not in _REF_TO_FAMILY, "tile 0x%02X is in two families" % _ref
        _REF_TO_FAMILY[_ref] = _name


def tile_family(tile_ref):
    # No silent default: an unclassified tile is a bug to fix here, not
    # something to quietly paint as ground (that is how tiles used to slip
    # through unnoticed).
    if tile_ref not in _REF_TO_FAMILY:
        raise KeyError("tileRef 0x%02X is not in any TERRAIN_FAMILIES entry; "
                       "classify it (see verify_half_tiles)" % tile_ref)
    return _REF_TO_FAMILY[tile_ref]


def tile_color(tile_ref):
    return TERRAIN_FAMILIES[tile_family(tile_ref)][0]


MID_LAYER_REFS = TERRAIN_FAMILIES["forest"][1] | MID_LAYER_EXTRA

GEN_HEADER = os.path.join(ROOT, "..", "Loz", "Game", "Loz",
                          "OverworldMidLayerTiles.gen.h")


def write_mid_layer_header():
    """Emit MID_LAYER_REFS as a C++ lookup table for World.cpp."""
    table = ["true " if ref in MID_LAYER_REFS else "false"
             for ref in range(64)]
    rows = ",\n".join("    " + ", ".join(table[i:i + 8])
                      for i in range(0, 64, 8))
    text = (
        "// GENERATED FILE -- do not edit by hand.\n"
        "// Written by zelda3ds/tools/gen-terrain-tables.py from its\n"
        "// TERRAIN_FAMILIES/MID_LAYER_EXTRA tables; edit those and rebuild\n"
        "// (python3 zelda3ds/tools/gen-terrain-tables.py).\n"
        "//\n"
        "// true = this overworld tileRef is drawn into the Mid stereoscopic\n"
        "// depth layer (it stands on the terrain) rather than Ground (it is\n"
        "// the terrain). Indexed by tileRef, 0x00-0x3F. See\n"
        "// WorldImpl::DrawMap()/IsOverworldTreeTile() in World.cpp.\n"
        "#pragma once\n\n"
        "static const bool kOverworldMidLayerTile[64] = {\n"
        + rows + "\n};\n")

    write_if_changed(GEN_HEADER, text)


def verify_half_tiles(room_grids, attrs):
    """Every half tile must be in the same family as the terrain it is half OF.

    This is the check that would have caught all three rounds of the
    "purple corners/edges" bug on the first run instead of on the
    console. A half tile (one or more, but not all, blocked quadrants) is
    never its own kind of terrain -- it is the diagonal/edge piece that
    finishes a run of some full terrain. So: find the blocked tile it most
    often sits next to in the real room layouts, and require that both are in
    the same family. If someone later files a corner tile under the wrong
    terrain, generation fails loudly here instead of quietly painting a
    different colour along an edge.
    """
    def blocked_quadrants(ref):
        return attrs[ref] & 0x0F if ref < len(attrs) else 0

    neighbours = {}
    for grid in room_grids:
        for r in range(ROWS):
            for c in range(COLS):
                counts = neighbours.setdefault(grid[r][c], {})
                for dr, dc in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    rr, cc = r + dr, c + dc
                    if 0 <= rr < ROWS and 0 <= cc < COLS:
                        counts[grid[rr][cc]] = counts.get(grid[rr][cc], 0) + 1

    problems = []
    for ref, counts in sorted(neighbours.items()):
        quads = blocked_quadrants(ref)
        if quads in (0x00, 0x0F):      # fully open or fully solid: not a half tile
            continue
        if ref in HALF_TILE_EXEMPT:
            continue
        # The dominant *blocked* neighbour is the terrain this piece belongs to;
        # open ground is the thing it borders, not the thing it is made of.
        blocked_nb = [(n, ref2) for ref2, n in counts.items()
                      if blocked_quadrants(ref2) != 0x00]
        if not blocked_nb:
            continue
        _, dominant = max(blocked_nb)
        if tile_family(ref) != tile_family(dominant):
            problems.append(
                "  half tile 0x%02X is '%s' but sits against 0x%02X which is "
                "'%s' -- a half tile must match the terrain it finishes"
                % (ref, tile_family(ref), dominant, tile_family(dominant)))

    if problems:
        raise SystemExit("Half-tile classification check FAILED:\n"
                         + "\n".join(problems))
    print("Half-tile check passed: every half/corner tile matches its terrain.")


CLASS_ORDER = ["water", "forest", "mountain", "grave", "sand", "ground"]
# The class table belongs to the ROM extractor (patcher/src/extract), which sits
# next to this game folder in the development checkout and one level up in the
# public repository.
CLASS_HEADER = next(
    (os.path.join(d, "TerrainClasses.gen.h") for d in (
        os.path.join(ROOT, "..", "patcher", "src", "extract"),
        os.path.join(ROOT, "..", "..", "patcher", "src", "extract"))
     if os.path.isdir(d)),
    None)


def write_if_changed(path, text):
    path = os.path.normpath(path)
    old = open(path).read() if os.path.exists(path) else None
    if old != text:                      # keep mtime stable when unchanged
        with open(path, "w") as f:
            f.write(text)
        print("Wrote %s" % os.path.relpath(path, ROOT))


def write_class_header():
    if CLASS_HEADER is None:
        raise SystemExit("patcher/src/extract not found next to or above this folder")
    """Tile -> terrain class table and class colours for the console code.
    Tiles in no family default to ground (the room data never uses one, see
    the <assetdir> check)."""
    default = CLASS_ORDER.index("ground")
    table = [CLASS_ORDER.index(_REF_TO_FAMILY[r]) if r in _REF_TO_FAMILY else default
             for r in range(64)]
    rows = ",\n".join("    " + ", ".join("%d" % v for v in table[i:i + 16])
                      for i in range(0, 64, 16))
    colours = "\n".join("    { 0x%02X, 0x%02X, 0x%02X },  // %s" % (
        TERRAIN_FAMILIES[n][0][0], TERRAIN_FAMILIES[n][0][1], TERRAIN_FAMILIES[n][0][2], n)
        for n in CLASS_ORDER)
    text = (
        "// GENERATED FILE -- do not edit by hand.\n"
        "// Written by zelda3ds/tools/gen-terrain-tables.py from its\n"
        "// TERRAIN_FAMILIES table; edit that and rerun the script.\n"
        "#pragma once\n#include <stdint.h>\n\n"
        "// Live-map terrain class per overworld tileRef (0x00-0x3F):\n"
        "// %s.\n" % ", ".join("%d %s" % (i, n) for i, n in enumerate(CLASS_ORDER)) +
        "static const uint8_t kTerrainClassOfTile[64] = {\n" + rows + "\n};\n\n"
        "static const int kTerrainClassCount = %d;\n\n" % len(CLASS_ORDER) +
        "// RGB per class, in the order above.\n"
        "static const uint8_t kTerrainClassRgb[%d][3] = {\n" % len(CLASS_ORDER) + colours + "\n};\n")
    write_if_changed(CLASS_HEADER, text)


def main():
    if len(sys.argv) > 1:
        grids, attrs = load_room_grids(sys.argv[1])
        # Every tile that actually occurs must be classified, and every half
        # tile must match the terrain it finishes. Run before writing.
        for g in grids:
            for row in g:
                for ref in row:
                    tile_family(ref)
        verify_half_tiles(grids, attrs)
    write_mid_layer_header()
    write_class_header()


if __name__ == "__main__":
    main()
