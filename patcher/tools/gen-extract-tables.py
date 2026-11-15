#!/usr/bin/env python3
"""
Turns the ExtractLoz data files (patcher/data/, copied from Aldo Nunez's
Loz, Tools/ExtractLoz/Data, Apache-2.0) into a C++ header for the ROM
extractor (patcher/src/extract/RomExtract.cpp).

None of these tables come from the ROM: they say *where* to find things in
it (sprite arrangement maps, the NSF build recipe) or are hand-classified
attributes. The ROM bytes themselves are only ever read at runtime from the
player's own copy.

Usage: python3 patcher/tools/gen-extract-tables.py
Writes patcher/src/extract/RomExtractTables.gen.h (checked in; rerun only if a data
file changes).
"""
import csv
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.normpath(os.path.join(HERE, "..", "data"))
OUT = os.path.normpath(os.path.join(HERE, "..", "src", "extract", "RomExtractTables.gen.h"))

SPRITE_MAPS = [
    "PlayerItemsImage", "OWNpcsImage",
    "UWNpcsImageCommon", "UWNpcsImage127", "UWNpcsImage358", "UWNpcsImage469",
    "UWBossImage1257", "UWBossImage3468", "UWBossImage9",
]


def read_rows(name):
    with open(os.path.join(DATA, name + ".csv"), newline="", encoding="utf-8-sig") as f:
        return [[c.strip() for c in row] for row in csv.reader(f) if row]


def hex_map16(name):
    rows = read_rows(name)
    assert len(rows) == 8 and all(len(r) == 16 for r in rows), name
    return [[int(c, 16) for c in r] for r in rows]


def obj_attrs():
    # DatafileReader.ReadHexArray8(stream, {0, 1}): skip the header line,
    # ignore column 0 (the name), take 1 bit of column 1.
    rows = read_rows("ObjAttrs")[1:]
    return [int(r[1], 16) & 1 for r in rows]


def nsf_spec():
    rows = read_rows("NsfSpec")[1:]
    return [(int(r[0], 16), int(r[1], 16)) for r in rows]


def c_bytes(data, indent="    ", per_line=16):
    lines = []
    for i in range(0, len(data), per_line):
        lines.append(indent + ", ".join("0x%02X" % b for b in data[i:i + per_line]))
    return ",\n".join(lines)


def main():
    out = []
    out.append("// GENERATED FILE -- do not edit by hand.\n"
               "// Written by patcher/tools/gen-extract-tables.py from patcher/data/\n"
               "// (Copyright 2016 Aldo J. Nunez, Apache-2.0). Contains no ROM data.\n"
               "#pragma once\n#include <stdint.h>\n")

    for name in SPRITE_MAPS:
        m = hex_map16(name)
        rows = ",\n".join("    { " + ", ".join("0x%04X" % v for v in r) + " }" for r in m)
        out.append("static const uint16_t k%s[8][16] = {\n%s\n};\n" % (name, rows))

    attrs = obj_attrs()
    out.append("static const int kObjAttrsByHandCount = %d;\n" % len(attrs))
    out.append("static const uint8_t kObjAttrsByHand[%d] = {\n%s\n};\n"
               % (len(attrs), c_bytes(attrs)))

    spec = nsf_spec()
    out.append("struct NsfChunk { int offset; int length; };\n")
    out.append("static const NsfChunk kNsfChunks[%d] = {\n%s\n};\n"
               % (len(spec), ",\n".join("    { 0x%X, 0x%X }" % s for s in spec)))

    for name in ("NsfHeader", "NsfFooter"):
        data = open(os.path.join(DATA, name + ".bin"), "rb").read()
        out.append("static const uint8_t k%s[%d] = {\n%s\n};\n" % (name, len(data), c_bytes(data)))

    text = "\n".join(out)
    old = open(OUT).read() if os.path.exists(OUT) else None
    if old != text:
        with open(OUT, "w") as f:
            f.write(text)
        print("Wrote", OUT)
    else:
        print("Up to date:", OUT)


if __name__ == "__main__":
    main()
