#ifdef __3DS__
/*
   3DS graphics backend (citro2d).

   Replaces Loz's GLSL palette shader with CPU palette expansion: indexed tile
   sheets (2-bit indices, from .bin assets) are expanded to RGBA per palette row
   and uploaded as tiled 3DS textures, then drawn with citro2d. Ports the
   platform-neutral palette bookkeeping from Graphics.cpp verbatim.
*/
#include <citro2d.h>
#include "Common.h"
#include "Graphics.h"
#include "Util.h"
#include "platform_3ds_video.h"
#include "livemap.h"
#include "AssetStore.h"
#include "TerrainClasses.gen.h"
#include "LiveMapGeometry.h"
#include "Options.h"
#include "SoundId.h"   // phase 13.4 music player: Song_* IDs are a bare enum, no game-logic coupling
#include <cstdio>
#include <cstring>
#include <cmath>

//===========================================================================
// Frame/video plumbing (kept here so main_3ds stays citro2d-free)
//===========================================================================
static C3D_RenderTarget* s_top;
static C3D_RenderTarget* s_topRight;   // phase 12.2: right-eye top-screen target, stereoscopic 3D
static C3D_RenderTarget* s_bottom;

// Phase 12.2b: real depth layers, replacing 12.2's uniform
// whole-scene offset (kept the two screen targets/gfxSet3D from 12.2 --
// see that comment, still accurate, just superseded by per-layer offsets
// below instead of one flat kMaxParallaxPx). Ground/Mid/Fore match
// Graphics::TopLayer (Graphics.h) and 12.1's GROUND/MIDDLE/FOREGROUND plan:
// Ground (room background/doors) recedes, Mid (Link + regular objects) sits
// exactly where "3D off" already draws everything (0 offset -- no jump when
// toggling 3D), Fore (all projectiles + the lifted-object layer) pops
// toward the viewer. Same sign convention as 12.2 for Ground (uncrossed
// disparity: left eye shifts left, right eye shifts right); Fore is the
// opposite (crossed disparity: left eye shifts right, right eye shifts
// left) since it's *in front of* the screen plane, not behind it. Both are
// untested placeholder magnitudes, same reasoning as 12.2's kMaxParallaxPx
// was: Azahar can't emulate the slider, so there's no way to judge "how much
// depth feels right" without real hardware -- easy to retune (two
// constants) once there's real feedback. Fore is kept smaller than Ground:
// "popping out" reads as more intense per pixel of offset than "receding"
// (common stereo-3D guidance), so starting conservative there specifically.
static const float kGroundParallaxPx = 6.0f;
static const float kForeParallaxPx   = 3.0f;

// phase 6.1: the top screen's "special render method" — the shared game code
// (World.cpp etc.) keeps drawing in NES coordinates exactly as before
// (necessary: that code is byte-identical between the desktop and 3DS
// builds), but on 3DS those draws now land on an offscreen 2x buffer
// (512x352 = the 256x176 play area at a clean integer scale, nearest-
// neighbour — pixel-perfect, no stretch) instead of the physical screen
// directly. Video_BlitTopScreen() then draws THAT buffer onto the real
// 400x240 top screen once, with bilinear filtering and an 8:7 pixel-aspect-
// ratio correction (~399x240 — the NES's non-square pixels, same convention
// emulators/upscalers commonly use) — a "sharp-bilinear" look: crisp 2x
// pixels, smoothly (not jaggedly) resampled to the final size, correct
// proportions. Replaces the old single non-uniform stretch in
// SetFullPlayView() (1.36x vertical vs 1.5625x horizontal — a visibly
// "squished" look).
static C3D_RenderTarget* s_topOffscreen;
static C3D_Tex           s_topOffscreenTex;
// Phase 7.5a, second layer: the engine deliberately draws
// NOTHING on the first frame of some modes (DrawEnter's Start substate,
// and similar Goto->first-Draw gaps) -- on desktop that one-frame blank has
// always been invisible (black clear on a black-bordered window), but on a
// 60fps handheld it reads as a visible flicker on every room entry (it was
// *red* until the clear-colour byte-order fix; black instead of red is still
// a flicker). Real consoles
// never blank: the PPU keeps showing the last frame until new content
// lands. Mirror that: track whether World::Draw() put anything at all into
// the offscreen buffer this frame (any sprite/tile/text via drawRegion, any
// ClearScreen via al_clear_to_color, any FillBlack); if not,
// Video_BlitTopScreen() leaves s_top completely untouched for that frame,
// so the physical screen simply keeps presenting the previous frame --
// same "citro3d doesn't require a target to be touched every frame"
// property the phase 5.1b bottom-map skip already relies on.
static bool s_topContentDrawn  = false;
static bool s_topEverPresented = false;
static const int TopOffscreenW = 512;   // 256 (StdViewWidth) * 2
static const int TopOffscreenH = 352;   // 176 (play area, HUD excluded) * 2
// PICA200 textures must be power-of-2 (same reason the indexed tile-sheet
// loader below calls pow2up() on its own textures) — 352 isn't, so the
// backing texture is padded to 512x512 and only the top TopOffscreenH rows
// are ever drawn to or sampled. First attempt at this used an exact
// 512x352 C3D_TexInitVRAM() with no return-value check; it silently failed
// (non-pow2), and the resulting null/invalid texture crashed the guest with
// an unmapped-memory read on the very first frame. Lesson: always check
// C3D_Tex*Init's bool return on this hardware.
static const int TopOffscreenTexW = 512;
static const int TopOffscreenTexH = 512;

// Phase 12.2b: two more offscreen buffers, same size/format as
// s_topOffscreen(Tex) above, for the Ground and Fore depth layers (Mid
// reuses the existing s_topOffscreen buffer -- it's already exactly what
// "3D off" draws, see Graphics::TopLayer's comment in Graphics.h).
// Lazily allocated (EnsureLayeredBuffersReady(), called from
// Video_BeginFrame()) on the first frame the slider actually reads above 0,
// not at boot -- a session that never touches the slider pays zero extra
// VRAM (this project has hit real VRAM-pressure crashes before, see
// DEVELOPMENT.md's phase 11.1 write-up; render targets specifically can't
// fall back to linear RAM like tile-sheet textures can, so this matters).
static C3D_RenderTarget* s_groundOffscreen;
static C3D_Tex           s_groundOffscreenTex;
static C3D_RenderTarget* s_foreOffscreen;
static C3D_Tex           s_foreOffscreenTex;
static bool s_layeredAllocTried = false;
static bool s_layeredBuffersOk  = false;
// Set once per frame in Video_BeginFrame() (single osGet3DSliderState() read
// for the whole frame -- Graphics::SetTopLayer() and Video_BlitTopScreen()
// both consult this cached value rather than re-reading the slider, so a
// value that changed mid-frame can't make the two disagree).
static float s_slider3D      = 0.0f;
static bool  s_layeredActive = false;
// Which layer SetTopLayer() last selected, for Graphics::GetTopLayer().
static TopLayer s_curTopLayer = TopLayer::Mid;

// Static, pre-baked bottom-screen wood-grain backdrop (perf).
// The live DrawSpeckleField() version (840 C2D_DrawRectSolid calls + ~2520
// SpeckleHash computes, every single frame, since Video_DrawBottomMap runs
// during gameplay too) dragged the whole game below 60fps on real hardware.
// A render-to-texture GPU cache was tried next and failed three different
// ways on real hardware across three fix attempts (same-frame render+sample:
// red garbage; a standalone priming frame: partially blank; delaying the
// sample by a real frame: still broke, this time even in Azahar) -- none
// reproducible consistently, all pointing at this GPU/citro3d combination
// not handling render-to-texture-then-sample reliably regardless of timing.
// Sidesteps all of that with a genuinely static asset: tools/gen-woodgrain.py
// bakes the exact same visual (same SpeckleHash/DrawSpeckleField math,
// offline) to a PNG, converted to romfs/woodgrain.t3x by devkitPro's own
// tex3ds tool. A first attempt hand-rolled the GPU's tiled/Morton texture
// layout (matching ensureExpanded()'s tiledOffset(), used for tile sheets)
// to load a raw RGBA blob directly -- wrong: long streaks rendered
// transposed (vertical instead of horizontal), a bug tile-sheet content
// never exposed since most of it is small flat-colour regions where a local
// transpose is invisible. tex3ds/C2D_SpriteSheetLoad is the real, battle-
// tested tool for exactly this job -- no hand-rolled swizzling at all.
static C2D_SpriteSheet s_bottomBgSheet = nullptr;
static C2D_Image       s_bottomBgImg;
static bool            s_bottomBgTexOk = false;

// Per-room overworld terrain thumbnail atlas (several colours per map cell
// instead of one): 128 tiny 16x11 (one pixel
// per tile) thumbnails, one per overworld room -- real per-tile terrain
// (water/forest/mountain/sand/ground), not the old hand-typed 16x8 kBiomeMap
// ASCII art (BiomeColor(), kept below only as the load-failure fallback).
// One draw call per visited cell, same as the flat-colour fill it replaces.
//
// Built at startup from terrain.bin, which RomExtract derives from the
// player's ROM (the release ships no Nintendo data, so this can no longer be
// a pre-baked .t3x in romfs). Layout: an 8x16 grid of 16x16 cells in one
// 128x256 texture, room N at cell (N % 8, N / 8), top 11 rows used.
static C3D_Tex           s_terrainTex;
static Tex3DS_SubTexture s_terrainSub[128];
static bool              s_terrainTexOk  = false;
static void BuildTerrainTexture();

// Current clip rect in screen coords (tracked for scissor + clear).
static bool clipActive = false;
static int  clipX, clipY, clipW, clipH;

// Bottom-screen map text.
static C2D_TextBuf s_mapTextBuf;
static C2D_Text    s_dungeonText[9];
static C2D_Text    s_legendYou, s_legendSeen, s_legendDun, s_legendPin;
static C2D_Text    s_poiNames[Poi_Count];   // phase 10.5 info card category names
// Legend rework (a legend must say which colour means what, or go) --
// short one-word labels next
// to each POI colour swatch, since the old unlabelled swatch row (kept
// only "here's what the colours look like", never what they meant) failed
// exactly that test. Separate from s_poiNames above (those
// full names, e.g. "FAIRY FOUNTAIN", don't fit a 4-column legend grid).
static C2D_Text    s_legendPoiNames[Poi_Count];
static C2D_Text    s_infoTriforceYes, s_infoTriforceNo, s_infoTapClose;
static C2D_Text    s_uwTitleWord, s_uwLegendMap, s_uwLegendCompass, s_uwLegendKey, s_uwLegendBoss;
// Persistent header (phase 9.x bottom-screen remake): MAP/ITEMS toggle labels
// and the small B/Y tags under the equipped-weapon badges.
static C2D_Text    s_tabMap, s_tabItems, s_tabOptions, s_tagA, s_tagB, s_tagY;
static C2D_Text    s_musicNames[Song_Max], s_musicStop;   // phase 13.4 music player
static C2D_Text    s_statPlayTime, s_statDeaths, s_statKills, s_statRepair, s_statMin, s_statRupees;   // phase 13.3 stats
static C2D_Text    s_statSecrets, s_statSlash;   // phase 10.4
// phase 13.2 Manual (book/achievement-list screen, 3 scrollable sections).
static C2D_Text    s_bossNames[LIVEMAP_MANUAL_BOSS_ROWS];
static C2D_Text    s_swordNames[LIVEMAP_MANUAL_SWORD_ROWS];
static C2D_Text    s_itemManualNames[LIVEMAP_ITEM_GRID_SLOTS];
static C2D_Text    s_manualSectionNames[LIVEMAP_MANUAL_SECTION_ROWS];
static C2D_Text    s_manualLocked;                          // "???" -- shared across all 3 sections
static C2D_Text    s_manualDefeated, s_manualOwned, s_manualFound;   // per-section status tags
static C2D_Text    s_titlePressLabel, s_titleEditionLabel;   // title screen's bottom-screen companion (phase 11.1b)
static C2D_Text    s_contGameOver, s_contBtn1, s_contBtn2, s_contBtn2Dungeon, s_contHint;   // death continue panel
static C2D_Text    s_fpsLabel;   // Options screen live FPS counter
static C2D_Text    s_legendQ;   // "?" — the on-demand map legend's touch button
static C2D_Text    s_saveLabel, s_savedLabel;   // Items screen's SAVE button/toast (phase 9.x)
// Options screen (phase 11.1): one row label + two segment labels per setting.
static C2D_Text    s_optLabels[LIVEMAP_OPT_ROWS];
static C2D_Text    s_segNormal, s_segFast, s_segOff, s_segStrict, s_segOriginal, s_segClear;
static C2D_Text    s_segShow, s_segSkip;
// Save-slot management overlay: no name typing, just tap slot 1/2/3.
static C2D_Text    s_slotsTitle;
// Options screen action-row labels (LIVEMAP_OPT_ACTION_ROWS): MANUAL,
// MUSIC, STATS, SAVE SLOTS, in that order --
// see livemap.h's comment.
static C2D_Text    s_optActionLabels[LIVEMAP_OPT_ACTION_ROWS];
static C2D_Text    s_slotLabel[LIVEMAP_SLOTS_ROWS];
static C2D_Text    s_slotEmpty, s_slotPlaying, s_slotDeaths, s_slotDelete, s_slotConfirmDelete;
static C2D_Text    s_slotQuest1, s_slotQuest2;   // phase 14.1 new-game quest choice
static C2D_Text    s_slotCopy, s_slotCopyHint;   // phase 14.3 follow-up: save-slot copy

// Phase 12.2b: lazily creates the Ground/Fore offscreen render targets, the
// first time (and only the first time) a frame's slider reads above 0 --
// see the s_layeredAllocTried/s_layeredBuffersOk comment above for why this
// isn't done upfront in Video_Init(). Same defensive pattern as the
// original s_topOffscreenTex creation: check C3D_TexInitVRAM's bool return,
// log to the existing render-init-error.txt channel on failure instead of
// crashing, and simply don't enable layered 3D for the rest of this session
// (Graphics::SetTopLayer()/Video_BlitTopScreen() both gate on
// s_layeredBuffersOk via s_layeredActive).
static bool EnsureLayeredBuffersReady()
{
    if ( s_layeredAllocTried )
        return s_layeredBuffersOk;
    s_layeredAllocTried = true;

    bool groundOk = C3D_TexInitVRAM( &s_groundOffscreenTex, TopOffscreenTexW, TopOffscreenTexH, GPU_RGBA8 );
    bool foreOk   = groundOk && C3D_TexInitVRAM( &s_foreOffscreenTex, TopOffscreenTexW, TopOffscreenTexH, GPU_RGBA8 );
    if ( !groundOk || !foreOk )
    {
        FILE* f = fopen( "sdmc:/3ds/zelda3ds/render-init-error.txt", "a" );
        if ( f ) { fprintf( f, "phase 12.2b: C3D_TexInitVRAM failed for Ground/Fore layer buffer -- layered 3D disabled this session\n" ); fclose( f ); }
        return false;
    }

    C3D_TexSetFilter( &s_groundOffscreenTex, GPU_LINEAR, GPU_LINEAR );
    C3D_TexSetFilter( &s_foreOffscreenTex, GPU_LINEAR, GPU_LINEAR );
    s_groundOffscreen = C3D_RenderTargetCreateFromTex( &s_groundOffscreenTex, GPU_TEXFACE_2D, 0, C3D_DEPTHTYPE( -1 ) );
    s_foreOffscreen   = C3D_RenderTargetCreateFromTex( &s_foreOffscreenTex, GPU_TEXFACE_2D, 0, C3D_DEPTHTYPE( -1 ) );
    // Same frameBuf.width/height override as s_topOffscreen -- see that
    // creation site's comment for why (logical 512x352 canvas within a
    // padded 512x512 pow2 texture).
    s_groundOffscreen->frameBuf.width  = TopOffscreenW;
    s_groundOffscreen->frameBuf.height = TopOffscreenH;
    s_foreOffscreen->frameBuf.width    = TopOffscreenW;
    s_foreOffscreen->frameBuf.height   = TopOffscreenH;

    s_layeredBuffersOk = true;
    return true;
}

