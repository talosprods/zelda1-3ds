# zelda3ds-patcher

Turns your own copy of the NES ROM into the finished 3DS game:

```
zelda3ds-patcher "Legend of Zelda, The (USA).nes"
  -> zelda3ds.cia    (install with FBI)
  -> zelda3ds.3dsx   (Homebrew Launcher)
```

On Windows you can also drag the `.nes` file onto `zelda3ds-patcher.exe`.
The output files are written next to the ROM.

The same patcher also runs in the browser on the project website: the C++
below compiled to WebAssembly (`src/web.cpp`, `build-web.sh`). Both versions
give byte-identical files.

## What it does

All of this happens on your computer, in memory, in a fraction of a second.
Nothing is downloaded or uploaded, and the ROM is only read.

1. **Check the ROM.** It must be *The Legend of Zelda (USA)*, the first
   release (PRG0): CRC-32 `3FE272FB` without the iNES header. Rev 1 (PRG1)
   and other versions are refused with a message saying why.
2. **Extract the game's data** with `src/extract/RomExtract.cpp`, a C++ port
   of the extractor from Aldo Nunez's [Loz](https://github.com/aldonunez/Loz).
   It produces 73 files: tile and sprite sheets, room layouts, text, the music
   data (NSF) and the live map's terrain. The 3DS game runs the exact same
   code when you use the ROM-on-SD route instead of this patcher.
3. **Build the game files.** The patcher carries the game itself as an
   asset-free "base" CIA and 3DSX, built from `../game` by the project's CI.
   It takes their RomFS (the file system inside), adds the 73 extracted files,
   and lays the RomFS out exactly as `makerom` does: the level-3 file system
   plus the IVFC hash tree. Then it updates the NCCH, TMD and CIA sizes and
   SHA-256 hashes. No compiler or 3DS toolchain is involved.

The result is deterministic: the same ROM always gives byte-identical output.

## Files

| Path | What |
|---|---|
| `src/patch_core.cpp` | The work: ROM check + extraction, RomFS rebuild, CIA/3DSX patching |
| `src/patcher.cpp` | Command-line front end (the PC tool) |
| `src/web.cpp` | Browser front end (WebAssembly) |
| `src/sha256.h` | SHA-256 for the RomFS/NCCH/TMD hashes |
| `src/extract/` | The ROM extractor, shared with the game |
| `src/romextract-cli.cpp` | Developer tool: write the extracted files to a folder, or compare them |
| `data/` | Hand-made tables from Loz's extractor (no ROM data) |
| `tools/gen-extract-tables.py` | Turns `data/` into `src/extract/RomExtractTables.gen.h` |

## Building

Plain C++14, no dependencies:

```bash
./build-patcher.sh                          # needs --base-cia/--base-3dsx at run time
./build-patcher.sh base.cia base.3dsx       # embeds a base build (what releases do)
./build-romextract.sh                       # romextract <rom.nes> <outdir>
./build-web.sh <outdir>                     # browser version (needs Emscripten's em++)
```

Release builds (Windows, macOS, Linux) are made by `.github/workflows/build.yml`.
