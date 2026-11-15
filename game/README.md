# The game

The 3DS game itself. It contains no Nintendo data; see the
[top-level README](../README.md) for how the data gets in.

| Path | What |
|---|---|
| `Loz/Game/Loz/` | The game engine: Aldo Nunez's [Loz](https://github.com/aldonunez/Loz) remake (C++), with this port's changes |
| `Loz/Tools/ExtractNsf/Game_Music_Emu/gme/` | Game_Music_Emu, plays the game's music live on the console |
| `zelda3ds/source/` | The 3DS layer: graphics (citro2d), sound, input, saves, the live map, `AssetStore` |
| `zelda3ds/romfs/` | Data shipped with the game: palette, sprite-sheet layouts, sound lists, our own textures. None of it comes from the ROM |
| `zelda3ds/cia-assets/` | HOME-menu icon, banner and jingle (our own), CIA settings |
| `zelda3ds/tools/` | `build-cia.sh` and the generators for the banner, terrain tables and jingle |
| `zelda3ds/tests/` | Unit tests for the live-map geometry |

The ROM extractor the game uses for the ROM-on-SD route lives in
`../patcher/src/extract/`, shared with the PC patcher.

## Building

Needs [devkitPro](https://devkitpro.org/wiki/Getting_Started) with devkitARM,
libctru, citro2d and citro3d.

```bash
cd zelda3ds
make                        # -> zelda3ds.3dsx (asset-free base)
tools/build-cia.sh          # -> CIA; needs makerom + bannertool, see the CI workflow
tests/run.sh                # unit tests (host compiler)
```
