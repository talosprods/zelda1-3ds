// See patch_core.h. Moved out of patcher.cpp unchanged so the PC tool and the
// browser version run exactly the same code.

#include "patch_core.h"
#include "RomExtract.h"
#include "sha256.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Little helpers

static uint32_t Le32( const uint8_t* p ) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
static uint64_t Le64( const uint8_t* p ) { return Le32( p ) | ((uint64_t) Le32( p + 4 ) << 32); }
static uint32_t Be32( const uint8_t* p ) { return ((uint32_t) p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static void PutLe32( uint8_t* p, uint32_t v ) { for ( int i = 0; i < 4; i++ ) p[i] = (uint8_t) (v >> (8 * i)); }
static void PutLe64( uint8_t* p, uint64_t v ) { for ( int i = 0; i < 8; i++ ) p[i] = (uint8_t) (v >> (8 * i)); }
static void PutBe64( uint8_t* p, uint64_t v ) { for ( int i = 0; i < 8; i++ ) p[i] = (uint8_t) (v >> (56 - 8 * i)); }
static uint64_t Align( uint64_t v, uint64_t a ) { return (v + a - 1) / a * a; }

// ---------------------------------------------------------------------------
// RomFS level 3 (the file system itself)

// Reads every file in the root directory of a level-3 RomFS image.
static std::vector<RomFile> ParseLevel3( const uint8_t* l3, size_t size )
{
    if ( size < 0x28 || Le32( l3 ) != 0x28 )
        throw Fail{ "base build has no readable RomFS" };
    uint32_t fileTableOff = Le32( l3 + 0x1C ), fileTableLen = Le32( l3 + 0x20 );
    uint32_t dataOff = Le32( l3 + 0x24 );

    std::vector<RomFile> files;
    uint32_t pos = 0;
    while ( pos + 0x20 <= fileTableLen )
    {
        const uint8_t* e = l3 + fileTableOff + pos;
        if ( Le32( e ) != 0 )
            throw Fail{ "base RomFS has subdirectories; this patcher only handles a flat one" };
        uint64_t off = Le64( e + 8 ), len = Le64( e + 16 );
        uint32_t nameSize = Le32( e + 28 );
        RomFile f;
        for ( uint32_t i = 0; i + 1 < nameSize; i += 2 )
            f.name += (char) e[0x20 + i];
        if ( dataOff + off + len > size )
            throw Fail{ "base RomFS is truncated" };
        f.data.assign( l3 + dataOff + off, l3 + dataOff + off + len );
        files.push_back( std::move( f ) );
        pos += 0x20 + (uint32_t) Align( nameSize, 4 );
    }
    return files;
}

static uint32_t HashTableCount( uint32_t num )      // makerom GetHashTableCount
{
    uint32_t count = num;
    if ( num < 3 )
        count = 3;
    else if ( count < 19 )
        count |= 1;
    else
        while ( count % 2 == 0 || count % 3 == 0 || count % 5 == 0 || count % 7 == 0
             || count % 11 == 0 || count % 13 == 0 || count % 17 == 0 )
            count++;
    return count;
}

static uint32_t PathHash( uint32_t parent, const std::string& name )   // makerom CalcPathHash
{
    uint32_t hash = parent ^ 123456789;
    for ( unsigned char c : name )
    {
        hash = (hash >> 5) | (hash << 27);
        hash ^= c;      // UTF-16 code unit; all names here are ASCII
    }
    return hash;
}

// Builds a flat level-3 RomFS exactly like makerom's romfs_gen.c does for a
// root directory holding `files` in this order.
static Bytes BuildLevel3( const std::vector<RomFile>& files )
{
    const uint32_t Unused = 0xFFFFFFFF;
    const uint32_t dirTableLen = 0x18;                      // root only, no name
    uint32_t fileTableLen = 0;
    uint64_t dataLen = 0;
    for ( const RomFile& f : files )
    {
        fileTableLen += 0x20 + (uint32_t) Align( f.name.size() * 2, 4 );
        if ( !f.data.empty() )
            dataLen = Align( dataLen, 0x10 ) + f.data.size();
    }
    uint32_t dirHash = HashTableCount( 1 );
    uint32_t fileHash = HashTableCount( (uint32_t) files.size() );

    uint32_t pos = 0x28;
    uint32_t dirHashOff = pos;  pos += dirHash * 4;
    uint32_t dirTableOff = pos; pos += dirTableLen;
    uint32_t fileHashOff = pos; pos += fileHash * 4;
    uint32_t fileTableOff = pos; pos += fileTableLen;
    uint32_t dataOff = (uint32_t) Align( pos, 0x10 );

    Bytes l3( dataOff + dataLen, 0 );
    uint8_t* p = l3.data();
    PutLe32( p, 0x28 );
    PutLe32( p + 0x04, dirHashOff );   PutLe32( p + 0x08, dirHash * 4 );
    PutLe32( p + 0x0C, dirTableOff );  PutLe32( p + 0x10, dirTableLen );
    PutLe32( p + 0x14, fileHashOff );  PutLe32( p + 0x18, fileHash * 4 );
    PutLe32( p + 0x1C, fileTableOff ); PutLe32( p + 0x20, fileTableLen );
    PutLe32( p + 0x24, dataOff );
    for ( uint32_t i = 0; i < dirHash; i++ )  PutLe32( p + dirHashOff + i * 4, Unused );
    for ( uint32_t i = 0; i < fileHash; i++ ) PutLe32( p + fileHashOff + i * 4, Unused );

    // Root directory entry
    uint8_t* root = p + dirTableOff;
    PutLe32( root + 0x00, 0 );
    PutLe32( root + 0x04, Unused );
    PutLe32( root + 0x08, Unused );
    PutLe32( root + 0x0C, files.empty() ? Unused : 0 );
    uint32_t di = PathHash( 0, "" ) % dirHash;
    PutLe32( root + 0x10, Le32( p + dirHashOff + di * 4 ) );
    PutLe32( p + dirHashOff + di * 4, 0 );
    PutLe32( root + 0x14, 0 );

    uint32_t tablePos = 0;
    uint64_t dataPos = 0;
    for ( size_t i = 0; i < files.size(); i++ )
    {
        const RomFile& f = files[i];
        uint32_t entrySize = 0x20 + (uint32_t) Align( f.name.size() * 2, 4 );
        uint8_t* e = p + fileTableOff + tablePos;
        PutLe32( e + 0x00, 0 );
        PutLe32( e + 0x04, i + 1 < files.size() ? tablePos + entrySize : Unused );
        uint32_t hi = PathHash( 0, f.name ) % fileHash;
        PutLe32( e + 0x18, Le32( p + fileHashOff + hi * 4 ) );
        PutLe32( p + fileHashOff + hi * 4, tablePos );
        PutLe32( e + 0x1C, (uint32_t) f.name.size() * 2 );
        for ( size_t c = 0; c < f.name.size(); c++ )
            e[0x20 + c * 2] = (uint8_t) f.name[c];
        if ( !f.data.empty() )
        {
            dataPos = Align( dataPos, 0x10 );
            PutLe64( e + 0x08, dataPos );
            PutLe64( e + 0x10, f.data.size() );
            memcpy( p + dataOff + dataPos, f.data.data(), f.data.size() );
            dataPos += f.data.size();
        }
        tablePos += entrySize;
    }
    return l3;
}

// ---------------------------------------------------------------------------
// IVFC wrapper (the NCCH RomFS section), as makerom's BuildRomFsBinary.

struct NcchRomfs
{
    Bytes data;              // the whole section, block aligned
    uint64_t headerSize;     // makerom romfsHeaderSize (level 0 size)
};

static NcchRomfs BuildIvfc( const Bytes& l3 )
{
    const uint64_t Block = 0x1000, HashLen = 32, IvfcHdr = 0x5C, IvfcHdrAligned = 0x60;
    uint64_t size[4];
    size[3] = l3.size();
    size[2] = Align( size[3], Block ) / Block * HashLen;
    size[1] = Align( size[2], Block ) / Block * HashLen;
    size[0] = Align( size[1], Block ) / Block * HashLen + IvfcHdrAligned;

    uint64_t off[4];
    off[0] = 0;
    off[3] = off[0] + Align( size[0], Block );
    off[1] = off[3] + Align( size[3], Block );
    off[2] = off[1] + Align( size[1], Block );

    uint64_t logical[4] = { 0, 0, 0, 0 };
    for ( int i = 2; i < 4; i++ )
        logical[i] = Align( logical[i - 1] + size[i - 1], Block );

    uint64_t total = 0;
    for ( int i = 0; i < 4; i++ )
        total += Align( size[i], Block );

    NcchRomfs out;
    out.headerSize = size[0];
    out.data.assign( total, 0 );
    uint8_t* d = out.data.data();
    memcpy( d + off[3], l3.data(), l3.size() );

    memcpy( d, "IVFC", 4 );
    PutLe32( d + 4, 0x10000 );
    PutLe32( d + 8, (uint32_t) (Align( size[1], Block ) / Block * HashLen) );
    for ( int i = 1; i < 4; i++ )
    {
        uint8_t* lv = d + 0x0C + (i - 1) * 0x18;
        PutLe64( lv + 0, logical[i] );
        PutLe64( lv + 8, size[i] );
        PutLe32( lv + 16, 12 );
    }
    PutLe32( d + 0x54, (uint32_t) IvfcHdr );

    uint64_t pos[4] = { off[0] + IvfcHdrAligned, off[1], off[2], off[3] };
    for ( int i = 2; i >= 0; i-- )
    {
        uint64_t n = Align( size[i + 1], Block ) / Block;
        for ( uint64_t j = 0; j < n; j++ )
            Sha256::Hash( d + pos[i + 1] + Block * j, Block, d + pos[i] + HashLen * j );
    }
    return out;
}

// ---------------------------------------------------------------------------
// CIA

struct CiaLayout
{
    uint64_t certOff, tikOff, tmdOff, contentOff, metaOff;
    uint32_t certSize, tikSize, tmdSize, metaSize;
    uint64_t contentSize;
};

static CiaLayout ReadCiaLayout( const Bytes& cia )
{
    if ( cia.size() < 0x2020 || Le32( cia.data() ) != 0x2020 )
        throw Fail{ "base CIA has an unexpected header" };
    CiaLayout l;
    l.certSize = Le32( &cia[0x08] );
    l.tikSize = Le32( &cia[0x0C] );
    l.tmdSize = Le32( &cia[0x10] );
    l.metaSize = Le32( &cia[0x14] );
    l.contentSize = Le64( &cia[0x18] );
    l.certOff = Align( 0x2020, 64 );
    l.tikOff = l.certOff + Align( l.certSize, 64 );
    l.tmdOff = l.tikOff + Align( l.tikSize, 64 );
    l.contentOff = l.tmdOff + Align( l.tmdSize, 64 );
    l.metaOff = l.contentOff + Align( l.contentSize, 64 );
    if ( l.metaOff + l.metaSize > cia.size() )
        throw Fail{ "base CIA is truncated" };
    return l;
}

static uint64_t TmdHeaderOffset( const Bytes& cia, uint64_t tmdOff )
{
    uint32_t sigType = Be32( &cia[tmdOff] );
    uint64_t sigLen, pad;
    switch ( sigType )
    {
    case 0x10003: sigLen = 0x200; pad = 0x3C; break;
    case 0x10004: sigLen = 0x100; pad = 0x3C; break;
    case 0x10005: sigLen = 0x3C;  pad = 0x40; break;
    default: throw Fail{ "base CIA has an unknown TMD signature type" };
    }
    return tmdOff + 4 + sigLen + pad;
}

Bytes PatchCia( const Bytes& base, const std::vector<RomFile>& extraFiles,
    const std::vector<std::string>* order )
{
    CiaLayout l = ReadCiaLayout( base );
    uint64_t tmdHdr = TmdHeaderOffset( base, l.tmdOff );
    if ( ((base[tmdHdr + 0x9E] << 8) | base[tmdHdr + 0x9F]) != 1 )
        throw Fail{ "base CIA must have exactly one content" };
    uint64_t chunk = tmdHdr + 0xC4 + 64 * 0x24;
    if ( base[chunk + 7] & 1 )
        throw Fail{ "base CIA content is encrypted" };

    const uint8_t* ncch = &base[l.contentOff];
    if ( memcmp( ncch + 0x100, "NCCH", 4 ) != 0 )
        throw Fail{ "base CIA content is not an NCCH" };
    if ( !(ncch[0x18F] & 0x04) )
        throw Fail{ "base CIA NCCH is encrypted (needs NoCrypto)" };
    uint32_t romfsOffMu = Le32( ncch + 0x1B0 ), romfsSizeMu = Le32( ncch + 0x1B4 );
    if ( romfsSizeMu == 0 || (uint64_t) (romfsOffMu + romfsSizeMu) * 0x200 != l.contentSize )
        throw Fail{ "base CIA RomFS is not the last part of the NCCH" };

    // Existing RomFS: find level 3 from the IVFC header.
    const uint8_t* ivfc = ncch + (uint64_t) romfsOffMu * 0x200;
    if ( memcmp( ivfc, "IVFC", 4 ) != 0 )
        throw Fail{ "base CIA RomFS has no IVFC header" };
    uint64_t l3Off = Align( 0x60 + Le32( ivfc + 8 ), 0x1000 );
    uint64_t l3Size = Le64( ivfc + 0x3C + 8 );
    std::vector<RomFile> files = ParseLevel3( ivfc + l3Off, (size_t) l3Size );
    files.insert( files.end(), extraFiles.begin(), extraFiles.end() );

    if ( order )
    {
        // Testing aid: reproduce a reference build's file order.
        std::vector<RomFile> sorted;
        for ( const std::string& name : *order )
            for ( RomFile& f : files )
                if ( f.name == name ) { sorted.push_back( f ); break; }
        if ( sorted.size() != files.size() )
            throw Fail{ "--order-from does not list the same files" };
        files.swap( sorted );
    }

    NcchRomfs romfs = BuildIvfc( BuildLevel3( files ) );

    // New NCCH: everything before the RomFS, then the new RomFS.
    Bytes newNcch( ncch, ncch + (uint64_t) romfsOffMu * 0x200 );
    newNcch.insert( newNcch.end(), romfs.data.begin(), romfs.data.end() );
    uint8_t* h = newNcch.data();
    uint32_t newRomfsMu = (uint32_t) (romfs.data.size() / 0x200);
    PutLe32( h + 0x104, romfsOffMu + newRomfsMu );
    PutLe32( h + 0x1B4, newRomfsMu );
    uint32_t hashRegionMu = (uint32_t) (Align( romfs.headerSize, 0x200 ) / 0x200);
    PutLe32( h + 0x1B8, hashRegionMu );
    Sha256::Hash( romfs.data.data(), (size_t) hashRegionMu * 0x200, h + 0x1E0 );

    // New CIA: header..TMD as before (with fixed sizes/hashes), content, meta.
    Bytes out( base.begin(), base.begin() + l.contentOff );
    PutLe64( &out[0x18], newNcch.size() );
    PutBe64( &out[chunk + 8], newNcch.size() );
    Sha256::Hash( newNcch.data(), newNcch.size(), &out[chunk + 16] );
    Sha256::Hash( &out[chunk], 0x30, &out[tmdHdr + 0xC4 + 4] );          // content info record 0
    Sha256::Hash( &out[tmdHdr + 0xC4], 64 * 0x24, &out[tmdHdr + 0xA4] ); // TMD header

    out.insert( out.end(), newNcch.begin(), newNcch.end() );
    out.resize( Align( out.size(), 64 ), 0 );
    out.insert( out.end(), base.begin() + l.metaOff, base.begin() + l.metaOff + l.metaSize );
    out.resize( Align( out.size(), 64 ), 0 );
    return out;
}

// ---------------------------------------------------------------------------
// 3DSX: the RomFS (level 3 only) is everything from the extended header's
// fsOffset to the end of the file.

Bytes Patch3dsx( const Bytes& base, const std::vector<RomFile>& extraFiles )
{
    if ( base.size() < 0x2C || memcmp( base.data(), "3DSX", 4 ) != 0 )
        throw Fail{ "base 3DSX has an unexpected header" };
    uint16_t headerSize = base[4] | (base[5] << 8);
    uint32_t fsOff = headerSize >= 0x2C ? Le32( &base[0x28] ) : 0;
    if ( fsOff == 0 || fsOff >= base.size() )
        throw Fail{ "base 3DSX has no RomFS" };

    std::vector<RomFile> files = ParseLevel3( &base[fsOff], base.size() - fsOff );
    files.insert( files.end(), extraFiles.begin(), extraFiles.end() );

    Bytes out( base.begin(), base.begin() + fsOff );
    Bytes l3 = BuildLevel3( files );
    out.insert( out.end(), l3.begin(), l3.end() );
    return out;
}


std::vector<std::string> FileOrderOf( const Bytes& cia )
{
    CiaLayout l = ReadCiaLayout( cia );
    const uint8_t* ncch = &cia[l.contentOff];
    const uint8_t* ivfc = ncch + (uint64_t) Le32( ncch + 0x1B0 ) * 0x200;
    uint64_t l3Off = Align( 0x60 + Le32( ivfc + 8 ), 0x1000 );
    std::vector<std::string> names;
    for ( const RomFile& f : ParseLevel3( ivfc + l3Off, (size_t) Le64( ivfc + 0x3C + 8 ) ) )
        names.push_back( f.name );
    return names;
}


std::vector<RomFile> ExtractRomFiles( const Bytes& romFile )
{
    Bytes rom;
    RomExtract::Result r = RomExtract::NormalizeRom( romFile.data(), romFile.size(), rom );
    if ( r != RomExtract::Result_Ok )
        throw Fail{ RomExtract::Describe( r ) };

    RomExtract::FileMap extracted;
    RomExtract::ExtractAll( rom, extracted );
    std::vector<RomFile> files;
    for ( auto& kv : extracted )
        files.push_back( RomFile{ kv.first, kv.second } );
    return files;
}