void Video_Init()
{
    C3D_Init( C3D_DEFAULT_CMDBUF_SIZE );
    C2D_Init( C2D_DEFAULT_MAX_OBJECTS );
    C2D_Prepare();
    s_top      = C2D_CreateScreenTarget( GFX_TOP, GFX_LEFT );
    s_topRight = C2D_CreateScreenTarget( GFX_TOP, GFX_RIGHT );
    s_bottom   = C2D_CreateScreenTarget( GFX_BOTTOM, GFX_LEFT );
    // phase 12.2: enabled unconditionally, like every other 3DS game -- the
    // system itself only lights the 3D indicator / sends a right-eye image
    // to the parallax barrier when the physical slider is actually pushed
    // past 0 (osGet3DSliderState()), so this alone has no visible/behavioural
    // effect with the slider at 0. Video_BlitTopScreen() below only pays the
    // extra right-eye blit cost when the slider reads >0, so 3D-off old-3DS
    // 60fps is unaffected (see 12.1's architecture note).
    gfxSet3D( true );

    // phase 6.1 offscreen top-screen buffer — see the s_topOffscreen comment
    // above. RGBA8 to match the screen targets' format; VRAM-resident like
    // any other render target. Padded to a power-of-2 texture (see
    // TopOffscreenTexW/H comment) — checked, since an unchecked failure
    // here crashed the guest on the very first frame the first time.
    bool texOk = C3D_TexInitVRAM( &s_topOffscreenTex, TopOffscreenTexW, TopOffscreenTexH, GPU_RGBA8 );
    if ( !texOk )
    {
        FILE* f = fopen( "sdmc:/3ds/zelda3ds/render-init-error.txt", "w" );
        if ( f ) { fprintf( f, "C3D_TexInitVRAM failed for top-screen offscreen buffer\n" ); fclose( f ); }
    }
    // GPU_LINEAR (restored later): this is phase 6's intended
    // sharp-bilinear look. It was switched to GPU_NEAREST earlier to
    // kill a "crisp red fringe around every hard white/black edge" -- that
    // fringe is now fully explained by the clear-colour byte-order bug
    // fixed in Video_BeginFrame (the buffer's background was transparent
    // *red*, so bilinear sampling blended every content edge against red
    // neighbours; a box-filtered mip chain averaging the same red texels
    // showed it too, which is why that test back then didn't exonerate the
    // filter). With the clear actually opaque black, edges blend against
    // black as designed.
    C3D_TexSetFilter( &s_topOffscreenTex, GPU_LINEAR, GPU_LINEAR );
    s_topOffscreen = C3D_RenderTargetCreateFromTex( &s_topOffscreenTex, GPU_TEXFACE_2D, 0, C3D_DEPTHTYPE( -1 ) );
    // C3D_RenderTargetCreateFromTex sets frameBuf.width/height to the full
    // padded texture size (512x512) — that's also what C3D_FrameDrawOn uses
    // to configure the GPU viewport, so left alone every draw would land in
    // (and get scaled to fit) the wrong, larger canvas: exactly the "way too
    // small" render seen on the second attempt at this (content correctly
    // positioned within a 512x352 logical space, but that space itself sat
    // inside a viewport sized for 512x512, so the final blit's "512x352 of a
    // 512-wide texture" UV crop sampled the right pixels but at the wrong
    // scale). Override just the two size fields to the real logical canvas —
    // leaves colorBuf/depthBuf/colorFmt/block32 etc. (already correct for
    // the actual 512x512 allocation) untouched.
    s_topOffscreen->frameBuf.width  = TopOffscreenW;
    s_topOffscreen->frameBuf.height = TopOffscreenH;

    // Static wood-grain backdrop (see the s_bottomBgSheet comment above):
    // a .t3x built by devkitPro's own tex3ds tool, loaded the standard
    // citro2d way -- no manual texture-layout code at all.
    s_bottomBgSheet = C2D_SpriteSheetLoad( "romfs:/woodgrain.t3x" );
    if ( s_bottomBgSheet != nullptr && C2D_SpriteSheetCount( s_bottomBgSheet ) > 0 )
    {
        s_bottomBgImg = C2D_SpriteSheetGetImage( s_bottomBgSheet, 0 );
        s_bottomBgTexOk = true;
    }
    else
    {
        FILE* ef = fopen( "sdmc:/3ds/zelda3ds/render-init-error.txt", "a" );
        if ( ef ) { fprintf( ef, "woodgrain.t3x load failed -- bottom backdrop will be flat\n" ); fclose( ef ); }
    }

    // Per-room terrain thumbnail atlas -- see s_terrainTex's comment above.
    // Falls back to the old flat BiomeColor() fill on failure, same
    // convention as woodgrain above.
    BuildTerrainTexture();
    if ( !s_terrainTexOk )
    {
        FILE* ef = fopen( "sdmc:/3ds/zelda3ds/render-init-error.txt", "a" );
        if ( ef ) { fprintf( ef, "terrain texture build failed -- map cells fall back to flat BiomeColor()\n" ); fclose( ef ); }
    }

    // phase 10.4/save-slots additions pushed total parsed text close to the
    // old 512 cap -- bumped with headroom rather than trimming labels.
    s_mapTextBuf = C2D_TextBufNew( 1024 );
    C2D_TextParse( &s_tabMap,     s_mapTextBuf, "MAP" );
    C2D_TextParse( &s_tabItems,   s_mapTextBuf, "ITEMS" );
    C2D_TextParse( &s_tabOptions, s_mapTextBuf, "OPT" );
    C2D_TextParse( &s_tagA,     s_mapTextBuf, "A" );
    C2D_TextParse( &s_tagB,     s_mapTextBuf, "B" );
    C2D_TextParse( &s_tagY,     s_mapTextBuf, "Y" );
    C2D_TextOptimize( &s_tabMap );
    C2D_TextOptimize( &s_tabItems );
    C2D_TextOptimize( &s_tabOptions );
    C2D_TextOptimize( &s_tagA );
    C2D_TextOptimize( &s_tagB );
    C2D_TextOptimize( &s_tagY );
    C2D_TextParse( &s_legendYou,  s_mapTextBuf, "YOU" );
    C2D_TextParse( &s_legendSeen, s_mapTextBuf, "SEEN" );
    C2D_TextParse( &s_legendDun,  s_mapTextBuf, "DUNGEON" );
    // No colour claim here any more (legend swatches with no label were
    // useless) -- pins have no fixed meaning (you choose what
    // each of the 4 colours means to you), so the honest legend entry is
    // "here's the gesture", not a colour key that doesn't exist.
    C2D_TextParse( &s_legendPin,  s_mapTextBuf, "PIN: TAP A ROOM, CYCLES YOUR OWN 4 COLOURS" );
    C2D_TextOptimize( &s_legendYou );
    C2D_TextOptimize( &s_legendSeen );
    C2D_TextOptimize( &s_legendDun );
    C2D_TextOptimize( &s_legendPin );

    // phase 10.5 info card.
    static const char* poiNameStrs[Poi_Count] =
    {
        "SHOP", "POTION SHOP", "DOOR REPAIR", "WARP CAVE",
        "GIFT CAVE", "FAIRY FOUNTAIN", "FLUTE WARP SPOT",
    };
    for ( int i = 0; i < Poi_Count; i++ )
    {
        C2D_TextParse( &s_poiNames[i], s_mapTextBuf, poiNameStrs[i] );
        C2D_TextOptimize( &s_poiNames[i] );
    }
    // Legend rework: short one-word labels, actually next to their colour
    // swatch this time (same Poi_* order as kPoiColors/poiNameStrs above).
    static const char* legendPoiStrs[Poi_Count] =
        { "SHOP", "POTION", "REPAIR", "TUNNEL", "GIFT", "FAIRY", "FLUTE" };
    for ( int i = 0; i < Poi_Count; i++ )
    {
        C2D_TextParse( &s_legendPoiNames[i], s_mapTextBuf, legendPoiStrs[i] );
        C2D_TextOptimize( &s_legendPoiNames[i] );
    }
    C2D_TextParse( &s_infoTriforceYes, s_mapTextBuf, "TRIFORCE: FOUND" );
    C2D_TextParse( &s_infoTriforceNo,  s_mapTextBuf, "TRIFORCE: NOT YET" );
    C2D_TextParse( &s_infoTapClose,    s_mapTextBuf, "TAP TO CLOSE" );
    C2D_TextOptimize( &s_infoTriforceYes );
    C2D_TextOptimize( &s_infoTriforceNo );
    C2D_TextOptimize( &s_infoTapClose );

    // phase 13.4 music player: song names in Song_intro..Song_zelda order
    // (SoundId.h). Plain display names, not the in-game context they play
    // in (e.g. Song_triforce also underlies dungeon-item fanfares) --
    // that's a labelling simplification, not a functional one; the row
    // still plays the exact right song via its SongId.
    static const char* songNameStrs[Song_Max] =
    {
        "INTRO", "ENDING", "OVERWORLD", "UNDERWORLD", "ITEM LIFT",
        "TRIFORCE", "GANON", "LEVEL 9", "GAME OVER", "DEATH",
        "RECORDER", "ZELDA",
    };
    for ( int i = 0; i < Song_Max; i++ )
    {
        C2D_TextParse( &s_musicNames[i], s_mapTextBuf, songNameStrs[i] );
        C2D_TextOptimize( &s_musicNames[i] );
    }
    C2D_TextParse( &s_musicStop, s_mapTextBuf, "STOP" );
    C2D_TextOptimize( &s_musicStop );

    C2D_TextParse( &s_fpsLabel, s_mapTextBuf, "FPS" );
    C2D_TextOptimize( &s_fpsLabel );

    // phase 13.3 stats.
    C2D_TextParse( &s_statPlayTime, s_mapTextBuf, "PLAY TIME" );
    C2D_TextParse( &s_statDeaths,   s_mapTextBuf, "DEATHS" );
    C2D_TextParse( &s_statKills,    s_mapTextBuf, "KILLS" );
    C2D_TextParse( &s_statRepair,   s_mapTextBuf, "SPENT ON DOOR REPAIRS" );
    C2D_TextParse( &s_statMin,      s_mapTextBuf, "MIN" );
    C2D_TextParse( &s_statRupees,   s_mapTextBuf, "RUPEES" );
    C2D_TextParse( &s_statSecrets,  s_mapTextBuf, "SECRETS" );
    C2D_TextParse( &s_statSlash,    s_mapTextBuf, "/" );
    C2D_TextOptimize( &s_statPlayTime );
    C2D_TextOptimize( &s_statDeaths );
    C2D_TextOptimize( &s_statKills );
    C2D_TextOptimize( &s_statRepair );
    C2D_TextOptimize( &s_statMin );
    C2D_TextOptimize( &s_statRupees );
    C2D_TextOptimize( &s_statSecrets );
    C2D_TextOptimize( &s_statSlash );
    {
        // phase 13.2 Manual -- BOSSES section, order matches World.h's BossId.
        static const char* bossNameStrs[LIVEMAP_MANUAL_BOSS_ROWS] =
        {
            "AQUAMENTUS", "DODONGO", "MANHANDLA", "DIGDOGGER", "GLEEOK", "PATRA", "GANON"
        };
        for ( int i = 0; i < LIVEMAP_MANUAL_BOSS_ROWS; i++ )
        {
            C2D_TextParse( &s_bossNames[i], s_mapTextBuf, bossNameStrs[i] );
            C2D_TextOptimize( &s_bossNames[i] );
        }
        // Manual -- SWORD section, tier order 0=wooden..2=magical.
        static const char* swordNameStrs[LIVEMAP_MANUAL_SWORD_ROWS] =
        {
            "WOODEN SWORD", "WHITE SWORD", "MAGICAL SWORD"
        };
        for ( int i = 0; i < LIVEMAP_MANUAL_SWORD_ROWS; i++ )
        {
            C2D_TextParse( &s_swordNames[i], s_mapTextBuf, swordNameStrs[i] );
            C2D_TextOptimize( &s_swordNames[i] );
        }
        // Manual -- ITEMS section, same order as the Items screen's grid
        // (LiveMap_state.cpp's s_gridSlots).
        static const char* itemManualNameStrs[LIVEMAP_ITEM_GRID_SLOTS] =
        {
            "BOOMERANG", "BOMBS", "ARROW", "BOW", "CANDLE", "RECORDER", "FOOD",
            "LETTER / POTION", "MAGICAL ROD", "RAFT", "MAGIC BOOK", "RING",
            "LADDER", "MAGIC KEY", "POWER BRACELET"
        };
        for ( int i = 0; i < LIVEMAP_ITEM_GRID_SLOTS; i++ )
        {
            C2D_TextParse( &s_itemManualNames[i], s_mapTextBuf, itemManualNameStrs[i] );
            C2D_TextOptimize( &s_itemManualNames[i] );
        }
        static const char* manualSectionStrs[LIVEMAP_MANUAL_SECTION_ROWS] = { "BOSSES", "SWORD", "ITEMS" };
        for ( int i = 0; i < LIVEMAP_MANUAL_SECTION_ROWS; i++ )
        {
            C2D_TextParse( &s_manualSectionNames[i], s_mapTextBuf, manualSectionStrs[i] );
            C2D_TextOptimize( &s_manualSectionNames[i] );
        }
    }
    C2D_TextParse( &s_manualLocked,   s_mapTextBuf, "???" );
    C2D_TextParse( &s_manualDefeated, s_mapTextBuf, "DEFEATED" );
    C2D_TextParse( &s_manualOwned,    s_mapTextBuf, "OWNED" );
    C2D_TextParse( &s_manualFound,    s_mapTextBuf, "FOUND" );
    C2D_TextOptimize( &s_manualLocked );
    C2D_TextOptimize( &s_manualDefeated );
    C2D_TextOptimize( &s_manualOwned );
    C2D_TextOptimize( &s_manualFound );
    C2D_TextParse( &s_uwTitleWord,    s_mapTextBuf, "LEVEL" );
    C2D_TextParse( &s_uwLegendMap,    s_mapTextBuf, "MAP" );
    C2D_TextParse( &s_uwLegendCompass,s_mapTextBuf, "COMPASS" );
    C2D_TextParse( &s_uwLegendKey,    s_mapTextBuf, "KEYS" );
    C2D_TextParse( &s_uwLegendBoss,   s_mapTextBuf, "BOSS" );
    C2D_TextOptimize( &s_uwTitleWord );
    C2D_TextOptimize( &s_uwLegendMap );
    C2D_TextOptimize( &s_uwLegendCompass );
    C2D_TextOptimize( &s_uwLegendKey );
    C2D_TextOptimize( &s_uwLegendBoss );
    C2D_TextParse( &s_legendQ, s_mapTextBuf, "?" );
    C2D_TextOptimize( &s_legendQ );
    C2D_TextParse( &s_saveLabel,  s_mapTextBuf, "SAVE" );
    C2D_TextParse( &s_savedLabel, s_mapTextBuf, "SAVED" );
    C2D_TextOptimize( &s_saveLabel );
    C2D_TextOptimize( &s_savedLabel );
    // Real hardware crash (Azahar never caught this): this loop
    // used to run LIVEMAP_OPT_ROWS (7) times against a 6-entry
    // optLabelStrs[], reading one element past the array's end on the last
    // iteration -- garbage read as a const char*, handed straight to
    // C2D_TextParse, which crashed inside citro2d's decode_utf8() on a
    // translation fault (bad, near-null address). Deterministic every boot.
    // s_optLabels[] only needs to cover the 7 real toggle fields (indexed
    // by toggleIndex = logical - LIVEMAP_OPT_ACTION_ROWS, not logical
    // itself, since the 4 action rows -- MANUAL/MUSIC/STATS/SAVE SLOTS --
    // now come first) -- those draw s_optActionLabels[] instead (see the
    // LIVEMAP_OPT_ACTION_ROWS check below), matching optLabelStrs[]'s
    // actual size.
    static const char* optLabelStrs[LIVEMAP_OPT_TOGGLE_ROWS] =
        { "TEXT SPEED", "LOW HP BEEP", "SWORD BEAM", "HINT TEXT", "TITLE SCREEN", "SECRET HINTS", "FPS COUNTER" };
    for ( int i = 0; i < LIVEMAP_OPT_TOGGLE_ROWS; i++ )
    {
        C2D_TextParse( &s_optLabels[i], s_mapTextBuf, optLabelStrs[i] );
        C2D_TextOptimize( &s_optLabels[i] );
    }
    C2D_TextParse( &s_segNormal,   s_mapTextBuf, "NORMAL" );
    C2D_TextParse( &s_segFast,     s_mapTextBuf, "FAST" );
    C2D_TextParse( &s_segOff,      s_mapTextBuf, "OFF" );
    C2D_TextParse( &s_segStrict,   s_mapTextBuf, "STRICT" );
    C2D_TextParse( &s_segOriginal, s_mapTextBuf, "ORIGINAL" );
    C2D_TextParse( &s_segClear,    s_mapTextBuf, "CLEAR" );
    C2D_TextParse( &s_segShow,     s_mapTextBuf, "SHOW" );
    C2D_TextParse( &s_segSkip,     s_mapTextBuf, "SKIP" );
    C2D_TextOptimize( &s_segShow );
    C2D_TextOptimize( &s_segSkip );
    C2D_TextOptimize( &s_segNormal );
    C2D_TextOptimize( &s_segFast );
    C2D_TextOptimize( &s_segOff );
    C2D_TextOptimize( &s_segStrict );
    C2D_TextOptimize( &s_segOriginal );
    C2D_TextOptimize( &s_segClear );
    C2D_TextParse( &s_titlePressLabel,   s_mapTextBuf, "PRESS START" );
    C2D_TextParse( &s_titleEditionLabel, s_mapTextBuf, "3DS EDITION" );
    C2D_TextOptimize( &s_titlePressLabel );
    C2D_TextOptimize( &s_titleEditionLabel );
    C2D_TextParse( &s_contGameOver, s_mapTextBuf, "GAME OVER" );
    C2D_TextParse( &s_contBtn1,     s_mapTextBuf, "CONTINUE" );
    C2D_TextParse( &s_contBtn2,     s_mapTextBuf, "CONTINUE FROM START" );
    // phase 8.4d (in dungeons, continue always went back to the entrance,
    // so offer "continue" and "exit dungeon" there instead) -- CONTINUE FROM
    // START was a no-op duplicate of CONTINUE
    // inside a dungeon, since both always restart at the entrance; the
    // 2nd button now means something distinct there instead.
    C2D_TextParse( &s_contBtn2Dungeon, s_mapTextBuf, "EXIT DUNGEON" );
    C2D_TextParse( &s_contHint,     s_mapTextBuf, "TAP TO CHOOSE" );
    C2D_TextOptimize( &s_contGameOver );
    C2D_TextOptimize( &s_contBtn1 );
    C2D_TextOptimize( &s_contBtn2 );
    C2D_TextOptimize( &s_contBtn2Dungeon );
    C2D_TextOptimize( &s_contHint );
    C2D_TextParse( &s_slotsTitle,    s_mapTextBuf, "SAVE SLOTS" );
    {
        static const char* optActionStrs[LIVEMAP_OPT_ACTION_ROWS] = { "MANUAL", "MUSIC", "STATS", "SAVE SLOTS" };
        for ( int i = 0; i < LIVEMAP_OPT_ACTION_ROWS; i++ )
        {
            C2D_TextParse( &s_optActionLabels[i], s_mapTextBuf, optActionStrs[i] );
            C2D_TextOptimize( &s_optActionLabels[i] );
        }
    }
    static const char* slotLabelStrs[LIVEMAP_SLOTS_ROWS] = { "SLOT 1", "SLOT 2", "SLOT 3" };
    for ( int i = 0; i < LIVEMAP_SLOTS_ROWS; i++ )
    {
        C2D_TextParse( &s_slotLabel[i], s_mapTextBuf, slotLabelStrs[i] );
        C2D_TextOptimize( &s_slotLabel[i] );
    }
    C2D_TextParse( &s_slotEmpty,         s_mapTextBuf, "EMPTY - TAP FOR NEW GAME" );
    C2D_TextParse( &s_slotPlaying,       s_mapTextBuf, "PLAYING NOW" );
    C2D_TextParse( &s_slotDeaths,        s_mapTextBuf, "DEATHS" );
    C2D_TextParse( &s_slotDelete,        s_mapTextBuf, "X" );
    C2D_TextParse( &s_slotConfirmDelete, s_mapTextBuf, "TAP X AGAIN TO DELETE" );
    C2D_TextParse( &s_slotQuest1, s_mapTextBuf, "QUEST 1" );
    C2D_TextParse( &s_slotQuest2, s_mapTextBuf, "QUEST 2" );
    C2D_TextParse( &s_slotCopy,     s_mapTextBuf, "COPY" );
    C2D_TextParse( &s_slotCopyHint, s_mapTextBuf, "TAP A SLOT, THEN AN EMPTY ONE" );
    C2D_TextOptimize( &s_slotsTitle );
    C2D_TextOptimize( &s_slotEmpty );
    C2D_TextOptimize( &s_slotPlaying );
    C2D_TextOptimize( &s_slotDeaths );
    C2D_TextOptimize( &s_slotDelete );
    C2D_TextOptimize( &s_slotConfirmDelete );
    C2D_TextOptimize( &s_slotQuest1 );
    C2D_TextOptimize( &s_slotQuest2 );
    C2D_TextOptimize( &s_slotCopy );
    C2D_TextOptimize( &s_slotCopyHint );
    static char digits[9][3];
    for ( int i = 0; i < 9; i++ )
    {
        snprintf( digits[i], 3, "%d", i + 1 );
        C2D_TextParse( &s_dungeonText[i], s_mapTextBuf, digits[i] );
        C2D_TextOptimize( &s_dungeonText[i] );
    }

}

void Video_Fini()  { C2D_Fini(); C3D_Fini(); }

void Video_BeginFrame()
{
    C3D_FrameBegin( C3D_FRAME_SYNCDRAW );

    // Phase 12.2b: one slider read for the whole frame (see s_slider3D's
    // comment) -- lazily bring up the Ground/Fore buffers the first time
    // it's actually needed.
    s_slider3D = osGet3DSliderState();
    if ( s_slider3D > 0.0f && !s_layeredAllocTried )
        EnsureLayeredBuffersReady();
    s_layeredActive = ( s_slider3D > 0.0f ) && s_layeredBuffersOk;

    // World::Draw() renders into the offscreen 2x buffer, not the physical
    // top screen directly — see the s_topOffscreen comment above.
    // s_topOffscreen->frameBuf.width/height were corrected to the logical
    // 512x352 canvas at creation time (Video_Init), so plain C2D_SceneBegin
    // now sets up both the projection *and* the GPU viewport for that
    // logical size, not the padded 512x512 texture underneath — see that
    // frameBuf-override comment for why this matters (second attempt at
    // this rendered everything shrunk into the top-left corner without it).
    // C2D_TargetClear, NOT raw C3D_RenderTargetClear (phase 7.5a root
    // cause): C3D_RenderTargetClear takes the colour in raw framebuffer
    // byte order, not C2D_Color32's RGBA order -- C2D_TargetClear exists
    // precisely to __builtin_bswap32 it (confirmed in libcitro2d disassembly:
    // `rev` before C3D_FrameBufClear). Passing C2D_Color32(0,0,0,0xFF) raw
    // cleared to R=FF,G=0,B=0,A=0 -- *transparent red* instead of opaque
    // black. Any pixel the frame's draws didn't cover with opaque content
    // (mode-transition frames, the pre-FillBlack title backdrop, the 1px PAR
    // edge gap) showed red; the alpha=0 also made the final blit alpha-blend
    // those pixels through to s_top's equally-red clear. The bottom screen
    // always used C2D_TargetClear and never showed any of this.
    //
    // Phase 12.2b: when layered, s_topOffscreen (Mid) only receives Link +
    // regular objects, not the full room -- Ground now carries the
    // background separately, and Mid gets blitted *on top* of it on the
    // physical screen (standard alpha-composited layers, the same blending
    // drawRegion()'s transparent sprite pixels over opaque tiles already
    // relies on within a single buffer). So Mid's clear must be transparent
    // here, or it would blot out Ground's content wherever nothing else got
    // drawn. Not layered (today's exact behaviour, still the common case):
    // opaque black, since Mid alone carries the entire scene.
    C2D_TargetClear( s_topOffscreen, s_layeredActive ? C2D_Color32( 0, 0, 0, 0 ) : C2D_Color32( 0, 0, 0, 0xFF ) );
    if ( s_layeredActive )
    {
        // Ground is the backmost full-screen layer -- opaque black clear,
        // same reasoning as Mid's non-layered clear above (it alone needs a
        // well-defined background regardless of whether every pixel gets
        // drawn over). Fore is always composited on top of both, so it's
        // transparent like Mid.
        C2D_TargetClear( s_groundOffscreen, C2D_Color32( 0, 0, 0, 0xFF ) );
        C2D_TargetClear( s_foreOffscreen, C2D_Color32( 0, 0, 0, 0 ) );
    }
    C2D_SceneBegin( s_topOffscreen );
    C3D_SetScissor( GPU_SCISSOR_DISABLE, 0, 0, 0, 0 );
    clipActive = false;
    // See the s_topContentDrawn comment: everything between here and
    // Video_BlitTopScreen() is World::Draw()'s top-screen content, so any
    // draw call in that window marks the frame as non-empty.
    s_topContentDrawn = false;
}

