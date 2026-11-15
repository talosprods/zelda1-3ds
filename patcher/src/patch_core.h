// The patcher's core, shared by the PC tool (patcher.cpp) and the browser
// version (web.cpp): ROM check + extraction, RomFS rebuild, CIA/3DSX patching.
// See patcher.cpp's header comment for how the CIA is patched.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

typedef std::vector<uint8_t> Bytes;

// Thrown for anything that stops the patch, with a player-facing message.
struct Fail
{
    std::string msg;
};

struct RomFile
{
    std::string name;   // ASCII
    Bytes data;
};

// Checks the ROM (any accepted dump of Zelda USA PRG0) and returns every file
// RomExtract builds from it. Throws Fail with RomExtract's explanation.
std::vector<RomFile> ExtractRomFiles( const Bytes& romFile );

// The base build plus `extraFiles`, as a complete CIA / 3DSX. `order`, if
// given, fixes the RomFS file order (used to compare against makerom).
Bytes PatchCia( const Bytes& base, const std::vector<RomFile>& extraFiles,
    const std::vector<std::string>* order = nullptr );
Bytes Patch3dsx( const Bytes& base, const std::vector<RomFile>& extraFiles );

// The RomFS file order of an existing CIA (testing aid for --order-from).
std::vector<std::string> FileOrderOf( const Bytes& cia );
