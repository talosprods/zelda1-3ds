#include "AssetStore.h"
#include "RomExtract.h"

#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <3ds.h>     // osGetTime

static RomExtract::FileMap s_files;

// "Baked" build: the PC patcher (patcher/) already put the
// extracted assets into this build's romfs, so there's no ROM to look for.
// Detected by one file only the extractor produces.
static bool s_baked = false;

// Startup cost of the ROM-on-SD route, for the opt-in frame trace.
static char s_timing[192] = "AssetStore: assets built into romfs, no ROM extraction";
static const char* const BakedMarker = "romfs:/overworldTiles.bin";

static const char* BaseName( const char* path )
{
    const char* p = strrchr( path, '/' );
    if ( p ) return p + 1;
    p = strchr( path, ':' );
    return p ? p + 1 : path;
}

static bool HasNesExtension( const char* name )
{
    size_t n = strlen( name );
    return n > 4 && strcasecmp( name + n - 4, ".nes" ) == 0;
}

static bool ReadWholeFile( const char* path, std::vector<uint8_t>& out )
{
    FILE* f = fopen( path, "rb" );
    if ( !f ) return false;
    fseek( f, 0, SEEK_END );
    long size = ftell( f );
    fseek( f, 0, SEEK_SET );
    // The real ROM is 128 KiB (+16 header); refuse anything absurd rather
    // than allocating it.
    if ( size <= 0 || size > 4 * 1024 * 1024 ) { fclose( f ); return false; }
    out.resize( (size_t) size );
    bool ok = fread( out.data(), 1, out.size(), f ) == out.size();
    fclose( f );
    return ok;
}

bool AssetStore_Init( char* message, size_t messageSize )
{
    s_files.clear();

    if ( FILE* marker = fopen( BakedMarker, "rb" ) )
    {
        fclose( marker );
        s_baked = true;
        return true;
    }

    u64 T0 = osGetTime(), Tread = 0, Tcrc = 0;
    DIR* dir = opendir( ASSETSTORE_ROM_DIR );
    if ( !dir )
    {
        snprintf( message, messageSize, "The folder %s does not exist yet.", ASSETSTORE_ROM_DIR );
        return false;
    }

    std::string rejected;
    RomExtract::Result rejectReason = RomExtract::Result_Ok;
    bool found = false;
    std::vector<uint8_t> rom;

    while ( struct dirent* ent = readdir( dir ) )
    {
        if ( !HasNesExtension( ent->d_name ) )
            continue;

        std::string path = std::string( ASSETSTORE_ROM_DIR ) + ent->d_name;
        std::vector<uint8_t> file;
        u64 ta = osGetTime();
        if ( !ReadWholeFile( path.c_str(), file ) )
            continue;
        u64 tb = osGetTime();
        Tread += tb - ta;

        RomExtract::Result r = RomExtract::NormalizeRom( file.data(), file.size(), rom );
        Tcrc += osGetTime() - tb;
        if ( r == RomExtract::Result_Ok )
        {
            found = true;
            break;
        }
        if ( rejected.empty() )
        {
            rejected = ent->d_name;
            rejectReason = r;
        }
    }
    closedir( dir );

    if ( !found )
    {
        if ( rejected.empty() )
            snprintf( message, messageSize, "No .nes file found in %s", ASSETSTORE_ROM_DIR );
        else
            snprintf( message, messageSize, "%s: %s", rejected.c_str(), RomExtract::Describe( rejectReason ) );
        return false;
    }

    u64 T1 = osGetTime();
    RomExtract::ExtractAll( rom, s_files );
    u64 T2 = osGetTime();
    snprintf( s_timing, sizeof( s_timing ),
        "AssetStore (ROM on SD): %llu ms total = find+read %llu (of which read %llu, check %llu), extract %llu",
        (unsigned long long) (T2 - T0), (unsigned long long) (T1 - T0), (unsigned long long) Tread,
        (unsigned long long) Tcrc, (unsigned long long) (T2 - T1) );
    return true;
}

const unsigned char* AssetStore_Data( const char* name, size_t* size )
{
    auto it = s_files.find( BaseName( name ) );
    if ( it == s_files.end() && s_baked )
    {
        // Baked build: load from romfs on first use, then keep it.
        std::vector<uint8_t> data;
        if ( !ReadWholeFile( (std::string( "romfs:/" ) + BaseName( name )).c_str(), data ) )
            return nullptr;
        it = s_files.emplace( BaseName( name ), std::move( data ) ).first;
    }
    if ( it == s_files.end() )
        return nullptr;
    if ( size ) *size = it->second.size();
    return it->second.data();
}

FILE* AssetStore_Open( const char* path, const char* mode )
{
    if ( mode && mode[0] == 'r' && strchr( mode, '+' ) == nullptr )
    {
        size_t size = 0;
        const unsigned char* data = AssetStore_Data( path, &size );
        if ( data )
            return fmemopen( (void*) data, size, "rb" );
    }
    return fopen( path, mode );
}

const char* AssetStore_TimingReport()
{
    return s_timing;
}
