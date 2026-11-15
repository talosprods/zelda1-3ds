# The Legend of Zelda: 3DS Edition

An unofficial, native port of the original 1986 NES *The Legend of Zelda* to
the New Nintendo 3DS. It runs the game itself, not an emulator. The top
screen plays the game; the bottom screen shows a **live map of Hyrule** that
fills in as you explore.

The port is built on [Loz](https://github.com/aldonunez/Loz), Aldo Nunez's
C++ remake of the game.

> **No game files are included.** This repository and its releases contain no
> Nintendo code, graphics, music or data. The game needs **your own ROM**:
> on first start it reads the graphics, maps, text and music out of it, on
> the console, into memory.

## What it adds to the original

- **Live dual-screen map.** The overworld and dungeons, with real terrain on
  the bottom screen. Shops, fairies, caves, secrets and dungeon markers only
  appear once you have found them. Tap to zoom, and place your own pins.
- **Stereoscopic 3D.** The ground, the characters and trees, and projectiles
  sit at different depths.
- **Touch Items/Options screen**, plus a second item button.
- **Autosave**, a modern continue screen, and three save slots that you can
  copy and delete. Quest 1 and Quest 2 are both selectable.
- **Manual and achievements**, per-save statistics, and a music player.
- **Live NES sound synthesis** from the game's own music data.
- **Clean picture.** Sharp, CRT-correct scaling with the 8:7 pixel aspect.
  It is built to run at full speed (60 fps) on the New 3DS.

## Playing it

You need a 3DS with custom firmware (for example
[Luma3DS](https://3ds.hacks.guide/)) and FBI or the Homebrew Launcher.
There are three ways to get the game. All of them end in the same game.

### A. Patch in your browser (easiest)

Open the project website (this repository's GitHub Pages) and drop your `.nes`
file on it. You get `zelda3ds.cia` and `zelda3ds.3dsx` to download. The
patching runs entirely in your browser; the ROM is never uploaded. It is the
same code as the PC patcher, compiled to WebAssembly, and it gives
byte-identical files.

### A2. Patch on your PC

1. Download `zelda3ds-patcher-<windows|macos|linux>.zip` from
   [Releases](../../releases) and unzip it.
2. Drag your `.nes` file onto `zelda3ds-patcher` (Windows), or run
   `./zelda3ds-patcher your-rom.nes`. You get `zelda3ds.cia` and
   `zelda3ds.3dsx` next to the ROM.

On macOS, right-click the patcher and choose *Open* the first time; it is
not signed by Apple.

**Then:** install `zelda3ds.cia` with FBI and start it from the HOME menu.
Or copy `zelda3ds.3dsx` to `sd:/3ds/zelda3ds/` for the Homebrew Launcher.

### B. ROM on the SD card

1. Download `zelda3ds-load-rom-from-sd.cia` (or the `.3dsx`) from
   [Releases](../../releases) and install it.
2. Put your ROM in **`sd:/3ds/zelda3ds/`**. Any file name works, as long as
   it ends in `.nes`.
3. Start the game. It builds what it needs from the ROM in memory at every
   startup, which adds roughly a tenth of a second. A patched game has no
   such cost.

If the ROM is missing or wrong, the game shows a screen that says why.

### Which ROM

You need **The Legend of Zelda (USA), the first release, known as PRG0**.
Files with or without the 16-byte iNES header
both work; the patcher, the game and the website check it for you.

| | Works: first release (PRG0) | Does **not** work: Rev 1 (PRG1) |
|---|---|---|
| No-Intro name | `Legend of Zelda, The (USA).nes` | `Legend of Zelda, The (USA) (Rev 1).nes` |
| GoodNES name | `Legend of Zelda, The (U) (PRG0) [!].nes` | `Legend of Zelda, The (U) (PRG1) [!].nes` |
| CRC-32, file with header | `D7AE93DF` | `E47960E0` |
| CRC-32, without header | `3FE272FB` | `EAF7ED72` |
| SHA-1, file with header | `DAB79C84934F9AA5DB4E7DAD390E5D0C12443FA2` | |
| SHA-1, without header | `A12D74C73A0481599A5D832361D168F4737BBCF6` | |

Other versions don't work either: Europe, Japan (Famicom Disk System),
the GameCube/GBA re-releases and ROM hacks. The game, the patcher and the
website all tell you which one you have.

Dump the ROM from a cartridge you own. This project does not help you obtain
ROMs.

### What it writes to the SD card

Everything stays in `sd:/3ds/zelda3ds/`: the save files (`z1_<slot>.sav`),
the map progress (`livemap_<slot>.dat`, `pins_<slot>.dat`), `options.dat` and
`lastslot.dat`. The extracted assets are never written to the SD card.

## What's in this repository

Two folders, so you can see exactly what you run:

| Folder | What |
|---|---|
| [`game/`](game/) | The 3DS game: the Loz engine plus the 3DS layer (graphics, sound, live map, saves). Builds to an asset-free CIA/3DSX. |
| [`patcher/`](patcher/) | The patcher and the ROM extractor. Turns your ROM plus that asset-free build into the finished game, on your PC or (as WebAssembly) in your browser. Read [`patcher/README.md`](patcher/README.md) for exactly what it does. |

`docs/` is the project website (GitHub Pages) and `.github/` the build
pipeline, which also puts the WebAssembly patcher and the base builds on the
site at release time.

### How the ROM data gets in

`patcher/src/extract/RomExtract.cpp` is a C++ port of Loz's original C#
extractor ([ExtractLoz](https://github.com/aldonunez/Loz)). Both routes use
it:

- **The patcher** runs it on your PC and rebuilds the RomFS of the asset-free
  base build with the extracted files. It lays out the level-3 file system and
  IVFC hash tree as `makerom` does, then updates the NCCH, TMD and CIA hashes
  and sizes. Nothing is compiled on your computer, and the same ROM always
  gives byte-identical output.
- **On the console**, `game/zelda3ds/source/AssetStore.cpp` uses that RomFS
  if the patcher filled it. Otherwise it finds the ROM on the SD card and
  extracts everything into memory.

The base build's RomFS only holds data that does not come from the ROM: the
palette, the sprite-sheet layouts, the sound lists, and our own textures.

## Building from source

**The game** needs [devkitPro](https://devkitpro.org/wiki/Getting_Started)
with devkitARM, libctru, citro2d and citro3d (the `3ds-dev` group):

```bash
cd game/zelda3ds
make                  # -> zelda3ds.3dsx
tools/build-cia.sh    # -> CIA; needs makerom and bannertool, see below
```

For the CIA you also need `makerom` (from
[Project_CTR](https://github.com/3DSGuy/Project_CTR)) and `bannertool`
(vendored in `game/zelda3ds/tools/bannertool-src`), both in
`game/zelda3ds/tools/bin/`.
[`.github/workflows/build.yml`](.github/workflows/build.yml) does all of this.

**The patcher** is plain C++ with no dependencies:

```bash
patcher/build-patcher.sh                       # pass --base-cia/--base-3dsx when running
patcher/build-patcher.sh base.cia base.3dsx    # ...or embed a base build (as releases do)
patcher/build-romextract.sh                    # developer tool: dump the extracted files
```

[DEVELOPMENT.md](DEVELOPMENT.md) has a short architecture overview.

## Credits

- **Loz** by Aldo J. Nunez: the C++ remake this port is built on
  (Apache-2.0).
- **Game_Music_Emu** by Shay Green: NSF playback (LGPL-2.1).
- **bannertool** by Steveice10 and carstene1ns (MIT); **makerom** by 3DSGuy
  (MIT). The patcher's RomFS layout follows makerom's.
- The 3D HOME-menu banner model is based on
  [FBI](https://github.com/Steveice10/FBI)'s `banner_3ds.cgfx` (MIT). It
  is fetched when the CIA is built and is not stored in this repository.

## Legal

This is a non-commercial fan project. It is not affiliated with, endorsed by,
or connected to Nintendo. *The Legend of Zelda*, the Triforce, Nintendo, NES and Nintendo 3DS are
trademarks of Nintendo.
No copyrighted game data is distributed here. You need your own legally
obtained copy of the game.

The code is licensed under the Apache License 2.0 (see [LICENSE](LICENSE) and
[NOTICE](NOTICE)), except for the third-party parts listed there.
