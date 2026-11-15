#ifdef __3DS__
/*
   Live map state extractor. Includes World.h (so it must stay citro2d-free);
   tracks which overworld screens have been visited this session.
*/
#include "Common.h"
#include "World.h"
#include "Player.h"
#include "Profile.h"
#include "Input.h"
#include "ItemObj.h"
#include "Sound.h"
#include "SoundId.h"
#include "Options.h"
#include "SaveFolder.h"
#include "livemap.h"
#include "LiveMapGeometry.h"
#include <cstring>
#include <cstdio>
#include <sys/stat.h>

static uint8_t s_visited[LIVEMAP_COLS * LIVEMAP_ROWS];
static int     s_visitedSlot = -2;   // -2 = never loaded, -1 = real "no active profile"

// The explored map is per save slot. It used to be one single global file (`livemap.dat`), loaded once ever and
// never reloaded, so every save slot showed the same explored map regardless
// of which one was actually active. Fixed to mirror the per-slot pattern
// `pins_<slot>.dat` already used (see LoadPinsForSlot() below) instead of
// inventing a different one.
static const char* OldGlobalMapSavePath = "sdmc:/3ds/zelda3ds/livemap.dat";

static void VisitedPath( int slot, char* buf, size_t bufSize )
{
    snprintf( buf, bufSize, "sdmc:/3ds/zelda3ds/livemap_%d.dat", slot );
}

static void LoadVisitedForSlot( int slot )
{
    memset( s_visited, 0, sizeof s_visited );
    s_visitedSlot = slot;
    if ( slot < 0 )
        return;
    char path[64];
    VisitedPath( slot, path, sizeof path );
    FILE* f = fopen( path, "rb" );
    if ( f != nullptr )
    {
        fread( s_visited, 1, sizeof s_visited, f );
        fclose( f );
        return;
    }
    // One-time migration, whichever slot is active the first time this
    // per-slot code runs (not a hardcoded slot -- there's no way to know
    // which slot the old shared file's progress actually came from, since
    // being shared indiscriminately across every slot *was* the bug. The
    // slot being played right when the fix first takes effect is the best
    // available guess, and typically the one that matters). Every other
    // slot -- including this same one on any later boot -- starts clean.
    static bool s_migratedOldGlobal = false;
    if ( !s_migratedOldGlobal )
    {
        s_migratedOldGlobal = true;
        FILE* old = fopen( OldGlobalMapSavePath, "rb" );
        if ( old != nullptr )
        {
            fread( s_visited, 1, sizeof s_visited, old );
            fclose( old );
        }
    }
}

static void SaveVisited()
{
    if ( s_visitedSlot < 0 )
        return;
    mkdir( "sdmc:/3ds", 0777 );
    mkdir( "sdmc:/3ds/zelda3ds", 0777 );
    char path[64];
    VisitedPath( s_visitedSlot, path, sizeof path );
    FILE* f = fopen( path, "wb" );
    if ( f == nullptr ) return;
    fwrite( s_visited, 1, sizeof s_visited, f );
    fclose( f );
}

// Custom map pins (phase 10.2): unlike `visited` above, these are per save
// slot (different reminders on different slots make sense), so the path is keyed by World::GetProfileSlot() rather than a
// single fixed file. -2 is "never loaded"; -1 is the real "no active
// profile" sentinel World.cpp uses (see World.cpp's own -1-sentinel fix,
// phase 9.2) -- kept distinct so a genuine -1 doesn't look like "not checked
// yet" and skip re-checking once a real slot starts.
static uint8_t s_pins[LIVEMAP_COLS * LIVEMAP_ROWS];
static int     s_pinsSlot = -2;

static void PinsPath( int slot, char* buf, size_t bufSize )
{
    snprintf( buf, bufSize, "sdmc:/3ds/zelda3ds/pins_%d.dat", slot );
}

static void LoadPinsForSlot( int slot )
{
    memset( s_pins, 0, sizeof s_pins );
    s_pinsSlot = slot;
    if ( slot < 0 )
        return;
    char path[64];
    PinsPath( slot, path, sizeof path );
    FILE* f = fopen( path, "rb" );
    if ( f == nullptr ) return;
    fread( s_pins, 1, sizeof s_pins, f );
    fclose( f );
}

static void SavePins()
{
    if ( s_pinsSlot < 0 )
        return;
    mkdir( "sdmc:/3ds", 0777 );
    mkdir( "sdmc:/3ds/zelda3ds", 0777 );
    char path[64];
    PinsPath( s_pinsSlot, path, sizeof path );
    FILE* f = fopen( path, "wb" );
    if ( f == nullptr ) return;
    fwrite( s_pins, 1, sizeof s_pins, f );
    fclose( f );
}

// Phase 7.5c: the map must reset for a new save too -- the same class of bug phase 3 already fixed once for *switching*
// slots, but that fix never touched *starting a new game on*/*deleting* a
// slot: livemap_<slot>.dat/pins_<slot>.dat are only ever written by
// SaveVisited()/SavePins(), never removed, so New Game on an existing slot
// (or deleting one) left the old exploration/pins on disk -- the very next
// LoadVisitedForSlot()/LoadPinsForSlot() for that slot (any later boot, or
// even a same-session slot switch away and back) would happily reload the
// stale file. Deletes both files outright and, if that slot happens to be
// the one already loaded into memory, clears the in-memory arrays too and
// re-marks them as loaded (not "-2 never loaded") so a stray write from the
// same frame can't immediately recreate the just-deleted file with old data.
static void ResetSlotLiveMapFiles( int slot )
{
    char path[64];
    VisitedPath( slot, path, sizeof path );
    remove( path );
    PinsPath( slot, path, sizeof path );
    remove( path );

    if ( s_visitedSlot == slot )
    {
        memset( s_visited, 0, sizeof s_visited );
    }
    if ( s_pinsSlot == slot )
    {
        memset( s_pins, 0, sizeof s_pins );
    }
}

// Phase 14.3 follow-up (save-slot copy): duplicates srcSlot's
// explored-map/pins files to dstSlot -- a straight byte-copy of whatever's
// on disk (a source that's never explored anything just leaves dstSlot
// without a file either, same as a fresh slot). Called alongside
// World::CopySlot() so a copied save arrives with its map already filled
// in, matching "duplicate this save" rather than "start a fresh map on an
// old profile". Overwrites dstSlot's files outright, which also correctly
// clears out any stale leftovers from a slot that held a different,
// since-deleted save (World::DeleteSlot() doesn't itself touch these).
static void CopySlotLiveMapFiles( int srcSlot, int dstSlot )
{
    mkdir( "sdmc:/3ds", 0777 );
    mkdir( "sdmc:/3ds/zelda3ds", 0777 );

    char srcPath[64], dstPath[64];
    uint8_t buf[LIVEMAP_COLS * LIVEMAP_ROWS];

    VisitedPath( srcSlot, srcPath, sizeof srcPath );
    VisitedPath( dstSlot, dstPath, sizeof dstPath );
    memset( buf, 0, sizeof buf );
    FILE* f = fopen( srcPath, "rb" );
    if ( f != nullptr ) { fread( buf, 1, sizeof buf, f ); fclose( f ); }
    f = fopen( dstPath, "wb" );
    if ( f != nullptr ) { fwrite( buf, 1, sizeof buf, f ); fclose( f ); }
    if ( s_visitedSlot == dstSlot )
        memcpy( s_visited, buf, sizeof s_visited );

    PinsPath( srcSlot, srcPath, sizeof srcPath );
    PinsPath( dstSlot, dstPath, sizeof dstPath );
    memset( buf, 0, sizeof buf );
    f = fopen( srcPath, "rb" );
    if ( f != nullptr ) { fread( buf, 1, sizeof buf, f ); fclose( f ); }
    f = fopen( dstPath, "wb" );
    if ( f != nullptr ) { fwrite( buf, 1, sizeof buf, f ); fclose( f ); }
    if ( s_pinsSlot == dstSlot )
        memcpy( s_pins, buf, sizeof s_pins );
}

