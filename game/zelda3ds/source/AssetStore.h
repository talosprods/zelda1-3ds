// AssetStore -- serves the game's ROM-derived assets from memory.
//
// The game ships without any Nintendo data and gets it one of two ways:
//  1. Baked: the PC patcher (patcher/) has put the extracted
//     files into this build's romfs. Nothing to do; opens hit romfs.
//  2. ROM on SD: AssetStore_Init() finds the player's own ROM in
//     ASSETSTORE_ROM_DIR, checks it, and runs RomExtract to build every
//     ROM-derived file in RAM. All asset opens go through AssetStore_Open():
//     a name the extractor produced is served from memory (fmemopen),
//     anything else (palette, sprite-sheet tables, our own textures) falls
//     through to a normal fopen in romfs.
#pragma once

#include <stddef.h>
#include <stdio.h>

// Folder searched for the ROM (any *.nes file in it).
#define ASSETSTORE_ROM_DIR "sdmc:/3ds/zelda3ds/"

// Returns true when the ROM was found and extracted. On failure, writes a
// short player-facing explanation into `message`.
bool AssetStore_Init( char* message, size_t messageSize );

// fopen() replacement for asset reads. Paths may carry a "romfs:/" prefix.
FILE* AssetStore_Open( const char* path, const char* mode );

// Direct access to an extracted file (nullptr if not extracted).
const unsigned char* AssetStore_Data( const char* name, size_t* size );

// One line describing how long AssetStore_Init() took (written to the frame
// trace log when that is switched on, see main_3ds.cpp).
const char* AssetStore_TimingReport();
