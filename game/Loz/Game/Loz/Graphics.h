/*
   Copyright 2016 Aldo J. Nunez

   Licensed under the Apache License, Version 2.0.
   See the LICENSE text file for details.

   Modified for The Legend of Zelda: 3DS Edition (Nintendo 3DS port).
*/

#pragma once


enum TileSheetSlot
{
    Sheet_Background,
    Sheet_PlayerAndItems,
    Sheet_Npcs,
    Sheet_Boss,
    Sheet_Font,

    Sheet_Max
};


// Phase 12.2b: which stereoscopic-3D depth plane subsequent top-screen draws
// belong to. Ground = room background/doors (recedes), Mid = Link + regular
// objects (screen plane, matches the "3D off" position), Fore = projectiles
// + the object Link is currently lifting (pops toward the viewer). See
// Graphics::SetTopLayer().
enum class TopLayer { Ground, Mid, Fore };

struct SpriteFrame
{
    uint8_t     x;
    uint8_t     y;
    uint8_t     flags;
};

struct SpriteAnim
{
    uint8_t     length;
    uint8_t     width;
    uint8_t     height;
    SpriteFrame frames[1];
};


class Graphics
{
public:
    static bool Init();

    static void LoadTileSheet( int slot, const char* path );
    static void LoadTileSheet( int slot, const char* imagePath, const char* animPath );

    // Builds the GPU-ready texture for every palette row of `slot` right now
    // instead of lazily on first draw. No-op on desktop (only the 3DS backend
    // has a CPU-side expansion step). Call during Init(), before the main
    // loop starts, so the cost never lands on a real frame.
    static void PrewarmSheet( int slot );

    // Whether the persistent top status bar should stay visible during
    // normal (unpaused) play. True on desktop (its single screen is the
    // only place hearts/items show). False on 3DS, where that info lives
    // on the bottom screen's live map, so hiding the top bar frees space
    // for SetFullPlayView() to use.
    static bool ShouldShowTopHud();

    // Whether Start should open the classic top-screen inventory/pause
    // overlay (Submenu — equipment cursor, passive items, triforce/map, the
    // Up+A manual save). True on desktop (its single screen is the only
    // place that info lives). False on 3DS: the bottom screen's persistent
    // header + Items screen already show equipped items/hearts/inventory
    // live, and its own SAVE button covers the manual save, so the old
    // inventory screen is redundant there. Keeping this false
    // also means WorldImpl::Draw() never takes the SetNormalView() branch on
    // 3DS (submenuOffsetY can't move once Start can't open the menu that
    // drives it) — one fewer top-screen view mode for phase 6's render-path
    // rewrite to get right.
    static bool ShouldUseClassicPauseMenu();

    // 3DS-only: fills the current draw target with solid black. Used by
    // DrawTitle() to force a black backdrop no matter what -- see the
    // call site in World.cpp for why. No-op on desktop (its title screen
    // has always rendered black; this exists to defend the 3DS render
    // path specifically).
    static void FillBlack();

    // Fills one tile cell with opaque black, in the same game coordinates
    // DrawTile() uses. Exists for exactly one caller: WorldImpl::DrawMap()
    // putting an opaque backdrop behind a foreground tile in the Mid
    // stereoscopic layer. No-op on desktop, which has no layers -- see
    // SetTopLayer()'s comment for the same convention.
    //
    // Why it is needed: palette index 0 is transparent in this port
    // (Graphics.cpp's SetPaletteIndexed writes an ARGB8 zero for it, and the
    // 3DS expander does the same), and background tiles use it heavily -- the
    // forest wall 0x1B is 27% index-0, the lone boulder 0x13 is 29%. In 2D
    // those holes reveal the black ClearScreen() underneath, which is exactly
    // the dark gaps the artwork intends. Split across depth layers they
    // instead reveal the *parallax-shifted Ground layer*, so you look straight
    // through every bush at the terrain behind it, offset per eye: the ground
    // appears doubled with a faint layer on top, and water shows up where
    // there is none -- real terrain, seen through the bushes next to it.
    static void DrawTileBackdrop( int x, int y, int width, int height );

    // Crops out the top-screen HUD band and scales the play area to fill
    // the freed space (3DS); no-op on desktop, which always shows the full
    // canvas. SetNormalView() restores the standard 1:1 view (used while
    // paused, so the equipment/inventory overlay stays correctly
    // positioned).
    static void SetFullPlayView();
    static void SetNormalView();

    // Phase 12.2b: marks which stereoscopic depth layer (Graphics.h's
    // TopLayer) subsequent top-screen draws belong to -- no-op on desktop
    // (single flat view, no stereo 3D) and on 3DS whenever the 3D slider
    // reads 0 (redirecting draws to separate offscreen buffers only matters
    // once there's an actual second eye to offset). WorldImpl::DrawPlay()/
    // DrawObjects() (World.cpp, shared code) call this at each layer
    // boundary; see phase 12.2b for the exact call sites and why.
    static void SetTopLayer( TopLayer layer );

    // The layer SetTopLayer() last selected. Lets a helper retarget drawing
    // briefly and put it back exactly as it found it, instead of every caller
    // having to remember to restore -- see World.cpp's ClearScreen(), which
    // must always clear the Ground layer no matter which layer the current
    // draw function happened to be working in. Always TopLayer::Mid on desktop
    // and whenever layering is inactive, matching SetTopLayer()'s no-op.
    static TopLayer GetTopLayer();

    // True only when draws are really being split across separate per-depth
    // buffers (3DS with the 3D slider above 0). False on desktop and whenever
    // the slider is at 0, where SetTopLayer() is a no-op and everything lands
    // in one buffer. Lets shared code skip work that only exists to make the
    // layer split look right -- see WorldImpl::DrawMap(), which re-draws
    // foreground tiles into Mid and must not pay for that when there are no
    // separate layers to draw into.
    static bool IsTopLayered();

    static void Begin();
    static void End();
    static void DrawSpriteTile( 
        int slot, 
        int srcX, 
        int srcY, 
        int width, 
        int height, 
        int destX, 
        int destY, 
        int palette,
        int flags
        );
    static void DrawTile( 
        int slot, 
        int srcX, 
        int srcY, 
        int width, 
        int height, 
        int destX, 
        int destY, 
        int palette,
        int flags
        );
    static void DrawBitmap(
        ALLEGRO_BITMAP* bitmap,
        int srcX, 
        int srcY,
        int width,
        int height,
        int destX,
        int destY,
        int palette,
        int flags
        );

    static void LoadSystemPalette( const int* colorsArgb8 );
    static ALLEGRO_COLOR GetSystemColor( int sysColor );
    static void SetColor( int paletteIndex, int colorIndex, int colorArgb8 );
    static void SetColorIndexed( int paletteIndex, int colorIndex, int sysColor );
    static void SetPaletteIndexed( int paletteIndex, const uint8_t* sysColors );
    static void UpdatePalettes();

    static void EnableGrayscale();
    static void DisableGrayscale();

    static void SetViewParams( float scale, float x, float y );
    static void SetClip( int x, int y, int width, int height );
    static void ResetClip();

    static const SpriteAnim* GetAnimation( int slot, int animIndex );

private:
    static void SetPalette( int paletteIndex, const int* colorsArgb8 );
    static void SwitchSystemPalette( int* newSystemPalette );
};