// ComputeMapCell()/CyclePin() (phase 10.2 tap-to-cycle math) now live in
// LiveMapGeometry.h/.cpp -- pure functions with no World.h/citro2d
// dependency, split out so they can be unit-tested on the host directly
// (see zelda3ds/tests/) instead of only indirectly via a live Azahar tap.

// Debug mode: if sdmc:/zelda3ds-debug.txt exists, Link is invincible.
// (Test/dev aid; absent on a normal SD card, so releases are unaffected.)
static bool s_debugChecked = false;
static bool s_debugMode = false;

static void DebugUpdate()
{
    if ( !s_debugChecked )
    {
        s_debugChecked = true;
        FILE* f = fopen( "sdmc:/zelda3ds-debug.txt", "r" );
        if ( f != nullptr ) { s_debugMode = true; fclose( f ); }
    }

    if ( s_debugMode )
    {
        Player* player = World::GetPlayer();
        if ( player != nullptr )
            player->SetInvincibilityTimer( 0x10 );
    }
}

// Fixed-order grid of the classic quest's ownable items (mirrors the pause
// menu's slot lists in Submenu.cpp), for the Items screen (phase 9.x).
static const uint8_t s_gridSlots[LIVEMAP_ITEM_GRID_SLOTS] =
{
    ItemSlot_Boomerang, ItemSlot_Bombs, ItemSlot_Arrow, ItemSlot_Bow,
    ItemSlot_Candle, ItemSlot_Recorder, ItemSlot_Food, ItemSlot_Letter,
    ItemSlot_Rod, ItemSlot_Raft, ItemSlot_Book, ItemSlot_Ring,
    ItemSlot_Ladder, ItemSlot_MagicKey, ItemSlot_Bracelet,
};

// The Letter slot's displayed/assignable item becomes Potion once one is
// owned (mirrors Submenu.cpp's pause-menu quick-slot logic — the letter
// isn't cleared from the save, the potion just takes priority). Shared by
// icon resolution and the touch-to-assign handler (phase 8.2) below so both
// agree on what a tap on that grid cell actually means.
static int EffectiveGridSlot( int itemSlot )
{
    if ( itemSlot == ItemSlot_Letter && World::GetProfile().Items[ItemSlot_Potion] != 0 )
        return ItemSlot_Potion;
    return itemSlot;
}

// Resolves an owned-item slot to its icon; { -1, -1 } if nothing to show
// (itemSlot == 0, i.e. no B/Y item assigned yet, or the item isn't owned).
static LiveMapIcon ResolveIcon( int itemSlot )
{
    LiveMapIcon icon = { -1, -1 };
    if ( itemSlot == 0 )
        return icon;

    itemSlot = EffectiveGridSlot( itemSlot );

    Profile& profile = World::GetProfile();
    int value = profile.Items[itemSlot];
    if ( value == 0 )
        return icon;

    int itemId = ItemValueToItemId( itemSlot, value );
    int animId, palette;
    if ( GetItemIcon( itemId, animId, palette ) )
    {
        icon.animId = (int8_t) animId;
        icon.palette = (int8_t) palette;
    }
    return icon;
}

// Resolves a raw ItemId (not an owned-item-slot value like ResolveIcon()
// above -- phase 10.5's info card shows what a shop/cave *offers*, not what
// the player owns) straight to its icon. { -1, -1 } if the sheet lookup
// fails (shouldn't happen for any of the fixed IDs GetPoiItems() below uses).
static LiveMapIcon ResolveIconById( int itemId )
{
    LiveMapIcon icon = { -1, -1 };
    int animId, palette;
    if ( GetItemIcon( itemId, animId, palette ) )
    {
        icon.animId = (int8_t) animId;
        icon.palette = (int8_t) palette;
    }
    return icon;
}

// phase 10.5: what a Shop/Potion/Gift-cave POI room actually offers, for the
// info card. Read from the same real CaveSpecList data as the phase 10.3
// room lists (phase 10.3/10.5 has the full offline-dump writeup) --
// Potion and Gift caves sell/give the same fixed set regardless of which of
// their rooms you're in; Shop varies by which of the 4 CaveSpecList shop
// entries (13/14/15/16) a given room maps to, so that one needs a per-room
// table. Returns false (no items to show) for the other POI kinds and for
// dungeon entrances -- those cards are text-only.
static bool GetPoiItems( int room, int kind, int items[3] )
{
    if ( kind == Poi_Potion )
    {
        items[0] = Item_BluePotion; items[1] = Item_None; items[2] = Item_RedPotion;
        return true;
    }
    if ( kind == Poi_Gift )
    {
        items[0] = Item_RedPotion; items[1] = Item_None; items[2] = Item_HeartContainer;
        return true;
    }
    if ( kind == Poi_Shop )
    {
        struct ShopItems { uint8_t room; uint8_t items[3]; };
        static const ShopItems s_shopItems[] =
        {
            // CaveSpecList index 13: merchant
            { 0x25, { Item_MagicShield, Item_Bomb, Item_WoodArrow } },
            { 0x44, { Item_MagicShield, Item_Bomb, Item_WoodArrow } },
            { 0x4A, { Item_MagicShield, Item_Bomb, Item_WoodArrow } },
            { 0x6F, { Item_MagicShield, Item_Bomb, Item_WoodArrow } },
            // CaveSpecList index 14: merchant (different stock)
            { 0x0C, { Item_MagicShield, Item_Key, Item_BlueCandle } },
            { 0x15, { Item_MagicShield, Item_Key, Item_BlueCandle } },
            { 0x5E, { Item_MagicShield, Item_Key, Item_BlueCandle } },
            { 0x66, { Item_MagicShield, Item_Key, Item_BlueCandle } },
            // CaveSpecList index 15: expensive merchant
            { 0x12, { Item_MagicShield, Item_Food, Item_Heart } },
            { 0x26, { Item_MagicShield, Item_Food, Item_Heart } },
            { 0x46, { Item_MagicShield, Item_Food, Item_Heart } },
            { 0x4D, { Item_MagicShield, Item_Food, Item_Heart } },
            // CaveSpecList index 16: expensive merchant (different stock)
            { 0x34, { Item_Key, Item_BlueRing, Item_Food } },
        };
        for ( unsigned int i = 0; i < sizeof( s_shopItems ) / sizeof( s_shopItems[0] ); i++ )
        {
            if ( s_shopItems[i].room == room )
            {
                items[0] = s_shopItems[i].items[0];
                items[1] = s_shopItems[i].items[1];
                items[2] = s_shopItems[i].items[2];
                return true;
            }
        }
    }
    return false;
}

// The sword (A slot) needs its own resolver: ItemSlot_Sword is literally 0,
// which ResolveIcon() above treats as "nothing assigned" (the sentinel
// profile.SelectedItem/Player::SelectedItem2 use for "no B/Y item picked
// yet"). Unlike B/Y, the sword has no such "unassigned" state — you either
// own a sword level or you don't — so this skips that guard entirely.
static LiveMapIcon ResolveSwordIcon()
{
    LiveMapIcon icon = { -1, -1 };
    Profile& profile = World::GetProfile();
    int value = profile.Items[ItemSlot_Sword];
    if ( value == 0 )
        return icon;

    int itemId = ItemValueToItemId( ItemSlot_Sword, value );
    int animId, palette;
    if ( GetItemIcon( itemId, animId, palette ) )
    {
        icon.animId = (int8_t) animId;
        icon.palette = (int8_t) palette;
    }
    return icon;
}

