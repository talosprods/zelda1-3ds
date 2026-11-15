// romextract -- run the ROM extractor (src/extract/RomExtract.cpp) on a PC.
//
//   romextract <rom.nes> <outdir>          write every ROM-derived asset
//   romextract <rom.nes> --compare <dir>   compare against existing files
//
// Build: patcher/build-romextract.sh (plain c++, no dependencies).
#include "RomExtract.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

static bool ReadFile( const std::string& path, std::vector<uint8_t>& out )
{
    FILE* f = fopen( path.c_str(), "rb" );
    if ( !f ) return false;
    fseek( f, 0, SEEK_END );
    long size = ftell( f );
    fseek( f, 0, SEEK_SET );
    out.resize( size > 0 ? (size_t) size : 0 );
    bool ok = out.empty() || fread( out.data(), 1, out.size(), f ) == out.size();
    fclose( f );
    return ok;
}

int main( int argc, char** argv )
{
    if ( argc != 3 && !(argc == 4 && strcmp( argv[2], "--compare" ) == 0) )
    {
        fprintf( stderr, "usage: %s <rom.nes> <outdir>\n       %s <rom.nes> --compare <dir>\n", argv[0], argv[0] );
        return 2;
    }

    std::vector<uint8_t> file, rom;
    if ( !ReadFile( argv[1], file ) )
    {
        fprintf( stderr, "cannot read %s\n", argv[1] );
        return 1;
    }
    RomExtract::Result r = RomExtract::NormalizeRom( file.data(), file.size(), rom );
    if ( r != RomExtract::Result_Ok )
    {
        fprintf( stderr, "%s\n", RomExtract::Describe( r ) );
        return 1;
    }

    RomExtract::FileMap files;
    RomExtract::ExtractAll( rom, files );

    if ( argc == 4 )
    {
        int same = 0, differ = 0, missing = 0;
        for ( auto& kv : files )
        {
            std::vector<uint8_t> ref;
            if ( !ReadFile( std::string( argv[3] ) + "/" + kv.first, ref ) )
            {
                printf( "  (no reference) %s\n", kv.first.c_str() );
                missing++;
                continue;
            }
            if ( ref == kv.second )
                same++;
            else
            {
                size_t n = std::min( ref.size(), kv.second.size() ), at = 0;
                while ( at < n && ref[at] == kv.second[at] ) at++;
                printf( "  DIFFERS %s (ours %zu bytes, ref %zu bytes, first diff at %zu)\n",
                    kv.first.c_str(), kv.second.size(), ref.size(), at );
                differ++;
            }
        }
        printf( "%d identical, %d differ, %d without reference (of %zu)\n",
            same, differ, missing, files.size() );
        return differ ? 1 : 0;
    }

    for ( auto& kv : files )
    {
        std::string path = std::string( argv[2] ) + "/" + kv.first;
        FILE* f = fopen( path.c_str(), "wb" );
        if ( !f || fwrite( kv.second.data(), 1, kv.second.size(), f ) != kv.second.size() )
        {
            fprintf( stderr, "cannot write %s\n", path.c_str() );
            return 1;
        }
        fclose( f );
    }
    printf( "Wrote %zu files to %s\n", files.size(), argv[2] );
    return 0;
}
