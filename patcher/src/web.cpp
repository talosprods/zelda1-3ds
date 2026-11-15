// Browser front end of the patcher, compiled to WebAssembly by build-web.sh.
//
// The page (docs/index.html) loads this module, copies the ROM and the two
// asset-free base builds into its memory and calls zp_patch(). Everything
// runs inside the visitor's browser: nothing is uploaded anywhere. The work
// itself is the same patch_core.cpp the PC tool uses, so both produce
// byte-identical files.

#include "patch_core.h"

#include <emscripten/emscripten.h>

static Bytes s_cia, s_3dsx;
static std::string s_error;
static int s_fileCount = 0;

extern "C" {

// Returns 1 on success, 0 on failure (see zp_error()). Either base may be
// empty (length 0) to skip that output.
EMSCRIPTEN_KEEPALIVE int zp_patch( const uint8_t* rom, int romLen,
                                   const uint8_t* baseCia, int baseCiaLen,
                                   const uint8_t* base3dsx, int base3dsxLen )
{
    s_cia.clear();
    s_3dsx.clear();
    s_error.clear();
    s_fileCount = 0;
    try
    {
        std::vector<RomFile> files = ExtractRomFiles( Bytes( rom, rom + romLen ) );
        s_fileCount = (int) files.size();
        if ( baseCiaLen > 0 )
            s_cia = PatchCia( Bytes( baseCia, baseCia + baseCiaLen ), files );
        if ( base3dsxLen > 0 )
            s_3dsx = Patch3dsx( Bytes( base3dsx, base3dsx + base3dsxLen ), files );
        return 1;
    }
    catch ( const Fail& f )
    {
        s_error = f.msg;
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE const char* zp_error() { return s_error.c_str(); }
EMSCRIPTEN_KEEPALIVE int zp_file_count() { return s_fileCount; }
EMSCRIPTEN_KEEPALIVE const uint8_t* zp_cia() { return s_cia.data(); }
EMSCRIPTEN_KEEPALIVE int zp_cia_len() { return (int) s_cia.size(); }
EMSCRIPTEN_KEEPALIVE const uint8_t* zp_3dsx() { return s_3dsx.data(); }
EMSCRIPTEN_KEEPALIVE int zp_3dsx_len() { return (int) s_3dsx.size(); }

}
