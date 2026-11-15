/*
   RomExtract -- C++ port of ExtractLoz (github.com/aldonunez/Loz,
   Tools/ExtractLoz/Program.cs).

   Original C# extractor: Copyright 2016 Aldo J. Nunez, Apache License 2.0.
   This port: same license. Changes from the original: runs in memory, emits
   raw 8-bit-index images (.bin: u16 width, u16 height, width*height indices)
   instead of PNGs, skips the outputs the 3DS build never loads (WAVs, the
   sprite-VRAM debug sheet), and adds terrain.bin for the live map.

   Every function below mirrors the C# function of the same name; the
   comments that matter for correctness are carried over. See
   patcher/src/romextract-cli.cpp for the byte-for-byte check against the
   original tool's output.
*/

#include "RomExtract.h"
#include "RomExtractTables.gen.h"
#include "TerrainClasses.gen.h"

#include <stdio.h>
#include <string.h>

namespace RomExtract
{

// No-Intro "Legend of Zelda, The (USA)" = GoodNES "(U) (PRG0)". With the
// iNES header the file CRC-32 is D7AE93DF, SHA-1 DAB79C84934F9AA5DB4E7DAD390E5D0C12443FA2.
const uint32_t RequiredPrgCrc32 = 0x3FE272FB;
// No-Intro "Legend of Zelda, The (USA) (Rev 1)" = GoodNES "(U) (PRG1)".
const uint32_t Rev1PrgCrc32 = 0xEAF7ED72;

namespace
{

const size_t HeaderSize = 16;
const size_t PrgSize = 0x20000;

// ---------------------------------------------------------------------------
// Little-endian growable writer with the BinaryWriter/Stream semantics the
// C# code relies on: seeking past the end and writing zero-fills the gap.

struct Writer
{
    std::vector<uint8_t> buf;
    size_t pos = 0;

    Writer() { buf.reserve( 4096 ); }

    void U8( uint8_t v )
    {
        if ( pos == buf.size() )
            buf.push_back( v );             // the common, appending case
        else
        {
            if ( pos > buf.size() )
                buf.resize( pos, 0 );       // seeked past the end: zero-fill
            if ( pos == buf.size() ) buf.push_back( v ); else buf[pos] = v;
        }
        pos++;
    }
    void U16( uint16_t v ) { U8( v & 0xFF ); U8( v >> 8 ); }
    void I32( int32_t v )
    {
        uint32_t u = (uint32_t) v;
        U8( u & 0xFF ); U8( (u >> 8) & 0xFF ); U8( (u >> 16) & 0xFF ); U8( u >> 24 );
    }
    void Bytes( const uint8_t* p, size_t n )
    {
        if ( pos > buf.size() )
            buf.resize( pos, 0 );
        size_t overlap = buf.size() - pos < n ? buf.size() - pos : n;
        if ( overlap )
            memcpy( &buf[pos], p, overlap );
        buf.insert( buf.end(), p + overlap, p + n );
        pos += n;
    }
    void Bytes( const std::vector<uint8_t>& v ) { Bytes( v.data(), v.size() ); }

    // Utility.AlignStream
    size_t Align( size_t alignment )
    {
        pos = (pos + alignment - 1) & ~(alignment - 1);
        return pos;
    }
    // Utility.PadStream: align to 4, then SetLength(Position).
    void Pad()
    {
        Align( 4 );
        buf.resize( pos, 0 );
    }
    void Seek( size_t p ) { pos = p; }

    void FixedString( const char* s, size_t length )
    {
        size_t n = s ? strlen( s ) : 0;
        for ( size_t i = 0; i < length; i++ )
            U8( i < n ? (uint8_t) s[i] : 0 );
    }
};

// ---------------------------------------------------------------------------
// Positioned reader over the (header-prefixed) ROM image. Out-of-range reads
// return zeros instead of crashing; NormalizeRom has already checked the CRC,
// so this only guards against bugs.

struct Reader
{
    const std::vector<uint8_t>& rom;
    size_t pos = 0;

    explicit Reader( const std::vector<uint8_t>& r ) : rom( r ) {}

    uint8_t U8() { return pos < rom.size() ? rom[pos++] : (pos++, 0); }
    uint16_t U16() { uint16_t lo = U8(); return lo | (uint16_t) (U8() << 8); }
    void Read( uint8_t* dst, size_t n )
    {
        if ( pos <= rom.size() && n <= rom.size() - pos )
        {
            memcpy( dst, &rom[pos], n );
            pos += n;
        }
        else
            for ( size_t i = 0; i < n; i++ ) dst[i] = U8();
    }
    std::vector<uint8_t> Bytes( size_t n )
    {
        std::vector<uint8_t> v( n );
        if ( n ) Read( v.data(), n );
        return v;
    }
};

// ---------------------------------------------------------------------------
// 2-bit-per-pixel canvas. Index 0 is both the stand-in black and "never
// drawn"; the 3DS .bin format doesn't distinguish them either.

struct Bitmap
{
    int w, h;
    std::vector<uint8_t> px;

    Bitmap( int w_, int h_ ) : w( w_ ), h( h_ ), px( (size_t) w_ * h_, 0 ) {}

