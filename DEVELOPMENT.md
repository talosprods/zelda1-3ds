# Development notes

The full development log of this port is not published. Code comments
sometimes refer to it ("DEVELOPMENT.md §N") or to development phases
("phase 10.4"); the comments next to them carry the essential reasoning
themselves.

## Where things are

See the top-level README, [`game/README.md`](game/README.md) and
[`patcher/README.md`](patcher/README.md).

## Architecture in short

- **Graphics.** The PICA200 can't look up a palette per pixel, so the
  8-bit-index tile sheets are expanded per palette on the CPU into tiled RGBA4
  textures and drawn with citro2d. The top screen renders offscreen and is
  scaled with sharp bilinear filtering at the NES 8:7 pixel aspect ratio.
  Stereoscopic 3D comes from three layers (ground / characters and trees /
  projectiles), composited per eye with premultiplied alpha.
- **Sound.** The game's NSF is rebuilt from the ROM and played live through
  Game_Music_Emu, one emulator instance per sound slot. There are no
  pre-rendered audio files.
- **Timing.** `World::Update()` is held to 60 Hz of real time (`osGetTime`),
  independent of the CPU clock; the New 3DS speedup is enabled.
- **Assets.** `RomExtract.cpp` is a byte-exact C++ port of ExtractLoz. Its
  output was checked file by file against the original C# tool. The base
  build looks in its RomFS first (filled by the patcher), then for a `.nes`
  in `sd:/3ds/zelda3ds/`.

## Testing

- `game/zelda3ds/tests/run.sh`: host-side unit tests for the live-map geometry.
- `patcher/romextract <rom> --compare <dir>`: diff the extractor's
  output against a directory of known-good files.
- In the Azahar emulator, `sdmc:/zelda3ds-input.txt` drives the game with
  scripted input (`<frames> [Up Down Left Right A B Y Start Select]` per
  line). `sdmc:/zelda-frametrace.txt` switches on frame timing and the
  startup asset timing in `sdmc:/3ds/zelda3ds/frametime.txt`.
