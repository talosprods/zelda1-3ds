#!/bin/bash
# Builds the installable CIA ("Legend of Zelda 3DS edition.cia" in the
# zelda3ds/ game folder, or $CIA_OUT). The CIA is asset-free: the game takes its
# graphics, maps and sound from the player's ROM (see AssetStore.cpp), or the
# PC patcher fills its RomFS (patcher/).
#
# Why a CIA and not only a .3dsx: a CIA gets a real exheader with normal
# memory permissions, a .3dsx loaded through hb:ldr does not. Early builds hit
# a VRAM crash and performance/colour regressions on real hardware that only
# the .3dsx route showed.
#
# Needs tools/bin/makerom (Project_CTR) and tools/bin/bannertool (built from
# tools/bannertool-src). The 3D banner also needs FBI's banner model, see
# below. .github/workflows/build.yml shows how CI fetches all three.
#
# Usage: build-cia.sh [tetra|2d]
#   tetra  (default) 3D banner: a spinning golden tetrahedron with the
#          Triforce on every face. See tools/gen-tetra.py.
#   2d     static banner.png. Fallback if the 3D banner ever breaks.
#
# Always builds the .elf first (make is a no-op when nothing changed), then
# banner and icon, then the CIA itself.

set -euo pipefail

BANNER_MODE="${1:-tetra}"

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ZDIR="$REPO_ROOT/zelda3ds"
TOOLS="$ZDIR/tools/bin"
ASSETS="$ZDIR/cia-assets"
CIA_OUT="${CIA_OUT:-$REPO_ROOT/Legend of Zelda 3DS edition.cia}"

case "$BANNER_MODE" in
    tetra|2d) ;;
    *) echo "ERROR: unknown banner mode '$BANNER_MODE' (choose: tetra | 2d)"
       exit 1 ;;
esac

if [[ ! -x "$TOOLS/makerom" || ! -x "$TOOLS/bannertool" ]]; then
    echo "ERROR: $TOOLS/makerom and/or bannertool are missing."
    echo "See .github/workflows/build.yml for how to fetch makerom and build bannertool."
    exit 1
fi

echo "Building (.elf/.3dsx)..."
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro} DEVKITARM=${DEVKITARM:-/opt/devkitpro/devkitARM} make -C "$ZDIR" -j4

if [[ "$BANNER_MODE" == 2d ]]; then
    echo "Building banner (static 2D) + icon..."
    "$TOOLS/bannertool" makebanner \
        -i "$ASSETS/banner.png" \
        -a "$ASSETS/banner-audio.wav" \
        -o "$ASSETS/banner.bnr"
else
    # Generating a CGFX from scratch does not work: a dozen pycgfx variants
    # each drew nothing at all on hardware. What works is taking FBI's proven
    # banner_3ds.cgfx and patching its geometry and pixels in place without
    # moving a single offset.
    BASE_CGFX="$ASSETS/fbi_banner_base.cgfx"
    if [[ ! -f "$BASE_CGFX" ]]; then
        echo "ERROR: $BASE_CGFX is missing. Fetch it with:"
        echo "  curl -fsSL -o $BASE_CGFX https://raw.githubusercontent.com/Steveice10/FBI/ec259153e25fc77ee999b8caadac7e73d2e5e41a/meta/banner_3ds.cgfx"
        exit 1
    fi
    echo "Building 3D banner (tetrahedron)..."
    # gen-tetra.py runs gen-spinning-banner.py itself with the right texture
    # settings. Deliberately no environment variables here: twice a change
    # was tested by hand with a flag and then shipped without it.
    python3 "$ZDIR/tools/gen-tetra.py"
    CGFX_IN="$ASSETS/tetra.cgfx"

    echo "Building banner (3D CGFX) + icon..."
    "$TOOLS/bannertool" makebanner \
        -ci "$CGFX_IN" \
        -a "$ASSETS/banner-audio.wav" \
        -o "$ASSETS/banner.bnr"
fi

"$TOOLS/bannertool" makesmdh \
    -s "Zelda 3DS" \
    -l "The Legend of Zelda - 3DS Edition" \
    -p "Zelda 3DS Edition" \
    -i "$ASSETS/icon.png" \
    -o "$ASSETS/icon.smdh"