    void Set( int x, int y, uint8_t v )
    {
        // The C# Bitmap.SetPixel throws out of range; the original extraction
        // never hits that, so silently clipping keeps behaviour identical.
        if ( x >= 0 && y >= 0 && x < w && y < h )
            px[(size_t) y * w + x] = v;
    }
    void FillBlack( int x, int y, int fw, int fh )
    {
        for ( int j = 0; j < fh; j++ )
            for ( int i = 0; i < fw; i++ )
                Set( x + i, y + j, 0 );
    }
    std::vector<uint8_t> ToBin() const
    {
        std::vector<uint8_t> out;
        out.reserve( 4 + px.size() );
        out.push_back( (uint8_t) w ); out.push_back( (uint8_t) (w >> 8) );
        out.push_back( (uint8_t) h ); out.push_back( (uint8_t) (h >> 8) );
        out.insert( out.end(), px.begin(), px.end() );
        return out;
    }
};

const int PrimarySquareTable = 0x1697C + 16;
const int SecondarySquareTable = 0x169B4 + 16;
const int SecretSquareTable = 0x16976 + 16;
const int UnderworldSquareTable = 0x16718 + 16;
const int OWTileCHR = 0xC93B + 16;
const int UWTileCHR = 0xC11B + 16;
const int Misc1CHR = 0x877F + 16;
const int Misc2CHR = 0x8E7F + 16;

const int LinkCHR = 0x807F + 16;
const int Common2CHR = 0x4DB4 + 16;
const int CommonUWSprites = 0xDCBB + 16;
const int OWMonsterCHR = 0xD15B + 16;
const int UW358SpriteCHR = 0xD87B + 16;
const int UW469SpriteCHR = 0xDA9B + 16;
const int UW127SpriteCHR = 0xDDBB + 16;
const int UW1257BossCHR = 0xDFDB + 16;
const int UW3468BossCHR = 0xE3DB + 16;
const int UW9BossCHR = 0xE7DB + 16;

const int TileSize = 16;

const int DefaultSystemPaletteLength = 64;

// ---------------------------------------------------------------------------
// Tile drawing (DrawTile and friends). `transparent` is always false at every
// call site in the original, so it is dropped here.

void DrawTile( Reader& reader, Bitmap& bmp, int x, int y, bool flipX = false, bool flipY = false )
{
    uint8_t tileBuf[TileSize];
    reader.Read( tileBuf, TileSize );

    for ( int v = 0; v < 8; v++ )
    {
        for ( int u = 0; u < 8; u++ )
        {
            int lo = (tileBuf[v] >> (7 - u)) & 1;
            int hi = (tileBuf[v + 8] >> (7 - u)) & 1;
            int pixel = lo | (hi << 1);

            int xOffset = flipX ? (7 - u) : u;
            int yOffset = flipY ? (7 - v) : v;

            bmp.Set( x + xOffset, y + yOffset, (uint8_t) pixel );
        }
    }
}

const int TileXOffset[4] = { 0, 0, 8, 8 };
const int TileYOffset[4] = { 0, 8, 0, 8 };

void SeekBgTile( Reader& reader, int chrBase, int t )
{
    if ( t < 0x70 )
        reader.pos = Misc1CHR + t * TileSize;
    else if ( t >= 0xF2 )
        reader.pos = Misc2CHR + (t - 0xF2) * TileSize;
    else
        reader.pos = chrBase + (t - 0x70) * TileSize;
}

void DrawBgSquare( Reader& reader, Bitmap& bmp, int x, int y, int chrBase, const uint8_t* indexes )
{
    for ( int i = 0; i < 4; i++ )
    {
        SeekBgTile( reader, chrBase, indexes[i] );
        DrawTile( reader, bmp, x + TileXOffset[i], y + TileYOffset[i] );
    }
}

void SeekUWSpriteTile( Reader& reader, int chrBase, int t )
{
    if ( t < 0x70 )
        reader.pos = LinkCHR + t * TileSize;
    else if ( t < 0x8E )
        reader.pos = Common2CHR + (t - 0x70) * TileSize;
    else if ( t < 0x9E )
        reader.pos = CommonUWSprites + (t - 0x8E) * TileSize;
    else
        reader.pos = chrBase + (t - 0x9E) * TileSize;
}

void SeekUWBossTile( Reader& reader, int chrBase, int t )
{
    if ( t < 0x70 )
        reader.pos = LinkCHR + t * TileSize;
    else if ( t < 0x8E )
        reader.pos = Common2CHR + (t - 0x70) * TileSize;
    else if ( t < 0x9E )
        reader.pos = CommonUWSprites + (t - 0x8E) * TileSize;
    else if ( t < 0xC0 )
        reader.pos = (size_t) -1;   // C#: InvalidOperationException; never hit
    else
        reader.pos = chrBase + (t - 0xC0) * TileSize;
}

void DrawSpriteTile( Reader& reader, Bitmap& bmp, int x, int y, int chrBase, int t,
    bool flipX = false, bool flipY = false )
{
    if ( t < 0x70 )
        reader.pos = LinkCHR + t * TileSize;
    else if ( t < 0x8E )
        reader.pos = Common2CHR + (t - 0x70) * TileSize;
    else
        reader.pos = chrBase + (t - 0x8E) * TileSize;

    DrawTile( reader, bmp, x, y, flipX, flipY );
}

void DrawHalfSprite( Reader& reader, Bitmap& bmp, int x, int y, int chrBase, int index,
    bool flipX = false, bool flipY = false )
{
    if ( flipY )
    {
        DrawSpriteTile( reader, bmp, x, y + 8, chrBase, index, flipX, flipY );
        DrawSpriteTile( reader, bmp, x, y, chrBase, index + 1, flipX, flipY );
    }
    else
    {
        DrawSpriteTile( reader, bmp, x, y, chrBase, index, flipX, flipY );
        DrawSpriteTile( reader, bmp, x, y + 8, chrBase, index + 1, flipX, flipY );
    }
}

void DrawUWSpriteTile( Reader& reader, Bitmap& bmp, int x, int y, int chrBase, int index,
    bool flipX, bool flipY )
{
    SeekUWSpriteTile( reader, chrBase, index );
    DrawTile( reader, bmp, x, y, flipX, flipY );
}

void DrawUWHalfSprite( Reader& reader, Bitmap& bmp, int x, int y, int chrBase, int index,
    bool flipX = false, bool flipY = false )
{
    if ( flipY )
    {
        DrawUWSpriteTile( reader, bmp, x, y + 8, chrBase, index, flipX, flipY );
        DrawUWSpriteTile( reader, bmp, x, y, chrBase, index + 1, flipX, flipY );
    }
    else
    {
        DrawUWSpriteTile( reader, bmp, x, y, chrBase, index, flipX, flipY );
        DrawUWSpriteTile( reader, bmp, x, y + 8, chrBase, index + 1, flipX, flipY );
    }
}

void DrawUWBossTile( Reader& reader, Bitmap& bmp, int x, int y, int chrBase, int index,
    bool flipX, bool flipY )
{
    SeekUWBossTile( reader, chrBase, index );
    DrawTile( reader, bmp, x, y, flipX, flipY );
}

void DrawUWBossHalfSprite( Reader& reader, Bitmap& bmp, int x, int y, int chrBase, int index,
    bool flipX = false, bool flipY = false )
{
    if ( flipY )
    {
        DrawUWBossTile( reader, bmp, x, y + 8, chrBase, index, flipX, flipY );
        DrawUWBossTile( reader, bmp, x, y, chrBase, index + 1, flipX, flipY );
    }
    else
    {
        DrawUWBossTile( reader, bmp, x, y, chrBase, index, flipX, flipY );
        DrawUWBossTile( reader, bmp, x, y + 8, chrBase, index + 1, flipX, flipY );
    }
}

void DrawUWBorderLineH( Reader& reader, Bitmap& bmp, int x, int y, int length )
{
    for ( int j = 0; j < length; j++ )
    {
        SeekBgTile( reader, UWTileCHR, 0xF6 );
        DrawTile( reader, bmp, x + j * 8, y );
    }
}

void DrawUWBorderLineV( Reader& reader, Bitmap& bmp, int x, int y, int length )
{
    for ( int j = 0; j < length; j++ )
    {
        SeekBgTile( reader, UWTileCHR, 0xF6 );
        DrawTile( reader, bmp, x, y + j * 8 );
    }
}

// ---------------------------------------------------------------------------
// Text bundle

void ExtractFont( Reader& reader, FileMap& files )
{
    Bitmap bmp( 16 * 8, 16 * 8 );
    int x = 0;
    int y = 0;

    reader.pos = Misc1CHR;

    for ( int i = 0; i < 0x70; i++ )
    {
        DrawTile( reader, bmp, x, y );

        x += 8;
        if ( x >= bmp.w )
        {
            x = 0;
            y += 8;
        }
    }

    SeekBgTile( reader, UWTileCHR, 0xE2 );
    x = 0;

    for ( int i = 0; i < 16; i++ )
    {
        DrawTile( reader, bmp, x, 0x68 );
        x += 8;
    }

    int t = 0xE5;
    x = 0x28;
    y = 0x70;

    for ( int i = 0; i < 27; i++ )
    {
        SeekBgTile( reader, OWTileCHR, t );
        DrawTile( reader, bmp, x, y );

        t++;
        x += 8;
        if ( x >= bmp.w )
        {
            x = 0;
            y += 8;
        }
    }

    SeekUWSpriteTile( reader, UW127SpriteCHR, 0x3E );
    DrawTile( reader, bmp, 0, 0x70 );

    files["font.bin"] = bmp.ToBin();
}

void ExtractText( Reader& reader, FileMap& files )
{
    const int TextPtrs = 0x4000 + 16;

    reader.pos = TextPtrs;
    uint16_t listPtrs[38];
    for ( int i = 0; i < 38; i++ )
        listPtrs[i] = reader.U16();

    std::vector<uint8_t> heap = reader.Bytes( 0x556 );

    Writer w;
    w.U16( 38 );
    for ( int i = 0; i < 38; i++ )
        w.U16( (uint16_t) (listPtrs[i] - listPtrs[0]) );
    w.Bytes( heap );
    w.Pad();
    files["text.tab"] = w.buf;
}

void ExtractCredits( Reader& reader, FileMap& files )
{
    const int TextPtrs = 0xAC2E + 16;

    reader.pos = TextPtrs;
    std::vector<uint8_t> lo = reader.Bytes( 0x17 );
    std::vector<uint8_t> hi = reader.Bytes( 0x17 );
    uint16_t listPtrs[0x17];
    for ( int i = 0; i < 0x17; i++ )
        listPtrs[i] = (uint16_t) (lo[i] | (hi[i] << 8));

    std::vector<uint8_t> heap = reader.Bytes( 0x19E );

    Writer w;
    w.U16( 0x17 );
    for ( int i = 0; i < 0x17; i++ )
        w.U16( (uint16_t) (listPtrs[i] - listPtrs[0]) );
    w.Bytes( heap );
    w.Pad();
    files["credits.tab"] = w.buf;

    const int LineBitmap = 0xAC22 + 16;
    reader.pos = LineBitmap;
    files["creditsLinesBmp.dat"] = reader.Bytes( 12 );
}

// ---------------------------------------------------------------------------
// Tile images

void ExtractUnderworldCellarTiles( Reader& reader, FileMap& files )
{
    Bitmap bmp( 16 * 16, 4 * 16 );

    reader.pos = PrimarySquareTable;            // UWCellarPrimarySquareTable
    std::vector<uint8_t> primaries = reader.Bytes( 56 );
    reader.pos = SecondarySquareTable;          // UWCellarSecondarySquareTable
    std::vector<uint8_t> secondaries = reader.Bytes( 16 * 4 );

    // Underworld cellar rooms are layed out like Overworld rooms, but there
    // are no secret tiles.
    int x = 0, y = 0;
    uint8_t tileIndexes[4];

    for ( int i = 0; i < 56; i++ )
    {
        if ( i < 16 )
        {
            for ( int j = 0; j < 4; j++ )
                tileIndexes[j] = secondaries[i * 4 + j];
        }
        else
        {
            uint8_t primary = primaries[i];
            for ( int j = 0; j < 4; j++ )
                tileIndexes[j] = (uint8_t) (primary + j);
        }
        DrawBgSquare( reader, bmp, x, y, UWTileCHR, tileIndexes );

        if ( (i % 16) == 15 ) { x = 0; y += 16; }
        else x += 16;
    }

    files["underworldCellarTiles.bin"] = bmp.ToBin();
}

void ExtractUnderworldWalls( Reader& reader, Bitmap& bmp )
{
    const int Walls = 0x15fa0 + 16;

    reader.pos = Walls;
    std::vector<uint8_t> wallTiles = reader.Bytes( 78 );

    int row = 0;
    int col = 0;
    uint8_t map[22][32];
    memset( map, 0, sizeof( map ) );

    for ( int i = 0; i < 78; i++ )
    {
        if ( wallTiles[i] != 0 )
        {
            uint8_t tile = wallTiles[i];

            map[row + 1][col + 1] = tile;

            tile = wallTiles[i];
            if ( tile != 0xf5 && tile != 0xde )
                tile++;
            map[(10 - row) + 10][col + 1] = tile;

            tile = wallTiles[i];
            if ( tile == 0xde || tile == 0xdc )
                tile++;
            else if ( tile != 0xf5 && tile != 0xe0 )
                tile += 2;
            map[(10 - row) + 10][(12 - col) + 16 + 2] = tile;

            tile = wallTiles[i];
            if ( tile == 0xde || tile == 0xe0 )
                tile++;
            else if ( tile != 0xf5 && tile != 0xdc )
                tile += 3;
            map[row + 1][(12 - col) + 16 + 2] = tile;
        }

        row++;
        if ( row == 10 || wallTiles[i] == 0 )
        {
            col++;
            row = 0;
        }
    }

    for ( int i = 0; i < 32; i++ )
    {
        map[0][i] = 0xF6;
        map[21][i] = 0xF6;
    }
    for ( int i = 0; i < 20; i++ )
    {
        map[i + 1][0] = 0xF6;
        map[i + 1][31] = 0xF6;
    }

    for ( int r = 0; r < 22; r++ )
    {
        for ( int c = 0; c < 32; c++ )
        {
            uint8_t tile = map[r][c];
            if ( tile != 0 )
            {
                SeekBgTile( reader, UWTileCHR, tile );
                DrawTile( reader, bmp, c * 8, r * 8 );
            }
        }
    }

    bmp.FillBlack( 224, 72, 32, 32 );
    bmp.FillBlack( 0, 72, 32, 32 );
    bmp.FillBlack( 112, 144, 32, 32 );
    bmp.FillBlack( 112, 0, 32, 32 );
}

void ExtractUnderworldDoors( Reader& reader, Bitmap& bmp )
{
    const int EastWall = 0x15FEE + 16;
    const int WestWall = 0x1602A + 16;
    const int SouthWall = 0x16066 + 16;
    const int NorthWall = 0x160A2 + 16;

    reader.pos = EastWall;  std::vector<uint8_t> wallE = reader.Bytes( 60 );
    reader.pos = WestWall;  std::vector<uint8_t> wallW = reader.Bytes( 60 );
    reader.pos = SouthWall; std::vector<uint8_t> wallS = reader.Bytes( 60 );
    reader.pos = NorthWall; std::vector<uint8_t> wallN = reader.Bytes( 60 );

    int baseY = 0;
    bool transparent = false;
    int row, col, k;

    for ( int layer = 0; layer < 2; layer++, baseY += 128 )
    {
        k = 0;

        for ( int i = 0; i < 5; i++ )
        {
            int startX = i * 32;
            row = 0;
            col = 0;
            for ( int j = 0; j < 12; j++, k++ )
            {
                if ( !transparent || row > 0 )
                {
                    SeekBgTile( reader, UWTileCHR, wallS[k] );
                    DrawTile( reader, bmp, startX + col * 8, row * 8 + baseY + 0 );
                }
                if ( !transparent || row < 2 )
                {
                    SeekBgTile( reader, UWTileCHR, wallN[k] );
                    DrawTile( reader, bmp, startX + col * 8, row * 8 + baseY + 40 );
                }

                row++;
                if ( row == 3 )
                {
                    col++;
                    row -= 3;
                }
            }
        }

        for ( int i = 0; i < 2; i++ )
            DrawUWBorderLineH( reader, bmp, 0, i * 8 + baseY + 24, 20 );

        k = 0;

        for ( int i = 0; i < 5; i++ )
        {
            int startX = i * 32;
            row = 0;
            col = 0;
            for ( int j = 0; j < 12; j++, k++ )
            {
                if ( !transparent || col > 0 )
                {
                    SeekBgTile( reader, UWTileCHR, wallE[k] );
                    DrawTile( reader, bmp, startX + col * 8, row * 8 + baseY + 64 );
                }
                if ( !transparent || col < 2 )
                {
                    SeekBgTile( reader, UWTileCHR, wallW[k] );
                    DrawTile( reader, bmp, startX + col * 8 + 8, row * 8 + baseY + 96 );
                }

                row++;
                if ( (row % 2) == 0 )
                {
                    col++;
                    row -= 2;

                    if ( col == 3 )
                    {
                        col = 0;
                        row = 2;
                    }
                }
            }
            DrawUWBorderLineV( reader, bmp, startX + 24, baseY + 64, 4 );
            DrawUWBorderLineV( reader, bmp, startX + 0, baseY + 96, 4 );
        }
        transparent = true;
    }
}

void ExtractUnderworldTiles( Reader& reader, FileMap& files )
{
    {
        Bitmap bmp( 16 * 16, 4 * 16 );

        reader.pos = UnderworldSquareTable;
        std::vector<uint8_t> primaries = reader.Bytes( 8 );
        int x = 0, y = 0;
        uint8_t tileIndexes[4];

        for ( int i = 0; i < 8; i++ )
        {
            uint8_t primary = primaries[i];

            if ( primary < 0x70 || primary > 0xF2 )
            {
                for ( int j = 0; j < 4; j++ )
                    tileIndexes[j] = primary;
            }
            else
            {
                for ( int j = 0; j < 4; j++ )
                    tileIndexes[j] = (uint8_t) (primary + j);
            }
            DrawBgSquare( reader, bmp, x, y, UWTileCHR, tileIndexes );

            if ( (i % 16) == 15 ) { x = 0; y += 16; }
            else x += 16;
        }
        files["underworldTiles.bin"] = bmp.ToBin();
    }
    {
        Bitmap bmp( 16 * 16, 11 * 16 );
        ExtractUnderworldWalls( reader, bmp );
        files["underworldWalls.bin"] = bmp.ToBin();
    }
    {
        Bitmap bmp( 16 * 16, 16 * 16 );
        ExtractUnderworldDoors( reader, bmp );
        files["underworldDoors.bin"] = bmp.ToBin();
    }
}

void ExtractOverworldTiles( Reader& reader, FileMap& files )
{
    Bitmap bmp( 16 * 16, 4 * 16 );

    reader.pos = PrimarySquareTable;
    std::vector<uint8_t> primaries = reader.Bytes( 56 );
    reader.pos = SecondarySquareTable;
    std::vector<uint8_t> secondaries = reader.Bytes( 16 * 4 );   // 16 squares, 4 8x8 tiles each
    reader.pos = SecretSquareTable;
    std::vector<uint8_t> secrets = reader.Bytes( 6 );            // 1 byte refs to primary squares

    int x = 0, y = 0;
    uint8_t tileIndexes[4];

    for ( int i = 0; i < 56; i++ )
    {
        if ( i < 16 )
        {
            for ( int j = 0; j < 4; j++ )
                tileIndexes[j] = secondaries[i * 4 + j];
        }
        else
        {
            uint8_t primary = primaries[i];
            if ( primary >= 0xE5 && primary <= 0xEA )
                primary = secrets[primary - 0xE5];
            for ( int j = 0; j < 4; j++ )
                tileIndexes[j] = (uint8_t) (primary + j);
        }
        DrawBgSquare( reader, bmp, x, y, OWTileCHR, tileIndexes );

        if ( (i % 16) == 15 ) { x = 0; y += 16; }
        else x += 16;
    }

    files["overworldTiles.bin"] = bmp.ToBin();
}

// ---------------------------------------------------------------------------
// Tile attributes

enum TileAction
{
    TA_None, TA_Push, TA_Bomb, TA_Burn, TA_Headstone, TA_Ladder, TA_Raft,
    TA_Cave, TA_Stairs, TA_Ghost, TA_Armos,
};

bool IsWalkable( int t )
{
    switch ( t )
    {
    case 0xDF: case 0xD5: case 0xD2: case 0xCC: case 0xAD:
    case 0xAC: case 0x9C: case 0x91: case 0x8D:
        return true;
    default:
        return t < 0x89;
    }
}

TileAction GetAction( int t )
{
    switch ( t )
    {
    case 0x0B: return TA_Raft;
    case 0x0C:
    case 0x0F: return TA_Cave;
    case 0x12: return TA_Stairs;
    case 0x14: return TA_Ghost;
    case 0x26: return TA_Push;
    case 0x27: return TA_Bomb;
    case 0x28: return TA_Burn;
    case 0x29: return TA_Headstone;
    case 0x2A:
    case 0x2B:
    case 0x2C: return TA_Armos;
    default:
        if ( (t >= 0x5 && t <= 0x9) || (t >= 0x15 && t <= 0x18) )
            return TA_Ladder;
        return TA_None;
    }
}

bool IsUnderworldWalkable( int t ) { return t < 0x78; }

TileAction GetUnderworldAction( int t )
{
    switch ( t )
    {
    case 4: return TA_Stairs;
    case 6: return TA_Ladder;
    default: return TA_None;
    }
}

void ExtractOverworldTileAttrs( Reader& reader, FileMap& files )
{
    reader.pos = PrimarySquareTable;
    std::vector<uint8_t> primaries = reader.Bytes( 56 );
    reader.pos = SecondarySquareTable;
    std::vector<uint8_t> secondaries = reader.Bytes( 16 * 4 );
    reader.pos = SecretSquareTable;
    std::vector<uint8_t> secrets = reader.Bytes( 6 );

    Writer w;
    for ( int i = 0; i < 56; i++ )
    {
        int attr = 0;
        int walkBit = 1;

        for ( int j = 0; j < 4; j++ )
        {
            int t;
            if ( i < 16 )
                t = secondaries[i * 4 + j];
            else
            {
                uint8_t primary = primaries[i];
                if ( primary >= 0xE5 && primary <= 0xEA )
                    primary = secrets[primary - 0xE5];
                t = (uint8_t) (primary + j);
            }
            if ( !IsWalkable( t ) )
                attr |= walkBit;
            walkBit <<= 1;
        }
        w.U8( (uint8_t) (attr | ((int) GetAction( i ) << 4)) );
    }
    w.Pad();
    files["overworldTileAttrs.dat"] = w.buf;
}

void ExtractUnderworldTileAttrs( Reader& reader, FileMap& files )
{
    reader.pos = UnderworldSquareTable;
    std::vector<uint8_t> primaries = reader.Bytes( 8 );

    Writer w;
    for ( int i = 0; i < 8; i++ )
    {
        int attr = 0;
        int walkBit = 1;
        uint8_t primary = primaries[i];

        for ( int j = 0; j < 4; j++ )
        {
            int t = (primary < 0x70 || primary > 0xF2) ? primary : (uint8_t) (primary + j);
            if ( !IsUnderworldWalkable( t ) )
                attr |= walkBit;
            walkBit <<= 1;
        }
        w.U8( (uint8_t) (attr | ((int) GetUnderworldAction( i ) << 4)) );
    }
    w.U8( 0xF );     // tileAttrs[8] = 0xF, action None
    w.Pad();
    files["underworldTileAttrs.dat"] = w.buf;
}

void ExtractUnderworldCellarTileAttrs( Reader& reader, FileMap& files )
{
    reader.pos = PrimarySquareTable;
    std::vector<uint8_t> primaries = reader.Bytes( 56 );
    reader.pos = SecondarySquareTable;
    std::vector<uint8_t> secondaries = reader.Bytes( 16 * 4 );

    Writer w;
    for ( int i = 0; i < 56; i++ )
    {
        int attr = 0;
        int walkBit = 1;

        for ( int j = 0; j < 4; j++ )
        {
            int t = (i < 16) ? secondaries[i * 4 + j] : (uint8_t) (primaries[i] + j);
            if ( !IsUnderworldWalkable( t ) )
                attr |= walkBit;
            walkBit <<= 1;
        }
        w.U8( (uint8_t) attr );     // tileAction left as None
    }
    w.Pad();
    files["underworldCellarTileAttrs.dat"] = w.buf;
}

// ---------------------------------------------------------------------------
// Room layouts

void WriteColTab( Writer& w, const std::vector<uint16_t>& colTablePtrs, const std::vector<uint8_t>& colTables )
{
    w.U16( (uint16_t) colTablePtrs.size() );
    for ( size_t i = 0; i < colTablePtrs.size(); i++ )
        w.U16( (uint16_t) (colTablePtrs[i] - colTablePtrs[0]) );
    w.Bytes( colTables );
    w.Pad();
}

std::vector<uint16_t> ReadColTablePtrs( Reader& reader, int count )
{
    // Each pointer becomes relative to the first raw one; then [0] = 0.
    std::vector<uint16_t> ptrs( count, 0 );
    for ( int i = 0; i < count; i++ )
        ptrs[i] = (uint16_t) (reader.U16() - ptrs[0]);
    ptrs[0] = 0;
    return ptrs;
}

void ExtractOverworldMap( Reader& reader, FileMap& files )
{
    const int OWRoomCols = 0x15418 + 16;
    const int OWColDir = 0x19D0F + 16;
    const int OWColTables = 0x15BD8 + 16;

    reader.pos = OWRoomCols;
    files["overworldRoomCols.dat"] = reader.Bytes( 124 * 16 );

    reader.pos = OWColDir;
    std::vector<uint16_t> colTablePtrs = ReadColTablePtrs( reader, 16 );

    // There are only 10 columns in the last table
    reader.pos = OWColTables;
    std::vector<uint8_t> colTables = reader.Bytes( 964 );

    Writer w;
    WriteColTab( w, colTablePtrs, colTables );
    files["overworldCols.tab"] = w.buf;
}

void ExtractUnderworldMap( Reader& reader, FileMap& files )
{
    const int UWRoomCols = 0x160DE + 16;
    const int UWColDir = 0x16704 + 16;
    const int UWColTables = 0x162D6 + 16;

    reader.pos = UWRoomCols;
    std::vector<uint8_t> roomCols = reader.Bytes( 64 * 12 );

    reader.pos = UWColDir;
    std::vector<uint16_t> colTablePtrs = ReadColTablePtrs( reader, 10 );

    // There are only 9 columns in the last table
    reader.pos = UWColTables;
    std::vector<uint8_t> colTables = reader.Bytes( 222 );

    Writer rc;
    for ( int i = 0; i < 64; i++ )
    {
        rc.Bytes( &roomCols[i * 12], 12 );
        for ( int p = 0; p < 4; p++ )
            rc.U8( 0 );
    }
    files["underworldRoomCols.dat"] = rc.buf;

    Writer w;
    WriteColTab( w, colTablePtrs, colTables );
    files["underworldCols.tab"] = w.buf;
}

void ExtractUnderworldCellarMap( Reader& reader, FileMap& files )
{
    const int UWCellarRoomCols = 0x163B4 + 16;
    const int UWCellarColTables = 0x163D4 + 16;

    reader.pos = UWCellarRoomCols;
    files["underworldCellarRoomCols.dat"] = reader.Bytes( 2 * 16 );

    // There's only one column table, so it's at offset 0 in the output heap.
    std::vector<uint16_t> colTablePtrs( 1, 0 );

    // There are only 5 columns in the last table
    reader.pos = UWCellarColTables;
    std::vector<uint8_t> colTables = reader.Bytes( 34 );

    Writer w;
    WriteColTab( w, colTablePtrs, colTables );
    files["underworldCellarCols.tab"] = w.buf;
}

// ---------------------------------------------------------------------------
// Overworld room attributes

const int OWOuterRoomAttr = 0x18400 + 16;
const int OWInnerRoomAttr = 0x18480 + 16;
const int OWMonsterListID = 0x18500 + 16;
const int OWMapLayout = 0x18580 + 16;
const int OWOtherRoomAttr = 0x18680 + 16;
const int OWMonsterCount = 0x19324 + 16;

struct OWRoomAttrs
{
    std::vector<uint8_t> monsterCounts, outer, inner, monsterListIDs, worldLayout, other;
};

OWRoomAttrs ReadOverworldRoomAttrs( Reader& reader )
{
    OWRoomAttrs a;
    reader.pos = OWMonsterCount;  a.monsterCounts = reader.Bytes( 4 );
    reader.pos = OWOuterRoomAttr; a.outer = reader.Bytes( 128 );
    reader.pos = OWInnerRoomAttr; a.inner = reader.Bytes( 128 );
    reader.pos = OWMonsterListID; a.monsterListIDs = reader.Bytes( 128 );
    reader.pos = OWMapLayout;     a.worldLayout = reader.Bytes( 128 );
    reader.pos = OWOtherRoomAttr; a.other = reader.Bytes( 128 );
    return a;
}

void WriteConvertedOWRoomAttrs( Writer& w, int index, const OWRoomAttrs& a )
{
    int outerPalette = a.outer[index] & 0x03;
    int seaSound = (a.outer[index] & 0x04) >> 2;
    int zora = (a.outer[index] & 0x08) >> 3;
    int exitColumn = (a.outer[index] & 0xF0) >> 4;

    int innerPalette = a.inner[index] & 0x03;
    int cave = (a.inner[index] & 0xFC) >> 2;

    int monsterListIDLo6 = a.monsterListIDs[index] & 0x3F;
    int monsterCountIndex = (a.monsterListIDs[index] & 0xC0) >> 6;

    int uniqueRoomID = a.worldLayout[index] & 0x7F;
    int monsterListIDHi1 = (a.worldLayout[index] & 0x80) >> 7;

    int exitRow = a.other[index] & 0x07;
    int edgeMonsters = (a.other[index] & 0x08) >> 3;
    int shortcutStairsIndex = (a.other[index] & 0x30) >> 4;
    int questSecrets = (a.other[index] & 0xC0) >> 6;

    int monsterCount = a.monsterCounts[monsterCountIndex] & 0x0F;

    w.U8( (uint8_t) uniqueRoomID );
    w.U8( (uint8_t) (outerPalette | (innerPalette << 2) | (monsterCount << 4)) );
    w.U8( (uint8_t) (monsterListIDLo6 | (monsterListIDHi1 << 6)) );
    w.U8( (uint8_t) (exitColumn | (exitRow << 4)) );
    w.U8( (uint8_t) (cave | (questSecrets << 6)) );
    w.U8( (uint8_t) (shortcutStairsIndex | (zora << 2) | (edgeMonsters << 3) | (seaSound << 4)) );
    w.U8( 0 );
}

void ExtractOverworldMapAttrs( Reader& reader, FileMap& files )
{
    OWRoomAttrs a = ReadOverworldRoomAttrs( reader );
    Writer w;
    for ( int i = 0; i < 128; i++ )
        WriteConvertedOWRoomAttrs( w, i, a );
    w.Pad();
    files["overworldRoomAttr.dat"] = w.buf;
}

// ---------------------------------------------------------------------------
// Underworld room attributes

const int UWOuterRoomAttr = 0x18700 + 16;
const int UWInnerRoomAttr = 0x18780 + 16;
const int UWMonsterListID = 0x18800 + 16;
const int UWMapLayout = 0x18880 + 16;
const int UWItemRoomAttr = 0x18900 + 16;
const int UWSpecialRoomAttr = 0x18980 + 16;
const int UWMonsterCount = 0x19420 + 16;

void ExtractUnderworldMapAttrs( Reader& reader, FileMap& files, int uwLevelGroup )
{
    int groupOffset = 768 * uwLevelGroup;

    // The counts are the same for all UW levels.
    reader.pos = UWMonsterCount;                  std::vector<uint8_t> monsterCounts = reader.Bytes( 4 );
    reader.pos = UWOuterRoomAttr + groupOffset;   std::vector<uint8_t> outerSN = reader.Bytes( 128 );
    reader.pos = UWInnerRoomAttr + groupOffset;   std::vector<uint8_t> innerEW = reader.Bytes( 128 );
    reader.pos = UWMonsterListID + groupOffset;   std::vector<uint8_t> monsterListIDs = reader.Bytes( 128 );
    reader.pos = UWMapLayout + groupOffset;       std::vector<uint8_t> worldLayout = reader.Bytes( 128 );
    reader.pos = UWItemRoomAttr + groupOffset;    std::vector<uint8_t> items = reader.Bytes( 128 );
    reader.pos = UWSpecialRoomAttr + groupOffset; std::vector<uint8_t> special = reader.Bytes( 128 );

    Writer w;
    for ( int index = 0; index < 128; index++ )
    {
        int roomLeft = outerSN[index];
        int roomRight = innerEW[index];

        int outerPalette = outerSN[index] & 0x03;
        int south = (outerSN[index] >> 2) & 0x07;
        int north = (outerSN[index] >> 5) & 0x07;

        int innerPalette = innerEW[index] & 0x03;
        int east = (innerEW[index] >> 2) & 0x07;
        int west = (innerEW[index] >> 5) & 0x07;

        int monsterListIDLo6 = monsterListIDs[index] & 0x3F;
        int monsterCountIndex = (monsterListIDs[index] >> 6) & 3;

        int uniqueRoomID = worldLayout[index] & 0x3F;
        int pushBlock = (worldLayout[index] >> 6) & 1;
        int monsterListIDHi1 = (worldLayout[index] >> 7) & 1;

        int item = items[index] & 0x1F;
        int sound = (items[index] >> 5) & 3;
        int dark = (items[index] >> 7) & 1;

        int secret = special[index] & 0x07;
        int itemPosIndex = (special[index] >> 4) & 3;

        int monsterCount = monsterCounts[monsterCountIndex] & 0x0F;
        bool special3E = uniqueRoomID == 0x3E || uniqueRoomID == 0x3F;

        w.U8( (uint8_t) uniqueRoomID );
        w.U8( (uint8_t) (outerPalette | (innerPalette << 2) | (monsterCount << 4)) );
        w.U8( (uint8_t) (monsterListIDLo6 | (monsterListIDHi1 << 6)) );
        w.U8( special3E ? (uint8_t) roomLeft : (uint8_t) (south | (north << 3)) );
        w.U8( special3E ? (uint8_t) roomRight : (uint8_t) (east | (west << 3)) );
        w.U8( (uint8_t) (item | (itemPosIndex << 5)) );
        w.U8( (uint8_t) (secret | (pushBlock << 3) | (dark << 4) | (sound << 5)) );
    }
    w.Pad();

    char name[32];
    snprintf( name, sizeof( name ), "underworldRoomAttr%d.dat", uwLevelGroup );
    files[name] = w.buf;
}

// ---------------------------------------------------------------------------
// Overworld sparse attributes

void ExtractOverworldMapSparseAttrs( Reader& reader, FileMap& files )
{
    const int AttrLines = 11;
    const int Alignment = 2;

    const int OWArmosStairsRoomCount = 6;
    const int OWArmosStairsRoomId = 0x10CB3 + 16;
    const int OWArmosStairsCol = 0x10CBA + 16;
    const int OWArmosStairsRow = 0x10CE5 + 16;
    const int OWArmosItemRoomId = 0x10CB2 + 16;
    const int OWArmosItemX = 0x10CB9 + 16;
    const int OWArmosItemId = 0x10CF5 + 16;
    const int OWItemRoomId = 0x1789A + 16;
    const int OWItemId = 0x1788A + 16;
    const int OWItemX = 0x1788E + 16;
    const int OWItemY = 0x17890 + 16;
    const int OWShortcutCount = 4;
    const int OWShortcutRoomId = 0x19334 + 16;
    const int OWMazePath = 0x6D97 + 16;
    const int OWMazePathLen = 4;
    const int OWLadderRoomCount = 6;
    const int OWLadderRoomId = 0x1F20D + 16;
    const int OWRecorderRoomCount = 11;
    const int OWRecorderRoomId = 0x1EF66 + 16;

    const uint8_t dockRoomIds[] = { 0x3F, 0x55 };
    const uint8_t mazeRoomIds[] = { 0x61, 0x1B };
    const uint8_t mazeExitDirs[] = { 0x01, 0x02 };
    const uint8_t secretShiftRoomIds[] = { 0x1F };
    const uint8_t secretShiftDirs[] = { 0x08 };
    const uint8_t fairyRoomIds[] = { 0x39, 0x43 };
    const uint8_t attrReplacementRoomIds[] = { 0x0B, 0x0E, 0x0F, 0x22, 0x34, 0x3C, 0x74 };

    reader.pos = OWArmosStairsRoomId; std::vector<uint8_t> armosStairsRoomIds = reader.Bytes( OWArmosStairsRoomCount );
    reader.pos = OWArmosStairsCol;    std::vector<uint8_t> armosStairsXs = reader.Bytes( OWArmosStairsRoomCount );
    reader.pos = OWArmosStairsRow;    uint8_t armosSecretY = reader.U8();
    reader.pos = OWArmosItemRoomId;   uint8_t armosItemRoomId = reader.U8();
    reader.pos = OWArmosItemX;        uint8_t armosItemX = reader.U8();
    reader.pos = OWArmosItemId;       uint8_t armosItemId = reader.U8();
    reader.pos = OWItemRoomId;        uint8_t itemRoomId = reader.U8();
    reader.pos = OWItemX;             uint8_t itemX = reader.U8();
    reader.pos = OWItemY;             uint8_t itemY = reader.U8();
    reader.pos = OWItemId;            uint8_t itemId = reader.U8();
    reader.pos = OWShortcutRoomId;    std::vector<uint8_t> shortcutRoomIds = reader.Bytes( OWShortcutCount );

    reader.pos = OWMazePath;
    std::vector<uint8_t> mazePaths[2];
    for ( int i = 0; i < 2; i++ )
        mazePaths[i] = reader.Bytes( OWMazePathLen );

    reader.pos = OWLadderRoomId;      std::vector<uint8_t> ladderRoomIds = reader.Bytes( OWLadderRoomCount );
    reader.pos = OWRecorderRoomId;    std::vector<uint8_t> recorderRoomIds = reader.Bytes( OWRecorderRoomCount );
    std::vector<uint8_t> recorderStairsPositions( recorderRoomIds.size(), 0x69 );

    OWRoomAttrs roomAttrs = ReadOverworldRoomAttrs( reader );
    roomAttrs.outer[0x3C] = 0x72;
    roomAttrs.outer[0x74] = 0x72;
    roomAttrs.inner[0x0E] = 0x7B;
    roomAttrs.inner[0x0F] = 0x83;
    roomAttrs.inner[0x22] = 0x84;
    roomAttrs.inner[0x34] = 0x0F;
    roomAttrs.inner[0x3C] = 0x0B;
    roomAttrs.inner[0x45] = 0x12;
    roomAttrs.inner[0x74] = 0x7A;
    roomAttrs.monsterListIDs[0x0B] = 0x2F;
    roomAttrs.worldLayout[0x0B] = 0x7B;
    roomAttrs.worldLayout[0x3C] = 0x7B;
    roomAttrs.worldLayout[0x74] = 0x5A;
    roomAttrs.other[0x3C] = 0x01;
    roomAttrs.other[0x74] = 0x00;

    Writer w;
    uint16_t ptrs[AttrLines];
    int i = 0;
    const int bufBase = (1 + AttrLines) * 2;

    w.Seek( bufBase );

    auto writeRooms = [&w]( const uint8_t* ids, size_t n )
    {
        w.U8( (uint8_t) n );
        w.U8( 1 );
        for ( size_t k = 0; k < n; k++ )
            w.U8( ids[k] );
    };
    auto writeRoomItem = [&w]( uint8_t roomId, uint8_t x, uint8_t y, uint8_t id )
    {
        w.U8( 1 ); w.U8( 4 ); w.U8( roomId ); w.U8( x ); w.U8( y ); w.U8( id );
    };
    auto writeRoomPairs = [&w]( const uint8_t* ids, const uint8_t* vals, size_t n )
    {
        // WriteRoomPos and WriteRoomByte have the same layout.
        w.U8( (uint8_t) n );
        w.U8( 2 );
        for ( size_t k = 0; k < n; k++ ) { w.U8( ids[k] ); w.U8( vals[k] ); }
    };

    // Armos stairs (WriteRoomXY)
    ptrs[i++] = (uint16_t) w.Align( Alignment );
    w.U8( (uint8_t) armosStairsRoomIds.size() );
    w.U8( 3 );
    for ( size_t k = 0; k < armosStairsRoomIds.size(); k++ )
    {
        w.U8( armosStairsRoomIds[k] );
        w.U8( armosStairsXs[k] );
        w.U8( armosSecretY );
    }

    ptrs[i++] = (uint16_t) w.Align( Alignment );
    writeRoomItem( armosItemRoomId, armosItemX, armosSecretY, armosItemId );

    ptrs[i++] = (uint16_t) w.Align( Alignment );
    writeRooms( dockRoomIds, sizeof( dockRoomIds ) );

    ptrs[i++] = (uint16_t) w.Align( Alignment );
    writeRoomItem( itemRoomId, itemX, itemY, itemId );

    ptrs[i++] = (uint16_t) w.Align( Alignment );
    writeRooms( shortcutRoomIds.data(), shortcutRoomIds.size() );

    // Mazes (WriteRoomMaze)
    ptrs[i++] = (uint16_t) w.Align( Alignment );
    w.U8( 2 );
    w.U8( 6 );
    for ( int k = 0; k < 2; k++ )
    {
        w.U8( mazeRoomIds[k] );
        w.U8( mazeExitDirs[k] );
        w.Bytes( mazePaths[k] );
    }

    ptrs[i++] = (uint16_t) w.Align( Alignment );
    writeRoomPairs( secretShiftRoomIds, secretShiftDirs, 1 );

    ptrs[i++] = (uint16_t) w.Align( Alignment );
    writeRooms( ladderRoomIds.data(), ladderRoomIds.size() );

    ptrs[i++] = (uint16_t) w.Align( Alignment );
    writeRoomPairs( recorderRoomIds.data(), recorderStairsPositions.data(), recorderRoomIds.size() );

    ptrs[i++] = (uint16_t) w.Align( Alignment );
    writeRooms( fairyRoomIds, sizeof( fairyRoomIds ) );

    // Room attr replacements (WriteRoomAttrReplacements)
    ptrs[i++] = (uint16_t) w.Align( Alignment );
    w.U8( (uint8_t) sizeof( attrReplacementRoomIds ) );
    w.U8( 7 );
    for ( size_t k = 0; k < sizeof( attrReplacementRoomIds ); k++ )
    {
        w.U8( attrReplacementRoomIds[k] );
        WriteConvertedOWRoomAttrs( w, attrReplacementRoomIds[k], roomAttrs );
    }

    // Pad now, because after this we'll seek to the beginning of the file.
    w.Pad();

    w.Seek( 0 );
    w.U16( AttrLines );
    for ( int k = 0; k < AttrLines; k++ )
        w.U16( (uint16_t) (ptrs[k] - bufBase) );

    files["overworldRoomSparseAttr.tab"] = w.buf;
}

// ---------------------------------------------------------------------------
// Level info blocks and directories

const int OWInfoBlock = 0x19300 + 16;
const int InfoBlockPalettesOffset = 3;
const int InfoBlockStartY = 0x28;
const int InfoBlockStartRoomId = 0x2F;
const int InfoBlockTriforceRoomId = 0x30;
const int InfoBlockBossRoomId = 0x3E;
const int InfoBlockLevelNumber = 0x33;
const int InfoBlockDrawnMapOffset = 0x2D;
const int InfoBlockCellarRoomIdArray = 0x34;
const int InfoBlockShortcutPosArray = 0x29;
const int InfoBlockDrawnMap = 0x3F;
const int InfoBlockCellarPalette1 = 0x7C;
const int InfoBlockCellarPalette2 = 0x9C;
const int InfoBlockDarkPalette = 0xBC;
const int InfoBlockDeathPalette = 0xDC;
const int InfoBlockShortcutPosCount = 4;
const int InfoBlockCellarRoomIdCount = 10;
const int OWLastSpritePal = 0x1A281 + 16;
const int InfoBlockSize = 0xFC;

const int PaletteByteCount = 8 * 4;

void WritePalettes( Writer& w, const std::vector<uint8_t>& paletteBytes, size_t count )
{
    for ( size_t i = 0; i < count; i++ )
    {
        uint8_t colorIndex = paletteBytes[i];
        if ( colorIndex >= DefaultSystemPaletteLength )
            colorIndex = 0;
        w.U8( colorIndex );
    }
}

void WriteTrailingPalettes( Reader& reader, Writer& w, int blockOffset )
{
    reader.pos = OWInfoBlock + blockOffset + InfoBlockCellarPalette1;
    WritePalettes( w, reader.Bytes( PaletteByteCount ), PaletteByteCount );
    reader.pos = OWInfoBlock + blockOffset + InfoBlockCellarPalette2;
    WritePalettes( w, reader.Bytes( PaletteByteCount ), PaletteByteCount );
    reader.pos = OWInfoBlock + blockOffset + InfoBlockDarkPalette;
    WritePalettes( w, reader.Bytes( PaletteByteCount ), PaletteByteCount );
    reader.pos = OWInfoBlock + blockOffset + InfoBlockDeathPalette;
    w.Bytes( reader.Bytes( PaletteByteCount ) );
    w.Pad();
}

struct LevelDirectory
{
    const char* names[18] = {};
    // LevelInfoBlock, RoomCols, ColTables, TileAttrs, TilesImage, PlayerImage,
    // PlayerSheet, NpcImage, NpcSheet, BossImage, BossSheet, RoomAttrs,
    // LevelInfoEx, ObjLists, Extra1, Extra2, Extra3, Extra4
};

void WriteLevelDir( FileMap& files, int quest, int level, const LevelDirectory& dir )
{
    const int EntryLength = 32;
    Writer w;
    for ( int i = 0; i < 18; i++ )
        w.FixedString( dir.names[i], EntryLength );

    char name[32];
    snprintf( name, sizeof( name ), "levelDir_%d_%d.dat", quest, level );
    files[name] = w.buf;
}

void ExtractOverworldInfo( Reader& reader, FileMap& files )
{
    Writer w;

    reader.pos = OWInfoBlock + InfoBlockPalettesOffset;
    std::vector<uint8_t> paletteBytes = reader.Bytes( PaletteByteCount );

    // Overwrite the last sprite palette, because the original doesn't seem to
    // be used, but the other one is. It's used by zoras and moblins, and for
    // flashing.
    reader.pos = OWLastSpritePal;
    reader.Read( &paletteBytes[7 * 4], 4 );
    w.Bytes( paletteBytes );

    reader.pos = OWInfoBlock + InfoBlockStartY;       w.U8( reader.U8() );
    reader.pos = OWInfoBlock + InfoBlockStartRoomId;  w.U8( reader.U8() );
    reader.pos = OWInfoBlock + InfoBlockTriforceRoomId; w.U8( reader.U8() );
    reader.pos = OWInfoBlock + InfoBlockBossRoomId;   w.U8( reader.U8() );
    w.U8( 2 );  // song
    reader.pos = OWInfoBlock + InfoBlockLevelNumber;
    uint8_t levelNumber = reader.U8();
    w.U8( levelNumber );
    // The Overworld's effective level number is the same.
    w.U8( levelNumber );
    reader.pos = OWInfoBlock + InfoBlockDrawnMapOffset; w.U8( reader.U8() );
    reader.pos = OWInfoBlock + InfoBlockCellarRoomIdArray; w.Bytes( reader.Bytes( InfoBlockCellarRoomIdCount ) );
    reader.pos = OWInfoBlock + InfoBlockShortcutPosArray;  w.Bytes( reader.Bytes( InfoBlockShortcutPosCount ) );
    reader.pos = OWInfoBlock + InfoBlockDrawnMap;          w.Bytes( reader.Bytes( 16 ) );
    w.Pad();

    WriteTrailingPalettes( reader, w, 0 );
    files["overworldInfo.dat"] = w.buf;

    LevelDirectory dir;
    dir.names[0] = "overworldInfo.dat";
    dir.names[1] = "overworldRoomCols.dat";
    dir.names[2] = "overworldCols.tab";
    dir.names[3] = "overworldTileAttrs.dat";
    dir.names[4] = "overworldTiles.png";
    dir.names[5] = "playerItem.png";
    dir.names[6] = "playerItemsSheet.tab";
    dir.names[7] = "owNpcs.png";
    dir.names[8] = "owNpcsSheet.tab";
    dir.names[11] = "overworldRoomAttr.dat";
    dir.names[12] = "overworldInfoEx.tab";
    dir.names[13] = "objLists.tab";
    dir.names[14] = "overworldRoomSparseAttr.tab";
    WriteLevelDir( files, 0, 0, dir );
    WriteLevelDir( files, 1, 0, dir );
}

const char* const bossImageFilenames[10] =
{
    "", "uwBoss1257.png", "uwBoss1257.png", "uwBoss3468.png", "uwBoss3468.png",
    "uwBoss1257.png", "uwBoss3468.png", "uwBoss1257.png", "uwBoss3468.png", "uwBoss9.png",
};

const char* const bossSheetFilenames[10] =
{
    "", "uwBossSheet1257.tab", "uwBossSheet1257.tab", "uwBossSheet3468.tab", "uwBossSheet3468.tab",
    "uwBossSheet1257.tab", "uwBossSheet3468.tab", "uwBossSheet1257.tab", "uwBossSheet3468.tab", "uwBossSheet9.tab",
};

void ExtractUnderworldInfo( Reader& reader, FileMap& files, int quest, int level )
{
    const int InfoBlockDiffPtrs = 0x183A4 + 0x10;
    const int FirstInfoBlockDiff = 0x1816F + 0x10;
    const int InfoBlockDiffEffectiveLevelNumber = 0xA;
    const int InfoBlockDiffStartRoomId = 6;
    const int InfoBlockDiffTriforceRoomId = 7;
    const int InfoBlockDiffBossRoomId = 0x15;
    const int InfoBlockDiffDrawnMapOffset = 4;
    const int InfoBlockDiffCellarRoomIdArray = 0xB;
    const int InfoBlockDiffDrawnMap = 0x16;
    const int InfoBlockDiffShortcutPosArray = 0;

    char filename[32];
    snprintf( filename, sizeof( filename ), "levelInfo_%d_%d.dat", quest, level );
    int effectiveLevel = level;
    int quest2DiffAddr = 0;

    if ( quest == 1 )
    {
        reader.pos = InfoBlockDiffPtrs;
        int firstPtr = reader.U16();
        reader.pos = InfoBlockDiffPtrs + (level - 1) * 2;
        int ptr = reader.U16();
        quest2DiffAddr = (ptr - firstPtr) + FirstInfoBlockDiff;

        reader.pos = quest2DiffAddr + InfoBlockDiffEffectiveLevelNumber;
        effectiveLevel = reader.U8();
        if ( effectiveLevel < 1 || effectiveLevel > 9 )
            effectiveLevel = level;     // guard; never true for the real ROM

        reader.pos = InfoBlockDiffPtrs + (effectiveLevel - 1) * 2;
        ptr = reader.U16();
        quest2DiffAddr = (ptr - firstPtr) + FirstInfoBlockDiff;
    }

    int blockOffset = InfoBlockSize * effectiveLevel;
    auto field = [&]( int q1Offset, int q2Offset ) -> size_t
    {
        return quest == 0 ? (size_t) (OWInfoBlock + blockOffset + q1Offset)
                          : (size_t) (quest2DiffAddr + q2Offset);
    };

    Writer w;
    reader.pos = OWInfoBlock + blockOffset + InfoBlockPalettesOffset;
    w.Bytes( reader.Bytes( PaletteByteCount ) );

    reader.pos = OWInfoBlock + blockOffset + InfoBlockStartY; w.U8( reader.U8() );
    reader.pos = field( InfoBlockStartRoomId, InfoBlockDiffStartRoomId ); w.U8( reader.U8() );
    reader.pos = field( InfoBlockTriforceRoomId, InfoBlockDiffTriforceRoomId ); w.U8( reader.U8() );
    reader.pos = field( InfoBlockBossRoomId, InfoBlockDiffBossRoomId ); w.U8( reader.U8() );

    w.U8( (uint8_t) (level < 9 ? 3 : 7) );  // song

    if ( quest == 0 )
    {
        reader.pos = OWInfoBlock + blockOffset + InfoBlockLevelNumber;
        w.U8( reader.U8() );
    }
    else
        w.U8( (uint8_t) level );

    w.U8( (uint8_t) effectiveLevel );

    reader.pos = field( InfoBlockDrawnMapOffset, InfoBlockDiffDrawnMapOffset ); w.U8( reader.U8() );

    reader.pos = field( InfoBlockCellarRoomIdArray, InfoBlockDiffCellarRoomIdArray );
    std::vector<uint8_t> cellarRoomIds = reader.Bytes( InfoBlockCellarRoomIdCount );
    if ( quest == 0 && level == 3 )
        cellarRoomIds[0] = 0x0F;
    w.Bytes( cellarRoomIds );

    reader.pos = field( InfoBlockShortcutPosArray, InfoBlockDiffShortcutPosArray );
    w.Bytes( reader.Bytes( InfoBlockShortcutPosCount ) );

    reader.pos = field( InfoBlockDrawnMap, InfoBlockDiffDrawnMap );
    w.Bytes( reader.Bytes( 16 ) );
    w.Pad();

    WriteTrailingPalettes( reader, w, blockOffset );
    files[filename] = w.buf;

    int levelGroup = 0;
    if ( level >= 7 )
        levelGroup++;
    if ( quest == 1 )
        levelGroup += 2;

    char roomAttrFilename[32];
    snprintf( roomAttrFilename, sizeof( roomAttrFilename ), "underworldRoomAttr%d.dat", levelGroup );

    // TODO (original): Some of these are for the OW, until we extract the
    // matching UW parts.
    LevelDirectory dir;
    dir.names[0] = filename;
    dir.names[1] = "underworldRoomCols.dat";
    dir.names[2] = "underworldCols.tab";
    dir.names[3] = "underworldTileAttrs.dat";
    dir.names[4] = "underworldTiles.png";
    dir.names[5] = "playerItem.png";
    dir.names[6] = "playerItemsSheet.tab";
    dir.names[7] = "uwNpcs.png";
    dir.names[8] = "uwNpcsSheet.tab";
    dir.names[9] = bossImageFilenames[effectiveLevel];
    dir.names[10] = bossSheetFilenames[effectiveLevel];
    dir.names[11] = roomAttrFilename;
    dir.names[12] = "overworldInfoEx.tab";
    dir.names[13] = "objLists.tab";
    dir.names[14] = "overworldRoomSparseAttr.tab";
    dir.names[15] = "underworldWalls.png";
    dir.names[16] = "underworldDoors.png";
    WriteLevelDir( files, quest, level, dir );
}

// ---------------------------------------------------------------------------
// overworldInfoEx.tab

void ReadTranslateOWPondColors( Reader& reader, Writer& w )
{
    reader.pos = 0x1FEE8 + 16;
    std::vector<uint8_t> colorIndexes = reader.Bytes( 12 );
    w.I32( (int32_t) colorIndexes.size() );
    w.Bytes( colorIndexes );
}

void ReadTranslateSpawnSpots( Reader& reader, Writer& w )
{
    reader.pos = 0x1464E + 16;
    std::vector<uint8_t> spots = reader.Bytes( 9 * 4 );
    w.I32( (int32_t) spots.size() );
    w.Bytes( spots );
}

void ReadTranslateObjAttrs( Reader& reader, Writer& w )
{
    const int ObjAttrs = 0x1FAEF + 0x10;
    uint16_t finalAttrs[128] = {};

    reader.pos = ObjAttrs;
    std::vector<uint8_t> origAttrs = reader.Bytes( 128 );

    for ( int i = 0; i < 128; i++ )
        finalAttrs[i] |= origAttrs[i];

    for ( int i = 0; i < kObjAttrsByHandCount; i++ )
        finalAttrs[i] |= (uint16_t) (kObjAttrsByHand[i] << 8);

    // Extract the item drop classes.
    for ( int i = 1; i < 128; i++ )
        finalAttrs[i] |= (4 << 9);

    const int addrs[4] = { 0x1301B + 0x10, 0x13022 + 0x10, 0x13028 + 0x10, 0x13031 + 0x10 };
    const int lengths[4] = { 7, 6, 9, 9 };

    for ( int i = 0; i < 4; i++ )
    {
        reader.pos = addrs[i];
        std::vector<uint8_t> types = reader.Bytes( lengths[i] );
        for ( size_t j = 0; j < types.size(); j++ )
        {
            int type = types[j] & 0x7F;
            finalAttrs[type] = (uint16_t) (finalAttrs[type] & ~(7 << 9));
            finalAttrs[type] |= (uint16_t) (i << 9);
        }
    }

    for ( int i = 0; i < 128; i++ )
        w.U16( finalAttrs[i] );
}

void ReadTranslateCavePalettes( Reader& reader, Writer& w )
{
    reader.pos = 0x1A260 + 16;
    std::vector<uint8_t> colorIndexes = reader.Bytes( 8 );
    w.I32( 2 );
    w.I32( 2 );
    WritePalettes( w, colorIndexes, colorIndexes.size() );
}

void ReadTranslateCaves( Reader& reader, Writer& w )
{
    reader.pos = 0x6E6F + 16;  std::vector<uint8_t> types = reader.Bytes( 20 );
    reader.pos = 0x45A2 + 16;  std::vector<uint8_t> stringIds = reader.Bytes( 20 );
    reader.pos = 0x18600 + 16; std::vector<uint8_t> items = reader.Bytes( 20 * 3 );
    reader.pos = 0x1863C + 16; std::vector<uint8_t> prices = reader.Bytes( 20 * 3 );

    w.I32( 20 );
    for ( int i = 0; i < 20; i++ )
    {
        uint8_t orig = stringIds[i];
        uint8_t stringAttr = (uint8_t) ((orig & 0xC0) | ((orig & 0x3F) / 2));
        w.U8( types[i] );
        w.U8( stringAttr );
        w.Bytes( &items[i * 3], 3 );
        w.Bytes( &prices[i * 3], 3 );
    }
}

void ReadTranslateLevelPersonStringIds( Reader& reader, Writer& w )
{
    const int addrs[3] = { 0x4A1B + 0x10, 0x4A61 + 0x10, 0x4A80 + 0x10 };
    const int lens[3] = { 8, 8, 4 };
    for ( int k = 0; k < 3; k++ )
    {
        reader.pos = addrs[k];
        std::vector<uint8_t> ids = reader.Bytes( lens[k] );
        for ( size_t i = 0; i < ids.size(); i++ )
            w.U8( (uint8_t) (ids[i] / 2) );
    }
    for ( int i = 0; i < 4; i++ )
        w.U8( 0 );
}

void ReadTranslateHitPoints( Reader& reader, Writer& w )
{
    reader.pos = 0x1FB4E + 0x10;
    w.Bytes( reader.Bytes( 128 ) );
}

void ReadTranslatePlayerDamage( Reader& reader, Writer& w )
{
    reader.pos = 0x72BA + 0x10;
    w.Bytes( reader.Bytes( 128 ) );
}

void ExtractOverworldInfoEx( Reader& reader, FileMap& files )
{
    const int Alignment = 4;
    const int DataLines = 8;

    typedef void (*Converter)( Reader&, Writer& );
    const Converter converters[DataLines] =
    {
        ReadTranslateOWPondColors,
        ReadTranslateSpawnSpots,
        ReadTranslateObjAttrs,
        ReadTranslateCavePalettes,
        ReadTranslateCaves,
        ReadTranslateLevelPersonStringIds,
        ReadTranslateHitPoints,
        ReadTranslatePlayerDamage,
    };

    Writer w;
    uint16_t ptrs[DataLines];
    const int bufBase = (1 + DataLines) * 2;

    w.Seek( bufBase );
    for ( int i = 0; i < DataLines; i++ )
    {
        ptrs[i] = (uint16_t) w.Align( Alignment );
        converters[i]( reader, w );
    }

    // Pad now, because after this we'll seek to the beginning of the file.
    w.Pad();

    w.Seek( 0 );
    w.U16( DataLines );
    for ( int i = 0; i < DataLines; i++ )
        w.U16( (uint16_t) (ptrs[i] - bufBase) );

    files["overworldInfoEx.tab"] = w.buf;
}

void ExtractObjLists( Reader& reader, FileMap& files )
{
    const int ObjListDir = 0x1473F + 16;
    const int ObjLists = 0x14676 + 16;

    reader.pos = ObjLists;
    std::vector<uint8_t> lists = reader.Bytes( ObjListDir - ObjLists );

    reader.pos = ObjListDir;
    uint16_t listPtrs[30];
    for ( int i = 0; i < 30; i++ )
        listPtrs[i] = reader.U16();

    Writer w;
    w.U16( 30 );
    for ( int i = 0; i < 30; i++ )
        w.U16( (uint16_t) (listPtrs[i] - listPtrs[0]) );
    w.Bytes( lists );
    w.Pad();
    files["objLists.tab"] = w.buf;
}

// ---------------------------------------------------------------------------
// Sprites

void ExtractPlayerItemSprites( Reader& reader, Bitmap& bmp )
{
    int y = 0;
    for ( int r = 0; r < 8; r++ )
    {
        int x = 0;
        for ( int c = 0; c < 16; c++ )
        {
            int code = kPlayerItemsImage[r][c];
            int s = code & 0xFF;
            bool flipX = (code & 0x100) != 0;
            bool flipY = (code & 0x200) != 0;
            bool skip = (code & 0x8000) != 0;
            bool narrow = ((code & 0xFF00) == 0x100) && (s >= 0x62) && (s < 0x6C);

            if ( narrow )
                x--;
            if ( !skip )
                DrawHalfSprite( reader, bmp, x, y, OWMonsterCHR, s, flipX, flipY );
            if ( narrow )
                x++;
            x += 8;
        }
        y += 16;
    }

    // Draw the heart from the background tiles.
    DrawHalfSprite( reader, bmp, 11 * 8, 4 * 16, Misc2CHR, 0x8E );

    // Draw the full triforce.
    DrawUWBossHalfSprite( reader, bmp, 0x70, 0x50, UW9BossCHR, 0xF2 );
    DrawUWBossHalfSprite( reader, bmp, 0x78, 0x50, UW9BossCHR, 0xF4 );
}

void ExtractOverworldNpcSprites( Reader& reader, Bitmap& bmp )
{
    int y = 0;
    for ( int r = 0; r < 8; r++ )
    {
        int x = 0;
        for ( int c = 0; c < 16; c++ )
        {
            int code = kOWNpcsImage[r][c];
            if ( !(code & 0x8000) )
                DrawHalfSprite( reader, bmp, x, y, OWMonsterCHR, code & 0xFF,
                    (code & 0x100) != 0, (code & 0x200) != 0 );
            x += 8;
        }
        y += 16;
    }
}

void ExtractUnderworldNpcSpriteGroup( Reader& reader, Bitmap& bmp, const uint16_t map[8][16],
    int chrBase, int baseY )
{
    int y = baseY;
    for ( int r = 0; r < 8; r++ )
    {
        int x = 0;
        for ( int c = 0; c < 16; c++ )
        {
            int code = map[r][c];
            if ( !(code & 0x8000) )
                DrawUWHalfSprite( reader, bmp, x, y, chrBase, code & 0xFF,
                    (code & 0x100) != 0, (code & 0x200) != 0 );
            x += 8;
        }
        y += 16;
    }
}

void ExtractUnderworldBossSpriteGroup( Reader& reader, Bitmap& bmp, const uint16_t map[8][16],
    int chrBase, int baseY )
{
    int y = baseY;
    for ( int r = 0; r < 8; r++ )
    {
        int x = 0;
        for ( int c = 0; c < 16; c++ )
        {
            int code = map[r][c];
            if ( !(code & 0x8000) )
                DrawUWBossHalfSprite( reader, bmp, x, y, chrBase, code & 0xFF,
                    (code & 0x100) != 0, (code & 0x200) != 0 );
            x += 8;
        }
        y += 16;
    }
}

void ExtractSprites( Reader& reader, FileMap& files )
{
    {
        Bitmap bmp( 128, 128 );
        ExtractPlayerItemSprites( reader, bmp );
        files["playerItem.bin"] = bmp.ToBin();
    }
    {
        Bitmap bmp( 128, 128 );
        ExtractOverworldNpcSprites( reader, bmp );
        files["owNpcs.bin"] = bmp.ToBin();
    }
    {
        Bitmap bmp( 128, 128 );
        ExtractUnderworldNpcSpriteGroup( reader, bmp, kUWNpcsImageCommon, CommonUWSprites, 0 );
        ExtractUnderworldNpcSpriteGroup( reader, bmp, kUWNpcsImage127, UW127SpriteCHR, 0x10 );
        ExtractUnderworldNpcSpriteGroup( reader, bmp, kUWNpcsImage358, UW358SpriteCHR, 0x30 );
        ExtractUnderworldNpcSpriteGroup( reader, bmp, kUWNpcsImage469, UW469SpriteCHR, 0x50 );
        // Draw the moldorm ball from the player tiles.
        DrawHalfSprite( reader, bmp, 12 * 8, 0, 0, 0x44 );
        files["uwNpcs.bin"] = bmp.ToBin();
    }
    {
        Bitmap bmp( 128, 128 );
        ExtractUnderworldBossSpriteGroup( reader, bmp, kUWBossImage1257, UW1257BossCHR, 0 );
        files["uwBoss1257.bin"] = bmp.ToBin();
    }
    {
        Bitmap bmp( 128, 128 );
        ExtractUnderworldBossSpriteGroup( reader, bmp, kUWBossImage3468, UW3468BossCHR, 0 );
        files["uwBoss3468.bin"] = bmp.ToBin();
    }
    {
        Bitmap bmp( 128, 128 );
        ExtractUnderworldBossSpriteGroup( reader, bmp, kUWBossImage9, UW9BossCHR, 0 );
        files["uwBoss9.bin"] = bmp.ToBin();
    }
}

// ---------------------------------------------------------------------------
// Sound: the game's NSF, rebuilt exactly like ExtractLoz.BuildMemoryNsf. The
// 3DS synthesizes all music and effects from it live (Sound_3ds.cpp).

void BuildNsf( const std::vector<uint8_t>& rom, FileMap& files )
{
    Writer w;
    w.Bytes( kNsfHeader, sizeof( kNsfHeader ) );
    for ( const NsfChunk& chunk : kNsfChunks )
    {
        Reader r( rom );
        r.pos = (size_t) chunk.offset;
        w.Bytes( r.Bytes( (size_t) chunk.length ) );
    }
    w.Bytes( kNsfFooter, sizeof( kNsfFooter ) );
    files["zelda.nsf"] = w.buf;
}

// ---------------------------------------------------------------------------
// terrain.bin: one terrain class per tile for each of the 128 overworld
// screens, for the bottom-screen live map's per-room thumbnails. Replaces the
// romfs/terrain.t3x atlas that a build-time script used to bake from
// the same data; the colours are applied on the console (Graphics_3ds.cpp).
//
// Layout: 128 rooms x 11 rows x 16 columns, one byte each:
//   0 water, 1 forest, 2 mountain, 3 grave, 4 sand, 5 ground.
// The classification comes from TerrainClasses.gen.h, generated by
// tools/gen-terrain-tables.py (which also checks it against the room data).

uint8_t TerrainOf( int tileRef )
{
    return kTerrainClassOfTile[tileRef & 0x3F];
}

void BuildTerrain( const FileMap& files, FileMap& out )
{
    const int Rows = 11, Cols = 16, Rooms = 128;
    const int MaxColStartOffset = (Cols - 1) * Rows;

    const std::vector<uint8_t>& attrs = files.at( "overworldRoomAttr.dat" );
    const std::vector<uint8_t>& roomCols = files.at( "overworldRoomCols.dat" );
    const std::vector<uint8_t>& tab = files.at( "overworldCols.tab" );

    int tabLength = tab[0] | (tab[1] << 8);
    const uint8_t* tabBuf = &tab[2];
    size_t tabBufSize = tab.size() - 2;
    int heapBase = tabLength * 2;

    std::vector<uint8_t> terrain( (size_t) Rooms * Rows * Cols, kTerrainClassOfTile[0] );

    for ( int room = 0; room < Rooms; room++ )
    {
        int uniqueId = attrs[room * 7] & 0x7F;
        uint8_t* grid = &terrain[(size_t) room * Rows * Cols];

        for ( int i = 0; i < Cols; i++ )
        {
            size_t descAt = (size_t) uniqueId * 16 + i;
            if ( descAt >= roomCols.size() )
                break;
            int desc = roomCols[descAt];
            int tableIndex = (desc & 0xF0) >> 4;
            int columnIndex = desc & 0x0F;
            if ( tableIndex >= tabLength )
                continue;

            size_t start = heapBase + (tabBuf[tableIndex * 2] | (tabBuf[tableIndex * 2 + 1] << 8));
            auto at = [&]( int j ) -> uint8_t
            {
                size_t p = start + j;
                return p < tabBufSize ? tabBuf[p] : 0;
            };

            // Same search as WorldImpl::LoadLayout(): find the start of the
            // columnIndex-th column (each starts with bit 7 set).
            int j = 0, k = 0;
            for ( ; j <= MaxColStartOffset; j++ )
            {
                if ( at( j ) & 0x80 )
                {
                    if ( k == columnIndex )
                        break;
                    k++;
                }
            }
            if ( j > MaxColStartOffset )
                continue;

            for ( int r = 0; r < Rows; j++ )
            {
                uint8_t t = at( j );
                uint8_t cls = TerrainOf( t & 0x3F );
                grid[r * Cols + i] = cls;
                r++;
                if ( (t & 0x40) && r < Rows )
                {
                    grid[r * Cols + i] = cls;
                    r++;
                }
            }
        }
    }

    out["terrain.bin"] = terrain;
}

} // namespace

// ---------------------------------------------------------------------------

uint32_t Crc32( const uint8_t* data, size_t size )
{
    // Table-driven: the bitwise version cost ~8 shifts per byte, which adds
    // up on a 268 MHz ARM11 for the 128 KiB check at every startup.
    static uint32_t table[256];
    static bool ready = false;
    if ( !ready )
    {
        for ( uint32_t n = 0; n < 256; n++ )
        {
            uint32_t c = n;
            for ( int k = 0; k < 8; k++ )
                c = (c & 1) ? 0xEDB88320 ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFF;
    for ( size_t i = 0; i < size; i++ )
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

Result NormalizeRom( const uint8_t* data, size_t size, std::vector<uint8_t>& romOut )
{
    const uint8_t* prg = nullptr;

    if ( size >= HeaderSize + PrgSize
        && data[0] == 'N' && data[1] == 'E' && data[2] == 'S' && data[3] == 0x1A )
        prg = data + HeaderSize;
    else if ( size == PrgSize )
        prg = data;                         // headerless dump
    else if ( size < PrgSize )
        return Result_TooSmall;
    else
        return Result_NotNesRom;

    // The original tool hashes everything after the header; the real ROM is
    // exactly 128 KiB of PRG (no CHR), so this is the same check.
    size_t prgLen = (prg == data) ? size : size - HeaderSize;
    if ( prgLen != PrgSize )
        return Result_WrongRom;
    uint32_t crc = Crc32( prg, PrgSize );
    if ( crc == Rev1PrgCrc32 )
        return Result_Rev1;
    if ( crc != RequiredPrgCrc32 )
        return Result_WrongRom;

    static const uint8_t header[HeaderSize] =
        { 'N', 'E', 'S', 0x1A, 0x08, 0x00, 0x12, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 };
    romOut.assign( header, header + HeaderSize );
    romOut.insert( romOut.end(), prg, prg + PrgSize );
    return Result_Ok;
}

void ExtractAll( const std::vector<uint8_t>& rom, FileMap& files )
{
    Reader reader( rom );

    // Overworld bundle
    ExtractOverworldTiles( reader, files );
    ExtractOverworldTileAttrs( reader, files );
    ExtractOverworldMap( reader, files );
    ExtractOverworldMapAttrs( reader, files );
    ExtractOverworldMapSparseAttrs( reader, files );
    ExtractOverworldInfo( reader, files );
    ExtractOverworldInfoEx( reader, files );
    ExtractObjLists( reader, files );

    // Underworld bundle
    ExtractUnderworldTiles( reader, files );
    ExtractUnderworldTileAttrs( reader, files );
    ExtractUnderworldMap( reader, files );
    for ( int group = 0; group < 4; group++ )
        ExtractUnderworldMapAttrs( reader, files, group );
    for ( int quest = 0; quest < 2; quest++ )
        for ( int level = 1; level < 10; level++ )
            ExtractUnderworldInfo( reader, files, quest, level );
    ExtractUnderworldCellarMap( reader, files );
    ExtractUnderworldCellarTiles( reader, files );
    ExtractUnderworldCellarTileAttrs( reader, files );

    // Sprites (the palette and sheet tables don't come from the ROM and ship
    // in romfs as-is)
    ExtractSprites( reader, files );

    // Text
    ExtractFont( reader, files );
    ExtractText( reader, files );
    ExtractCredits( reader, files );

    BuildNsf( rom, files );
    BuildTerrain( files, files );
}

const char* Describe( Result result )
{
    switch ( result )
    {
    case Result_Ok:        return "OK";
    case Result_TooSmall:  return "The file is too small to be the Legend of Zelda NES ROM.";
    case Result_NotNesRom: return "The file is not an NES ROM.";
    case Result_WrongRom:  return "This is not the supported ROM. You need \"Legend of Zelda, The (USA)\", "
                                  "the first release (PRG0). Other regions and versions do not work.";
    case Result_Rev1:      return "This is the later Rev 1 (PRG1) release, which is not supported. You need "
                                  "\"Legend of Zelda, The (USA)\", the first release (PRG0).";
    }
    return "Unknown error.";
}

}
