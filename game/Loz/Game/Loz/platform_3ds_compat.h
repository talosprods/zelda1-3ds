/*
   3DS compatibility shim.

   The shared Loz game code (everything except the platform files Graphics/Sound/
   Input/Loz/SaveFolder, which are rewritten natively for the 3DS) still refers to
   a small set of Allegro types and helpers. This header stands in for them on the
   3DS build. The helper functions are implemented in Graphics_3ds.cpp.
*/
#pragma once
#ifdef __3DS__

#include <cstdint>
// Headers the shared game code used to get transitively via Allegro on desktop.
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// --- Allegro type stand-ins ------------------------------------------------
struct ALLEGRO_COLOR { float r, g, b, a; };
typedef struct ALLEGRO_BITMAP ALLEGRO_BITMAP;   // opaque backend image
typedef struct ALLEGRO_CONFIG ALLEGRO_CONFIG;   // opaque (unused on 3DS)

// --- Draw flags ------------------------------------------------------------
enum
{
    ALLEGRO_FLIP_HORIZONTAL = 1,
    ALLEGRO_FLIP_VERTICAL   = 2,
};

// --- Debug hotkey key codes (no hardware keyboard on 3DS; never triggered) --
enum
{
    ALLEGRO_KEY_H = 1000,
    ALLEGRO_KEY_EQUALS,
    ALLEGRO_KEY_MINUS,
};

// --- Allegro helper stand-ins used by shared game code ---------------------
ALLEGRO_COLOR   al_map_rgb( unsigned char r, unsigned char g, unsigned char b );
ALLEGRO_COLOR   al_map_rgba( unsigned char r, unsigned char g, unsigned char b, unsigned char a );
void            al_clear_to_color( ALLEGRO_COLOR color );
ALLEGRO_BITMAP* al_load_bitmap( const char* path );
void            al_destroy_bitmap( ALLEGRO_BITMAP* bmp );

#endif // __3DS__
