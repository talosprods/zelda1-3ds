// RomExtract -- builds every game asset that comes from the original ROM,
// in memory, from the player's own copy of the NES ROM.
//
// This is a C++ port of Aldo Nunez's ExtractLoz tool (github.com/aldonunez/Loz,
// Tools/ExtractLoz; C#, Apache-2.0), trimmed to what the 3DS build loads and
// writing the raw
// 8-bit-index .bin images the 3DS renderer uses instead of PNGs. It has no
// platform dependencies: the game runs it on the console at startup, and
// patcher/src/patcher.cpp and romextract-cli.cpp run the same code on a PC.
//
// Nothing Nintendo-made is compiled into this file or shipped with the game:
// all game data is read out of the ROM the player supplies.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

namespace RomExtract
{

enum Result
{
    Result_Ok,
    Result_TooSmall,        // not even the size of the game's PRG data
    Result_NotNesRom,       // no iNES header and not the headerless size
    Result_WrongRom,        // an NES ROM, but not Zelda (U) PRG0
    Result_Rev1,            // Zelda (USA) Rev 1 / PRG1: not supported
};

typedef std::map<std::string, std::vector<uint8_t>> FileMap;

// Checks the ROM and returns it with an iNES header in front (a headerless
// dump gets a synthetic one), which is the layout every offset in the
// extractor assumes. Accepts the file as read from disk.
Result NormalizeRom( const uint8_t* data, size_t size, std::vector<uint8_t>& romOut );

// Fills `files` with every ROM-derived asset, keyed by the bare filename the
// game opens (e.g. "overworldTiles.bin", "text.tab", "zelda.nsf").
// `rom` must come from NormalizeRom().
void ExtractAll( const std::vector<uint8_t>& rom, FileMap& files );

// One-line, player-facing explanation of a failed NormalizeRom().
const char* Describe( Result result );

// CRC-32 (IEEE) of the PRG data the game requires (the ROM minus its 16-byte
// iNES header), exposed for the error screen and the CLI.
uint32_t Crc32( const uint8_t* data, size_t size );

extern const uint32_t RequiredPrgCrc32;      // PRG0, headerless: 3FE272FB
extern const uint32_t Rev1PrgCrc32;          // PRG1 / Rev 1, headerless: EAF7ED72

}