// Draws the offscreen 2x buffer onto the real top screen: bilinear-filtered
// (smooths the crisp 2x pixels instead of a second nearest/jagged resample),
// 8:7 pixel-aspect-ratio corrected (~399x240, matches the classic NES-on-CRT
// convention), centred (the ~1px leftover margin from 400 is split evenly).
// Called once per frame, after World::Draw() has finished populating the
// offscreen buffer and before the bottom-screen map draws (which targets
// s_bottom, an entirely separate target — unaffected either way).
void Video_BlitTopScreen()
{
    // Phase 7.5a — SOLVED. The "solid red top screen on every
    // World::GetMode() transition" that survived C3D_FrameSplit/TexFlush/
    // FrameSync attempts and a full content-bisection was never a GPU race
    // or a drawing bug: both top-screen clears passed C2D_Color32 colours
    // straight into C3D_RenderTargetClear, which expects raw framebuffer
    // byte order — so "opaque black" was actually transparent red (see the
    // Video_BeginFrame comment for the full mechanics). Every finding of
    // the earlier investigation matched: deterministic, content-
    // independent (a frame whose draws don't opaquely cover the buffer
    // shows the clear itself), top-screen-only (bottom always used
    // C2D_TargetClear). Both clears now go through C2D_TargetClear; the
    // explicit C3D_FrameSplit(0) that used to sit here is gone because
    // C2D_TargetClear performs the same flush + C3D_FrameSplit(0)
    // internally (verified in libcitro2d disassembly), so the earlier
    // behaviour is preserved.

    // Phase 7.5a, second layer -- see the s_topContentDrawn comment at the
    // top of this file. On a frame where World::Draw() drew nothing at all,
    // leave s_top alone entirely (no clear, no blit): the screen keeps
    // showing the previous frame, exactly like the original console, so
    // there is no flicker of ANY colour on mode transitions. The one
    // exception is before the first real presented frame (s_top still holds
    // whatever the Homebrew Launcher left in VRAM -- the earlier
    // hardware bug below): until something has been presented once, run the
    // clear+blit even for an empty frame so boot shows black, not garbage.
    if ( !s_topContentDrawn && s_topEverPresented )
        return;
    s_topEverPresented = true;

    // Real hardware bug (a persistent ~1px coloured
    // border on both edges of the top screen, and a whole stale
    // Homebrew-Launcher frame ("ELIMINATION MODE" over launcher text)
    // visible before the first real frame). s_top (the *physical* top
    // screen target) was never actually cleared anywhere -- only
    // s_topOffscreen was, in Video_BeginFrame(). The blit below covers
    // ~399/400px (destW is a hair under 400 for the 8:7 PAR correction),
    // so any sub-pixel gap at the edges -- and any frame where s_top hasn't
    // been fully overdrawn yet -- was showing whatever was already sitting
    // in that VRAM, e.g. the Homebrew Launcher's own last-drawn frame.
    // Azahar always starts VRAM zeroed, so this was invisible there.
    // (The "red" colour of that border was this clear's own byte-order bug,
    // fixed — see Video_BeginFrame.)
    const float destH = 240.0f;
    const float destW = 400.0f;
    // Real hardware bug (a persistent 1px coloured
    // column on both edges, during normal gameplay too, tracked to a red
    // colour bleeding through this exact gap). The 8:7-PAR-correct width
    // (~399.0/400) leaves a fractional gap outside this blit that the
    // C3D_RenderTargetClear above should cover, but evidently doesn't
    // reliably enough on real hardware. Simplest fix: don't leave a gap to leak through at all -- fill the
    // full 400px. The resulting stretch (399->400, <0.3%) is imperceptible,
    // and removes the edge column as a place anything can show through.
    // (phase 12.2: the left/right eyes below each shift by a small offset
    // around this same centred destX -- see the parallax comment further
    // down.)
    const float destX = 0.0f;

    // UV rect: only the top-left TopOffscreenW x TopOffscreenH of the padded
    // TopOffscreenTexW x TopOffscreenTexH physical texture holds real content
    // (see the pow2-padding comment on TopOffscreenTexW/H) — same "logical
    // size within a padded pow2 texture" UV math loadIndexed()/drawRegion()
    // already use for tile sheets. Render-target textures are also stored
    // bottom-up relative to how C2D_Image expects to sample them, hence the
    // top/bottom flip (top=1 = row 0 of what was drawn, not 0..1 top-to-bottom).
    Tex3DS_SubTexture st;
    st.width  = TopOffscreenW;
    st.height = TopOffscreenH;
    st.left   = 0.0f;
    st.right  = (float) TopOffscreenW / (float) TopOffscreenTexW;
    st.top    = 1.0f;
    // Half-texel inset on the bottom edge only (with the
    // GPU_LINEAR restore): content row 351 borders the never-cleared pow2
    // padding rows (352-511; the clear only covers frameBuf's 512x352), and
    // exactly-on-the-boundary UVs would let bilinear sampling bleed that
    // garbage into the bottom output row. The other three edges clamp at
    // real texture borders and need no inset. Costs a 0.14% vertical
    // resample shift, imperceptible.
    st.bottom = 1.0f - ( (float) TopOffscreenH - 0.5f ) / (float) TopOffscreenTexH;
    C2D_Image imgMid = { &s_topOffscreenTex, &st };

    const float scaleX = destW / (float) TopOffscreenW;
    const float scaleY = destH / (float) TopOffscreenH;

    // Phase 12.2b: s_layeredActive was decided once for the whole frame in
    // Video_BeginFrame() (3D on, and the Ground/Fore buffers exist). When
    // false -- 3D off, *or* the lazy buffer allocation failed this session
    // (graceful degrade, see EnsureLayeredBuffersReady()) -- this is exactly
    // the original single-target, zero-offset blit: no stereo at all, and
    // the old-3DS 3D-off 60fps guarantee holds because none of the extra
    // clears/blits below ever run.
    if ( !s_layeredActive )
    {
        C2D_TargetClear( s_top, C2D_Color32( 0, 0, 0, 0xFF ) );
        C2D_SceneBegin( s_top );
        C2D_DrawImageAt( imgMid, destX, 0.0f, 0.0f, nullptr, scaleX, scaleY );
        return;
    }

    C2D_Image imgGround = { &s_groundOffscreenTex, &st };
    C2D_Image imgFore   = { &s_foreOffscreenTex,   &st };

    // s_slider3D was cached once in Video_BeginFrame() -- see that comment
    // for why this doesn't re-read osGet3DSliderState() here. Ground and
    // Fore each scale independently (Fore's smaller max, see
    // kForeParallaxPx's comment); Mid gets no offset at all, on purpose --
    // it's the screen-plane anchor, exactly where "3D off" already draws it.
    float groundOffset = kGroundParallaxPx * 0.5f * s_slider3D;
    float foreOffset   = kForeParallaxPx   * 0.5f * s_slider3D;

    // Each eye blits all three layers back-to-front (Ground, then Mid, then
    // Fore) so the transparent pixels of the nearer layers let the ones
    // beneath show through -- the same alpha compositing drawRegion()
    // already relies on for sprites-over-tiles within a single buffer (see
    // Video_BeginFrame's clear-colour comment for why Mid/Fore are cleared
    // transparent here instead of opaque). Left eye: Ground shifts left
    // (uncrossed disparity, recedes), Fore shifts right (crossed disparity,
    // pops out -- opposite sign from Ground since it's in front of the
    // screen plane, not behind it). Right eye: mirrored.
    // Premultiplied-alpha compositing for these six blits, restored to
    // citro3d's normal straight-alpha immediately afterwards.
    //
    // This is the dark outline that kept showing around everything in the
    // Mid layer -- a thin line between the layers, worst under sprites. It is
    // not a depth or layout problem at all, it is what straight-alpha blending
    // does to a bilinear-filtered edge. Mid and Fore are cleared to
    // (0,0,0, alpha 0), so a texel sampled halfway between an opaque sprite
    // pixel and the cleared void interpolates to half that colour AND half
    // alpha. Blended the normal way (src.rgb * src.a) that half-colour gets
    // halved a second time -- a green edge texel composites as a quarter-
    // strength green over the terrain instead of half, i.e. a dark rim. The
    // vertical rim reads worst because the final blit squashes 352 rows into
    // 240 (scaleY 0.68) against 512->400 horizontally, so it smears further.
    //
    // Premultiplied blending (GPU_ONE instead of GPU_SRC_ALPHA) multiplies
    // exactly once and lands on the right colour. The layer buffers are
    // already valid premultiplied data without changing how anything is drawn
    // into them: tile textures sample with GPU_NEAREST, so every texel in
    // there is either fully opaque or fully transparent-with-RGB-zero. The
    // interpolation that needs fixing happens later, at this blit.
    //
    // Safe to set directly: citro2d never touches the blend function itself
    // (checked -- neither C2D_Prepare nor C2Di_Update calls C3D_AlphaBlend),
    // so this is not fighting the library for state. The restore matters
    // though: the bottom screen genuinely uses semi-transparent fills (the
    // fog-of-war cells, the dimming overlays) and those need straight alpha.
    C3D_AlphaBlend( GPU_BLEND_ADD, GPU_BLEND_ADD,
                    GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA,
                    GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA );

    C2D_TargetClear( s_top, C2D_Color32( 0, 0, 0, 0xFF ) );
    C2D_SceneBegin( s_top );
    C2D_DrawImageAt( imgGround, destX - groundOffset, 0.0f, 0.0f, nullptr, scaleX, scaleY );
    C2D_DrawImageAt( imgMid,    destX,                0.0f, 0.0f, nullptr, scaleX, scaleY );
    C2D_DrawImageAt( imgFore,   destX + foreOffset,   0.0f, 0.0f, nullptr, scaleX, scaleY );

    C2D_TargetClear( s_topRight, C2D_Color32( 0, 0, 0, 0xFF ) );
    C2D_SceneBegin( s_topRight );
    C2D_DrawImageAt( imgGround, destX + groundOffset, 0.0f, 0.0f, nullptr, scaleX, scaleY );
    C2D_DrawImageAt( imgMid,    destX,                0.0f, 0.0f, nullptr, scaleX, scaleY );
    C2D_DrawImageAt( imgFore,   destX - foreOffset,   0.0f, 0.0f, nullptr, scaleX, scaleY );

    // Flush before restoring: the blend function is global GPU state applied
    // at draw time, so leaving these six quads queued past the restore would
    // silently render them with the mode we just switched away from.
    C2D_Flush();
    C3D_AlphaBlend( GPU_BLEND_ADD, GPU_BLEND_ADD,
                    GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                    GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA );
}

void Video_EndFrame() { C3D_FrameEnd( 0 ); }

//---------------------------------------------------------------------------
// Bottom-screen live map. First-quest dungeon entrance screens (row*16+col).
//---------------------------------------------------------------------------
// kDungeonRooms / PoiKind / the kPoiRooms room->category table all now live
// in LiveMapGeometry.h/.cpp (phase 10.5) -- shared with LiveMap_state.cpp's
// touch hit-test (tap a POI/dungeon badge for an info card) so renderer and
// hit-test can't drift apart. See that file for what each PoiKind means and
// where its room list came from (phase 10.3 has the full data-source
// writeup).

// One colour per PoiKind -- shared by the map badges (Video_DrawBottomMap)
// and the legend swatches (DrawLegendOverlay) so the two can't drift apart,
// same reasoning as every other shared-constant pair in this file.
static const u32 kPoiColors[Poi_Count] =
{
    C2D_Color32( 0xD8, 0xA8, 0x50, 0xFF ),   // Shop     -- warm tan
    C2D_Color32( 0xE0, 0x60, 0xC0, 0xFF ),   // Potion   -- magenta
    C2D_Color32( 0xE8, 0x90, 0x30, 0xFF ),   // Repair   -- orange
    C2D_Color32( 0x90, 0x60, 0xE0, 0xFF ),   // Tunnel   -- purple
    C2D_Color32( 0xE8, 0x50, 0x50, 0xFF ),   // Gift     -- red (possible heart container)
    C2D_Color32( 0x60, 0xE8, 0xE0, 0xFF ),   // Fairy    -- cyan
    C2D_Color32( 0x50, 0x90, 0xE8, 0xFF ),   // FluteWarp-- blue
};

// Coarse Hyrule biome map (16x8). Visited cells reveal their terrain colour,
// so exploring "uncovers" the map. M=mountain F=forest D=dark woods P=plains
// G=graveyard W=water S=sand.
static const char* kBiomeMap[LIVEMAP_ROWS] =
{
    "MMMMMMMMMMMMMMMW",
    "MMMMMMMMMMMMMMMW",
    "MMMMMFFFPPPPPPFW",
    "PPPPWWFPPPPPFFPW",
    "MPFFWWWPPPPPPPFW",
    "PGGPPFFPPPPPPFFW",
    "DDDFFFPPPPPPPDDW",
    "SSSSPPPSSSSPPSSW",
};

static u32 BiomeColor( int room )
{
    int r = room / LIVEMAP_COLS, c = room % LIVEMAP_COLS;
    switch ( kBiomeMap[r][c] )
    {
    case 'M': return C2D_Color32( 0x8A, 0x6E, 0x52, 0xFF );   // mountains
    case 'F': return C2D_Color32( 0x3F, 0x7A, 0x3C, 0xFF );   // forest
    case 'D': return C2D_Color32( 0x2C, 0x5A, 0x30, 0xFF );   // dark woods
    case 'P': return C2D_Color32( 0x58, 0x94, 0x4E, 0xFF );   // plains
    case 'G': return C2D_Color32( 0x8A, 0x8A, 0x92, 0xFF );   // graveyard
    case 'W': return C2D_Color32( 0x3E, 0x6F, 0xA6, 0xFF );   // water
    case 'S': return C2D_Color32( 0xC7, 0xB0, 0x78, 0xFF );   // sand
    }
    return C2D_Color32( 0x58, 0x94, 0x4E, 0xFF );
}

// Dungeon (underworld) floor plan — the core bottom-screen view
// while inside a numbered level. Direction bits below deliberately mirror
// Global.h's Direction enum (Right=1, Left=2, Down=4, Up=8) without including
// it: this renderer stays free of World.h/game types (see livemap.h), so the
// bridge (LiveMap_state.cpp) resolves door types into these plain bitmasks.
enum { UwDir_Right = 1, UwDir_Left = 2, UwDir_Down = 4, UwDir_Up = 8 };

// Visual language for the bottom-screen map, restyled after A Link Between
// Worlds' touch-screen map (warm wood frame, gold-rimmed rounded badges,
// round "you are here" marker) instead of the earlier flat squares-on-black
// look. All shapes stay cheap citro2d primitives — no new textures/art.

// citro2d's own C2D_DrawEllipse/C2D_DrawCircleSolid silently draws nothing
// under Azahar's Vulkan backend (verified with a giant opaque test circle —
// it never appeared, while rects/triangles/text all render fine). So circles
// here are hand-rolled as a triangle fan instead of relying on citro2d's
// built-in (and apparently broken-on-this-backend) ellipse path.
static void DrawFilledCircle( float cx, float cy, float r, u32 color )
{
    const int segments = 12;
    float prevX = cx + r, prevY = cy;
    for ( int i = 1; i <= segments; i++ )
    {
        float ang = (6.2831853f * i) / segments;
        float x = cx + r * cosf( ang );
        float y = cy + r * sinf( ang );
        C2D_DrawTriangle( cx, cy, color, prevX, prevY, color, x, y, color, 0.0f );
        prevX = x; prevY = y;
    }
}

// Classic two-lobe, pointed heart built from two circles + a triangle body.
// Small (~12-14px) but reads clearly, matching the original NES heart icons
// more closely than a plain square ever did.
static void DrawHeart( float cx, float cy, float r, u32 color )
{
    C2D_DrawTriangle( cx - r, cy - 0.15f * r, color,
                       cx + r, cy - 0.15f * r, color,
                       cx,     cy + r,         color, 0.0f );
    DrawFilledCircle( cx - r * 0.5f, cy - 0.2f * r, r * 0.62f, color );
    DrawFilledCircle( cx + r * 0.5f, cy - 0.2f * r, r * 0.62f, color );
}

// 4-sided rectangle border of the given thickness — a plain, low-detail
// alternative to DrawGoldFrame's rounded corners, used for the persistent
// header's toggle buttons and the Items screen's slots (phase 9.x: the first
// concept pass was too small/detailed for a ~320x240 screen, so this whole
// screen intentionally keeps its shapes plain).
static void DrawRectOutline( float x, float y, float w, float h, float t, u32 color )
{
    C2D_DrawRectSolid( x, y, 0, w, t, color );
    C2D_DrawRectSolid( x, y + h - t, 0, w, t, color );
    C2D_DrawRectSolid( x, y, 0, t, h, color );
    C2D_DrawRectSolid( x + w - t, y, 0, t, h, color );
}

