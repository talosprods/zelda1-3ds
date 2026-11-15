// zelda3ds-patcher -- turns your own Zelda ROM into the finished 3DS game.
//
//   zelda3ds-patcher <rom.nes>
//
// Reads the ROM, extracts the game's assets from it (the same RomExtract code
// the console uses) and writes zelda3ds.cia and zelda3ds.3dsx next to the
// ROM, with the assets built into their RomFS. The game code itself comes
// from the asset-free base builds published with each release: embedded in
// this program for release builds, or given with --base-cia/--base-3dsx.
//
// Nothing is sent anywhere; the ROM is only read.
//
// How the CIA is patched: the base CIA is a normal makerom build whose
// content is an unencrypted NCCH with the RomFS as its last section. The
// RomFS is rebuilt (level-3 file system + IVFC hash tree, laid out exactly as
// makerom lays it out), the NCCH header's RomFS size/hash fields and content
// size are updated, then the TMD's content size/hash and hash chain and the
// CIA header's content size. Signatures are left as makerom wrote them;
// homebrew CIAs are never Nintendo-signed, so nothing checks them.
//
// This file is the command-line front end; the work happens in
// patch_core.cpp, which the browser version (web.cpp) shares.

#include "patch_core.h"

#include <stdio.h>
#include <string>
#include <vector>

#ifdef PATCHER_EMBED_BASE
#include "embedded_base.h"   // generated: kBaseCia/kBaseCiaSize, kBase3dsx/kBase3dsxSize
#endif

static const char* ToolVersion = "1.0";

static bool ReadFile( const std::string& path, Bytes& out )
{
    FILE* f = fopen( path.c_str(), "rb" );
    if ( !f ) return false;
    fseek( f, 0, SEEK_END );
    long size = ftell( f );
    fseek( f, 0, SEEK_SET );
    if ( size < 0 ) { fclose( f ); return false; }
    out.resize( (size_t) size );
    bool ok = out.empty() || fread( out.data(), 1, out.size(), f ) == out.size();
    fclose( f );
    return ok;
}

static bool WriteFile( const std::string& path, const Bytes& data )
{
    FILE* f = fopen( path.c_str(), "wb" );
    if ( !f ) return false;
    bool ok = fwrite( data.data(), 1, data.size(), f ) == data.size();
    return fclose( f ) == 0 && ok;
}

static std::string DirOf( const std::string& path )
{
    size_t p = path.find_last_of( "/\\" );
    return p == std::string::npos ? std::string( "." ) : path.substr( 0, p );
}


static void WaitIfDoubleClicked( bool interactive )
{
#ifdef _WIN32
    if ( interactive )
    {
        printf( "\nPress Enter to close this window." );
        getchar();
    }
#else
    (void) interactive;
#endif
}

static int Usage()
{
    printf( "zelda3ds-patcher %s -- The Legend of Zelda: 3DS Edition\n\n", ToolVersion );
    printf( "Usage: zelda3ds-patcher [options] <your-zelda-rom.nes>\n" );
    printf( "       (on Windows you can also drag the .nes file onto this program)\n\n" );
    printf( "Writes zelda3ds.cia and zelda3ds.3dsx next to the ROM.\n\n" );
    printf( "Options:\n" );
    printf( "  --out <dir>          write the output there instead\n" );
    printf( "  --base-cia <file>    asset-free base CIA (default: built in)\n" );
    printf( "  --base-3dsx <file>   asset-free base 3DSX (default: built in)\n" );
    printf( "  --order-from <cia>   (testing) copy RomFS file order from a CIA\n" );
    return 2;
}

int main( int argc, char** argv )
{
    std::string romPath, outDir, baseCiaPath, base3dsxPath, orderFrom;
    for ( int i = 1; i < argc; i++ )
    {
        std::string a = argv[i];
        if ( (a == "--out" || a == "--base-cia" || a == "--base-3dsx" || a == "--order-from") && i + 1 < argc )
        {
            std::string v = argv[++i];
            if ( a == "--out" ) outDir = v;
            else if ( a == "--base-cia" ) baseCiaPath = v;
            else if ( a == "--base-3dsx" ) base3dsxPath = v;
            else orderFrom = v;
        }
        else if ( a == "-h" || a == "--help" )
            return Usage();
        else if ( romPath.empty() && a[0] != '-' )
            romPath = a;
        else
            return Usage();
    }
    bool interactive = argc == 2;   // launched by drag-and-drop or with just a ROM

    if ( romPath.empty() )
    {
        Usage();
        WaitIfDoubleClicked( argc == 1 );
        return 2;
    }
    if ( outDir.empty() )
        outDir = DirOf( romPath );

    try
    {
        Bytes file;
        if ( !ReadFile( romPath, file ) )
            throw Fail{ "cannot read " + romPath };
        std::vector<RomFile> extra = ExtractRomFiles( file );
        printf( "ROM OK: The Legend of Zelda (USA, PRG0).\n" );
        printf( "Extracted %zu game files.\n", extra.size() );

        Bytes baseCia, base3dsx;
        if ( !baseCiaPath.empty() )
        {
            if ( !ReadFile( baseCiaPath, baseCia ) ) throw Fail{ "cannot read " + baseCiaPath };
        }
#ifdef PATCHER_EMBED_BASE
        else baseCia.assign( kBaseCia, kBaseCia + kBaseCiaSize );
#endif
        if ( !base3dsxPath.empty() )
        {
            if ( !ReadFile( base3dsxPath, base3dsx ) ) throw Fail{ "cannot read " + base3dsxPath };
        }
#ifdef PATCHER_EMBED_BASE
        else base3dsx.assign( kBase3dsx, kBase3dsx + kBase3dsxSize );
#endif
        if ( baseCia.empty() && base3dsx.empty() )
            throw Fail{ "no base build: this copy has none built in, pass --base-cia / --base-3dsx" };

        std::vector<std::string> order;
        if ( !orderFrom.empty() )
        {
            Bytes ref;
            if ( !ReadFile( orderFrom, ref ) ) throw Fail{ "cannot read " + orderFrom };
            order = FileOrderOf( ref );
        }

        if ( !baseCia.empty() )
        {
            std::string path = outDir + "/zelda3ds.cia";
            if ( !WriteFile( path, PatchCia( baseCia, extra, order.empty() ? nullptr : &order ) ) )
                throw Fail{ "cannot write " + path };
            printf( "Wrote %s  (install with FBI)\n", path.c_str() );
        }
        if ( !base3dsx.empty() )
        {
            std::string path = outDir + "/zelda3ds.3dsx";
            if ( !WriteFile( path, Patch3dsx( base3dsx, extra ) ) )
                throw Fail{ "cannot write " + path };
            printf( "Wrote %s  (copy to sd:/3ds/zelda3ds/ for the Homebrew Launcher)\n", path.c_str() );
        }
        printf( "Done.\n" );
        WaitIfDoubleClicked( interactive );
        return 0;
    }
    catch ( const Fail& f )
    {
        fprintf( stderr, "Error: %s\n", f.msg.c_str() );
        WaitIfDoubleClicked( interactive );
        return 1;
    }
}
