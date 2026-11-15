#ifdef __3DS__
/*
   3DS save backend: profiles are stored on the SD card (sdmc:/3ds/zelda3ds/),
   same file layout as the desktop build (raw Profile structs, z1_<slot>.sav).
*/
#include "Common.h"
#include "SaveFolder.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>

static const char* SaveDir = "sdmc:/3ds/zelda3ds";

enum OpenMode
{
    Open_Read,
    Open_Write,
};

static FILE* OpenSaveFile( int slot, OpenMode mode )
{
    // Ensure the directory chain exists (harmless if already there).
    mkdir( "sdmc:/3ds", 0777 );
    mkdir( SaveDir, 0777 );

    char path[64];
    snprintf( path, sizeof path, "%s/z1_%d.sav", SaveDir, slot );

    if ( mode == Open_Write )
    {
        FILE* f = fopen( path, "r+b" );
        if ( f == nullptr )
            f = fopen( path, "wb" );
        return f;
    }
    return fopen( path, "rb" );
}

void SaveFolder::ReadSummaries( ProfileSummarySnapshot& summaries )
{
    for ( int i = 0; i < MaxProfiles; i++ )
    {
        ProfileSummary& summary = summaries.Summaries[i];
        Profile profile;
        size_t bytesRead = 0;

        // Same growth-safety fix as ReadProfile() below (zero + byte-count
        // fread, not "1 whole struct"): this copy was missed when that fix
        // went in, so it silently regressed on every later Profile growth --
        // a real save one byte shorter than the current struct made
        // fread(ptr,sizeof,1,file) return 0 records, and the file-select
        // screen would show that slot as empty/inactive even though
        // ReadProfile() loads the same file fine once picked. Caught while
        // adding phase 13.2's EverHadItem[] field, the next growth after this
        // was fixed -- this is the platform this actually would have bitten
        // (real 3DS saves).
        memset( &profile, 0, sizeof( Profile ) );

        FILE* file = OpenSaveFile( i, Open_Read );
        if ( file != nullptr )
        {
            bytesRead = fread( &profile, 1, sizeof( Profile ), file );
            fclose( file );
        }

        if ( bytesRead == 0 )
        {
            memset( &summary, 0, sizeof(ProfileSummary) );
        }
        else
        {
            summary.Deaths = profile.Deaths;
            summary.HeartContainers = profile.Items[ItemSlot_HeartContainers];
            summary.Quest = profile.Quest;
            summary.NameLength = profile.NameLength;
            memcpy( summary.Name, profile.Name, profile.NameLength );
            summary.PlayTimeSeconds = profile.PlayTimeSeconds;

            uint8_t pieces = profile.Items[ItemSlot_TriforcePieces];
            uint8_t count = 0;
            for ( ; pieces != 0; pieces >>= 1 )
                count += pieces & 1;
            summary.TriforcePieceCount = count;
        }
    }
}

bool SaveFolder::ReadProfile( int index, Profile& profile )
{
    assert( index < MaxProfiles );

    // See SaveFolder.cpp's identical fix (phase 13.x) for the full reasoning:
    // zero first (callers' stack `Profile profile;` has no constructor),
    // then read by byte count so a future-shorter/older save still loads
    // instead of getting wiped -- fixes the root cause phase 8.1 previously
    // just avoided by freezing Profile's size. Existing save files on the
    // 3DS SD are exactly what this protects.
    memset( &profile, 0, sizeof( Profile ) );

    size_t bytesRead = 0;
    FILE* file = OpenSaveFile( index, Open_Read );
    if ( file != nullptr )
    {
        bytesRead = fread( &profile, 1, sizeof( Profile ), file );
        fclose( file );
    }

    if ( bytesRead == 0 )
    {
        memset( &profile, 0, sizeof( Profile ) );
        return false;
    }
    return true;
}

bool SaveFolder::WriteProfile( int index, const Profile& profile )
{
    assert( index < MaxProfiles );

    FILE* file = OpenSaveFile( index, Open_Write );
    if ( file == nullptr )
        return false;

    size_t lenWritten = fwrite( &profile, sizeof( Profile ), 1, file );
    fclose( file );

    return lenWritten == 1;
}

static const char* LastSlotPath = "sdmc:/3ds/zelda3ds/lastslot.dat";

int SaveFolder::ReadLastSlot()
{
    FILE* f = fopen( LastSlotPath, "rb" );
    if ( f == nullptr )
        return -1;

    int slot = -1;
    size_t lenRead = fread( &slot, sizeof( slot ), 1, f );
    fclose( f );

    if ( lenRead < 1 || slot < 0 || slot >= MaxProfiles )
        return -1;
    return slot;
}

void SaveFolder::WriteLastSlot( int index )
{
    mkdir( "sdmc:/3ds", 0777 );
    mkdir( SaveDir, 0777 );

    FILE* f = fopen( LastSlotPath, "wb" );
    if ( f == nullptr )
        return;
    fwrite( &index, sizeof( index ), 1, f );
    fclose( f );
}

static const char* OptionsPath = "sdmc:/3ds/zelda3ds/options.dat";

bool SaveFolder::ReadOptions( GameOptions& options )
{
    FILE* f = fopen( OptionsPath, "rb" );
    if ( f == nullptr )
    {
        options = GameOptions{ 0 };
        return false;
    }

    // Byte count, not "1 whole struct" -- see SaveFolder.cpp's identical
    // fix (phase 10.1) for why: this lets an options.dat written by an older
    // build (shorter GameOptions) still load, leaving new fields at their
    // zero-initialized default instead of wiping every *existing* option
    // back to default the moment the struct grows -- an existing
    // options.dat on the 3DS SD is exactly the file this protects.
    size_t bytesRead = fread( &options, 1, sizeof( options ), f );
    fclose( f );

    if ( bytesRead == 0 )
    {
        options = GameOptions{ 0 };
        return false;
    }
    return true;
}

void SaveFolder::WriteOptions( const GameOptions& options )
{
    mkdir( "sdmc:/3ds", 0777 );
    mkdir( SaveDir, 0777 );

    FILE* f = fopen( OptionsPath, "wb" );
    if ( f == nullptr )
        return;
    fwrite( &options, sizeof( options ), 1, f );
    fclose( f );
}

#endif // __3DS__