echo "Building CIA..."
# The title version goes up with every build. The HOME menu caches the banner
# per title and version: if the version stays the same (or goes down), it can
# keep showing the old cached banner.
#
# The title version is one u16: major(6) | minor(6) | micro(4); 3dbrew's own
# example is v2069 = 2.1.5.
#
# What it does NOT do (checked in FBI's source, installcias.c): block an
# install. FBI calls AM_DeleteTitle + AM_DeleteTicket right before every
# AM_StartCiaInstall without comparing versions, so installing a lower version
# over a higher one is a clean delete-and-install.
#
# Pitfall this is written around: without -major, makerom silently takes
# RemasterVersion from the RSF as the major version. An earlier version of
# this stamping passed major 0 while older builds carried 2.0.0, so every new
# build was "older" than the installed one. Hence a fixed floor, counting up
# from there, and the read-back check below.
VERSION_FLOOR=1024          # 1.0.0
BUILDNO_FILE="$ASSETS/.buildno"
# Post-increment: this build gets the stored number, the next one gets one
# more, so the first build after a reset is exactly VERSION_FLOOR (1.0.0).
BUILDNO=$(cat "$BUILDNO_FILE" 2>/dev/null || echo 0)
echo "$(( BUILDNO + 1 ))" > "$BUILDNO_FILE"

RAW_VERSION=$(( VERSION_FLOOR + BUILDNO ))
if (( RAW_VERSION > 65535 )); then
    echo "ERROR: title version $RAW_VERSION does not fit in a u16." >&2
    exit 1
fi
V_MAJOR=$(( RAW_VERSION >> 10 ))
V_MINOR=$(( (RAW_VERSION >> 4) & 63 ))
V_MICRO=$(( RAW_VERSION & 15 ))
echo "Title version: $V_MAJOR.$V_MINOR.$V_MICRO (raw $RAW_VERSION, build $BUILDNO)"

"$TOOLS/makerom" -f cia \
    -o "$CIA_OUT" \
    -major "$V_MAJOR" -minor "$V_MINOR" -micro "$V_MICRO" \
    -rsf "$ASSETS/zelda3ds.rsf" \
    -elf "$ZDIR/zelda3ds.elf" \
    -icon "$ASSETS/icon.smdh" \
    -banner "$ASSETS/banner.bnr" \
    -exefslogo \
    -target t \
    -DAPP_ENCRYPTED=false \
    -DAPP_ROMFS="$ZDIR/romfs"

# Read the title version back from the TMD of the CIA just built. Passing
# -major/-minor/-micro is not the same as getting them in (see above), so
# check rather than assume.
python3 - "$CIA_OUT" "$RAW_VERSION" <<'PYCHECK'
import struct, sys
cia, want = sys.argv[1], int(sys.argv[2])
d = open(cia, "rb").read()
hdr, _, _, cert, tik, _tmd_sz, _meta = struct.unpack_from("<IHHIIII", d, 0)
al = lambda x: (x + 63) & ~63
off = al(hdr) + al(cert) + al(tik)                  # -> TMD
sig_type, = struct.unpack_from(">I", d, off)
sizes = {0x00010003: (0x200, 0x3C), 0x00010004: (0x100, 0x3C),
         0x00010005: (0x3C, 0x40), 0x00010006: (0x3C, 0x40), 0x00010007: (0x3C, 0x40)}
if sig_type not in sizes:
    sys.exit("ERROR: unknown TMD signature type 0x%08X" % sig_type)
ss, pad = sizes[sig_type]
h = off + 4 + ss + pad                              # -> TMD header
got, = struct.unpack_from(">H", d, h + 0x9C)
fmt = lambda v: "%d.%d.%d" % (v >> 10, (v >> 4) & 63, v & 15)
if got != want:
    sys.exit("ERROR: the CIA carries title version %s (raw %d), but this script "
             "stamped %s (raw %d).\nThe console would see something other than "
             "what is reported here -- do not ship." % (fmt(got), got, fmt(want), want))
print("Checked: CIA carries title version %s (raw %d)." % (fmt(got), got))
PYCHECK

# Check that the banner just packed really contains the CGFX just built. Twice
# a change was generated correctly and then shipped without it because the
# build step had not changed along with it. bannertool LZ11-compresses the CGFX
# inside the CBMD, so a byte compare is not possible -- but the LZ11 header
# states the uncompressed size, which must equal the .cgfx on disk.
if [[ -n "${CGFX_IN:-}" && -f "$CGFX_IN" ]]; then
    python3 - "$CGFX_IN" "$ASSETS/banner.bnr" <<'PYCHECK'
import struct, sys
cgfx, bnr = sys.argv[1], sys.argv[2]
want = len(open(cgfx, "rb").read())
b = open(bnr, "rb").read()
assert b[:4] == b"CBMD", "banner.bnr is not a CBMD"
off = struct.unpack_from("<I", b, 8)[0]
got = int.from_bytes(b[off + 1:off + 4], "little")
if got != want:
    sys.exit("ERROR: the banner contains a CGFX of %d bytes, but %s is %d bytes.\n"
             "The change is NOT in this build." % (got, cgfx, want))
print("Checked: banner contains the CGFX just built (%d bytes)." % want)
PYCHECK
fi

echo
echo "Done: $CIA_OUT  (banner mode: $BANNER_MODE)"
echo "Copy it to the SD card and install it with FBI."