void LiveMap_Update( LiveMapState& state )
{
    int profileSlot = World::GetProfileSlot();
    if ( profileSlot != s_visitedSlot )
        LoadVisitedForSlot( profileSlot );
    if ( profileSlot != s_pinsSlot )
        LoadPinsForSlot( profileSlot );

    DebugUpdate();

    GameMode mode = World::GetMode();

    // phase 5.1b: see livemap.h's skipBottomMapDraw comment. Counts
    // consecutive Mode_LoadLevel frames; resets the instant loading ends
    // (or never started), so the very next load always redraws normally
    // for its own first 2 frames regardless of how the previous one ended.
    static int s_loadFrames = 0;
    if ( mode == Mode_LoadLevel )
        s_loadFrames++;
    else
        s_loadFrames = 0;
    state.skipBottomMapDraw = s_loadFrames > 2;

    state.playing   = World::IsPlaying( mode ) || mode == Mode_Scroll
                   || mode == Mode_Enter || mode == Mode_Leave;
    state.overworld = World::IsOverworld();
    // Phase 14.2: set early (before the touch-handling block further down),
    // unlike visited[]/triforcePieces below which stay read from their raw
    // source there -- same reasoning, just done up front this time instead
    // of adding another raw-read workaround.
    state.quest2 = World::GetProfile().Quest == 1;
    // Walking into another room used to show the player marker on the far
    // side of the room first: GetRoomId() already flips to the destination room the
    // instant the scroll starts (World.cpp's UpdateScroll_LoadRoom() calls
    // LoadRoom() up front so the new room's tiles are ready to pan in),
    // several frames before the player's actual on-screen position reaches
    // it. Using it here made the marker jump to wherever the player's
    // still-old-room-relative X/Y happened to land inside the *new* room's
    // frame, then visibly correct itself once the per-frame scroll caught
    // position up to match. Stay on the room being scrolled *out of* for the
    // whole transition instead, matching what the top screen is actually
    // showing until GotoEnter() ends Mode_Scroll.
    state.curRoom   = mode == Mode_Scroll ? World::GetScrollOldRoomId() : World::GetRoomId();
    state.titleScreen = mode == Mode_Title;
    state.continueScreen = mode == Mode_ContinueQuestion;

    // Select toggles a 2x close-up centred on the player (phase 7.3: two zoom
    // levels). Unused by the shared game code during
    // normal play (Select only means something on the boot slot-select hold
    // and the game-over continue screen — see World.cpp), so it's free to
    // repurpose here without touching shared code. New 3DS ZL/ZR (phase 13.5,
    // InputButtons::Zoom) does the same thing, as a more natural one-handed
    // shortcut than reaching for Select -- purely additive, Select still works.
    static bool s_detailZoom = false;
    if ( state.playing && (Input::IsButtonPressing( InputButtons::Select )
                         || Input::IsButtonPressing( InputButtons::Zoom )) )
        s_detailZoom = !s_detailZoom;
    state.detailZoom = s_detailZoom;

    // Which bottom-screen view is showing. Used to X-cycle through Map ->
    // Items -> Options -> Music -> Stats -> Bestiary -> Map (phase 9.x/11.1/
    // 13.4). Hidden menus don't work: an unlabelled button with no on-screen
    // affordance isn't discoverable. Map/Items/Options keep their direct
    // header tabs (touch, checked below); Music/Stats/Manual are now
    // reached via visible, tappable action rows at the bottom of the
    // Options list instead (LIVEMAP_OPT_ACTION_ROWS, checked further down),
    // the same "tap a row to act" pattern SAVE SLOTS already used.
    static uint8_t s_view = LiveMapView_Map;

    // Phase 7.5d (first hardware play session): Start does
    // nothing during normal 3DS play -- the classic Start-opened pause/
    // inventory subscreen is disabled here (phase 8.8), and Select/ZL/ZR are
    // already claimed by the map zoom (phase 7.3/13.5) -- so there was no
    // reachable "pause" gesture left for a button-only player (no touch).
    // Make it a direct shortcut into the Options tab, which already doubles
    // as pause (World::SetPaused() below, same as tapping the OPT tab/
    // X-cycling into it). Pressing it again should go back to playing -- now
    // a real toggle: Start while already paused (s_view == Options) resumes
    // play instead of just re-opening the same screen; Start from anywhere
    // else still jumps straight to Options as before.
    if ( state.playing && Input::IsButtonPressing( InputButtons::Start ) )
        s_view = ( s_view == LiveMapView_Options ) ? LiveMapView_Map : LiveMapView_Options;

    // Phase 11.1: the four phase-11 settings, tapped directly here instead of
    // hidden behind Start/Y+Start/B+Start (phase 11.2-11.4's shipped-first
    // stand-in). One row per setting: a pointer straight at
    // the live GameOptions field keeps this table and Graphics_3ds.cpp's
    // drawing of the same rows from drifting apart, and means adding a
    // future option is a one-line addition here.
    struct OptionRow { uint8_t* field; };
    static OptionRow s_optionRows[LIVEMAP_OPT_TOGGLE_ROWS] =
    {
        { &Options::Get().TextSpeed },
        { &Options::Get().LowHealthBeep },
        { &Options::Get().SwordBeamStrict },
        { &Options::Get().TextVariant },
        { &Options::Get().SkipTitleScreen },
        { &Options::Get().ShowSecretHints },
        { &Options::Get().ShowFpsCounter },
    };

    // On-demand map legend (replaces the old always-on corner text): a tap
    // on the "?" button opens it, a tap anywhere while open closes it again.
    // Only meaningful on the map screen — force-closed elsewhere so it can't
    // get stuck open across a view/mode switch.
    static bool s_legendOpen = false;
    static int  s_infoRoom = -1;   // phase 10.5: room whose info card is open, -1 = closed
    static int  s_playingSongId = -1;   // phase 13.4: music player's "now playing" row, -1 = stopped
    static int  s_saveFlashFrames = 0;
    // Save-slot management overlay: opened from a button on the Options
    // screen (no hidden button-hold to switch/delete a save).
    static bool s_slotsOpen = false;
    static int  s_pendingDeleteSlot = -1;   // 2-tap delete confirm, -1 = none armed
    static int  s_pendingNewGameSlot = -1;  // phase 14.1 QUEST 1/2 choice armed on this empty slot, -1 = none
    static int  s_deleteArmFrames = 0;      // counts down; hits 0 -> auto-disarm
    // Phase 14.3 follow-up (save-slot copy): a dedicated COPY
    // toggle in the overlay's title bar, rather than squeezing a second icon
    // into the already-tight per-row layout (hearts/deaths/triforce/playtime
    // + the delete icon already fill it, see DrawSaveSlotsOverlay). Tapping
    // COPY arms copy mode; tapping any active row then picks/re-picks the
    // source (tap it again to cancel); tapping an empty row with a source
    // armed performs the copy. Target must be empty -- overwriting an
    // existing slot would need its own confirmation UX, not built here.
    static bool s_copyMode = false;
    static int  s_copySourceSlot = -1;      // -1 = none armed yet
    static int  s_optScroll = 0;   // Options screen scroll offset (phase save-slots)
    static int  s_manualScroll = 0;   // Manual screen scroll offset (phase 13.2 expansion)
    int touchX, touchY;
    // Read once, unconditionally, so the idle-dim tracker below always sees
    // this frame's touch activity too -- GetTouchPressed() is a pure read of
    // a flag Input::Update() already set this frame (Input_3ds.cpp), so
    // calling it here instead of only inside the `state.playing` branch
    // below doesn't change what it reports or consume anything.
    bool touched = Input::GetTouchPressed( touchX, touchY );

    // Idle bottom-screen dim (phase 13.6, battery): any button or touch
    // activity resets the idle counter and snaps dimAlpha back to 0 the same
    // frame; past LIVEMAP_DIM_IDLE_FRAMES of nothing, it fades in over
    // LIVEMAP_DIM_FADE_FRAMES. Scoped to actual gameplay (state.playing) --
    // the title screen and menus have their own reasons to stay lit.
    static uint32_t s_idleFrames = 0;
    bool inputActive = Input::GetButtons().Buttons != 0 || touched;
    if ( !state.playing || inputActive )
        s_idleFrames = 0;
    else
        s_idleFrames++;

    if ( s_idleFrames <= LIVEMAP_DIM_IDLE_FRAMES )
    {
        state.dimAlpha = 0;
    }
    else
    {
        uint32_t fadeFrames = s_idleFrames - LIVEMAP_DIM_IDLE_FRAMES;
        state.dimAlpha = (uint8_t) ( fadeFrames >= LIVEMAP_DIM_FADE_FRAMES
            ? LIVEMAP_DIM_MAX_ALPHA
            : (fadeFrames * LIVEMAP_DIM_MAX_ALPHA) / LIVEMAP_DIM_FADE_FRAMES );
    }

    // Death continue screen: two touch buttons (not gated by state.playing --
    // Mode_ContinueQuestion isn't a "playing" mode). CONTINUE (1) resumes on
    // the screen you died; CONTINUE FROM START (2) restarts at the start tile.
    // World::ChooseContinue feeds WorldImpl::UpdateContinueQuestion.
    if ( state.continueScreen && touched )
    {
        if ( touchX >= LIVEMAP_CONT_BTN_X && touchX < LIVEMAP_CONT_BTN_X + LIVEMAP_CONT_BTN_W )
        {
            if ( touchY >= LIVEMAP_CONT_BTN1_Y && touchY < LIVEMAP_CONT_BTN1_Y + LIVEMAP_CONT_BTN_H )
                World::ChooseContinue( 1 );
            else if ( touchY >= LIVEMAP_CONT_BTN2_Y && touchY < LIVEMAP_CONT_BTN2_Y + LIVEMAP_CONT_BTN_H )
                World::ChooseContinue( 2 );
        }
    }

    if ( state.playing && touched )
    {
        if ( s_infoRoom >= 0 )
        {
            // phase 10.5: any tap while the info card is open just closes it
            // (same "tap anywhere to dismiss" convention as the legend
            // popup below), checked first so it can't also register as a
            // tab/pin/etc. tap on the same touch.
            s_infoRoom = -1;
        }
        else if ( s_legendOpen )
        {
            s_legendOpen = false;
        }
        else if ( s_slotsOpen && touchX >= LIVEMAP_SLOTS_COPY_X && touchX < LIVEMAP_SLOTS_COPY_X + LIVEMAP_SLOTS_COPY_W
            && touchY >= LIVEMAP_SLOTS_COPY_Y && touchY < LIVEMAP_SLOTS_COPY_Y + LIVEMAP_SLOTS_COPY_H )
        {
            // Phase 14.3 follow-up: COPY toggle, checked before the row-area
            // hit-test below -- its X range sits inside LIVEMAP_SLOTS_X..
            // +LIVEMAP_SLOTS_W but its Y is above every row (LIVEMAP_SLOTS_Y0),
            // so it needs its own branch rather than falling through to
            // "missed every row -> close the overlay".
            s_copyMode = !s_copyMode;
            s_copySourceSlot = -1;
            s_pendingDeleteSlot = -1;
            s_pendingNewGameSlot = -1;
            Sound::PlayEffect( SEffect_cursor );
        }
        else if ( s_slotsOpen )
        {
            bool hitRow = false;
            if ( touchX >= LIVEMAP_SLOTS_X && touchX < LIVEMAP_SLOTS_X + LIVEMAP_SLOTS_W )
            {
                for ( int i = 0; i < LIVEMAP_SLOTS_ROWS; i++ )
                {
                    float rowY = LIVEMAP_SLOTS_Y0 + i * LIVEMAP_SLOTS_ROW_H;
                    if ( touchY < rowY || touchY >= rowY + LIVEMAP_SLOTS_ROW_H )
                        continue;
                    hitRow = true;

                    ProfileSummarySnapshot summaries;
                    SaveFolder::ReadSummaries( summaries );
                    bool active = summaries.Summaries[i].IsActive();

                    if ( s_copyMode )
                    {
                        // Copy mode intercepts every row tap -- including
                        // what would normally be the delete-icon zone on an
                        // active row -- to pick a source/destination instead
                        // of delete/switch/new-game.
                        if ( active )
                        {
                            // Tapping the already-armed source again cancels
                            // it (a natural toggle-off); tapping a different
                            // active row just re-arms to that one instead.
                            s_copySourceSlot = ( s_copySourceSlot == i ) ? -1 : i;
                            Sound::PlayEffect( SEffect_cursor );
                        }
                        else if ( s_copySourceSlot >= 0 )
                        {
                            World::CopySlot( s_copySourceSlot, i );
                            CopySlotLiveMapFiles( s_copySourceSlot, i );
                            s_copyMode = false;
                            s_copySourceSlot = -1;
                            Sound::PlayEffect( SEffect_secret );
                        }
                        // Empty row tapped with no source armed yet: no-op
                        // (nothing to paste) -- no sound, so a stray tap
                        // before picking a source doesn't feel like it did
                        // something.
                        break;
                    }

                    float delX = LIVEMAP_SLOTS_X + LIVEMAP_SLOTS_W - LIVEMAP_SLOTS_DEL_W;

                    if ( active && touchX >= delX )
                    {
                        s_pendingNewGameSlot = -1;
                        // Delete icon: first tap arms a short confirm window,
                        // a second tap on the *same* icon before it expires
                        // commits. Tapping anything else (including a
                        // different row's delete icon) just re-arms/disarms
                        // rather than stacking multiple pending deletes.
                        if ( s_pendingDeleteSlot == i )
                        {
                            World::DeleteSlot( i );
                            ResetSlotLiveMapFiles( i );
                            s_pendingDeleteSlot = -1;
                            Sound::PlayEffect( SEffect_secret );
                        }
                        else
                        {
                            s_pendingDeleteSlot = i;
                            s_deleteArmFrames = 180;   // ~3s to confirm
                            Sound::PlayEffect( SEffect_cursor );
                        }
                    }
                    else if ( active )
                    {
                        s_pendingDeleteSlot = -1;
                        s_pendingNewGameSlot = -1;
                        if ( i != World::GetProfileSlot() )
                        {
                            World::SwitchToSlot( i );
                            s_slotsOpen = false;
                            Sound::PlayEffect( SEffect_secret );
                        }
                        // i == current slot: already playing this one --
                        // tapping its row is a harmless no-op (falls through
                        // without closing, so a stray re-tap can't be
                        // mistaken for a restart).
                    }
                    else   // empty slot
                    {
                        s_pendingDeleteSlot = -1;
                        if ( !Options::Get().Quest2Unlocked )
                        {
                            // Quest 2 isn't reachable yet on any slot -- skip
                            // the choice UI, same one-tap-and-go behaviour
                            // this always had.
                            World::StartNewGameInSlot( i, 0 );
                            ResetSlotLiveMapFiles( i );
                            s_slotsOpen = false;
                            Sound::PlayEffect( SEffect_secret );
                        }
                        else if ( s_pendingNewGameSlot != i )
                        {
                            // First tap on this empty slot: arm the QUEST 1 /
                            // QUEST 2 choice, drawn inside this same row
                            // (DrawSaveSlotsOverlay) -- no more hidden
                            // Select-hold: hold/combo-gated options are hard
                            // to discover and to use.
                            s_pendingNewGameSlot = i;
                            Sound::PlayEffect( SEffect_cursor );
                        }
                        else
                        {
                            int quest = ( touchX >= LIVEMAP_SLOTS_X + LIVEMAP_SLOTS_W * 0.5f ) ? 1 : 0;
                            World::StartNewGameInSlot( i, quest );
                            ResetSlotLiveMapFiles( i );
                            s_slotsOpen = false;
                            s_pendingNewGameSlot = -1;
                            Sound::PlayEffect( SEffect_secret );
                        }
                    }
                    break;
                }
            }
            if ( !hitRow )
            {
                s_slotsOpen = false;
                s_pendingDeleteSlot = -1;
                s_pendingNewGameSlot = -1;
                s_copyMode = false;
                s_copySourceSlot = -1;
            }
        }
        else if ( touchX >= LIVEMAP_TAB_MAP_X && touchX < LIVEMAP_TAB_MAP_X + LIVEMAP_TAB_W
            && touchY >= LIVEMAP_TAB_Y && touchY < LIVEMAP_TAB_Y + LIVEMAP_TAB_H )
        {
            s_view = LiveMapView_Map;
        }
        else if ( touchX >= LIVEMAP_TAB_ITEMS_X && touchX < LIVEMAP_TAB_ITEMS_X + LIVEMAP_TAB_W
            && touchY >= LIVEMAP_TAB_Y && touchY < LIVEMAP_TAB_Y + LIVEMAP_TAB_H )
        {
            s_view = LiveMapView_Items;
        }
        else if ( touchX >= LIVEMAP_TAB_OPTIONS_X && touchX < LIVEMAP_TAB_OPTIONS_X + LIVEMAP_TAB_W
            && touchY >= LIVEMAP_TAB_Y && touchY < LIVEMAP_TAB_Y + LIVEMAP_TAB_H )
        {
            s_view = LiveMapView_Options;
        }
        // The "?" legend button only exists on the overworld map screen —
        // dungeons keep their own always-on MAP/COMPASS/KEY legend row
        // (DrawDungeonMap, unchanged) rather than the popup, so gate this on
        // state.overworld too or a stray tap in a dungeon would silently
        // "open" a legend that's never drawn.
        else if ( s_view == LiveMapView_Map && state.overworld
            && touchX >= LIVEMAP_LEGEND_BTN_X && touchX < LIVEMAP_LEGEND_BTN_X + LIVEMAP_LEGEND_BTN_SIZE
            && touchY >= LIVEMAP_LEGEND_BTN_Y && touchY < LIVEMAP_LEGEND_BTN_Y + LIVEMAP_LEGEND_BTN_SIZE )
        {
            s_legendOpen = true;
        }
        // Items screen's SAVE pill (phase 9.x): the classic top-screen pause
        // menu's Up+A manual save (phase 8.5) had no home once 3DS stopped
        // opening that menu at all (Graphics::ShouldUseClassicPauseMenu()) —
        // this replaces it. Unthrottled write via the same World::SaveNow()
        // phase 9.2's HOME-exit hook uses, plus a brief "SAVED" toast.
        else if ( s_view == LiveMapView_Items
            && touchX >= LIVEMAP_SAVE_BTN_X && touchX < LIVEMAP_SAVE_BTN_X + LIVEMAP_PILL_W
            && touchY >= LIVEMAP_PILL_Y && touchY < LIVEMAP_PILL_Y + LIVEMAP_PILL_H )
        {
            World::SaveNow();
            Sound::PlayEffect( SEffect_secret );   // same confirmation sound phase 8.5 used
            s_saveFlashFrames = 90;
        }
        // Tap an owned item in the Items-screen grid to assign it (phase 8.2 —
        // no pause menu needed). Plain tap -> B (profile.SelectedItem, same
        // field the pause menu's Left/Right picker writes); holding Y while
        // tapping -> the second item slot (Player::SelectedItem2, phase 8.1's
        // quick-slot) instead, mirroring how Y already means "second slot" in
        // the pause menu.
        //
        // Gameplay deliberately keeps running while this screen is showing, so
        // that Y press used to *also* fire its normal "use item 2" action on
        // the top screen. This comment used to call that "a minor, harmless
        // overlap, not a bug" — it was a bug, and it showed on hardware:
        // with bombs in the second slot you drop one every time
        // you re-equip. Consumables are now refused while this screen is open,
        // via Player::InventoryScreenOpen (set once per frame at the end of
        // LiveMap_Update, not here — a flag set on a touch event would stay
        // stuck the moment you leave the screen mid-press).
        else if ( s_view == LiveMapView_Items
            && touchX >= LIVEMAP_ITEM_GRID_X && touchX < LIVEMAP_ITEM_GRID_X + LIVEMAP_ITEM_GRID_W
            && touchY >= LIVEMAP_ITEM_GRID_Y && touchY < LIVEMAP_ITEM_GRID_Y + LIVEMAP_ITEM_GRID_H )
        {
            int col = (int) ((touchX - LIVEMAP_ITEM_GRID_X) / (LIVEMAP_ITEM_GRID_W / LIVEMAP_ITEM_GRID_COLS));
            int row = (int) ((touchY - LIVEMAP_ITEM_GRID_Y) / (LIVEMAP_ITEM_GRID_H / LIVEMAP_ITEM_GRID_ROWS));
            int slot = row * LIVEMAP_ITEM_GRID_COLS + col;
            if ( slot >= 0 && slot < LIVEMAP_ITEM_GRID_SLOTS )
            {
                int itemSlot = EffectiveGridSlot( s_gridSlots[slot] );
                Profile& profile = World::GetProfile();
                if ( profile.Items[itemSlot] != 0 )   // owned — dashed/empty slots don't assign
                {
                    if ( Input::IsButtonDown( InputButtons::Y ) )
                        Player::SelectedItem2 = (uint8_t) itemSlot;
                    else
                        profile.SelectedItem = (uint8_t) itemSlot;
                    Sound::PlayEffect( SEffect_cursor );
                }
            }
        }
        // Options screen scroll arrows: the options scroll -- a real
        // viewport instead of repeatedly shrinking rows to cram in one more
        // control). Checked before the row-tap handling below so an arrow
        // tap can't also land on whatever row happens to be under it.
        else if ( s_view == LiveMapView_Options
            && touchX >= LIVEMAP_OPT_ARROW_X && touchX < LIVEMAP_OPT_ARROW_X + LIVEMAP_OPT_ARROW_SIZE * 2
            && touchY >= LIVEMAP_OPT_ARROW_UP_Y && touchY < LIVEMAP_OPT_ARROW_UP_Y + LIVEMAP_OPT_ARROW_SIZE * 2 )
        {
            if ( s_optScroll > 0 )
            {
                s_optScroll--;
                Sound::PlayEffect( SEffect_cursor );
            }
        }
        else if ( s_view == LiveMapView_Options
            && touchX >= LIVEMAP_OPT_ARROW_X && touchX < LIVEMAP_OPT_ARROW_X + LIVEMAP_OPT_ARROW_SIZE * 2
            && touchY >= LIVEMAP_OPT_ARROW_DN_Y && touchY < LIVEMAP_OPT_ARROW_DN_Y + LIVEMAP_OPT_ARROW_SIZE * 2 )
        {
            if ( s_optScroll < LIVEMAP_OPT_ROWS - LIVEMAP_OPT_VISIBLE_ROWS )
            {
                s_optScroll++;
                Sound::PlayEffect( SEffect_cursor );
            }
        }
        // Manual screen scroll arrows -- same shape/reasoning as the Options
        // ones just above, just against LIVEMAP_MANUAL_ROWS/VISIBLE_ROWS.
        else if ( s_view == LiveMapView_Manual
            && touchX >= LIVEMAP_MANUAL_ARROW_X && touchX < LIVEMAP_MANUAL_ARROW_X + LIVEMAP_MANUAL_ARROW_SIZE * 2
            && touchY >= LIVEMAP_MANUAL_ARROW_UP_Y && touchY < LIVEMAP_MANUAL_ARROW_UP_Y + LIVEMAP_MANUAL_ARROW_SIZE * 2 )
        {
            if ( s_manualScroll > 0 )
            {
                s_manualScroll--;
                Sound::PlayEffect( SEffect_cursor );
            }
        }
        else if ( s_view == LiveMapView_Manual
            && touchX >= LIVEMAP_MANUAL_ARROW_X && touchX < LIVEMAP_MANUAL_ARROW_X + LIVEMAP_MANUAL_ARROW_SIZE * 2
            && touchY >= LIVEMAP_MANUAL_ARROW_DN_Y && touchY < LIVEMAP_MANUAL_ARROW_DN_Y + LIVEMAP_MANUAL_ARROW_SIZE * 2 )
        {
            if ( s_manualScroll < LIVEMAP_MANUAL_ROWS - LIVEMAP_MANUAL_VISIBLE_ROWS )
            {
                s_manualScroll++;
                Sound::PlayEffect( SEffect_cursor );
            }
        }
        // Options screen (phase 11.1, scrollable since phase save-slots): tap
        // either half of a toggle row's segmented control to set it
        // immediately, or tap one of the 4 action rows at logical index
        // 0..LIVEMAP_OPT_ACTION_ROWS-1 (MANUAL, MUSIC, STATS, SAVE SLOTS,
        // in that order -- replacing X-cycling as the way to
        // reach the first three, and moved ahead of the toggles with MANUAL
        // first, see livemap.h's
        // LIVEMAP_OPT_ACTION_ROWS comment). Only LIVEMAP_OPT_VISIBLE_ROWS
        // are actually on screen at once -- `i` below is the visible row,
        // `logical` is which real entry that maps to at the current scroll
        // offset.
        else if ( s_view == LiveMapView_Options
            && touchX >= LIVEMAP_OPT_AREA_X && touchX < LIVEMAP_OPT_AREA_X + LIVEMAP_OPT_AREA_W )
        {
            for ( int i = 0; i < LIVEMAP_OPT_VISIBLE_ROWS; i++ )
            {
                int logical = s_optScroll + i;
                if ( logical >= LIVEMAP_OPT_ROWS )
                    break;

                float rowY = LIVEMAP_OPT_ROW0_Y + i * (LIVEMAP_OPT_ROW_H + LIVEMAP_OPT_ROW_GAP);
                if ( touchY < rowY || touchY >= rowY + LIVEMAP_OPT_ROW_H )
                    continue;

                if ( logical < LIVEMAP_OPT_ACTION_ROWS )   // one of the 4 action rows
                {
                    switch ( logical )
                    {
                    case 0: s_view = LiveMapView_Manual; break;
                    case 1: s_view = LiveMapView_Music; break;
                    case 2: s_view = LiveMapView_Stats; break;
                    case 3: s_slotsOpen = true; break;
                    }
                    Sound::PlayEffect( SEffect_cursor );
                    break;
                }

                int toggleIndex = logical - LIVEMAP_OPT_ACTION_ROWS;
                float segY = rowY + (LIVEMAP_OPT_ROW_H - LIVEMAP_OPT_SEG_H) / 2.0f;
                if ( touchY < segY || touchY >= segY + LIVEMAP_OPT_SEG_H )
                    break;

                float leftSegX  = LIVEMAP_OPT_AREA_X + LIVEMAP_OPT_AREA_W - 2 * LIVEMAP_OPT_SEG_W;
                float rightSegX = leftSegX + LIVEMAP_OPT_SEG_W;
                uint8_t newValue;
                if ( touchX >= leftSegX && touchX < leftSegX + LIVEMAP_OPT_SEG_W )
                    newValue = 0;
                else if ( touchX >= rightSegX && touchX < rightSegX + LIVEMAP_OPT_SEG_W )
                    newValue = 1;
                else
                    break;

                if ( *s_optionRows[toggleIndex].field != newValue )
                {
                    *s_optionRows[toggleIndex].field = newValue;
                    Options::Save();
                    Sound::PlayEffect( SEffect_cursor );
                }
                break;
            }
        }
        // Music player (phase 13.4): tap a song row to play it (looping) on
        // top of whatever's currently in the main song stream -- a genuine
        // "jukebox" rather than a fake preview, since Sound::PlaySong() is
        // the same call World.cpp itself uses at room/mode transitions
        // (World.cpp only calls it at those specific transition points, not
        // every frame, so a manual selection isn't instantly stomped -- it
        // naturally reverts next time a real transition fires, e.g.
        // changing rooms). Last row is STOP (Sound::StopSongs()).
        else if ( s_view == LiveMapView_Music
            && touchX >= LIVEMAP_MUSIC_X && touchX < LIVEMAP_MUSIC_X + LIVEMAP_MUSIC_W
            && touchY >= LIVEMAP_MUSIC_Y0 && touchY < LIVEMAP_MUSIC_Y0 + LIVEMAP_MUSIC_ROWS * LIVEMAP_MUSIC_ROW_H )
        {
            int row = (int) ((touchY - LIVEMAP_MUSIC_Y0) / LIVEMAP_MUSIC_ROW_H);
            if ( row >= 0 && row < Song_Max )
            {
                Sound::PlaySong( row, Sound::MainSongStream, true );
                s_playingSongId = row;
                Sound::PlayEffect( SEffect_cursor );
            }
            else if ( row == Song_Max )   // the STOP row, right after the last song
            {
                Sound::StopSongs();
                s_playingSongId = -1;
                Sound::PlayEffect( SEffect_cursor );
            }
        }
        // Custom map pins (phase 10.2, "a la ALBW") / info cards (phase 10.5):
        // tapping any overworld room on the map screen (that didn't already
        // hit a button above) either opens an info card (if that room has a
        // dungeon entrance or a phase 10.3 POI badge) or cycles a pin there
        // (if it doesn't). You can't place a custom pin directly on top of a
        // built-in badge anyway, so diverting that tap to "show info"
        // instead is a natural disambiguation, not a lost gesture. Works at
        // both zoom levels since ComputeMapCell() accounts for the phase 7.3
        // detail-zoom viewport. Gated the same way the "?" legend button is
        // (map view, overworld only) so a stray tap inside a dungeon or on
        // the Items/Options screen can't do either.
        else if ( s_view == LiveMapView_Map && state.overworld )
        {
            int room;
            if ( ComputeMapCell( state, touchX, touchY, room ) )
            {
                int level, kind, nextIdx;
                bool isDungeon = FindDungeonNumber( room, state.quest2, level );
                bool isPoi = !isDungeon && FindPoiAt( room, 0, kind, nextIdx );
                // Phase 7.5e/7.5f: tapping must agree with what's
                // actually drawn there (Graphics_3ds.cpp's Video_DrawBottomMap
                // gates these same badges on visited[]/next-dungeon-only), or
                // probing empty-looking tiles by tap would leak exactly the
                // spoiler info those gates exist to hide -- a POI/dungeon room
                // that shows no badge must fall through to the plain
                // pin-cycle case below, same as any ordinary room.
                // Note: state.visited[]/state.triforcePieces aren't filled in
                // until later this same LiveMap_Update() call (the memcpy /
                // World::GetItem() calls below) -- read the underlying
                // s_visited static and World:: state directly here instead,
                // same values, just not staged into `state` yet.
                bool dungeonRevealed = isDungeon
                    && ( room < LIVEMAP_COLS * LIVEMAP_ROWS && s_visited[room] );
                if ( isDungeon && !dungeonRevealed )
                {
                    uint8_t triforcePieces = (uint8_t) World::GetItem( ItemSlot_TriforcePieces );
                    int nextDungeon = -1;
                    for ( int d = 0; d < 9; d++ )
                    {
                        if ( !(triforcePieces & (1 << d)) ) { nextDungeon = d; break; }
                    }
                    dungeonRevealed = ( level - 1 ) == nextDungeon;
                }
                bool poiRevealed = isPoi && room < LIVEMAP_COLS * LIVEMAP_ROWS && s_visited[room];
                if ( dungeonRevealed || poiRevealed )
                {
                    s_infoRoom = room;
                    Sound::PlayEffect( SEffect_cursor );
                }
                else
                {
                    s_pins[room] = CyclePin( s_pins[room] );
                    SavePins();
                    Sound::PlayEffect( SEffect_cursor );
                }
            }
        }
    }
    if ( s_saveFlashFrames > 0 )
        s_saveFlashFrames--;
    state.saveFlashFrames = (uint8_t) s_saveFlashFrames;

    state.view = s_view;
    if ( state.view != LiveMapView_Map || !state.playing || !state.overworld )
    {
        s_legendOpen = false;
        s_infoRoom = -1;
    }

    // Pause on the Options screen: Start/Select are now
    // the map zoom, so there was no way to actually pause any more. Opening
    // Options doubles as the pause button; leaving it (X-cycle or tapping
    // another tab) resumes. Only touches game state while actually playing --
    // Options is also reachable from the title/game-over screens, where
    // there's no live game to pause.
    World::SetPaused( state.playing && s_view == LiveMapView_Options );

    // The Items screen deliberately does NOT pause -- swapping gear without
    // pausing is the whole point of it (phase 8.2, "no pause menu needed"). But
    // assigning to the second slot means holding Y, and that same Y reaches the
    // game's item button, so a consumable would get spent every time you
    // re-equip (see the grid-tap handler above). Flagged rather than paused, so
    // Player.cpp can refuse just the items that cost something and leave the
    // sword/boomerang/candle alone. Recomputed every frame -- it can't stick.
    Player::InventoryScreenOpen = state.playing && s_view == LiveMapView_Items;
    if ( state.view != LiveMapView_Options || !state.playing )
    {
        s_slotsOpen = false;
        s_pendingDeleteSlot = -1;
        s_pendingNewGameSlot = -1;
        s_copyMode = false;
        s_copySourceSlot = -1;
        s_optScroll = 0;
    }
    if ( state.view != LiveMapView_Manual )
        s_manualScroll = 0;
    state.optScroll = s_optScroll;
    state.manualScroll = s_manualScroll;
    state.legendOpen = s_legendOpen;
    state.playingSongId = s_playingSongId;

    if ( s_deleteArmFrames > 0 )
    {
        s_deleteArmFrames--;
        if ( s_deleteArmFrames == 0 )
            s_pendingDeleteSlot = -1;
    }

    state.slotsOpen = s_slotsOpen;
    state.pendingDeleteSlot = s_pendingDeleteSlot;
    state.pendingNewGameSlot = s_pendingNewGameSlot;
    state.copyMode = s_copyMode;
    state.copySourceSlot = s_copySourceSlot;
    if ( s_slotsOpen )
    {
        ProfileSummarySnapshot summaries;
        SaveFolder::ReadSummaries( summaries );
        int currentSlot = World::GetProfileSlot();
        for ( int i = 0; i < LIVEMAP_SLOTS_ROWS; i++ )
        {
            state.slotActive[i]    = summaries.Summaries[i].IsActive();
            state.slotIsCurrent[i] = state.slotActive[i] && (i == currentSlot);
            state.slotHearts[i]    = summaries.Summaries[i].HeartContainers;
            state.slotDeaths[i]    = summaries.Summaries[i].Deaths;
            state.slotPlayTimeSeconds[i] = summaries.Summaries[i].PlayTimeSeconds;
            state.slotTriforcePieces[i]  = summaries.Summaries[i].TriforcePieceCount;
        }
    }

    state.infoRoom = s_infoRoom;
    for ( int i = 0; i < LIVEMAP_INFO_ITEM_SLOTS; i++ )
        state.infoItems[i] = LiveMapIcon{ -1, -1 };
    // Phase 7.5g: shop contents only show after a visit (a spoiler-light
    // map). Gated on s_visited (not state.visited, which
    // isn't filled in until below) so a shop/cave's actual item assortment
    // only appears on its info card once Link has actually stepped into that
    // room -- tapping the badge before then still opens a card (SHOP/POTION/
    // GIFT etc.), just with the item slots blank, same as an unowned Items-
    // screen slot. The dungeon LEVEL n + triforce card isn't gated here: a
    // dungeon's mere existence is already always visible (phase 3's original
    // "where to go" layer, further scoped by phase 7.5f below), so there's no
    // additional secret left in that card to hide.
    if ( s_infoRoom >= 0 && s_infoRoom < LIVEMAP_COLS * LIVEMAP_ROWS && s_visited[s_infoRoom] )
    {
        int level, kind, nextIdx;
        int items[3];
        if ( !FindDungeonNumber( s_infoRoom, state.quest2, level )
            && FindPoiAt( s_infoRoom, 0, kind, nextIdx )
            && GetPoiItems( s_infoRoom, kind, items ) )
        {
            for ( int i = 0; i < LIVEMAP_INFO_ITEM_SLOTS; i++ )
                state.infoItems[i] = ResolveIconById( items[i] );
        }
    }

    // Phase 7.5i: one representative item icon per POI, only actually drawn
    // by the renderer while zoomed (see LIVEMAP_MAX_POIS's comment in
    // livemap.h) -- filled unconditionally every frame like infoItems/
    // itemGrid above rather than gated on state.detailZoom, same "cheap,
    // don't bother gating on which screen is showing" convention already
    // used throughout this function. Same visited[]-only gate as the badge
    // itself (phase 7.5e) and the info card's items (phase 7.5g just above) --
    // an un-zoomed badge and a zoomed one must never disagree about whether
    // a room's contents are known yet.
    {
        int poiCount = GetPoiCount();
        if ( poiCount > LIVEMAP_MAX_POIS ) poiCount = LIVEMAP_MAX_POIS;
        for ( int i = 0; i < poiCount; i++ )
        {
            int room, kind;
            GetPoiByIndex( i, room, kind );
            state.poiIcon[i] = LiveMapIcon{ -1, -1 };
            if ( room >= LIVEMAP_COLS * LIVEMAP_ROWS || !s_visited[room] )
                continue;
            int items[3];
            if ( !GetPoiItems( room, kind, items ) )
                continue;
            for ( int k = 0; k < 3; k++ )
            {
                LiveMapIcon ic = ResolveIconById( items[k] );
                if ( ic.animId >= 0 ) { state.poiIcon[i] = ic; break; }
            }
        }
    }

    unsigned int heartsValue = World::GetProfile().Hearts;
    unsigned int partialValue = heartsValue & 0xFF;
    state.fullHearts  = (uint8_t) (heartsValue >> 8);
    state.halfHeart   = partialValue > 0 && partialValue < 0x80;
    if ( partialValue >= 0x80 )
        state.fullHearts++;
    state.totalHearts = World::GetItem( ItemSlot_HeartContainers );

    // The marker's position *within* its room. Two fixes stacked here:
    //
    // (1) fracY unsigned-underflow (root cause of the up/down bug: up swept
    //     to the opposite edge, down lagged in the previous screen). player->GetY() is UNSIGNED, so
    //     `GetY() - World::TileMapBaseY` wraps to a huge positive value the
    //     moment GetY() < TileMapBaseY (64) -- which happens for the top ~3px
    //     rows of the play area (gy 61..63, reached at the top of every room
    //     and on entry to a room from above). The (float) cast came *after* the
    //     wrap, so fracY clamped to 1.00 (bottom) instead of 0.00 (top). Traced
    //     live: gy=61 -> fracY=1.00, gy=64 -> fracY=0.00. That flipped value
    //     poisoned the marker (and s_lastFracY below). fracX never hit this --
    //     GetX()/StdViewWidth has no subtraction -- which is exactly why
    //     left/right always looked fine. Fix: do the subtraction in signed int.
    //
    // (2) Mode_Scroll freeze (phase 7.2b, the left/right fix): during the scroll
    //     the engine animates GetX()/GetY() toward the *new* room's entry edge
    //     (UpdateScroll_Scroll, World.cpp), which with the marker still pinned
    //     to the old room would sweep the dot to the opposite edge. Hold the
    //     last real in-room position for the whole scroll instead. With fix (1)
    //     that captured value is now correct for vertical exits too.
    static float s_lastFracX = 0.5f, s_lastFracY = 0.5f;
    state.playerFracX = 0.5f;
    state.playerFracY = 0.5f;
    if ( state.playing )
    {
        if ( mode == Mode_Scroll )
        {
            state.playerFracX = s_lastFracX;
            state.playerFracY = s_lastFracY;
        }
        else
        {
            Player* player = World::GetPlayer();
            if ( player != nullptr )
            {
                float fx = (float) player->GetX() / (float) StdViewWidth;
                float fy = (float) ( (int) player->GetY() - (int) World::TileMapBaseY )
                         / (float) ( (int) StdViewHeight - (int) World::TileMapBaseY );
                state.playerFracX = s_lastFracX = fx < 0.0f ? 0.0f : (fx > 1.0f ? 1.0f : fx);
                state.playerFracY = s_lastFracY = fy < 0.0f ? 0.0f : (fy > 1.0f ? 1.0f : fy);
            }
        }
    }


    if ( state.playing && state.overworld
        && state.curRoom >= 0 && state.curRoom < LIVEMAP_COLS * LIVEMAP_ROWS
        && !s_visited[state.curRoom] )
    {
        s_visited[state.curRoom] = 1;
        SaveVisited();   // only on newly-discovered rooms; cheap
    }

    memcpy( state.visited, s_visited, sizeof s_visited );
    memcpy( state.pins, s_pins, sizeof s_pins );

    // Items screen (phase 9.x). Key count is now filled unconditionally (used
    // to be dungeon-only) so it reads correctly on the Items screen too.
    Profile& profile = World::GetProfile();
    state.keyCount  = World::GetItem( ItemSlot_Keys );
    state.triforcePieces = World::GetItem( ItemSlot_TriforcePieces );

    // phase 13.3 save statistics.
    {
        Profile& p = World::GetProfile();
        state.playTimeSeconds = p.PlayTimeSeconds;
        state.killCount = p.KillCount;
        state.repairRupeesSpent = p.RepairRupeesSpent;
        state.deathCount = p.Deaths;

        // phase 13.2 Manual -- BOSSES section.
        for ( int i = 0; i < LIVEMAP_MANUAL_BOSS_ROWS; i++ )
            state.bossDefeated[i] = p.EverDefeatedBoss[i] != 0;

        // Manual -- SWORD section: tier i owned once the current sword tier
        // (never downgrades) has ever reached i+1.
        for ( int i = 0; i < LIVEMAP_MANUAL_SWORD_ROWS; i++ )
            state.swordOwned[i] = p.Items[ItemSlot_Sword] >= (i + 1);

        // Manual -- ITEMS section: same 15 slots/order as the Items screen's
        // itemGrid (s_gridSlots below). The Letter slot is special-cased --
        // a Potion (a distinct Profile slot, ItemSlot_Potion) can replace it
        // without the raw Letter slot itself ever having been set, so either
        // one counts as "discovered" for that row.
        for ( int i = 0; i < LIVEMAP_ITEM_GRID_SLOTS; i++ )
            state.itemDiscovered[i] = p.EverHadItem[s_gridSlots[i]] != 0;
        state.itemDiscovered[7] = p.EverHadItem[ItemSlot_Letter] != 0
                                || p.EverHadItem[ItemSlot_Potion] != 0;

        // phase 10.4: armos-stairs rooms use their own persistent bitmask
        // (World.cpp's OnActivatedArmos); the armos-item/buried-item rooms
        // already have a real "found" flag via the normal pickup path.
        state.secretsTotal = (uint8_t) GetSecretCount();
        state.secretsFound = 0;
        for ( int i = 0; i < GetSecretCount(); i++ )
        {
            int room; float fx, fy;
            GetSecretByIndex( i, room, fx, fy );
            if ( GetSecretKind( i ) == SecretKind_ArmosStairs )
            {
                int bit = GetArmosStairsBit( i );
                if ( p.ArmosStairsSolved & (1 << bit) )
                    state.secretsFound++;
            }
            else if ( p.OverworldFlags[room].GetItemState() )
            {
                state.secretsFound++;
            }
        }

        // Phase 13.7 follow-up: the 45 tree/rock secrets
        // (kTreeRockSecrets, already shown on the zoomed map since phase
        // 10.1) now count toward this total too -- N/8 becomes N/53.
        // World::TakeSecret() (World.cpp, called from Tree::Update()/
        // RockWall::Update() in ItemObj.cpp on a successful burn/bomb) sets
        // exactly this per-room OWRoomFlags::GetSecretState() bit and
        // nothing else -- confirmed by checking every call site, not
        // assumed -- so it's already the correct "solved" flag for these,
        // no new persistent state needed.
        state.secretsTotal = (uint8_t) ( state.secretsTotal + GetTreeRockSecretCount() );
        for ( int i = 0; i < GetTreeRockSecretCount(); i++ )
        {
            int room; bool isBomb; float fx, fy;
            GetTreeRockSecretByIndex( i, room, isBomb, fx, fy );
            if ( p.OverworldFlags[room].GetSecretState() )
                state.secretsFound++;
        }
    }
    state.curRupees = World::GetItem( ItemSlot_Rupees );
    state.curBombs  = World::GetItem( ItemSlot_Bombs );
    state.equippedA = ResolveSwordIcon();
    state.equippedB = ResolveIcon( profile.SelectedItem );
    state.equippedY = ResolveIcon( Player::SelectedItem2 );
    for ( int i = 0; i < LIVEMAP_ITEM_GRID_SLOTS; i++ )
        state.itemGrid[i] = ResolveIcon( s_gridSlots[i] );

    // Dungeon floor plan. Room-visited/door-unlocked state is already
    // persisted per save slot via Profile::LevelFlags1/2 (UWRoomFlags) — no
    // separate livemap-style save file needed here, unlike the overworld grid.
    const World::LevelInfoBlock* levelInfo = World::GetLevelInfo();
    state.dungeonLevel = ( state.playing && !state.overworld ) ? levelInfo->LevelNumber : 0;

    if ( state.dungeonLevel != 0 )
    {
        state.hasDungeonMap = World::HasCurrentMap();
        state.hasCompass    = World::HasCurrentCompass();

        int triforceRoomId = levelInfo->TriforceRoomId;
        state.bossCollected = World::GotItem( triforceRoomId );

        for ( int c = 0; c < LIVEMAP_UW_COLS; c++ )
        {
            uint8_t presentMask = levelInfo->DrawnMap[c + 4];
            state.dungeonPresent[c] = presentMask;

            uint8_t visitedMask = 0;
            for ( int r = 0; r < LIVEMAP_UW_ROWS; r++ )
            {
                if ( (presentMask & (0x80 >> r)) == 0 )
                    continue;

                int roomId = (r << 4) | ((c - levelInfo->DrawnMapOffset + 0x10 + 4) & 0xF);
                UWRoomFlags& roomFlags = World::GetUWRoomFlags( roomId );
                bool visited = roomFlags.GetVisitState();
                if ( visited )
                    visitedMask |= (0x80 >> r);

                uint8_t doorSum = 0;
                uint8_t bombSum = 0, falseSum = 0;
                for ( uint doorBit = 8; doorBit != 0; doorBit >>= 1 )
                {
                    DoorType doorType = World::GetDoorType( roomId, (Direction) doorBit );
                    if ( doorType == DoorType_Open )
                        doorSum |= doorBit;
                    else if ( roomFlags.GetDoorState( doorBit )
                        && (doorType == DoorType_Bombable
                        ||  doorType == DoorType_Key
                        ||  doorType == DoorType_Key2) )
                        doorSum |= doorBit;

                    // phase 10.1/#3: hidden passages, regardless of open state --
                    // this is the secret the zoom map reveals under SHOW HINTS.
                    if ( doorType == DoorType_Bombable )
                        bombSum |= doorBit;
                    else if ( doorType == DoorType_FalseWall
                        ||  doorType == DoorType_FalseWall2 )
                        falseSum |= doorBit;
                }
                state.dungeonDoors[c * LIVEMAP_UW_ROWS + r] = doorSum;
                state.dungeonBombWalls[c * LIVEMAP_UW_ROWS + r] = bombSum;
                state.dungeonFalseWalls[c * LIVEMAP_UW_ROWS + r] = falseSum;
            }
            state.dungeonVisited[c] = visitedMask;
        }

        int triforceRow = (triforceRoomId >> 4) & 0xF;
        int triforceCol = triforceRoomId & 0xF;
        triforceCol = (triforceCol + levelInfo->DrawnMapOffset) & 0xF;
        triforceCol -= 4;
        if ( triforceCol >= 0 && triforceCol < LIVEMAP_UW_COLS )
        {
            state.bossCol = (int8_t) triforceCol;
            state.bossRow = (int8_t) triforceRow;
        }
        else
        {
            state.bossCol = -1;
            state.bossRow = -1;
        }

        int curRoomId = state.curRoom;
        int playerRow = (curRoomId >> 4) & 0xF;
        int playerCol = curRoomId & 0xF;
        playerCol = (playerCol + levelInfo->DrawnMapOffset) & 0xF;
        playerCol -= 4;
        if ( World::IsUWMain( curRoomId ) && playerCol >= 0 && playerCol < LIVEMAP_UW_COLS )
        {
            state.curCol = (int8_t) playerCol;
            state.curRow = (int8_t) playerRow;
        }
        else
        {
            state.curCol = -1;
            state.curRow = -1;
        }
    }
}

#endif // __3DS__