// Cheap deterministic pseudo-random wood-grain texture (the flat
// single-colour wood background and buttons looked bare). Seeded by position
// (not frame count), so the same streaks land every frame -- reads as a
// fixed texture rather than shimmering static. citro2d has no procedural-
// noise primitive, so this is just a bunch of thin filled rects at hashed
// positions -- cheap enough to call per-panel per-frame (tens of draws,
// same order of magnitude as everything else already drawn here).
//
// First attempt scattered small square dots -- it did not look like wood:
// isotropic dot noise reads as static/
// film-grain, not wood. Real wood grain is anisotropic: long, thin,
// mostly-parallel fibres running one direction, each a slightly different
// shade, often not spanning the whole board. This redraws each "grain" as a
// short-to-long near-horizontal streak (randomised length/position/shade
// instead of a uniform dot) to get that longitudinal look with the same
// per-panel draw-call budget as before (no frametime regression expected).
static uint32_t SpeckleHash( uint32_t x )
{
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static void DrawSpeckleField( float x, float y, float w, float h, int count, uint32_t seed, u32 color )
{
    u8 baseA = (u8) ( (color >> 24) & 0xFF );
    u32 rgb  = color & 0x00FFFFFFu;
    for ( int i = 0; i < count; i++ )
    {
        uint32_t hLen   = SpeckleHash( seed * 2u + 1u + (uint32_t) i * 7919u );
        uint32_t hPos   = SpeckleHash( seed * 3u + 5u + (uint32_t) i * 104729u );
        uint32_t hShade = SpeckleHash( seed * 5u + 9u + (uint32_t) i * 200003u );

        // Length: 20%-75% of the panel width -- fibres of varied length,
        // most not spanning the whole board (a full-width line every time
        // would look like ruled paper, not grain).
        float lineW = w * ( 0.20f + (hLen % 1000u) / 1000.0f * 0.55f );
        float px    = x + (hPos % 1000u) / 1000.0f * ( w - lineW );
        float py    = y + ( (hPos / 1000u) % 1000u ) / 1000.0f * h;
        // v2 (thinner, 0.45-0.75px) still read as individual drawn lines --
        // so a blur instead of yet another thinness tweak. citro2d has no blur/
        // post-process pass to reach for, so this fakes one the cheap way:
        // wide, low-alpha soft bands (no crisp core rect any more) that
        // overlap and accumulate through ordinary alpha blending -- many
        // faint blobs stacking into a hazy band reads as blurred grain,
        // where one thin high-contrast rect reads as a drawn line. Same
        // single-draw-per-streak budget as v1/v2, just wider + fainter.
        float thick = 1.6f + ( (hLen >> 10) & 1u ? 1.4f : 0.0f );   // 1.6 or 3.0px

        // Shade variance: some fibres darker, some lighter than the base
        // colour, by scaling alpha -- cheaper than blending a second colour
        // and gives the "some grain lines stand out more" look real wood
        // has. Lower ceiling than v1/v2 -- these bands are wide, so full
        // opacity would look like solid stripes, not haze.
        float shadeScale = 0.25f + (hShade % 1000u) / 1000.0f * 0.45f;   // 0.25x-0.70x
        u8 a = (u8) fminf( 255.0f, baseA * shadeScale );
        u32 streakColor = rgb | ( (u32) a << 24 );

        C2D_DrawRectSolid( px, py, 0, lineW, thick, streakColor );
    }
}

// Draws a resolved item icon (phase 9.x Items screen / equip badges),
// centred at (cx, cy). Uses the same Graphics::DrawSpriteTile path normal
// gameplay drawing uses for items — safe to call here only because
// Video_DrawBottomMap resets the view transform to identity first (that
// path is otherwise tuned for the top screen's play-view scale). No-op if
// nothing is resolved for this slot (animId < 0 — see livemap.h).
static void DrawItemIcon( const LiveMapIcon& icon, float cx, float cy )
{
    if ( icon.animId < 0 )
        return;
    const SpriteAnim* anim = Graphics::GetAnimation( Sheet_PlayerAndItems, icon.animId );
    if ( anim == nullptr )
        return;
    float x = cx - anim->width / 2.0f;
    float y = cy - anim->height / 2.0f;
    Graphics::DrawSpriteTile( Sheet_PlayerAndItems,
        anim->frames[0].x, anim->frames[0].y, anim->width, anim->height,
        (int) x, (int) y, icon.palette, anim->frames[0].flags );
}

// Small local digit renderer for the Items screen's rupee/key counts (Sheet_
// Font, palette 0 — the exact sheet+palette StatusBar.cpp's original HUD
// counters use, confirmed via DrawCount() there, so this is a known-good
// combination). Not routed through ItemObj.h's DrawString/DrawChar: this
// file stays free of game-logic headers (see the livemap.h file comment)
// and the tile math is only two lines.
static void DrawDigitChar( int digit, float x, float y )
{
    int srcX = (digit & 0x0F) * 8;
    int srcY = (digit & 0xF0) / 2;
    Graphics::DrawTile( Sheet_Font, srcX, srcY, 8, 8, (int) x, (int) y, 0, 0 );
}

static void DrawNumber( int value, float x, float y )
{
    uint8_t digits[3];
    int n = 0;
    if ( value <= 0 )
    {
        digits[n++] = 0;
    }
    else
    {
        while ( value > 0 && n < 3 )
        {
            digits[n++] = value % 10;
            value /= 10;
        }
    }
    for ( int i = 0; i < n; i++ )
        DrawDigitChar( digits[n - 1 - i], x + i * 8.0f, y );
}

// Same idea as DrawNumber() but up to 6 digits (phase 13.3: stats like total
// play minutes or a long-playthrough kill count can realistically exceed
// DrawNumber's 3-digit/999 cap, which silently truncates -- fine for rupee/
// key counts (uint8_t, max 255) but not here).
static void DrawNumberWide( uint32_t value, float x, float y )
{
    uint8_t digits[6];
    int n = 0;
    if ( value == 0 )
    {
        digits[n++] = 0;
    }
    else
    {
        while ( value > 0 && n < 6 )
        {
            digits[n++] = (uint8_t) (value % 10);
            value /= 10;
        }
    }
    for ( int i = 0; i < n; i++ )
        DrawDigitChar( digits[n - 1 - i], x + i * 8.0f, y );
}

// Compact heart counter (phase 15.x, replaces the earlier silver/gold/red
// "bundle" group). Two problems with the old group:
// (1) at exactly 4 heart containers its computed width ran into the OPTIONS
// tab, and (2) the bundle abstraction (1 silver = 10 hearts etc.) made it
// impossible to read the exact count. A per-container heart *row* isn't an
// option either — the header slot right of the three tabs is only ~60px
// wide, nowhere near enough for up to 16 individual hearts. So instead of a
// row, two stacked glyph+count lines that stay a fixed tiny size no matter
// how many containers Link has: a red heart with the number of full hearts,
// and below it a black heart with the number still empty/lost. A half heart
// (transient, only mid-damage) counts toward the empty line.
static void DrawHeartCount( const LiveMapState& state )
{
    if ( !state.playing || state.totalHearts == 0 )
        return;

    const u32 clrRed   = C2D_Color32( 0xE8, 0x38, 0x38, 0xFF );
    const u32 clrBlack = C2D_Color32( 0x3A, 0x2A, 0x22, 0xFF );

    int total = state.totalHearts;
    int full  = state.fullHearts;
    if ( full > total ) full = total;
    int empty = total - full;   // a half-heart container is not full -> counted here

    const float r = 6.0f;
    const float heartX = 268.0f;         // left of the two stacked hearts
    const float numX   = heartX + r + 6.0f;
    const float topY   = 26.0f;          // red line (full)
    const float botY   = 42.0f;          // black line (empty)

    DrawHeart( heartX, topY, r, clrRed );
    DrawNumber( full,  numX, topY - 4.0f );

    DrawHeart( heartX, botY, r, clrBlack );
    DrawNumber( empty, numX, botY - 4.0f );
}

// Persistent header (phase 9.x bottom-screen remake, approved concept):
// equipped weapons left, MAP/ITEMS view toggle centre (also touch-tappable —
// LIVEMAP_TAB_* rects in livemap.h, hit-tested in LiveMap_state.cpp),
// grouped hearts right. Drawn every frame regardless of which screen (map or
// items) is active below it, so loadout/health are never hidden by a view
// switch.
static void DrawHeader( const LiveMapState& state, u32 clrGold, u32 clrText )
{
    const u32 clrBadgeBg = C2D_Color32( 0x2A, 0x20, 0x18, 0xFF );
    const u32 clrActiveText = C2D_Color32( 0x24, 0x1A, 0x08, 0xFF );

    // Equipped weapons, left — gold-ringed circular badges (same "bigger
    // disc behind a smaller one" ring trick DrawDungeonMap's number badges
    // use) with the real item sprite centred inside. A (sword) first, then
    // B and Y (phase 8.1's second item-use button) — added because a
    // picked-up sword wasn't reflected anywhere once the top HUD was hidden. Smaller/tighter than an earlier 2-badge layout to fit a
    // third badge in the same header row.
    const LiveMapIcon* equipped[3] = { &state.equippedA, &state.equippedB, &state.equippedY };
    const C2D_Text* tags[3] = { &s_tagA, &s_tagB, &s_tagY };
    float ex = 20.0f;
    for ( int i = 0; i < 3; i++ )
    {
        DrawFilledCircle( ex, 33.0f, 13.0f, clrGold );
        DrawFilledCircle( ex, 33.0f, 11.0f, clrBadgeBg );
        DrawItemIcon( *equipped[i], ex, 33.0f );
        C2D_DrawText( tags[i], C2D_WithColor, ex - 3.0f, 48.0f, 0.0f, 0.38f, 0.38f, clrGold );
        ex += 28.0f;
    }

    // MAP / ITEMS / OPTIONS toggle, centre (phase 11.1 added the third tab).
    const float tabX[3] = { LIVEMAP_TAB_MAP_X, LIVEMAP_TAB_ITEMS_X, LIVEMAP_TAB_OPTIONS_X };
    const C2D_Text* tabLabel[3] = { &s_tabMap, &s_tabItems, &s_tabOptions };
    const float tabTextOffsetX[3] = { 12.0f, 8.0f, 6.0f };
    for ( int i = 0; i < 3; i++ )
    {
        bool active = state.view == i;
        C2D_DrawRectSolid( tabX[i], LIVEMAP_TAB_Y, 0, LIVEMAP_TAB_W, LIVEMAP_TAB_H,
                           active ? clrGold : clrBadgeBg );
        // Speckle contrast flips with the fill: dark flecks on the bright
        // gold "active" fill, light flecks on the dark "inactive" fill.
        DrawSpeckleField( tabX[i], LIVEMAP_TAB_Y, LIVEMAP_TAB_W, LIVEMAP_TAB_H, 18,
                         0x7A00u + (uint32_t) i,
                         active ? C2D_Color32( 0, 0, 0, 0x50 ) : C2D_Color32( 0x70, 0x58, 0x38, 0x70 ) );
        if ( !active )
            DrawRectOutline( tabX[i], LIVEMAP_TAB_Y, LIVEMAP_TAB_W, LIVEMAP_TAB_H, 2.0f, clrGold );
        C2D_DrawText( tabLabel[i], C2D_WithColor, tabX[i] + tabTextOffsetX[i],
                     LIVEMAP_TAB_Y + 7.0f, 0.0f, 0.42f, 0.42f, active ? clrActiveText : clrText );
    }

    // Hearts, right of the tabs — two stacked glyph+count lines (red=full,
    // black=empty), see DrawHeartCount for why a per-heart row won't fit.
    DrawHeartCount( state );

    // Live FPS counter.
    // In the shared header, not just the Options screen: Options now pauses
    // the game (World::SetPaused(), LiveMap_state.cpp) since Start/Select
    // became the map zoom, so a counter that only lived on the Options
    // screen could only ever show the FPS of a frozen game -- useless
    // exactly where the real slowdowns were (live gameplay, browsing the
    // Map). Here it's visible on every view, paused or not.
    // DrawNumberWide (tile-based digits), not C2D_TextParse, for the number
    // -- see the comment on DrawStatsScreen for why a per-frame text-parse
    // is a real cost to avoid; only the static "FPS" label uses C2D_DrawText.
    // y=2: every other header element starts at y=20, so this strip is clear.
    // Options::Get().ShowFpsCounter -- off by default, same
    // preserve-classic-behaviour-unless-opted-in convention every other
    // GameOptions field follows.
    if ( Options::Get().ShowFpsCounter )
    {
        uint32_t fpsRounded = (uint32_t) (state.fps + 0.5f);
        C2D_DrawText( &s_fpsLabel, C2D_WithColor, 258.0f, 2.0f,
                     0.0f, 0.36f, 0.36f, clrText );
        DrawNumberWide( fpsRounded, 286.0f, 1.0f );
    }
}

// Items/inventory screen (phase 9.x): a 5x3 grid of the classic quest's
// ownable items (fixed order — see LIVEMAP_ITEM_GRID_SLOTS / LiveMap_state.
// cpp's s_gridSlots) plus rupee/key counts. Dashed-look empty outline for
// slots not yet owned. Equipped B/Y items aren't repeated here — the header
// above already shows them on every screen.
static void DrawItemsScreen( const LiveMapState& state, u32 clrGold )
{
    const u32 clrBadgeBg     = C2D_Color32( 0x2A, 0x20, 0x18, 0xFF );
    const u32 clrEmptyBorder = C2D_Color32( 0x6B, 0x5C, 0x3C, 0x80 );
    const u32 clrRupee       = C2D_Color32( 0x5C, 0xC2, 0x6A, 0xFF );

    // Geometry shared with LiveMap_state.cpp's touch-to-assign hit-test
    // (phase 8.2) via livemap.h — must stay in lockstep with where a tap
    // actually lands, so these come from the shared constants, not local ones.
    const int   cols = LIVEMAP_ITEM_GRID_COLS, rows = LIVEMAP_ITEM_GRID_ROWS;
    const float gx = LIVEMAP_ITEM_GRID_X, gy = LIVEMAP_ITEM_GRID_Y;
    const float gw = LIVEMAP_ITEM_GRID_W, gh = LIVEMAP_ITEM_GRID_H;
    const float cw = gw / cols, ch = gh / rows;
    const float pad = 4.0f;

    for ( int i = 0; i < LIVEMAP_ITEM_GRID_SLOTS; i++ )
    {
        int col = i % cols, row = i / cols;
        float x = gx + col * cw, y = gy + row * ch;
        const LiveMapIcon& icon = state.itemGrid[i];

        if ( icon.animId >= 0 )
        {
            C2D_DrawRectSolid( x + pad, y + pad, 0, cw - pad * 2, ch - pad * 2, clrBadgeBg );
            // phase 11.1c: same per-element wood-grain treatment already used
            // on OPT/SLOT rows -- the Items grid's badges were still flat,
            // one of the two remaining plain spots noted there. A first
            // attempt at 4/slot (up to 60 calls with every slot full)
            // measured live at bottomMap up to 6.5ms -- same per-call cost
            // problem the map grid hit (see that change's comment). Cut to
            // 1/slot (15 calls worst case); measured back within budget.
            // phase 11.1c: same per-element wood-grain treatment already used
            // on OPT/SLOT rows -- the Items grid's badges were still flat,
            // one of the two remaining plain spots noted there. A first
            // attempt at 4/slot measured a bottomMap spike, but an A/B test
            // (speckle fully disabled) reproduced the exact same spike --
            // it's a pre-existing cost of a fully-stocked Items screen
            // (DrawItemIcon x15), not this addition (see phase
            // 11.1c). Kept at a cheap 1/slot regardless, confirmed to add
            // near-zero cost on top of that pre-existing baseline either way.
            DrawSpeckleField( x + pad, y + pad, cw - pad * 2, ch - pad * 2, 1,
                             0x9100u + (uint32_t) i, C2D_Color32( 0x70, 0x58, 0x30, 0x60 ) );
            DrawRectOutline( x + pad, y + pad, cw - pad * 2, ch - pad * 2, 2.0f, clrGold );
            DrawItemIcon( icon, x + cw / 2.0f, y + ch / 2.0f );
        }
        else
        {
            DrawRectOutline( x + pad, y + pad, cw - pad * 2, ch - pad * 2, 2.0f, clrEmptyBorder );
        }
    }

    // Rupee / key / SAVE, three pills in a row — geometry shared with
    // LiveMap_state.cpp's touch hit-tests via livemap.h.
    DrawRectOutline( LIVEMAP_RUPEE_PILL_X, LIVEMAP_PILL_Y, LIVEMAP_PILL_W, LIVEMAP_PILL_H, 2.0f, clrGold );
    DrawFilledCircle( LIVEMAP_RUPEE_PILL_X + 18.0f, LIVEMAP_PILL_Y + LIVEMAP_PILL_H / 2.0f, 7.0f, clrRupee );
    DrawNumber( state.curRupees, LIVEMAP_RUPEE_PILL_X + 34.0f, LIVEMAP_PILL_Y + 7.0f );

    DrawRectOutline( LIVEMAP_KEY_PILL_X, LIVEMAP_PILL_Y, LIVEMAP_PILL_W, LIVEMAP_PILL_H, 2.0f, clrGold );
    DrawFilledCircle( LIVEMAP_KEY_PILL_X + 16.0f, LIVEMAP_PILL_Y + LIVEMAP_PILL_H / 2.0f, 6.0f, clrGold );
    DrawNumber( state.keyCount, LIVEMAP_KEY_PILL_X + 32.0f, LIVEMAP_PILL_Y + 7.0f );

    // SAVE — the classic top-screen pause menu's Up+A manual save (phase 8.5)
    // moved here: 3DS no longer opens that menu at all (see
    // Graphics::ShouldUseClassicPauseMenu()). Filled solid like the
    // header's active tab, not just outlined, so it reads as a real button.
    DrawRectOutline( LIVEMAP_SAVE_BTN_X, LIVEMAP_PILL_Y, LIVEMAP_PILL_W, LIVEMAP_PILL_H, 2.0f, clrGold );
    C2D_DrawText( &s_saveLabel, C2D_WithColor, LIVEMAP_SAVE_BTN_X + 24.0f, LIVEMAP_PILL_Y + 6.0f,
                 0.0f, 0.48f, 0.48f, clrGold );

    if ( state.saveFlashFrames > 0 )
    {
        u32 a = state.saveFlashFrames > 20 ? 0xFF : (u32) (state.saveFlashFrames * 0xFF / 20);
        C2D_DrawRectSolid( LIVEMAP_SAVE_BTN_X, LIVEMAP_PILL_Y, 0, LIVEMAP_PILL_W, LIVEMAP_PILL_H,
                           C2D_Color32( 0xF8, 0xD0, 0x40, a ) );
        C2D_DrawText( &s_savedLabel, C2D_WithColor, LIVEMAP_SAVE_BTN_X + 15.0f, LIVEMAP_PILL_Y + 6.0f,
                     0.0f, 0.48f, 0.48f, C2D_Color32( 0x24, 0x1A, 0x08, a ) );
    }
}

// Options screen (phase 11.1): a real, tappable settings screen replacing
// the Start/Y+Start/B+Start combos phase 11.2-11.4 shipped behind
// temporarily (button combos were only ever a stopgap). One row per
// GameOptions field, read straight
// from Options::Get() -- no LiveMapState plumbing needed since Options is
// already a globally accessible singleton. Row/segment geometry comes from
// the same LIVEMAP_OPT_* constants LiveMap_state.cpp hit-tests against, so
// drawing and touch can't drift apart.
static void DrawOptionsScreen( const LiveMapState& state, u32 clrGold, u32 clrText )
{
    const u32 clrBadgeBg    = C2D_Color32( 0x2A, 0x20, 0x18, 0xFF );
    const u32 clrActiveText = C2D_Color32( 0x24, 0x1A, 0x08, 0xFF );
    const u32 clrRowBg      = C2D_Color32( 0x20, 0x18, 0x10, 0xFF );

    GameOptions& options = Options::Get();
    const uint8_t* values[LIVEMAP_OPT_TOGGLE_ROWS] =
    {
        &options.TextSpeed, &options.LowHealthBeep, &options.SwordBeamStrict,
        &options.TextVariant, &options.SkipTitleScreen, &options.ShowSecretHints,
        &options.ShowFpsCounter
    };
    const C2D_Text* segOff[LIVEMAP_OPT_TOGGLE_ROWS] =
        { &s_segNormal, &s_segNormal, &s_segNormal, &s_segOriginal, &s_segShow, &s_segOff, &s_segOff };
    const C2D_Text* segOn[LIVEMAP_OPT_TOGGLE_ROWS] =
        { &s_segFast, &s_segOff, &s_segStrict, &s_segClear, &s_segSkip, &s_segShow, &s_segShow };

    // Scrollable viewport (so rows don't shrink with every new option) --
    // only LIVEMAP_OPT_VISIBLE_ROWS of the LIVEMAP_OPT_ROWS logical entries
    // are drawn per frame, at state.optScroll..+VISIBLE_ROWS. The 4 action
    // rows (MANUAL/MUSIC/STATS/SAVE SLOTS, in that order) now
    // come first, logical index 0..LIVEMAP_OPT_ACTION_ROWS-1, with the 7
    // toggle fields after -- the reverse of their original order.
    for ( int i = 0; i < LIVEMAP_OPT_VISIBLE_ROWS; i++ )
    {
        int logical = state.optScroll + i;
        if ( logical >= LIVEMAP_OPT_ROWS )
            break;

        float rowY = LIVEMAP_OPT_ROW0_Y + i * (LIVEMAP_OPT_ROW_H + LIVEMAP_OPT_ROW_GAP);

        C2D_DrawRectSolid( LIVEMAP_OPT_AREA_X, rowY, 0, LIVEMAP_OPT_AREA_W, LIVEMAP_OPT_ROW_H, clrRowBg );
        DrawSpeckleField( LIVEMAP_OPT_AREA_X, rowY, LIVEMAP_OPT_AREA_W, LIVEMAP_OPT_ROW_H, 26,
                         0x7B00u + (uint32_t) logical, C2D_Color32( 0x70, 0x58, 0x30, 0x70 ) );
        DrawRectOutline( LIVEMAP_OPT_AREA_X, rowY, LIVEMAP_OPT_AREA_W, LIVEMAP_OPT_ROW_H, 1.5f, clrGold );

        if ( logical < LIVEMAP_OPT_ACTION_ROWS )   // one of the 4 action rows -- no segments, just a tap target
        {
            C2D_DrawText( &s_optActionLabels[logical], C2D_WithColor,
                         LIVEMAP_OPT_AREA_X + LIVEMAP_OPT_AREA_W / 2.0f - 44.0f, rowY + 6.0f,
                         0.0f, 0.42f, 0.42f, clrGold );
            continue;
        }

        int toggleIndex = logical - LIVEMAP_OPT_ACTION_ROWS;
        C2D_DrawText( &s_optLabels[toggleIndex], C2D_WithColor, LIVEMAP_OPT_AREA_X + 10.0f, rowY + 11.0f,
                     0.0f, 0.42f, 0.42f, clrText );

        float segY = rowY + (LIVEMAP_OPT_ROW_H - LIVEMAP_OPT_SEG_H) / 2.0f;
        float leftSegX  = LIVEMAP_OPT_AREA_X + LIVEMAP_OPT_AREA_W - 2 * LIVEMAP_OPT_SEG_W;
        float rightSegX = leftSegX + LIVEMAP_OPT_SEG_W;
        bool  isOn = *values[toggleIndex] != 0;

        C2D_DrawRectSolid( leftSegX, segY, 0, LIVEMAP_OPT_SEG_W, LIVEMAP_OPT_SEG_H,
                           !isOn ? clrGold : clrBadgeBg );
        C2D_DrawText( segOff[toggleIndex], C2D_WithColor, leftSegX + 6.0f, segY + 6.0f,
                     0.0f, 0.36f, 0.36f, !isOn ? clrActiveText : clrText );

        C2D_DrawRectSolid( rightSegX, segY, 0, LIVEMAP_OPT_SEG_W, LIVEMAP_OPT_SEG_H,
                           isOn ? clrGold : clrBadgeBg );
        C2D_DrawText( segOn[toggleIndex], C2D_WithColor, rightSegX + 6.0f, segY + 6.0f,
                     0.0f, 0.36f, 0.36f, isOn ? clrActiveText : clrText );

        DrawRectOutline( leftSegX, segY, LIVEMAP_OPT_SEG_W * 2, LIVEMAP_OPT_SEG_H, 1.5f, clrGold );
    }

    // Up/down scroll arrows -- dimmed (not hidden, so the tap zone stays
    // predictable) once there's nothing further to scroll that direction.
    bool canUp = state.optScroll > 0;
    bool canDown = state.optScroll < LIVEMAP_OPT_ROWS - LIVEMAP_OPT_VISIBLE_ROWS;
    float upCx = LIVEMAP_OPT_ARROW_X + LIVEMAP_OPT_ARROW_SIZE;
    float upCy = LIVEMAP_OPT_ARROW_UP_Y + LIVEMAP_OPT_ARROW_SIZE;
    float dnCx = LIVEMAP_OPT_ARROW_X + LIVEMAP_OPT_ARROW_SIZE;
    float dnCy = LIVEMAP_OPT_ARROW_DN_Y + LIVEMAP_OPT_ARROW_SIZE;
    u32 upColor = canUp ? clrGold : clrBadgeBg;
    u32 dnColor = canDown ? clrGold : clrBadgeBg;
    float s = LIVEMAP_OPT_ARROW_SIZE * 0.5f;
    C2D_DrawTriangle( upCx, upCy - s, upColor, upCx - s, upCy + s, upColor, upCx + s, upCy + s, upColor, 0.0f );
    C2D_DrawTriangle( dnCx, dnCy + s, dnColor, dnCx - s, dnCy - s, dnColor, dnCx + s, dnCy - s, dnColor, 0.0f );
}

// Save-slot management overlay (no name typing, no hidden button combo --
// just tap SLOT 1/2/3 from the Options screen). Same "centred box,
// tap a row to act, tap elsewhere to close" shape as the legend/info-card
// popups, but with per-row actions instead of being purely informational.
static void DrawSaveSlotsOverlay( const LiveMapState& state, u32 clrGold, u32 clrTitle, u32 clrText )
{
    const float x = 20.0f, y = 56.0f, w = 280.0f, h = 180.0f;
    C2D_DrawRectSolid( x, y, 0, w, h, C2D_Color32( 0x0F, 0x0A, 0x06, 0xE8 ) );
    DrawRectOutline( x, y, w, h, 2.0f, clrGold );
    C2D_DrawText( &s_slotsTitle, C2D_WithColor, x + 16.0f, y + 8.0f, 0.0f, 0.5f, 0.5f, clrTitle );

    const u32 clrRowBg   = C2D_Color32( 0x20, 0x18, 0x10, 0xFF );
    const u32 clrCurrent = C2D_Color32( 0x30, 0x28, 0x10, 0xFF );
    const u32 clrDelete  = C2D_Color32( 0xB0, 0x30, 0x20, 0xFF );
    const u32 clrDeleteArmed = C2D_Color32( 0xF8, 0x50, 0x30, 0xFF );
    const u32 clrCopyOff = C2D_Color32( 0x50, 0x40, 0x20, 0xFF );
    const u32 clrCopyOn  = C2D_Color32( 0xD8, 0xA8, 0x40, 0xFF );
    const u32 clrCopySrc = C2D_Color32( 0x40, 0x38, 0x10, 0xFF );   // armed-source row highlight

    // Phase 14.3 follow-up (save-slot copy): a dedicated toggle in the header
    // band, not a second per-row icon -- see livemap.h's LIVEMAP_SLOTS_COPY_*
    // comment for why. Filled gold while active (matches the header's own
    // gold-on-dark convention for "this is the active thing").
    C2D_DrawRectSolid( LIVEMAP_SLOTS_COPY_X, LIVEMAP_SLOTS_COPY_Y, 0,
                       LIVEMAP_SLOTS_COPY_W, LIVEMAP_SLOTS_COPY_H,
                       state.copyMode ? clrCopyOn : clrCopyOff );
    DrawRectOutline( LIVEMAP_SLOTS_COPY_X, LIVEMAP_SLOTS_COPY_Y, LIVEMAP_SLOTS_COPY_W, LIVEMAP_SLOTS_COPY_H, 1.5f, clrGold );
    C2D_DrawText( &s_slotCopy, C2D_WithColor, LIVEMAP_SLOTS_COPY_X + 14.0f, LIVEMAP_SLOTS_COPY_Y + 3.0f,
                 0.0f, 0.36f, 0.36f, state.copyMode ? C2D_Color32( 0x20, 0x18, 0x08, 0xFF ) : clrText );

    for ( int i = 0; i < LIVEMAP_SLOTS_ROWS; i++ )
    {
        float rowY = LIVEMAP_SLOTS_Y0 + i * LIVEMAP_SLOTS_ROW_H;
        bool  active = state.slotActive[i];
        bool  current = state.slotIsCurrent[i];
        bool  armedSource = state.copyMode && state.copySourceSlot == i;

        C2D_DrawRectSolid( LIVEMAP_SLOTS_X, rowY, 0, LIVEMAP_SLOTS_W, LIVEMAP_SLOTS_ROW_H - 6.0f,
                           armedSource ? clrCopySrc : ( current ? clrCurrent : clrRowBg ) );
        DrawSpeckleField( LIVEMAP_SLOTS_X, rowY, LIVEMAP_SLOTS_W, LIVEMAP_SLOTS_ROW_H - 6.0f, 26,
                         0x7C00u + (uint32_t) i, C2D_Color32( 0x70, 0x58, 0x30, 0x70 ) );
        DrawRectOutline( LIVEMAP_SLOTS_X, rowY, LIVEMAP_SLOTS_W, LIVEMAP_SLOTS_ROW_H - 6.0f, 1.5f, clrGold );
        C2D_DrawText( &s_slotLabel[i], C2D_WithColor, LIVEMAP_SLOTS_X + 8.0f, rowY + 4.0f,
                     0.0f, 0.42f, 0.42f, clrText );

        if ( !active )
        {
            if ( state.pendingNewGameSlot == i )
            {
                // phase 14.1: QUEST 1 / QUEST 2 choice, split across this
                // row's two halves -- LiveMap_state.cpp's touch handler
                // uses the exact same midpoint to decide which was tapped.
                float midX = LIVEMAP_SLOTS_X + LIVEMAP_SLOTS_W * 0.5f;
                float segH = LIVEMAP_SLOTS_ROW_H - 6.0f;
                C2D_DrawRectSolid( midX, rowY, 0, 1.5f, segH, clrGold );
                C2D_DrawText( &s_slotQuest1, C2D_WithColor,
                             LIVEMAP_SLOTS_X + ( midX - LIVEMAP_SLOTS_X - 56.0f ) / 2.0f + 8.0f,
                             rowY + 13.0f, 0.0f, 0.42f, 0.42f, clrGold );
                C2D_DrawText( &s_slotQuest2, C2D_WithColor,
                             midX + ( LIVEMAP_SLOTS_X + LIVEMAP_SLOTS_W - midX - 56.0f ) / 2.0f + 8.0f,
                             rowY + 13.0f, 0.0f, 0.42f, 0.42f, clrGold );
            }
            else
            {
                C2D_DrawText( &s_slotEmpty, C2D_WithColor, LIVEMAP_SLOTS_X + 8.0f, rowY + 20.0f,
                             0.0f, 0.32f, 0.32f, clrText );
            }
            continue;
        }

        if ( current )
        {
            C2D_DrawText( &s_slotPlaying, C2D_WithColor, LIVEMAP_SLOTS_X + 8.0f, rowY + 20.0f,
                         0.0f, 0.32f, 0.32f, clrGold );
        }
        else
        {
            DrawFilledCircle( LIVEMAP_SLOTS_X + 14.0f, rowY + 26.0f, 6.0f, C2D_Color32( 0xF8, 0x38, 0x38, 0xFF ) );
            DrawNumber( state.slotHearts[i], LIVEMAP_SLOTS_X + 24.0f, rowY + 20.0f );
            C2D_DrawText( &s_slotDeaths, C2D_WithColor, LIVEMAP_SLOTS_X + 60.0f, rowY + 20.0f,
                         0.0f, 0.32f, 0.32f, clrText );
            DrawNumber( state.slotDeaths[i], LIVEMAP_SLOTS_X + 108.0f, rowY + 20.0f );

            // phase 14.3: triforce progress ("n/8") + total play minutes,
            // squeezed into the same row as hearts/deaths -- no vertical
            // room in a 40px row for a separate line (see livemap.h's
            // LIVEMAP_SLOTS_ROW_H). Small scale to fit both in the ~100px
            // left before the delete icon zone.
            DrawNumber( state.slotTriforcePieces[i], LIVEMAP_SLOTS_X + 140.0f, rowY + 22.0f );
            C2D_DrawText( &s_statSlash, C2D_WithColor, LIVEMAP_SLOTS_X + 148.0f, rowY + 20.0f,
                         0.0f, 0.32f, 0.32f, clrText );
            DrawNumber( 8, LIVEMAP_SLOTS_X + 156.0f, rowY + 22.0f );
            DrawNumberWide( state.slotPlayTimeSeconds[i] / 60, LIVEMAP_SLOTS_X + 182.0f, rowY + 22.0f );
            C2D_DrawText( &s_statMin, C2D_WithColor, LIVEMAP_SLOTS_X + 182.0f + 24.0f, rowY + 20.0f,
                         0.0f, 0.28f, 0.28f, clrText );

            // Delete icon, fixed zone at the row's right edge -- only shown
            // for active, non-current slots (can't delete the save you're
            // playing right now). Armed (pending second tap) flashes. Hidden
            // during copy mode (phase 14.3 follow-up): a tap there means
            // "pick this as the copy source" instead, so showing a delete X
            // would be actively misleading.
            if ( !state.copyMode )
            {
                float delX = LIVEMAP_SLOTS_X + LIVEMAP_SLOTS_W - LIVEMAP_SLOTS_DEL_W;
                bool  armed = state.pendingDeleteSlot == i;
                u32   delColor = armed ? clrDeleteArmed : clrDelete;
                DrawFilledCircle( delX + LIVEMAP_SLOTS_DEL_W / 2.0f, rowY + 14.0f, 11.0f, delColor );
                C2D_DrawText( &s_slotDelete, C2D_WithColor, delX + LIVEMAP_SLOTS_DEL_W / 2.0f - 4.0f, rowY + 7.0f,
                             0.0f, 0.4f, 0.4f, C2D_Color32( 0xFF, 0xFF, 0xFF, 0xFF ) );
            }
        }
    }

    if ( state.pendingDeleteSlot >= 0 )
    {
        C2D_DrawText( &s_slotConfirmDelete, C2D_WithColor, x + 16.0f, y + h - 20.0f,
                     0.0f, 0.34f, 0.34f, clrDeleteArmed );
    }
    else if ( state.copyMode )
    {
        // Mutually exclusive with the delete-confirm hint above -- arming
        // either one clears the other (LiveMap_state.cpp), so both can
        // never be true at once.
        C2D_DrawText( &s_slotCopyHint, C2D_WithColor, x + 16.0f, y + h - 20.0f,
                     0.0f, 0.32f, 0.32f, clrCopyOn );
    }
}

// Music player (phase 13.4): a plain vertical list, one row per song
// (Song_intro..Song_zelda) plus a STOP row -- reachable via X-cycling, not a
// 4th header tab (see LIVEMAP_MUSIC_* comment in livemap.h for why). The
// highlighted row is `state.playingSongId` (-1 = none, i.e. STOP is
// highlighted) -- purely a UI selection LiveMap_state.cpp tracks, not
// anything read back from Sound:: (there's no "currently playing song" query).
static void DrawMusicScreen( const LiveMapState& state, u32 clrGold, u32 clrText )
{
    const u32 clrActiveText = C2D_Color32( 0x24, 0x1A, 0x08, 0xFF );
    const u32 clrRowBg      = C2D_Color32( 0x20, 0x18, 0x10, 0xFF );

    for ( int i = 0; i < LIVEMAP_MUSIC_ROWS; i++ )
    {
        float rowY = LIVEMAP_MUSIC_Y0 + i * LIVEMAP_MUSIC_ROW_H;
        bool isStopRow = ( i == Song_Max );
        bool active = isStopRow ? ( state.playingSongId < 0 ) : ( state.playingSongId == i );

        C2D_DrawRectSolid( LIVEMAP_MUSIC_X, rowY, 0, LIVEMAP_MUSIC_W, LIVEMAP_MUSIC_ROW_H - 1.0f,
                           active ? clrGold : clrRowBg );
        const C2D_Text* label = isStopRow ? &s_musicStop : &s_musicNames[i];
        C2D_DrawText( label, C2D_WithColor, LIVEMAP_MUSIC_X + 8.0f, rowY + 2.0f,
                     0.0f, 0.38f, 0.38f, active ? clrActiveText : clrText );
    }
}

// Save statistics (phase 13.3): four read-only rows, straight from Profile
// (via LiveMapState, filled in LiveMap_state.cpp -- see Profile.h for where
// each counter is actually incremented). Numbers via DrawNumberWide() (tile-
// based, no per-frame text-parse) -- the phase 10.3 perf lesson about
// C2D_DrawText's font-texture-bind cost applies just as much to
// C2D_TextParse, so dynamic values here deliberately avoid it, same as the
// Items screen's rupee/key counts already do.
static void DrawStatsScreen( const LiveMapState& state, u32 clrGold, u32 clrText )
{
    const u32 clrRowBg = C2D_Color32( 0x20, 0x18, 0x10, 0xFF );

    uint32_t totalMinutes = state.playTimeSeconds / 60;

    struct StatRow { const C2D_Text* label; uint32_t value; const C2D_Text* suffix; };
    const StatRow rows[LIVEMAP_STATS_ROWS - 1] =
    {
        { &s_statPlayTime, totalMinutes,             &s_statMin },
        { &s_statDeaths,   state.deathCount,          nullptr },
        { &s_statKills,    state.killCount,           nullptr },
        { &s_statRepair,   state.repairRupeesSpent,   &s_statRupees },
    };

    for ( int i = 0; i < LIVEMAP_STATS_ROWS - 1; i++ )
    {
        float rowY = LIVEMAP_STATS_Y0 + i * LIVEMAP_STATS_ROW_H;
        C2D_DrawRectSolid( LIVEMAP_STATS_X, rowY, 0, 280.0f, LIVEMAP_STATS_ROW_H - 6.0f, clrRowBg );
        DrawRectOutline( LIVEMAP_STATS_X, rowY, 280.0f, LIVEMAP_STATS_ROW_H - 6.0f, 1.5f, clrGold );
        C2D_DrawText( rows[i].label, C2D_WithColor, LIVEMAP_STATS_X + 8.0f, rowY + 4.0f,
                     0.0f, 0.42f, 0.42f, clrText );
        DrawNumberWide( rows[i].value, LIVEMAP_STATS_X + 190.0f, rowY + 6.0f );
        if ( rows[i].suffix != nullptr )
            C2D_DrawText( rows[i].suffix, C2D_WithColor, LIVEMAP_STATS_X + 230.0f, rowY + 4.0f,
                         0.0f, 0.36f, 0.36f, clrText );
    }

    // phase 10.4: last row, "SECRETS  N / total" -- a ratio rather than a
    // plain count, so drawn as two DrawNumberWide calls around a static "/"
    // instead of fitting the shared StatRow{label,value,suffix} shape above.
    {
        int i = LIVEMAP_STATS_ROWS - 1;
        float rowY = LIVEMAP_STATS_Y0 + i * LIVEMAP_STATS_ROW_H;
        C2D_DrawRectSolid( LIVEMAP_STATS_X, rowY, 0, 280.0f, LIVEMAP_STATS_ROW_H - 6.0f, clrRowBg );
        DrawRectOutline( LIVEMAP_STATS_X, rowY, 280.0f, LIVEMAP_STATS_ROW_H - 6.0f, 1.5f, clrGold );
        C2D_DrawText( &s_statSecrets, C2D_WithColor, LIVEMAP_STATS_X + 8.0f, rowY + 4.0f,
                     0.0f, 0.42f, 0.42f, clrText );
        // Phase 13.7 follow-up: total went from a single digit (max 8) to up
        // to two (max 53, tree/rock secrets now counted too) -- nudged the
        // slash/total 4-6px right of their old fixed offsets so a 2-digit
        // "found" count (up to 53) doesn't butt straight up against the
        // slash with zero gap; still well inside the row's 280px width.
        DrawNumberWide( state.secretsFound, LIVEMAP_STATS_X + 190.0f, rowY + 6.0f );
        C2D_DrawText( &s_statSlash, C2D_WithColor, LIVEMAP_STATS_X + 210.0f, rowY + 4.0f,
                     0.0f, 0.42f, 0.42f, clrText );
        DrawNumberWide( state.secretsTotal, LIVEMAP_STATS_X + 222.0f, rowY + 6.0f );
    }
}

// Manual (phase 13.2, expanded after hardware testing): a
// scrollable book/achievement-list screen, three sections in one logical
// row list (same "logical row -> real entry" scroll pattern as the Options
// screen, LIVEMAP_MANUAL_ROWS/VISIBLE_ROWS): BOSSES (7 rows, was the old
// standalone Bestiary), SWORD (3 tiers), ITEMS (the Items screen's existing
// 15 ownable slots). Each section starts with a plain, non-tappable header
// row. Undiscovered entries show "???" instead of their name -- real
// bestiary-style mystery -- and get their name revealed permanently the
// first time they're obtained/defeated, plus a gold status tag on the
// right (DEFEATED/OWNED/FOUND depending on section).
static void DrawManualScreen( const LiveMapState& state, u32 clrGold, u32 clrTitle, u32 clrText )
{
    const u32 clrRowBg  = C2D_Color32( 0x20, 0x18, 0x10, 0xFF );
    const u32 clrLocked = C2D_Color32( 0x70, 0x68, 0x58, 0xFF );
    const u32 clrBadgeBg= C2D_Color32( 0x2A, 0x20, 0x18, 0xFF );

    // Logical row layout (LIVEMAP_MANUAL_ROWS total): a header row, then
    // that section's data rows, repeated for BOSSES/SWORD/ITEMS.
    const int kBossesHeader = 0;
    const int kBossStart    = kBossesHeader + 1;
    const int kSwordHeader  = kBossStart + LIVEMAP_MANUAL_BOSS_ROWS;
    const int kSwordStart   = kSwordHeader + 1;
    const int kItemsHeader  = kSwordStart + LIVEMAP_MANUAL_SWORD_ROWS;
    const int kItemStart    = kItemsHeader + 1;

    for ( int i = 0; i < LIVEMAP_MANUAL_VISIBLE_ROWS; i++ )
    {
        int logical = state.manualScroll + i;
        if ( logical >= LIVEMAP_MANUAL_ROWS )
            break;

        float rowY = LIVEMAP_MANUAL_Y0 + i * (LIVEMAP_MANUAL_ROW_H + LIVEMAP_MANUAL_ROW_GAP);

        if ( logical == kBossesHeader || logical == kSwordHeader || logical == kItemsHeader )
        {
            int sectionIndex = ( logical == kBossesHeader ) ? 0 : ( logical == kSwordHeader ? 1 : 2 );
            C2D_DrawText( &s_manualSectionNames[sectionIndex], C2D_WithColor,
                         LIVEMAP_MANUAL_X, rowY + 2.0f, 0.0f, 0.4f, 0.4f, clrTitle );
            C2D_DrawRectSolid( LIVEMAP_MANUAL_X, rowY + LIVEMAP_MANUAL_ROW_H - 2.0f, 0,
                               LIVEMAP_MANUAL_W, 1.5f, clrGold );
            continue;
        }

        // Which data row this logical index maps to.
        const C2D_Text* nameText;
        const C2D_Text* statusText;
        bool unlocked;
        if ( logical < kSwordHeader )
        {
            int bossIdx = logical - kBossStart;
            unlocked = state.bossDefeated[bossIdx];
            nameText = unlocked ? &s_bossNames[bossIdx] : &s_manualLocked;
            statusText = &s_manualDefeated;
        }
        else if ( logical < kItemsHeader )
        {
            int swordIdx = logical - kSwordStart;
            unlocked = state.swordOwned[swordIdx];
            nameText = unlocked ? &s_swordNames[swordIdx] : &s_manualLocked;
            statusText = &s_manualOwned;
        }
        else
        {
            int itemIdx = logical - kItemStart;
            unlocked = state.itemDiscovered[itemIdx];
            nameText = unlocked ? &s_itemManualNames[itemIdx] : &s_manualLocked;
            statusText = &s_manualFound;
        }

        C2D_DrawRectSolid( LIVEMAP_MANUAL_X, rowY, 0, LIVEMAP_MANUAL_W, LIVEMAP_MANUAL_ROW_H, clrRowBg );
        DrawRectOutline( LIVEMAP_MANUAL_X, rowY, LIVEMAP_MANUAL_W, LIVEMAP_MANUAL_ROW_H, 1.5f,
                         unlocked ? clrGold : clrLocked );
        C2D_DrawText( nameText, C2D_WithColor, LIVEMAP_MANUAL_X + 8.0f, rowY + 3.0f,
                     0.0f, 0.36f, 0.36f, unlocked ? clrText : clrLocked );
        if ( unlocked )
        {
            C2D_DrawText( statusText, C2D_WithColor, LIVEMAP_MANUAL_X + 200.0f, rowY + 4.0f,
                         0.0f, 0.28f, 0.28f, clrGold );
        }
    }

    // Up/down scroll arrows -- same shape/dimming convention as the Options
    // screen's, just against LIVEMAP_MANUAL_ROWS/VISIBLE_ROWS.
    bool canUp = state.manualScroll > 0;
    bool canDown = state.manualScroll < LIVEMAP_MANUAL_ROWS - LIVEMAP_MANUAL_VISIBLE_ROWS;
    float upCx = LIVEMAP_MANUAL_ARROW_X + LIVEMAP_MANUAL_ARROW_SIZE;
    float upCy = LIVEMAP_MANUAL_ARROW_UP_Y + LIVEMAP_MANUAL_ARROW_SIZE;
    float dnCx = LIVEMAP_MANUAL_ARROW_X + LIVEMAP_MANUAL_ARROW_SIZE;
    float dnCy = LIVEMAP_MANUAL_ARROW_DN_Y + LIVEMAP_MANUAL_ARROW_SIZE;
    u32 upColor = canUp ? clrGold : clrBadgeBg;
    u32 dnColor = canDown ? clrGold : clrBadgeBg;
    float s = LIVEMAP_MANUAL_ARROW_SIZE * 0.5f;
    C2D_DrawTriangle( upCx, upCy - s, upColor, upCx - s, upCy + s, upColor, upCx + s, upCy + s, upColor, 0.0f );
    C2D_DrawTriangle( dnCx, dnCy + s, dnColor, dnCx - s, dnCy - s, dnColor, dnCx + s, dnCy - s, dnColor, 0.0f );
}

// The "?" touch button that opens the legend overlay below — a small
// gold-ringed circle in the map content area's top-right corner. Position
// matches LIVEMAP_LEGEND_BTN_* (livemap.h), which LiveMap_state.cpp hit-
// tests against.
static void DrawLegendButton( u32 clrGold )
{
    float cx = LIVEMAP_LEGEND_BTN_X + LIVEMAP_LEGEND_BTN_SIZE / 2.0f;
    float cy = LIVEMAP_LEGEND_BTN_Y + LIVEMAP_LEGEND_BTN_SIZE / 2.0f;
    float r = LIVEMAP_LEGEND_BTN_SIZE / 2.0f;
    DrawFilledCircle( cx, cy, r, clrGold );
    DrawFilledCircle( cx, cy, r - 2.0f, C2D_Color32( 0x2A, 0x20, 0x18, 0xFF ) );
    C2D_DrawText( &s_legendQ, C2D_WithColor, cx - 4.0f, cy - 7.0f, 0.0f, 0.5f, 0.5f, clrGold );
}

// On-demand overworld map legend (phase 9.x, replaces the old always-on
// bottom-left legend text). Triggered by tapping the "?" button (LIVEMAP_LEGEND_BTN_* in
// livemap.h, hit-tested in LiveMap_state.cpp), dismissed by tapping again
// anywhere. Dungeon mode keeps its own inline MAP/COMPASS/KEY legend row
// (DrawDungeonMap) rather than this popup — see LiveMap_state.cpp's touch
// handling for why.
// Reworked (a legend must say which colour means what). The old version
// showed the 7 POI
// colour swatches with no label at all -- looked like decoration, not a
// key. Now every swatch has a real one-word label next to it (4x2 grid);
// the top 3 rows shrank 38->30px spacing to make room without growing the
// box. Pins keep their gesture explanation but dropped the swatch row
// entirely -- pin colours are the player's own reminder system (no fixed
// meaning to label), so implying one with unlabelled swatches was the
// actual problem, not a rendering-space one.
static void DrawLegendOverlay( u32 clrGold, u32 clrTitle )
{
    const float x = 10.0f, y = 64.0f, w = 300.0f, h = 172.0f;
    C2D_DrawRectSolid( x, y, 0, w, h, C2D_Color32( 0x0F, 0x0A, 0x06, 0xE4 ) );
    DrawRectOutline( x, y, w, h, 2.0f, clrGold );

    float ly = y + 22.0f;
    DrawFilledCircle( x + 30.0f, ly + 9.0f, 8.0f, C2D_Color32( 0xF8, 0xD0, 0x40, 0xFF ) );
    C2D_DrawText( &s_legendYou, C2D_WithColor, x + 50.0f, ly - 2.0f, 0.0f, 0.56f, 0.56f, clrTitle );

    ly += 30.0f;
    DrawFilledCircle( x + 30.0f, ly + 9.0f, 8.0f, C2D_Color32( 0x58, 0x94, 0x4E, 0xFF ) );
    C2D_DrawText( &s_legendSeen, C2D_WithColor, x + 50.0f, ly - 2.0f, 0.0f, 0.56f, 0.56f, clrTitle );

    ly += 30.0f;
    DrawFilledCircle( x + 30.0f, ly + 9.0f, 8.0f, clrGold );
    DrawFilledCircle( x + 30.0f, ly + 9.0f, 6.0f, C2D_Color32( 0x28, 0x20, 0x18, 0xFF ) );
    C2D_DrawText( &s_legendDun, C2D_WithColor, x + 50.0f, ly - 2.0f, 0.0f, 0.56f, 0.56f, clrTitle );

    // Custom pins (phase 10.2): the gesture only, no colour-meaning claim.
    ly += 30.0f;
    C2D_DrawText( &s_legendPin, C2D_WithColor, x + 12.0f, ly, 0.0f, 0.36f, 0.36f, clrTitle );

    // Overworld points-of-interest (phase 10.3), now actually labelled: a
    // 4-column x 2-row grid, small colour square + one-word name, same
    // Poi_* order as kPoiColors/s_legendPoiNames.
    float poiY0 = ly + 20.0f;
    const float colW = (w - 24.0f) / 4.0f;
    for ( int i = 0; i < Poi_Count; i++ )
    {
        int   col = i % 4;
        int   row = i / 4;
        float px = x + 12.0f + col * colW;
        float py = poiY0 + row * 16.0f;
        C2D_DrawRectSolid( px, py, 0, 7.0f, 7.0f, kPoiColors[i] );
        C2D_DrawText( &s_legendPoiNames[i], C2D_WithColor, px + 11.0f, py - 3.0f, 0.0f, 0.32f, 0.32f, clrTitle );
    }
}

// phase 10.5: the "tap for info" card -- opened by tapping a dungeon-entrance
// or phase 10.3 POI badge (see LiveMap_state.cpp's touch handler). Same box
// geometry as DrawLegendOverlay (the two can never be open at once -- the
// touch handler's else-if chain guarantees that -- so reusing the region is
// safe, not just convenient). Dungeon entrances show the level number +
// whether that level's triforce piece has been collected (World.cpp's own
// `1 << (LevelNumber-1)` bit convention, mirrored in LiveMap_state.cpp);
// shop/potion/gift-cave POIs show up to 3 real item-sprite icons resolved
// game-side (LiveMap_state.cpp's GetPoiItems()/ResolveIconById()) via the
// same Sheet_PlayerAndItems path the Items screen already uses; the other
// POI kinds (repair/tunnel/fairy/flute-warp) are name-only -- there's
// nothing else fixed to show for them.
static void DrawInfoCard( const LiveMapState& state, u32 clrGold, u32 clrTitle )
{
    if ( state.infoRoom < 0 )
        return;

    const float x = 10.0f, y = 64.0f, w = 300.0f, h = 172.0f;
    C2D_DrawRectSolid( x, y, 0, w, h, C2D_Color32( 0x0F, 0x0A, 0x06, 0xE4 ) );
    DrawRectOutline( x, y, w, h, 2.0f, clrGold );

    int level;
    if ( FindDungeonNumber( state.infoRoom, state.quest2, level ) )
    {
        C2D_DrawText( &s_uwTitleWord, C2D_WithColor, x + 30.0f, y + 24.0f, 0.0f, 0.7f, 0.7f, clrTitle );
        C2D_DrawText( &s_dungeonText[level - 1], C2D_WithColor, x + 130.0f, y + 24.0f, 0.0f, 0.7f, 0.7f, clrTitle );

        bool haveTriforce = (state.triforcePieces & (1 << (level - 1))) != 0;
        DrawFilledCircle( x + 30.0f, y + 74.0f, 8.0f,
                          haveTriforce ? clrGold : C2D_Color32( 0x3A, 0x2A, 0x22, 0xFF ) );
        C2D_DrawText( haveTriforce ? &s_infoTriforceYes : &s_infoTriforceNo, C2D_WithColor,
                     x + 50.0f, y + 66.0f, 0.0f, 0.55f, 0.55f, clrTitle );
    }
    else
    {
        int kind, nextIdx;
        if ( FindPoiAt( state.infoRoom, 0, kind, nextIdx ) )
        {
            C2D_DrawText( &s_poiNames[kind], C2D_WithColor, x + 20.0f, y + 24.0f, 0.0f, 0.62f, 0.62f, clrTitle );

            bool anyItem = false;
            for ( int i = 0; i < LIVEMAP_INFO_ITEM_SLOTS; i++ )
                if ( state.infoItems[i].animId >= 0 ) anyItem = true;

            if ( anyItem )
            {
                const u32 clrBadgeBg = C2D_Color32( 0x2A, 0x20, 0x18, 0xFF );
                float ix = x + 60.0f;
                for ( int i = 0; i < LIVEMAP_INFO_ITEM_SLOTS; i++ )
                {
                    DrawFilledCircle( ix, y + 76.0f, 22.0f, clrGold );
                    DrawFilledCircle( ix, y + 76.0f, 19.0f, clrBadgeBg );
                    DrawItemIcon( state.infoItems[i], ix, y + 76.0f );
                    ix += 60.0f;
                }
            }
        }
    }

    C2D_DrawText( &s_infoTapClose, C2D_WithColor, x + 70.0f, y + h - 24.0f, 0.0f, 0.4f, 0.4f, clrTitle );
}

// Thin wood-toned inset line just inside the screen edge — the "picture
// frame" the parchment map sits in. Drawn first, behind everything else.
static void DrawFrame()
{
    const u32 clrWoodLight = C2D_Color32( 0x4A, 0x30, 0x1C, 0xFF );
    C2D_DrawRectSolid( 4,   4, 0, 312, 2, clrWoodLight );
    C2D_DrawRectSolid( 4, 234, 0, 312, 2, clrWoodLight );
    C2D_DrawRectSolid( 4,   4, 0, 2, 232, clrWoodLight );
    C2D_DrawRectSolid( 314, 4, 0, 2, 232, clrWoodLight );
}

// Gold border around a map grid, with corners rounded by stamping small
// filled circles over the sharp joins (a cheap illusion of a rounded frame
// — no true arcs needed).
static void DrawGoldFrame( float ox, float oy, float gw, float gh, u32 clrGold )
{
    C2D_DrawRectSolid( ox - 3, oy - 3, 0, gw + 6, 2, clrGold );
    C2D_DrawRectSolid( ox - 3, oy + gh + 1, 0, gw + 6, 2, clrGold );
    C2D_DrawRectSolid( ox - 3, oy - 3, 0, 2, gh + 6, clrGold );
    C2D_DrawRectSolid( ox + gw + 1, oy - 3, 0, 2, gh + 6, clrGold );
    const float r = 2.5f;
    DrawFilledCircle( ox - 2, oy - 2, r, clrGold );
    DrawFilledCircle( ox + gw + 2, oy - 2, r, clrGold );
    DrawFilledCircle( ox - 2, oy + gh + 2, r, clrGold );
    DrawFilledCircle( ox + gw + 2, oy + gh + 2, r, clrGold );
}

// Clamps a viewCols x viewRows window to fully fit inside a gridCols x
// gridRows grid, centred on (centerCol, centerRow). Shared by the overworld
// and dungeon detail-zoom modes (phase 7.3).
static void ClampViewport( int centerCol, int centerRow, int viewCols, int viewRows,
                           int gridCols, int gridRows, int& vx0, int& vy0 )
{
    vx0 = centerCol - viewCols / 2;
    vy0 = centerRow - viewRows / 2;
    if ( vx0 < 0 ) vx0 = 0;
    if ( vy0 < 0 ) vy0 = 0;
    if ( vx0 > gridCols - viewCols ) vx0 = gridCols - viewCols;
    if ( vy0 > gridRows - viewRows ) vy0 = gridRows - viewRows;
}

// Pulsing round "you are here" badge at Link's exact sub-cell position (phase
// 7.2). This used to also wash the whole current room in a soft gold tint
// (C2D_Color32(0xF8,0xD0,0x40,0x28) over the full cell) so the room "still
// reads at a glance" — dropped: the dot already says where you are, and the
// tint had a real
// cost: it shifted that one cell's terrain colours, which is exactly the kind
// of "this cell looks different for no reason the player cares about"
// distinction the terrain-atlas work spent three rounds removing.
static void DrawPlayerMarker( float x, float y, float cw, float ch,
                               float fracX, float fracY, uint32_t frame )
{
    const float margin = 4.0f;
    float dotX = x + margin + fracX * (cw - 2.0f * margin);
    float dotY = y + margin + fracY * (ch - 2.0f * margin);

    int t = frame % 60;
    int tri = t < 30 ? t : 60 - t;
    u8  a = (u8) (0xA0 + tri * 3);

    DrawFilledCircle( dotX, dotY, 6.0f, C2D_Color32( 0xF8, 0xD0, 0x40, a ) );
    DrawFilledCircle( dotX, dotY, 3.0f, C2D_Color32( 0xFF, 0xFF, 0xFF, 0xFF ) );
}

static void DrawDungeonMap( const LiveMapState& state, uint32_t frame,
                             u32 clrGold, u32 clrTitle, u32 clrGrid, u32 clrText )
{
    const u32 clrRevealed = C2D_Color32( 0x30, 0x2C, 0x24, 0xFF );   // seen via map item, not yet visited
    const u32 clrFloor    = C2D_Color32( 0x58, 0x4E, 0x38, 0xFF );   // visited room floor
    const u32 clrBadge    = C2D_Color32( 0x28, 0x20, 0x18, 0xC0 );

    // Phase 7.3 detail zoom: half the cells, twice the size, centred on the
    // player, filling the exact same on-screen grid footprint (gw/gh below
    // never change — only how many cells / how big each is does).
    bool zoom = state.detailZoom && state.curCol >= 0 && state.curRow >= 0;
    const int viewCols = zoom ? LIVEMAP_UW_COLS / 2 : LIVEMAP_UW_COLS;
    const int viewRows = zoom ? LIVEMAP_UW_ROWS / 2 : LIVEMAP_UW_ROWS;
    int vx0 = 0, vy0 = 0;
    if ( zoom )
        ClampViewport( state.curCol, state.curRow, viewCols, viewRows,
                      LIVEMAP_UW_COLS, LIVEMAP_UW_ROWS, vx0, vy0 );

    const float gw = LIVEMAP_UW_COLS * 32.0f, gh = LIVEMAP_UW_ROWS * 18.0f;
    const float cw = gw / viewCols, ch = gh / viewRows;
    const float ox = (320.0f - gw) / 2.0f;
    const float oy = 78.0f;   // below the persistent header + divider (phase 9.x)

    // Title: plain "LEVEL n", no accent-line flourish — decluttered after the
    // first concept pass came out too small/detailed.
    C2D_DrawText( &s_uwTitleWord, C2D_WithColor, ox + gw / 2 - 46, oy - 16.0f, 0.0f, 0.5f, 0.5f, clrTitle );
    if ( state.dungeonLevel >= 1 && state.dungeonLevel <= 9 )
        C2D_DrawText( &s_dungeonText[state.dungeonLevel - 1], C2D_WithColor,
                       ox + gw / 2 + 34, oy - 16.0f, 0.0f, 0.5f, 0.5f, clrTitle );

    DrawGoldFrame( ox, oy, gw, gh, clrGold );

    const float gap = cw * 0.1875f;

    for ( int c = vx0; c < vx0 + viewCols; c++ )
    {
        for ( int r = vy0; r < vy0 + viewRows; r++ )
        {
            bool present = (state.dungeonPresent[c] & (0x80 >> r)) != 0;
            if ( !present )
                continue;

            bool visited = (state.dungeonVisited[c] & (0x80 >> r)) != 0;
            if ( !visited && !state.hasDungeonMap )
                continue;

            float x = ox + (c - vx0) * cw;
            float y = oy + (r - vy0) * ch;
            C2D_DrawRectSolid( x, y, 0, cw - 1, ch - 1, visited ? clrFloor : clrRevealed );

            // phase 10.1/#3: dungeon hidden-passage secrets. Same map philosophy
            // as the overworld bomb/burn layer -- only on the 2x zoom, only
            // under SHOW HINTS. A dot on each secret wall's mid-edge: grey =
            // bombable wall (needs bombs), teal = walk-through false wall.
            // Drawn before the `!visited` skip so a room revealed by the Map
            // item (clrRevealed) shows its secrets too, matching the rule
            // that the zoomed map shows everything. Cheap (a few
            // DrawFilledCircle) -- within the phase 10.6 bottomMap budget.
            if ( zoom && Options::Get().ShowSecretHints )
            {
                uint8_t bomb  = state.dungeonBombWalls[c * LIVEMAP_UW_ROWS + r];
                uint8_t fake  = state.dungeonFalseWalls[c * LIVEMAP_UW_ROWS + r];
                float   rad   = cw * 0.09f;
                const u32 clrBomb  = C2D_Color32( 0xC0, 0xC0, 0xC8, 0xFF );
                const u32 clrFalse = C2D_Color32( 0x40, 0xC8, 0xB0, 0xFF );
                const u32 clrRing  = C2D_Color32( 0x18, 0x12, 0x0C, 0xFF );
                struct Edge { uint8_t bit; float ex, ey; } edges[4] = {
                    { UwDir_Up,    x + cw / 2, y          },
                    { UwDir_Down,  x + cw / 2, y + ch - 1 },
                    { UwDir_Left,  x,          y + ch / 2 },
                    { UwDir_Right, x + cw - 1, y + ch / 2 },
                };
                for ( const Edge& e : edges )
                {
                    bool isBomb = (bomb & e.bit) != 0;
                    bool isFake = (fake & e.bit) != 0;
                    if ( !isBomb && !isFake )
                        continue;
                    DrawFilledCircle( e.ex, e.ey, rad + 1.0f, clrRing );
                    DrawFilledCircle( e.ex, e.ey, rad, isBomb ? clrBomb : clrFalse );
                }
            }

            if ( !visited )
                continue;

            // Per-room door notches, cut independently on all 4 walls (own
            // room's door state only — matches the original pause-map's
            // single-tile-per-room approach, see Submenu.cpp::DrawMap).
            uint8_t doors = state.dungeonDoors[c * LIVEMAP_UW_ROWS + r];

            if ( doors & UwDir_Up )
            {
                C2D_DrawRectSolid( x, y, 0, (cw - gap) / 2, 1, clrGrid );
                C2D_DrawRectSolid( x + (cw + gap) / 2, y, 0, (cw - gap) / 2, 1, clrGrid );
            }
            else C2D_DrawRectSolid( x, y, 0, cw, 1, clrGrid );

            if ( doors & UwDir_Down )
            {
                C2D_DrawRectSolid( x, y + ch - 1, 0, (cw - gap) / 2, 1, clrGrid );
                C2D_DrawRectSolid( x + (cw + gap) / 2, y + ch - 1, 0, (cw - gap) / 2, 1, clrGrid );
            }
            else C2D_DrawRectSolid( x, y + ch - 1, 0, cw, 1, clrGrid );

            if ( doors & UwDir_Left )
            {
                C2D_DrawRectSolid( x, y, 0, 1, (ch - gap) / 2, clrGrid );
                C2D_DrawRectSolid( x, y + (ch + gap) / 2, 0, 1, (ch - gap) / 2, clrGrid );
            }
            else C2D_DrawRectSolid( x, y, 0, 1, ch, clrGrid );

            if ( doors & UwDir_Right )
            {
                C2D_DrawRectSolid( x + cw - 1, y, 0, 1, (ch - gap) / 2, clrGrid );
                C2D_DrawRectSolid( x + cw - 1, y + (ch + gap) / 2, 0, 1, (ch - gap) / 2, clrGrid );
            }
            else C2D_DrawRectSolid( x + cw - 1, y, 0, 1, ch, clrGrid );
        }
    }

    // Boss/triforce room marker — only once the compass is collected, blinks
    // (via visibility toggle) until the room's item is actually taken. Only
    // drawn when inside the current viewport (may be scrolled out of view
    // while zoomed).
    if ( state.hasCompass && state.bossCol >= 0 && state.bossRow >= 0
        && state.bossCol >= vx0 && state.bossCol < vx0 + viewCols
        && state.bossRow >= vy0 && state.bossRow < vy0 + viewRows )
    {
        float x = ox + (state.bossCol - vx0) * cw;
        float y = oy + (state.bossRow - vy0) * ch;
        bool  hide = !state.bossCollected && ((frame / 20) % 2) == 0;
        if ( !hide )
        {
            float s = cw / 32.0f;
            DrawFilledCircle( x + cw / 2, y + ch / 2, 5.0f * s, C2D_Color32( 0x20, 0x18, 0x10, 0xFF ) );
            DrawFilledCircle( x + cw / 2, y + ch / 2, 3.8f * s, clrGold );
        }
    }

    // Current position marker (phase 7.2 — fine-grained, not just room-level).
    // Hidden while the player is off-grid, e.g. inside a cellar/item room.
    // Always inside the viewport by construction when zoomed (centred on it).
    if ( state.curCol >= 0 && state.curRow >= 0 )
        DrawPlayerMarker( ox + (state.curCol - vx0) * cw, oy + (state.curRow - vy0) * ch, cw, ch,
                          state.playerFracX, state.playerFracY, frame );

    // Legend + item/key status row. Tighter gap than before (was +10): the
    // grid now starts lower (oy=78, under the new header) so there's less
    // room left above the wood frame's bottom edge (~236).
    float ly = oy + gh + 4.0f;
    DrawFilledCircle( ox + 4, ly + 6, 4.0f, state.hasDungeonMap ? clrGold : clrBadge );
    C2D_DrawText( &s_uwLegendMap, C2D_WithColor, ox + 12, ly, 0.0f, 0.42f, 0.42f, clrText );
    DrawFilledCircle( ox + 66, ly + 6, 4.0f, state.hasCompass ? clrGold : clrBadge );
    C2D_DrawText( &s_uwLegendCompass, C2D_WithColor, ox + 74, ly, 0.0f, 0.42f, 0.42f, clrText );

    C2D_DrawText( &s_uwLegendKey, C2D_WithColor, ox + 168, ly, 0.0f, 0.42f, 0.42f, clrText );
    float kx = ox + 168 + 34;
    for ( int i = 0; i < state.keyCount; i++ )
    {
        DrawFilledCircle( kx + 3.2f, ly + 6, 3.2f, clrGold );
        kx += 9;
    }
}

// Small filled up-pointing triangle (C2D_DrawTriangle, confirmed reliable --
// see DrawFilledCircle's comment above on why not citro2d's own primitives).
static void DrawFilledTriUp( float cx, float cy, float size, u32 color )
{
    C2D_DrawTriangle( cx, cy - size, color,
                       cx - size, cy + size, color,
                       cx + size, cy + size, color, 0.0f );
}

// Three small triangles in the classic Triforce arrangement (phase 11.1b).
static void DrawTriforceIcon( float cx, float cy, float size, u32 color )
{
    DrawFilledTriUp( cx, cy - size * 0.5f, size * 0.5f, color );
    DrawFilledTriUp( cx - size * 0.5f, cy + size * 0.5f, size * 0.5f, color );
    DrawFilledTriUp( cx + size * 0.5f, cy + size * 0.5f, size * 0.5f, color );
}

// Title screen's bottom-screen companion (phase 11.1b): same wood/gold frame
// the map/Items/Options screens use (DrawFrame()), but its own centred
// content -- a small Triforce + a breathing "PRESS START" -- instead of the
// normal header+tabs, which don't mean anything before a save is chosen.
static void DrawBootCompanion( u32 clrGold, u32 clrText, uint32_t frame )
{
    float cx = 160.0f;
    DrawTriforceIcon( cx, 90.0f, 22.0f, clrGold );

    C2D_DrawRectSolid( cx - 60.0f, 130.0f, 0, 120.0f, 1.5f, clrGold );

    // Breathing fade (slower, softer than a hard blink) -- calmer than the
    // original mockup's blinking prompt.
    float t = (float) (frame % 156) / 156.0f;
    float phase = t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f;
    u32 a = (u32) (0x50 + phase * 0xAF);
    C2D_DrawText( &s_titlePressLabel, C2D_WithColor, cx - 46.0f, 148.0f,
                 0.0f, 0.52f, 0.52f, C2D_Color32( 0xF6, 0xDE, 0x8C, a ) );
    C2D_DrawText( &s_titleEditionLabel, C2D_WithColor, cx - 44.0f, 172.0f,
                 0.0f, 0.36f, 0.36f, clrText );
}

// Death "continue" panel (replaces the classic Select-cycle NES list with a
// modern two-button touch screen, styled like the rest of the bottom UI).
// Button geometry is the shared LIVEMAP_CONT_BTN_* (livemap.h) so this and the
// touch hit-test in LiveMap_state.cpp can't drift. Two options, "CONTINUE"
// (resume where you died) and "CONTINUE FROM START" (start tile).
static void DrawContinueButton( const C2D_Text* label, float y, u32 clrGold, u32 clrText )
{
    const float x = LIVEMAP_CONT_BTN_X, w = LIVEMAP_CONT_BTN_W, h = LIVEMAP_CONT_BTN_H;
    C2D_DrawRectSolid( x, y, 0, w, h, C2D_Color32( 0x2A, 0x1C, 0x10, 0xF0 ) );
    DrawRectOutline( x, y, w, h, 2.0f, clrGold );
    // Auto-fit: "CONTINUE FROM START" is long -- shrink the scale if it would
    // overrun the button rather than pick one scale that clips the long label.
    float scale = 0.6f, tw, th;
    C2D_TextGetDimensions( label, scale, scale, &tw, &th );
    const float maxW = w - 20.0f;
    if ( tw > maxW ) { scale *= maxW / tw; C2D_TextGetDimensions( label, scale, scale, &tw, &th ); }
    C2D_DrawText( label, C2D_WithColor, x + ( w - tw ) / 2.0f, y + ( h - th ) / 2.0f,
                 0.0f, scale, scale, clrText );
}

static void DrawContinuePanel( const LiveMapState& state, u32 clrGold, u32 clrTitle, u32 clrText )
{
    float tw, th;
    C2D_TextGetDimensions( &s_contGameOver, 0.9f, 0.9f, &tw, &th );
    C2D_DrawText( &s_contGameOver, C2D_WithColor, ( 320.0f - tw ) / 2.0f, 40.0f,
                 0.0f, 0.9f, 0.9f, clrTitle );
    C2D_DrawRectSolid( 100.0f, 84.0f, 0, 120.0f, 1.5f, clrGold );

    C2D_TextGetDimensions( &s_contHint, 0.4f, 0.4f, &tw, &th );
    C2D_DrawText( &s_contHint, C2D_WithColor, ( 320.0f - tw ) / 2.0f, 90.0f,
                 0.0f, 0.4f, 0.4f, clrText );

    DrawContinueButton( &s_contBtn1, (float) LIVEMAP_CONT_BTN1_Y, clrGold, clrTitle );
    DrawContinueButton( state.overworld ? &s_contBtn2 : &s_contBtn2Dungeon,
                        (float) LIVEMAP_CONT_BTN2_Y, clrGold, clrTitle );
}

// Idle dim overlay (phase 13.6, battery): a black rect at state.dimAlpha
// drawn last, on top of whatever the current view drew, so the whole bottom
// screen (header included) darkens together rather than just the map/items
// content. No-op while dimAlpha is 0 (the common case).
static void DrawIdleDim( const LiveMapState& state )
{
    if ( state.dimAlpha == 0 )
        return;
    C2D_DrawRectSolid( 0, 0, 0, 320, 240, C2D_Color32( 0, 0, 0, state.dimAlpha ) );
}

// Draw the static, pre-baked wood-grain backdrop as a single textured quad.
// s_bottomBgImg's C2D_Image (from C2D_SpriteSheetGetImage) already carries
// the correct subtexture/UV info -- tex3ds handles the tiled-texture layout,
// nothing to do here but draw it.
static void DrawStaticBottomBackground()
{
    C2D_DrawImageAt( s_bottomBgImg, 0.0f, 0.0f, 0.0f, nullptr, 1.0f, 1.0f );
}

void Video_DrawBottomMap( const LiveMapState& state, uint32_t frame )
{
    // Dark wood-brown backdrop (was near-black) — the map now sits in a
    // framed "parchment window", styled after A Link Between Worlds' touch-
    // screen map (see phase 7.1/7.3 restyle note).
    const u32 clrWoodDark = C2D_Color32( 0x22, 0x16, 0x0C, 0xFF );
    const u32 clrGold       = C2D_Color32( 0xC8, 0xA0, 0x30, 0xFF );
    const u32 clrTitle      = C2D_Color32( 0xE8, 0xD8, 0xA8, 0xFF );
    // Visual polish pass: translucent instead of
    // fully opaque -- the wood-grain backdrop (DrawStaticBottomBackground(),
    // below) already sits directly under the grid (DrawGoldFrame() only
    // draws the border/corners, never an opaque fill), so an unseen cell
    // used to just paint flat over it. Letting the grain show through
    // faintly gives unexplored territory a "misty/unknown" texture instead
    // of a dead solid rectangle, while explored cells (BiomeColor(), still
    // fully opaque below) stay solid/vivid by contrast -- a small, free
    // (same single draw call, only the alpha changed) distinction between
    // "known" and "fog of war". A first pass at this, not a full restyle --
    // flag anything that still looks flat.
    const u32 clrUnseen     = C2D_Color32( 0x20, 0x1E, 0x1A, 0xB8 );
    const u32 clrGrid       = C2D_Color32( 0x3A, 0x30, 0x22, 0xFF );
    const u32 clrText       = C2D_Color32( 0xB0, 0xA8, 0x90, 0xFF );
    const u32 clrDunBadgeBg = C2D_Color32( 0x28, 0x20, 0x18, 0xFF );

    C2D_TargetClear( s_bottom, clrWoodDark );
    C2D_SceneBegin( s_bottom );

    // Item icons and the Items screen's digit counters go through the same
    // Graphics::DrawTile/DrawSpriteTile path normal gameplay drawing uses,
    // which is otherwise tuned for the top screen's play-view scale
    // (SetFullPlayView/SetNormalView). Reset to identity once, up front, so
    // those calls land at raw bottom-screen pixel coordinates instead — see
    // DrawItemIcon()/DrawNumber(). Nothing needs to restore this afterward:
    // the next frame's top-screen draw always sets its own view params
    // before drawing anything (WorldImpl::Draw()).
    Graphics::SetViewParams( 1.0f, 0.0f, 0.0f );

    // Wood-grain backdrop: a single blit of the static pre-baked texture
    // (see s_bottomBgTex's comment) instead of 840 per-frame speckle draws.
    // Falls back to live speckles only if the asset failed to load/init.
    if ( s_bottomBgTexOk )
        DrawStaticBottomBackground();
    else
    {
        DrawSpeckleField( 0, 0, 320, 240, 520, 0xB00Du, C2D_Color32( 0x00, 0x00, 0x00, 0x60 ) );
        DrawSpeckleField( 0, 0, 320, 240, 320, 0xC0FFu, C2D_Color32( 0x8A, 0x64, 0x34, 0x48 ) );
    }

    DrawFrame();

    if ( state.titleScreen )
    {
        DrawBootCompanion( clrGold, clrText, frame );
        return;
    }

    // Death continue screen: modern GAME OVER panel with two touch buttons,
    // instead of the header + map/UI (phase 8.4 follow-up).
    if ( state.continueScreen )
    {
        DrawContinuePanel( state, clrGold, clrTitle, clrText );
        return;
    }

    // Persistent header (phase 9.x): weapons / MAP-ITEMS toggle / hearts,
    // identical on every screen below it, plus the divider under it.
    DrawHeader( state, clrGold, clrText );
    C2D_DrawRectSolid( 10.0f, 58.0f, 0, 300.0f, 2.0f, clrGold );

    if ( state.view == LiveMapView_Options )
    {
        DrawOptionsScreen( state, clrGold, clrText );
        if ( state.slotsOpen )
            DrawSaveSlotsOverlay( state, clrGold, clrTitle, clrText );
        DrawIdleDim( state );
        return;
    }

    if ( state.view == LiveMapView_Music )
    {
        DrawMusicScreen( state, clrGold, clrText );
        DrawIdleDim( state );
        return;
    }

    if ( state.view == LiveMapView_Stats )
    {
        DrawStatsScreen( state, clrGold, clrText );
        DrawIdleDim( state );
        return;
    }

    if ( state.view == LiveMapView_Manual )
    {
        DrawManualScreen( state, clrGold, clrTitle, clrText );
        DrawIdleDim( state );
        return;
    }

    if ( state.view == LiveMapView_Items )
    {
        DrawItemsScreen( state, clrGold );
        DrawIdleDim( state );
        return;
    }

    if ( state.dungeonLevel != 0 )
    {
        DrawDungeonMap( state, frame, clrGold, clrTitle, clrGrid, clrText );
        DrawIdleDim( state );
        return;
    }

    // Phase 7.3 detail zoom: half the cells, twice the size, centred on the
    // player, filling the exact same on-screen grid footprint (gw/gh below
    // never change — only how many cells / how big each is does).
    bool zoomOk = state.playing && state.overworld
                && state.curRoom >= 0 && state.curRoom < LIVEMAP_COLS * LIVEMAP_ROWS;
    bool zoom = state.detailZoom && zoomOk;
    const int viewCols = zoom ? LIVEMAP_COLS / 2 : LIVEMAP_COLS;
    const int viewRows = zoom ? LIVEMAP_ROWS / 2 : LIVEMAP_ROWS;
    int vx0 = 0, vy0 = 0;
    if ( zoom )
    {
        int cc = state.curRoom % LIVEMAP_COLS, cr = state.curRoom / LIVEMAP_COLS;
        ClampViewport( cc, cr, viewCols, viewRows, LIVEMAP_COLS, LIVEMAP_ROWS, vx0, vy0 );
    }

    // Grid layout: 16x8 cells of 19x18 — size/shape untouched this pass, just
    // moved
    // down to sit below the new header (no title row needed here any more;
    // the MAP button already says as much). Constants shared with
    // LiveMap_state.cpp's ComputeMapCell() (phase 10.2) via livemap.h so the
    // two can't drift apart.
    const float gw = LIVEMAP_COLS * LIVEMAP_CELL_W, gh = LIVEMAP_ROWS * LIVEMAP_CELL_H;
    const float cw = gw / viewCols, ch = gh / viewRows;
    const float ox = LIVEMAP_GRID_OX;
    const float oy = LIVEMAP_GRID_OY;

    DrawGoldFrame( ox, oy, gw, gh, clrGold );

    for ( int r = vy0; r < vy0 + viewRows; r++ )
    {
        for ( int c = vx0; c < vx0 + viewCols; c++ )
        {
            int   room = r * LIVEMAP_COLS + c;
            float x = ox + (c - vx0) * cw;
            float y = oy + (r - vy0) * ch;

            // Visited cells: the real per-tile terrain thumbnail (water/
            // forest/mountain/etc, see s_terrainTex's comment) scaled up
            // to fill the cell -- one draw call, same as the flat rect it
            // replaces. Unvisited cells stay the plain translucent
            // clrUnseen fill (fog-of-war -- never reveal real terrain for
            // a room Link hasn't found yet). Atlas sub-image index == room
            // number directly (generator names files in that exact order).
            if ( state.visited[room] && s_terrainTexOk )
            {
                C2D_Image img = { &s_terrainTex, &s_terrainSub[room] };
                C2D_DrawImageAt( img, x, y, 0.0f, nullptr,
                                  (cw - 1) / img.subtex->width, (ch - 1) / img.subtex->height );
            }
            else
            {
                u32 fill = state.visited[room] ? BiomeColor( room ) : clrUnseen;
                C2D_DrawRectSolid( x, y, 0, cw - 1, ch - 1, fill );
            }

            // Subtle 3D: darker seam lines.
            C2D_DrawRectSolid( x + cw - 1, y, 0, 1, ch, clrGrid );
            C2D_DrawRectSolid( x, y + ch - 1, 0, cw, 1, clrGrid );
        }
    }

    // phase 11.1c: the grid itself was still flat solid-colour cells, the
    // other plain spot noted there (the Items grid above is the first).
    // One pass over the whole grid area, not per-cell -- per-cell would be
    // 128 extra DrawSpeckleField calls on the overworld, and phase 10.3
    // already found the hard way that per-cell draw calls on this grid blow
    // straight through the 5ms own-code-time budget. Even this single-call
    // form isn't free per speckle, though: a first attempt at count=260
    // measured live at +1.5-2ms bottomMap (slowframes.txt), pushing ownCode
    // over the 5ms ceiling on nearly every frame -- cut hard to 60, which
    // measured back within the existing baseline (see phase 11.1c).
    DrawSpeckleField( ox, oy, gw, gh, 24, 0x6A11u, C2D_Color32( 0x00, 0x00, 0x00, 0x30 ) );

    // Dungeon markers: round gold badge with the level number (the "where to
    // go" layer) — a small pin-like icon rather than a flat square tile.
    // Only drawn when inside the current viewport (may be scrolled out of
    // view while zoomed).
    //
    // Phase 7.5f (spoiler-light map): showing
    // all 9 at once on tiles Link has never even seen spoils the whole
    // quest-1 route in one glance. Keep the "where to go" hint (that's the
    // whole point of this layer, phase 3), but scope it to just the next
    // undone dungeon in sequence -- once a room is actually visited its
    // badge always stays (nothing left to spoil, and losing a marker for a
    // dungeon you've already found would just be confusing/feel like a bug).
    int nextDungeon = -1;
    for ( int d = 0; d < 9; d++ )
    {
        if ( !(state.triforcePieces & (1 << d)) ) { nextDungeon = d; break; }
    }
    for ( int d = 0; d < 9; d++ )
    {
        int room = state.quest2 ? kDungeonRoomsQuest2[d] : kDungeonRooms[d];
        if ( !state.visited[room] && d != nextDungeon )
            continue;
        int r = room / LIVEMAP_COLS, c = room % LIVEMAP_COLS;
        if ( c < vx0 || c >= vx0 + viewCols || r < vy0 || r >= vy0 + viewRows )
            continue;
        float cx = ox + (c - vx0) * cw + cw / 2.0f;
        float cy = oy + (r - vy0) * ch + ch / 2.0f;
        float s = cw / LIVEMAP_CELL_W;
        DrawFilledCircle( cx, cy, 7.5f * s, clrGold );
        DrawFilledCircle( cx, cy, 6.0f * s, clrDunBadgeBg );
        C2D_DrawText( &s_dungeonText[d], C2D_WithColor, cx - 3.0f * s, cy - 5.5f * s, 0.0f,
                     0.48f * s, 0.48f * s, clrGold );
    }

    // Overworld points-of-interest (phase 10.3): small square letter-badges,
    // deliberately a different shape from the round dungeon/pin badges so
    // they read as a distinct layer at a glance. Drawn along a cell's top
    // edge (dungeon badges own the centre, phase 10.2 pins own the bottom-
    // right corner) so the three layers can coexist in one cell without
    // overlapping. A cell can hold more than one badge (room 0x44 is both a
    // shop and a flute-warp destination) -- offset each additional one to
    // the right instead of picking just one, since which case that is isn't
    // hardcoded, only kPoiRooms's contents are.
    {
        int slotInCell[LIVEMAP_COLS * LIVEMAP_ROWS] = { 0 };
        int poiCount = GetPoiCount();
        for ( int i = 0; i < poiCount; i++ )
        {
            int room, kind;
            GetPoiByIndex( i, room, kind );
            // Phase 7.5e (spoiler-light map):
            // unlike the dungeon "where to go" layer above, shops/potion-
            // women/fairy fountains/etc. aren't meant to be findable from the
            // map before Link has actually found them on foot -- only reveal
            // the badge once the room itself is visited[]. The tap-to-open
            // info card's item CONTENTS get their own, separate gate
            // (LiveMap_state.cpp, phase 7.5g) for the same reason.
            if ( !state.visited[room] )
                continue;
            int r = room / LIVEMAP_COLS, c = room % LIVEMAP_COLS;
            if ( c < vx0 || c >= vx0 + viewCols || r < vy0 || r >= vy0 + viewRows )
                continue;
            float s = cw / LIVEMAP_CELL_W;
            float x = ox + (c - vx0) * cw + 2.0f * s + slotInCell[room] * 9.0f * s;
            float y = oy + (r - vy0) * ch + 2.0f * s;
            slotInCell[room]++;
            // Flat filled square, colour-only, no letter glyph and no
            // separate outline pass (phase 10.3 perf note: a bg-fill +
            // 4-rect-outline + C2D_DrawText version pushed 53 badges' worth
            // of draw calls well past the "no frame over 5ms own-code"
            // budget -- slowframes.txt showed bottomMap jump from its normal
            // ~1.6-2.4ms to ~4.7-5.2ms; dropping just the outline still left
            // it at ~3.9-4.7ms/~5.3-5.7ms ownCode, still over. C2D_DrawText's
            // font-texture bind is the expensive part, not the rect fills --
            // one draw call per badge instead of up to six. Meaning is
            // conveyed by colour + the legend instead of an on-badge letter,
            // same approach phase 10.2's custom pins already use.
            C2D_DrawRectSolid( x, y, 0, 7.0f * s, 7.0f * s, kPoiColors[kind] );

            // Phase 7.5i: the zoomed view's whole reason to exist is to show
            // something the un-zoomed one can't fit -- draw the POI's actual
            // primary item on top of the flat colour square, using the
            // spare room the doubled cell size (this whole function's `s`)
            // provides. Skipped at normal zoom (icon would be smaller than
            // the badge itself and just look like noise) and whenever
            // nothing resolved (warp/tunnel caves have no items -- animId
            // stays -1, DrawItemIcon() itself already no-ops on that).
            if ( zoom )
                DrawItemIcon( state.poiIcon[i], x + 3.5f * s, y + 3.5f * s );
        }
    }

    // Secret hints (phase 10.1, opt-in via the Options screen -- off by
    // default, same "preserve classic behaviour" convention as every other
    // QoL toggle). A small pulsing white dot at each known secret's actual
    // on-screen position within its room (kSecretRooms' fracX/fracY,
    // LiveMapGeometry.cpp) -- deliberately no colour/icon distinguishing
    // *what* the secret is (armos-stairs vs buried item): that would be a
    // fuller hint tier, not attempted this pass.
    // Read directly from Options rather than threaded through LiveMapState --
    // DrawOptionsScreen() already does the same (Options::Get() is a cheap
    // global singleton read, not per-frame game-state extraction).
    if ( Options::Get().ShowSecretHints )
    {
        int secretCount = GetSecretCount();
        for ( int i = 0; i < secretCount; i++ )
        {
            int room; float fracX, fracY;
            GetSecretByIndex( i, room, fracX, fracY );
            int r = room / LIVEMAP_COLS, c = room % LIVEMAP_COLS;
            if ( c < vx0 || c >= vx0 + viewCols || r < vy0 || r >= vy0 + viewRows )
                continue;
            float cellX = ox + (c - vx0) * cw;
            float cellY = oy + (r - vy0) * ch;
            float s = cw / LIVEMAP_CELL_W;
            int t = frame % 50;
            int tri = t < 25 ? t : 50 - t;
            u8 a = (u8) (0x90 + tri * 6);
            DrawFilledCircle( cellX + fracX * cw, cellY + fracY * ch, 3.0f * s,
                              C2D_Color32( 0xFF, 0xFF, 0xFF, a ) );
        }

        // phase 10.4: the bombable-wall / burnable-tree cave entrances. Map
        // philosophy: the full overworld map stays spoiler-light, so this
        // detailed layer is drawn ONLY when zoomed in. Bomb (grey, stone) vs burn (orange, fire) told
        // apart by colour; dark base ring for contrast on any biome, same
        // cheap DrawFilledCircle path as the pins (no text/icons -- keeps
        // bottomMap within the 5 ms budget verified in phase 10.6).
        if ( zoom )
        {
            int trCount = GetTreeRockSecretCount();
            for ( int i = 0; i < trCount; i++ )
            {
                int room; bool isBomb; float fracX, fracY;
                GetTreeRockSecretByIndex( i, room, isBomb, fracX, fracY );
                int r = room / LIVEMAP_COLS, c = room % LIVEMAP_COLS;
                if ( c < vx0 || c >= vx0 + viewCols || r < vy0 || r >= vy0 + viewRows )
                    continue;
                float s = cw / LIVEMAP_CELL_W;
                float px = ox + (c - vx0) * cw + fracX * cw;
                float py = oy + (r - vy0) * ch + fracY * ch;
                u32 clr = isBomb ? C2D_Color32( 0xB8, 0xB8, 0xC0, 0xFF )    // bomb: stone grey
                                 : C2D_Color32( 0xF0, 0x80, 0x28, 0xFF );  // burn: fire orange
                DrawFilledCircle( px, py, 3.6f * s, C2D_Color32( 0x18, 0x12, 0x0C, 0xFF ) );
                DrawFilledCircle( px, py, 2.4f * s, clr );
            }
        }
    }

    // Custom map pins (phase 10.2): drawn after the dungeon badges but before
    // the player marker, so a pin sharing a cell with the current-position
    // dot doesn't cover it. One colour-coded dot in the room's bottom-right
    // corner (rather than centred, where it would collide with dungeon
    // number badges) per non-zero LiveMapState::pins entry. Colours are
    // deliberately just colours (no icon/glyph) -- see LIVEMAP_PIN_TYPES's
    // comment in livemap.h for why a 4th "what does this mean" legend is
    // future work (phase 10.3), not required for place/cycle/remove to work.
    static const u32 s_pinColors[LIVEMAP_PIN_TYPES] =
    {
        C2D_Color32( 0xE0, 0x40, 0x30, 0xFF ),   // red    -- danger / remember
        C2D_Color32( 0x40, 0x90, 0xE8, 0xFF ),   // blue   -- investigate / secret
        C2D_Color32( 0x50, 0xB0, 0x58, 0xFF ),   // green  -- item / heart to fetch
        C2D_Color32( 0xE8, 0xC0, 0x30, 0xFF ),   // gold   -- treasure / misc
    };
    for ( int r = vy0; r < vy0 + viewRows; r++ )
    {
        for ( int c = vx0; c < vx0 + viewCols; c++ )
        {
            int room = r * LIVEMAP_COLS + c;
            uint8_t pin = state.pins[room];
            if ( pin == 0 || pin > LIVEMAP_PIN_TYPES )
                continue;
            float s = cw / LIVEMAP_CELL_W;
            float px = ox + (c - vx0) * cw + cw - 5.0f * s;
            float py = oy + (r - vy0) * ch + ch - 5.0f * s;
            DrawFilledCircle( px, py, 4.5f * s, C2D_Color32( 0x18, 0x12, 0x0C, 0xFF ) );
            DrawFilledCircle( px, py, 3.2f * s, s_pinColors[pin - 1] );
        }
    }

    // Current position marker (phase 7.2 — fine-grained, not just room-level).
    // Always inside the viewport by construction when zoomed (centred on it).
    if ( zoomOk )
    {
        int r = state.curRoom / LIVEMAP_COLS, c = state.curRoom % LIVEMAP_COLS;
        DrawPlayerMarker( ox + (c - vx0) * cw, oy + (r - vy0) * ch, cw, ch,
                          state.playerFracX, state.playerFracY, frame );
    }

    // On-demand legend button (phase 9.x — replaces the old always-on legend
    // row above). Only on the actual overworld screen, matching the touch
    // hit-test gate in LiveMap_state.cpp; skipped while dimmed for a cave
    // below, same as the old inline legend row never showed there either.
    if ( state.overworld )
        DrawLegendButton( clrGold );

    if ( state.playing && !state.overworld )
    {
        // Inside a cellar/cave (not a numbered dungeon — those return early
        // above via DrawDungeonMap): dim the map content, but not the
        // persistent header above it (weapons/toggle/hearts stay legible).
        C2D_DrawRectSolid( 0, 60.0f, 0, 320, 180.0f, C2D_Color32( 0, 0, 0, 0x60 ) );
    }

    if ( state.legendOpen )
        DrawLegendOverlay( clrGold, clrTitle );

    if ( state.infoRoom >= 0 )
        DrawInfoCard( state, clrGold, clrTitle );

    DrawIdleDim( state );
}

//===========================================================================
// Indexed image / tile sheet
//===========================================================================
struct ALLEGRO_BITMAP   // the opaque handle from platform_3ds_compat.h
{
    uint8_t* idx;                 // w*h palette indices (0..3)
    int      w, h, texW, texH;
    C3D_Tex  tex[PaletteCount];   // one expanded RGBA texture per palette row
    bool     texInit[PaletteCount];
    uint32_t gen[PaletteCount];   // palette generation each row was built at
    uint32_t rgba[PaletteCount][PaletteLength];  // resolved colours at last build
    char     debugPath[64];       // VRAM-usage measurement only, see LogSheetUse()
};

static ALLEGRO_BITMAP* sheets[Sheet_Max] = { 0 };
static Util::Table<SpriteAnim> animSpecs[Sheet_Max];
static void freeIndexed( ALLEGRO_BITMAP* b );   // defined below

// Small per-slot cache of recently-loaded tile-sheet bitmaps, keyed by path.
// Level transitions overwhelmingly reuse the same handful of sheet files
// (e.g. playerItem.png never changes; underworldTiles.png/uwNpcs.png are
// shared by all 9 dungeons) — LoadLevel() used to unconditionally re-read
// and rebuild these from romfs on every single transition. 3 entries/slot
// holds overworld+underworld+cellar (Background) or overworld+underworld
// (Npcs) simultaneously resident — World::Init() preloads them all at boot
// (see WorldImpl::Init() in World.cpp) so LoadLevel() never touches romfs
// for these again. Negligible VRAM cost at these sheet sizes.
static const int SheetCacheDepth = 3;
struct SheetCacheEntry
{
    char            path[64];
    ALLEGRO_BITMAP* bmp;
};
static SheetCacheEntry sheetCache[Sheet_Max][SheetCacheDepth];

// Returns a cached bitmap for (slot, path) if present (and moves it to MRU),
// else nullptr. On a fresh load, the caller must insertSheetCache() the result.
static ALLEGRO_BITMAP* findSheetCache( int slot, const char* path )
{
    SheetCacheEntry* c = sheetCache[slot];
    for ( int i = 0; i < SheetCacheDepth; i++ )
    {
        if ( c[i].bmp != nullptr && strcmp( c[i].path, path ) == 0 )
        {
            if ( i != 0 )
            {
                SheetCacheEntry tmp = c[0];
                c[0] = c[i];
                c[i] = tmp;
            }
            return c[0].bmp;
        }
    }
    return nullptr;
}

static void insertSheetCache( int slot, const char* path, ALLEGRO_BITMAP* bmp )
{
    SheetCacheEntry* c = sheetCache[slot];
    SheetCacheEntry& lru = c[SheetCacheDepth - 1];
    if ( lru.bmp != nullptr )
        freeIndexed( lru.bmp );
    for ( int i = SheetCacheDepth - 1; i > 0; i-- )
        c[i] = c[i - 1];
    strncpy( c[0].path, path, sizeof( c[0].path ) - 1 );
    c[0].path[sizeof( c[0].path ) - 1] = 0;
    c[0].bmp = bmp;
}

// Palette bookkeeping (mirrors Graphics.cpp).
static int      systemPalette[SysPaletteLength];
static int      grayscalePalette[SysPaletteLength];
static int*     activeSystemPalette = systemPalette;
static uint8_t  palettes[PaletteCount][PaletteLength];
static uint32_t paletteGen = 1;   // bumped on UpdatePalettes -> lazy re-expand

// View transform (256x240 NES view centred on the 400x240 top screen).
static float viewScale   = 1.0f;
static float viewOffsetX = 72.0f;
static float viewOffsetY = 0.0f;

//---------------------------------------------------------------------------
static int pow2up( int n )
{
    int p = 8;
    while ( p < n ) p <<= 1;
    return p;
}

// 3DS tiled (8x8 blocks, Morton order within block) pixel offset.
static inline u32 tiledOffset( u32 x, u32 y, u32 width )
{
    return (((y >> 3) * (width >> 3) + (x >> 3)) << 6)
         + ( (x & 1)        | ((y & 1) << 1)
           | ((x & 2) << 1) | ((y & 2) << 2)
           | ((x & 4) << 2) | ((y & 4) << 3) );
}

// Live-map terrain thumbnails from terrain.bin (RomExtract's BuildTerrain:
// 128 rooms x 11 rows x 16 columns, one terrain class per tile). The colours
// and classes come from TerrainClasses.gen.h (tools/gen-terrain-tables.py).
static void BuildTerrainTexture()
{
    const int Rows = 11, Cols = 16, Rooms = 128;
    const int TexW = 128, TexH = 256, CellsPerRow = TexW / 16;

    size_t size = 0;
    const unsigned char* terrain = AssetStore_Data( "terrain.bin", &size );
    if ( terrain == nullptr || size != (size_t) Rooms * Rows * Cols )
        return;
    if ( !C3D_TexInit( &s_terrainTex, TexW, TexH, GPU_RGBA4 ) )
        return;

    u16 rgba4[kTerrainClassCount];
    for ( int i = 0; i < kTerrainClassCount; i++ )
        rgba4[i] = (u16) ( ((kTerrainClassRgb[i][0] >> 4) << 12) | ((kTerrainClassRgb[i][1] >> 4) << 8)
                         | ((kTerrainClassRgb[i][2] >> 4) << 4) | 0xF );

    u16* dst = (u16*) s_terrainTex.data;
    memset( dst, 0, TexW * TexH * 2 );
    for ( int room = 0; room < Rooms; room++ )
    {
        int cx = (room % CellsPerRow) * 16;
        int cy = (room / CellsPerRow) * 16;
        const unsigned char* grid = terrain + (size_t) room * Rows * Cols;
        for ( int r = 0; r < Rows; r++ )
            for ( int c = 0; c < Cols; c++ )
                dst[ tiledOffset( cx + c, cy + r, TexW ) ] = rgba4[ grid[r * Cols + c] % kTerrainClassCount ];

        Tex3DS_SubTexture& st = s_terrainSub[room];
        st.width  = Cols;
        st.height = Rows;
        st.left   = (float) cx / TexW;
        st.right  = (float) (cx + Cols) / TexW;
        st.top    = 1.0f - (float) cy / TexH;
        st.bottom = 1.0f - (float) (cy + Rows) / TexH;
    }
    GSPGPU_FlushDataCache( s_terrainTex.data, TexW * TexH * 2 );
    // A deliberately blocky 1-pixel-per-tile mosaic: nearest filtering, and
    // it keeps neighbouring rooms from bleeding into a cell's edge when the
    // thumbnail is scaled up to fill it (it showed as purple edges).
    C3D_TexSetFilter( &s_terrainTex, GPU_NEAREST, GPU_NEAREST );
    C3D_TexFlush( &s_terrainTex );
    s_terrainTexOk = true;
}

static ALLEGRO_BITMAP* loadIndexed( const char* pngPath )
{
    // Assets are pre-converted to a raw index format: .png -> .bin
    char path[260];
    strncpy( path, pngPath, sizeof(path) - 1 );
    path[sizeof(path) - 1] = 0;
    char* dot = strrchr( path, '.' );
    if ( dot ) strcpy( dot, ".bin" ); else strcat( path, ".bin" );

    FILE* f = AssetStore_Open( path, "rb" );
    if ( f == nullptr )
        return nullptr;

    uint16_t wh[2] = { 0, 0 };
    if ( fread( wh, sizeof(uint16_t), 2, f ) != 2 ) { fclose( f ); return nullptr; }
    int w = wh[0], h = wh[1];

    ALLEGRO_BITMAP* b = new ALLEGRO_BITMAP();
    memset( b, 0, sizeof(*b) );
    b->w = w; b->h = h;
    b->texW = pow2up( w );
    b->texH = pow2up( h );
    b->idx = new uint8_t[w * h];
    fread( b->idx, 1, w * h, f );
    fclose( f );
    // VRAM-usage measurement (see LogSheetUse() below) -- just the filename,
    // not the full romfs path, so log lines stay short.
    const char* base = strrchr( pngPath, '/' );
    strncpy( b->debugPath, base ? base + 1 : pngPath, sizeof( b->debugPath ) - 1 );
    return b;
}

static void freeIndexed( ALLEGRO_BITMAP* b )
{
    if ( b == nullptr ) return;
    for ( int p = 0; p < PaletteCount; p++ )
        if ( b->texInit[p] ) C3D_TexDelete( &b->tex[p] );
    delete[] b->idx;
    delete b;
}

// Expand image `b` for palette row `row` into an RGBA tiled texture (lazy).
static void ensureExpanded( ALLEGRO_BITMAP* b, int row )
{
    if ( row < 0 || row >= PaletteCount || b == nullptr || b->idx == nullptr )
        return;
    if ( b->texInit[row] && b->gen[row] == paletteGen )
        return;   // already current this generation

    // Resolve the (up to 3) colours this row maps to. Index 0 is transparent.
    u32 rgba[PaletteLength];
    rgba[0] = 0;
    for ( int i = 1; i < PaletteLength; i++ )
    {
        int argb = activeSystemPalette[ palettes[row][i] ];
        u8 r  = (argb >> 16) & 0xFF;
        u8 g  = (argb >>  8) & 0xFF;
        u8 bl = (argb >>  0) & 0xFF;
        rgba[i] = (r << 24) | (g << 16) | (bl << 8) | 0xFF;
    }

    // Content-based invalidation: a palette-generation bump usually re-writes
    // the same colours (e.g. every same-context room load). If the resolved
    // colours are unchanged, the existing texture is still valid — just
    // re-stamp the generation and skip the expensive per-pixel re-expansion.
    if ( b->texInit[row] && memcmp( rgba, b->rgba[row], sizeof rgba ) == 0 )
    {
        b->gen[row] = paletteGen;
        return;
    }

    if ( !b->texInit[row] )
    {
        // RGBA4 (16-bit, 4 bits/channel) instead of RGBA8 -- halves every
        // tile-sheet texture's memory for no visible loss: the source data
        // only ever resolves to 4 flat colours per tile in the first place
        // (2-bit NES palette indices), so 8-bit-per-channel precision was
        // already wasted here. Bit layout confirmed against devkitPro's
        // own tex3ds encoder (source/encode.cpp): R(15-12) G(11-8) B(7-4)
        // A(3-0), truncating (>>4, not rounding) -- and unlike RGBA8/the
        // GPU colour-buffer formats, RGBA4 is *not* byte-reversed in
        // memory, so this packs directly into a u16 with no swap.
        //
        // Linear RAM, not VRAM. VRAM textures were tried again
        // once the earlier read-only-VRAM-mapping crash was root-caused and
        // fixed (see DEVELOPMENT.md "VRAM read-only mapping") -- CPU writes
        // into VRAM textures do work now. But turning tile sheets AND the new
        // bottom-screen background cache both VRAM-backed at once produced
        // real-hardware colour corruption -- the wood grain went red, and a
        // continue-screen photo showed a solid red block plus white fill
        // where gameplay tiles should be dark -- that the isolated
        // single-texture probe never hit. Almost certainly VRAM being
        // oversubscribed (the probe found only ~2 MB reliably allocatable)
        // causing allocations to alias one another rather than fail cleanly.
        // Since phase "SD-logging feedback loop" turned out to be the actual
        // cause of the real slowdown (see main_3ds.cpp's s_frameTrace
        // comment) -- not texture placement -- there's no longer a good
        // reason to take on VRAM's corruption risk for tile sheets. Plain
        // linear RAM only. (The separate persistent 1px red top-screen edge
        // predates this and is unrelated -- see Video_BlitTopScreen.)
        C3D_TexInit( &b->tex[row], b->texW, b->texH, GPU_RGBA4 );
        if ( b->tex[row].data == nullptr )
        {
            static bool loggedTexInitFail = false;
            if ( !loggedTexInitFail )
            {
                loggedTexInitFail = true;
                FILE* f = fopen( "sdmc:/3ds/zelda3ds/vram-fallback.txt", "w" );
                if ( f ) { fprintf( f, "tile-sheet row texture alloc failed (VRAM and linear) -- skipped\n" ); fclose( f ); }
            }
            return;
        }

        C3D_TexSetFilter( &b->tex[row], GPU_NEAREST, GPU_NEAREST );
        b->texInit[row] = true;
    }

    u16 rgba4[PaletteLength];
    for ( int i = 0; i < PaletteLength; i++ )
    {
        u8 r  = (rgba[i] >> 24) & 0xFF;
        u8 g  = (rgba[i] >> 16) & 0xFF;
        u8 bl = (rgba[i] >>  8) & 0xFF;
        u8 a  = (rgba[i] >>  0) & 0xFF;
        rgba4[i] = (u16) ( ((r >> 4) << 12) | ((g >> 4) << 8) | ((bl >> 4) << 4) | (a >> 4) );
    }

    u16* dst = (u16*) b->tex[row].data;
    memset( dst, 0, b->texW * b->texH * 2 );

    for ( int y = 0; y < b->h; y++ )
        for ( int x = 0; x < b->w; x++ )
            dst[ tiledOffset( x, y, b->texW ) ] = rgba4[ b->idx[y * b->w + x] & 3 ];

    GSPGPU_FlushDataCache( b->tex[row].data, b->texW * b->texH * 2 );
    // Phase 7.5a follow-up (on hardware: a red frame on the first load and
    // between screen changes -- the last frame before you can move again).
    // GSPGPU_FlushDataCache above only pushes
    // the CPU's just-written pixels out of the CPU dcache into main RAM --
    // it says nothing about the GPU's own texture unit cache, which can
    // still hold whatever it last sampled from this exact C3D_Tex object on
    // an earlier draw. This texture gets rewritten in place (not
    // reallocated) every time its resolved colours actually change
    // (paletteGen bump + content differs, see the memcmp early-out above),
    // which is precisely what happens on the very first room load (initial
    // palette set-up) and on every scroll/room transition that touches a
    // palette (SetLevelFgPalette()/UpdatePalettes(), called from GotoPlay())
    // -- exactly the two repro cases. Same underlying class of GPU-
    // side cache-coherency gap as phase 7.5a's C3D_FrameSplit fix (there it
    // was the top-screen offscreen render target read back as a texture
    // same-frame; here it's a tile-sheet texture rewritten and immediately
    // redrawn from, same frame) -- just a different texture object.
    // C3D_TexFlush() is citro3d's dedicated GPU-side texture-cache
    // invalidation for exactly this "I just wrote new data into this
    // existing C3D_Tex, make sure the GPU doesn't sample stale cached
    // texels" scenario.
    C3D_TexFlush( &b->tex[row] );
    memcpy( b->rgba[row], rgba, sizeof rgba );
    b->gen[row] = paletteGen;
}

// Temporary VRAM-usage measurement (VRAM/texture
// optimisation study): records which (sheet, palette row) combos are actually drawn
// from within each ~3s window, so a real playthrough answers "how many
// distinct combos are simultaneously live" instead of assuming the
// theoretical worst case (all 8 rows x every sheet). Test-only, same
// sdmc:/3ds/zelda3ds/*.txt convention as frametime.txt/slowframes.txt --
// remove once the measurement is done.
static const int LogWindowFrames = 180;   // ~3s at 60fps
struct SheetUseEntry { const char* path; int row; };
static SheetUseEntry s_seenThisWindow[64];
static int           s_seenCount = 0;
static uint32_t      s_windowStartFrame = 0;

static void LogSheetUse( const char* path, int row )
{
    // Test-only measurement, OFF unless sdmc:/zelda-frametrace.txt exists. It
    // runs per tile draw (drawRegion), and the dedup scan below is pure CPU
    // waste on the hot path in normal play -- checked once, then a single
    // early-out per call when disabled (the default).
    static int s_enabled = -1;
    if ( s_enabled < 0 )
    {
        FILE* fe = fopen( "sdmc:/zelda-frametrace.txt", "rb" );
        s_enabled = fe ? 1 : 0;
        if ( fe ) fclose( fe );
    }
    if ( !s_enabled ) return;

    for ( int i = 0; i < s_seenCount; i++ )
        if ( s_seenThisWindow[i].row == row && strcmp( s_seenThisWindow[i].path, path ) == 0 )
            return;   // already recorded this window
    if ( s_seenCount < (int) (sizeof( s_seenThisWindow ) / sizeof( s_seenThisWindow[0] )) )
        s_seenThisWindow[s_seenCount++] = { path, row };

    uint32_t frame = GetFrameCounter();
    if ( frame - s_windowStartFrame >= (uint32_t) LogWindowFrames )
    {
        FILE* f = fopen( "sdmc:/3ds/zelda3ds/vram-debug.txt", "a" );
        if ( f )
        {
            fprintf( f, "frame=%u combos=%d [", (unsigned int) frame, s_seenCount );
            for ( int i = 0; i < s_seenCount; i++ )
                fprintf( f, "%s%s:%d", i ? "," : "", s_seenThisWindow[i].path, s_seenThisWindow[i].row );
            fprintf( f, "]\n" );
            fclose( f );
        }
        s_seenCount = 0;
        s_windowStartFrame = frame;
    }
}

static void drawRegion( ALLEGRO_BITMAP* b, int palette,
                        int sx, int sy, int w, int h,
                        int dx, int dy, int flags )
{
    if ( b == nullptr ) return;
    s_topContentDrawn = true;
    ensureExpanded( b, palette );
    LogSheetUse( b->debugPath, palette );

    Tex3DS_SubTexture st;
    st.width  = w;
    st.height = h;
    float l  = (float) sx / b->texW;
    float r  = (float) (sx + w) / b->texW;
    float t  = 1.0f - (float) sy / b->texH;
    float bo = 1.0f - (float) (sy + h) / b->texH;
    if ( flags & ALLEGRO_FLIP_HORIZONTAL ) { float tmp = l; l = r; r = tmp; }
    st.left = l; st.right = r; st.top = t; st.bottom = bo;

    C2D_Image img = { &b->tex[palette], &st };
    float px = viewOffsetX + dx * viewScale;
    float py = viewOffsetY + dy * viewScale;

    // Phase 8.9 (hardware: sword-beam-down shot a wrong green
    // texture, "looks like part of Link"). This UV rect is bottom-up by
    // convention (t > bo normally, matching how ensureExpanded()'s Morton-
    // tiled textures are sampled -- see that function's own notes), which
    // every other sprite in the game relies on. Vertical flip used to swap
    // t/bo here, which is the *only* place in the whole codebase that ever
    // produces an inverted (top < bottom) Tex3DS_SubTexture -- horizontal
    // flip's left/right swap doesn't have this asymmetry, and Sword_Left
    // (flags=1, horizontal) is confirmed working on hardware while
    // Sword_Down (flags=2, vertical -- the only shipped user of vertical
    // flip) is the one reported broken. citro2d's own C2D_DrawImageAt
    // already has an official, documented mechanism for exactly this
    // (negative scaleY = vertical flip, base.h) that never touches the
    // source UV rect at all -- use that instead of hand-rolling it via an
    // inverted sub-texture rect.
    //
    // First attempt at this also shifted `py` down by one destination-height
    // before negating scaleY, on the assumption that a negative-height quad
    // extends *upward* from its anchor and the anchor would need to move to
    // compensate. Wrong: C2D_DrawImageAt's own doc comment says x/y is
    // always "the top left corner of the image" regardless of scale sign --
    // citro2d keeps that same bounding box and just flips the sample
    // direction internally. The extra shift landed the sprite one full
    // sprite-height too low (hardware-confirmed: one tile too low).
    // No py adjustment needed at all -- just negate scaleY.
    float scaleY = viewScale;
    if ( flags & ALLEGRO_FLIP_VERTICAL )
        scaleY = -viewScale;
    C2D_DrawImageAt( img, px, py, 0.0f, nullptr, viewScale, scaleY );
}

//===========================================================================
// Allegro helper stand-ins (declared in platform_3ds_compat.h)
//===========================================================================
ALLEGRO_COLOR al_map_rgb( unsigned char r, unsigned char g, unsigned char b )
{
    ALLEGRO_COLOR c = { r / 255.0f, g / 255.0f, b / 255.0f, 1.0f };
    return c;
}

ALLEGRO_COLOR al_map_rgba( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
    ALLEGRO_COLOR c = { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    return c;
}

void al_clear_to_color( ALLEGRO_COLOR color )
{
    // Fill the current clip rect (Allegro clears respect clipping); the whole
    // screen when no clip is active.
    s_topContentDrawn = true;
    u32 c = C2D_Color32f( color.r, color.g, color.b, color.a );
    if ( clipActive )
        C2D_DrawRectSolid( (float) clipX, (float) clipY, 0.0f, (float) clipW, (float) clipH, c );
    else
        C2D_DrawRectSolid( 0.0f, 0.0f, 0.0f, 400.0f, 240.0f, c );
}

ALLEGRO_BITMAP* al_load_bitmap( const char* path )
{
    return loadIndexed( path );
}

void al_destroy_bitmap( ALLEGRO_BITMAP* bmp )
{
    freeIndexed( bmp );
}

//===========================================================================
// Graphics
//===========================================================================
bool Graphics::Init()
{
    // Matches SetFullPlayView()'s phase 6.1 2x offscreen-buffer transform;
    // only matters before the first real SetFullPlayView()/SetNormalView()
    // call (nothing draws through this during that window in practice —
    // PrewarmSheet() only builds CPU-side texture data, no live scene).
    viewScale = 2.0f;
    viewOffsetX = 0.0f;
    viewOffsetY = -128.0f;
    return true;
}

void Graphics::LoadTileSheet( int slot, const char* path )
{
    ALLEGRO_BITMAP* cached = findSheetCache( slot, path );
    if ( cached != nullptr )
    {
        sheets[slot] = cached;
        return;
    }
    ALLEGRO_BITMAP* fresh = loadIndexed( path );
    insertSheetCache( slot, path, fresh );
    sheets[slot] = fresh;
}

void Graphics::LoadTileSheet( int slot, const char* imagePath, const char* animPath )
{
    LoadTileSheet( slot, imagePath );
    Util::LoadResource( animPath, &animSpecs[slot] );
}

void Graphics::PrewarmSheet( int slot )
{
    if ( slot < 0 || slot >= Sheet_Max || sheets[slot] == nullptr )
        return;
    for ( int row = 0; row < PaletteCount; row++ )
        ensureExpanded( sheets[slot], row );
}

const SpriteAnim* Graphics::GetAnimation( int slot, int animIndex )
{
    return animSpecs[slot].GetItem( animIndex );
}

void Graphics::LoadSystemPalette( const int* colorsArgb8 )
{
    memcpy( systemPalette, colorsArgb8, sizeof systemPalette );
    for ( int i = 0; i < SysPaletteLength; i++ )
        grayscalePalette[i] = systemPalette[i & 0x30];
}

ALLEGRO_COLOR Graphics::GetSystemColor( int sysColor )
{
    int argb8 = activeSystemPalette[sysColor];
    return al_map_rgba(
        (argb8 >> 16) & 0xFF,
        (argb8 >>  8) & 0xFF,
        (argb8 >>  0) & 0xFF,
        (argb8 >> 24) & 0xFF );
}

void Graphics::SetColor( int, int, int )
{
    // Not used on 3DS: colours come from palettes[][] + activeSystemPalette
    // at expansion time rather than a palette texture.
}

void Graphics::SetColorIndexed( int paletteIndex, int colorIndex, int sysColor )
{
    palettes[paletteIndex][colorIndex] = sysColor;
}

void Graphics::SetPaletteIndexed( int paletteIndex, const uint8_t* sysColors )
{
    memcpy( palettes[paletteIndex], sysColors, PaletteLength );
}

void Graphics::SetPalette( int, const int* )
{
}

void Graphics::UpdatePalettes()
{
    paletteGen++;   // invalidate all expanded textures; they re-expand lazily
}

void Graphics::SwitchSystemPalette( int* newSystemPalette )
{
    if ( activeSystemPalette == newSystemPalette )
        return;
    activeSystemPalette = newSystemPalette;
    UpdatePalettes();
}

void Graphics::EnableGrayscale()  { SwitchSystemPalette( grayscalePalette ); }
void Graphics::DisableGrayscale() { SwitchSystemPalette( systemPalette ); }

void Graphics::Begin() {}
void Graphics::End()   {}

void Graphics::DrawBitmap( ALLEGRO_BITMAP* bitmap, int srcX, int srcY,
                           int width, int height, int destX, int destY,
                           int palette, int flags )
{
    drawRegion( bitmap, palette, srcX, srcY, width, height, destX, destY, flags );
}

void Graphics::DrawTile( int slot, int srcX, int srcY, int width, int height,
                         int destX, int destY, int palette, int flags )
{
    drawRegion( sheets[slot], palette, srcX, srcY, width, height, destX, destY, flags );
}

void Graphics::DrawSpriteTile( int slot, int srcX, int srcY, int width, int height,
                               int destX, int destY, int palette, int flags )
{
    DrawTile( slot, srcX, srcY, width, height, destX, destY + 1, palette, flags );
}

void Graphics::SetViewParams( float scale, float x, float y )
{
    viewScale = scale;
    viewOffsetX = x;
    viewOffsetY = y;
}

bool Graphics::ShouldShowTopHud()
{
    // Bottom screen's live map already shows hearts (see Video_DrawBottomMap).
    return false;
}

bool Graphics::ShouldUseClassicPauseMenu()
{
    // The bottom screen's persistent header + Items screen (phase 8.7) already
    // show equipped items/hearts live, and its own SAVE button (phase 9.x)
    // covers manual save — the classic Start-opened inventory overlay would
    // just be a redundant, worse copy of the same info. See Graphics.h.
    return false;
}

void Graphics::FillBlack()
{
    // Real hardware/Azahar bug (the title screen went red at some point,
    // having been black at the start). Root cause not pinned down (index-0/
    // transparency handling for DrawItem/DrawString's RGBA4 tile textures is
    // the leading suspect, but unconfirmed) -- rather than keep chasing it,
    // force the backdrop black directly. Draws BEFORE DrawItem/DrawString in
    // DrawTitle(), so their sprites still layer on top normally; this only
    // guarantees what's behind them.
    //
    // First attempt drew exactly StdViewWidth x StdViewHeight (256x240) and
    // only covered the LEFT HALF of the screen -- C2D_DrawRectSolid evidently
    // doesn't get scaled by the active view the same way DrawItem/DrawString's
    // C2D_DrawImageAt path does (that renders correctly at the full 2x scale
    // DrawTitle() sets up). Rather than chase that mismatch too, drawn 4x
    // oversized here: harmless (this is the only thing on screen at this
    // point, and it's clipped to the real render target regardless), and
    // guarantees full coverage no matter which scale convention applies.
    s_topContentDrawn = true;
    C2D_DrawRectSolid( -(float) StdViewWidth, -(float) StdViewHeight, 0,
                       (float) StdViewWidth * 4, (float) StdViewHeight * 4,
                       C2D_Color32( 0, 0, 0, 0xFF ) );
}

// See Graphics.h for why this exists at all (background tiles are ~27%
// transparent, and across depth layers those holes look straight through to a
// parallax-shifted Ground). Coordinates are game coordinates, transformed the
// same way drawRegion() transforms a tile -- C2D_DrawRectSolid takes buffer
// coordinates, so the active view has to be applied by hand here exactly as it
// is there (FillBlack() above sidesteps that by drawing 4x oversized; a
// per-tile fill can't).
void Graphics::DrawTileBackdrop( int x, int y, int width, int height )
{
    s_topContentDrawn = true;
    C2D_DrawRectSolid( viewOffsetX + x * viewScale,
                       viewOffsetY + y * viewScale, 0.0f,
                       width * viewScale, height * viewScale,
                       C2D_Color32( 0, 0, 0, 0xFF ) );
}

void Graphics::SetFullPlayView()
{
    // phase 6.1: World::Draw() now renders into the offscreen 2x buffer
    // (s_topOffscreen, 512x352 = the 256x176 play area at a clean integer
    // scale), not the physical screen — so this is just "2x, with the
    // top-screen HUD band (StatusBar::StatusBarHeight = 64 NES-px,
    // hardcoded here rather than pulling in StatusBar.h for one constant)
    // cropped out of view." No more non-uniform stretch (was 1.36x vertical
    // vs 1.5625x horizontal — a visibly "squished" look); Video_BlitTopScreen() does the one real resize, bilinear +
    // 8:7-PAR-correct, when this buffer is drawn onto the actual screen.
    const float hudHeight = 64.0f;
    SetViewParams( 2.0f, 0.0f, -hudHeight * 2.0f );
}

void Graphics::SetNormalView()
{
    // Bugfix: this comment used to claim the path was
    // unreachable on 3DS since phase 8.8 disabled the classic pause menu —
    // wrong. WorldImpl::Draw() also routes Mode_Death/Mode_ContinueQuestion
    // through SetNormalView() (phase 6.4), independent of submenuOffsetY, so
    // it fires every time Link dies. That's how this bug was actually
    // found: the CONTINUE/SAVE/RETRY screen rendered cropped into the
    // upper-left with live gameplay bleeding through on the right/bottom —
    // same root cause as phase 11.1b's title-screen fix. This call left
    // viewScale at 1x while the offscreen buffer it draws into
    // (s_topOffscreen, phase 6.1) is sized for a *2x* canvas, same as
    // SetFullPlayView() accounts for. Scale doubled to match; the x-offset
    // (originally 72 at 1x, empirically centred the death/continue text)
    // doubles too, same "offset scales with viewScale" convention
    // SetFullPlayView() already uses for its -128 HUD-crop.
    SetViewParams( 2.0f, 144.0f, 0.0f );
}

void Graphics::SetTopLayer( TopLayer layer )
{
    // No-op when 3D is off (s_slider3D read once per frame in
    // Video_BeginFrame()) or the Ground/Fore buffers aren't available --
    // everything then keeps landing in s_topOffscreen exactly like before
    // phase 12.2b, since that's the target Video_BeginFrame() leaves active
    // (see s_layeredActive's comment).
    if ( !s_layeredActive )
        return;

    switch ( layer )
    {
    case TopLayer::Ground: C2D_SceneBegin( s_groundOffscreen ); break;
    case TopLayer::Mid:    C2D_SceneBegin( s_topOffscreen );    break;
    case TopLayer::Fore:   C2D_SceneBegin( s_foreOffscreen );   break;
    }
    s_curTopLayer = layer;
}

bool Graphics::IsTopLayered()
{
    return s_layeredActive;
}

TopLayer Graphics::GetTopLayer()
{
    // Mid is also what Video_BeginFrame() leaves active, so this is correct
    // (not just a placeholder) while layering is off.
    return s_layeredActive ? s_curTopLayer : TopLayer::Mid;
}

void Graphics::SetClip( int x, int y, int width, int height )
{
    // Same y-clamping as the desktop backend.
    int y2 = y + height;
    if ( y2 < 0 )                    { height = 0; y = 0; }
    else if ( y > StdViewHeight )    { height = 0; y = StdViewHeight; }
    else
    {
        if ( y < 0 )                 { height += y; y = 0; }
        if ( y2 > StdViewHeight )    { height = StdViewHeight - y; }
    }

    clipX = (int) (viewOffsetX + x * viewScale);
    clipY = (int) (viewOffsetY + y * viewScale);
    clipW = (int) (width * viewScale);
    clipH = (int) (height * viewScale);
    clipActive = true;

    // phase 6.1: this now clips the offscreen 2x buffer (s_topOffscreen), a
    // plain (non-screen-linked) render-to-texture target — no "240 -.../
    // 400 -..." flip needed any more. That flip specifically compensated
    // for the *physical* top screen's rotated hardware framebuffer layout
    // (C2D_CreateScreenTarget sets `linked=true`, which is what made
    // C2D_SceneBegin apply the 90-degree "tilt" projection); a target made
    // via C3D_RenderTargetCreateFromTex is never screen-linked, so C2D
    // gives it a plain, non-rotated projection instead — scissor coordinates
    // now just match the buffer's own pixel space directly.
    //
    // Phase 7.5a root cause: C3D_SetScissor's four coordinate
    // params are u32, but clipX/clipY above are signed and go negative
    // whenever a caller clips to NES-space y below 64 while
    // SetFullPlayView() is active (its -128 HUD-crop offset, cancelled by
    // TileMapBaseY==64 for every normal room draw -- but DrawLoadLevel()
    // deliberately clips to the *whole* screen, SetClip(0, 0, ...), landing
    // clipY at exactly -128). The implicit int->u32 conversion doesn't clamp,
    // it wraps to a huge value, handing the GPU an enormous, effectively
    // garbage scissor rect for that entire draw. Confirmed by dumping
    // s_topOffscreenTex's actual VRAM content right after World::Draw() on
    // the Title->LoadLevel transition frame: solid red (255,0,0) with
    // alpha=0 across the whole logical canvas, pre-blit -- a genuinely
    // corrupted draw, not a GPU-completion race (which every earlier fix
    // attempt in this file's history assumed). Clamping here (rather than
    // special-casing DrawLoadLevel/DrawWinGame's callers) covers every
    // current and future y<64 full-screen clip the same way.
    int scissorLeft   = clipX < 0 ? 0 : clipX;
    int scissorTop    = clipY < 0 ? 0 : clipY;
    int scissorRight  = clipX + clipW; if ( scissorRight  < scissorLeft ) scissorRight  = scissorLeft;
    int scissorBottom = clipY + clipH; if ( scissorBottom < scissorTop  ) scissorBottom = scissorTop;

    C2D_Flush();
    C3D_SetScissor( GPU_SCISSOR_NORMAL, scissorLeft, scissorTop, scissorRight, scissorBottom );
}

void Graphics::ResetClip()
{
    clipActive = false;
    C2D_Flush();
    C3D_SetScissor( GPU_SCISSOR_DISABLE, 0, 0, 0, 0 );
}

#endif // __3DS__
