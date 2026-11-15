#pragma once
/*
   Live map state: plain data bridge between the game side (LiveMap_state.cpp,
   which may include World.h) and the renderer (Graphics_3ds.cpp, which owns
   citro2d). Kept free of both World.h and citro2d types.
*/
#include <stdint.h>

#define LIVEMAP_COLS 16
#define LIVEMAP_ROWS 8

// Overworld grid geometry in native 320x240 touch-screen pixel space (phase
// 7.1's original layout constants, promoted to shared #defines for phase 10.2
// so LiveMap_state.cpp's touch-to-cell hit-test and Graphics_3ds.cpp's
// drawing can't drift apart -- same reasoning as every other LIVEMAP_* rect
// in this file). Cell size is per the *full* (non-zoomed) 16x8 grid; detail
// zoom (phase 7.3) just changes how many cells are visible/how big each one
// is on screen, not this base unit.
#define LIVEMAP_GRID_OX 8.0f
#define LIVEMAP_GRID_OY 64.0f
#define LIVEMAP_CELL_W  19.0f
#define LIVEMAP_CELL_H  18.0f

// Dungeon (underworld) floor-plan grid — same 8-row layout as the overworld's
// mini-map, one dungeon level's worth of rooms (see World::LevelInfoBlock::
// DrawnMap, which this mirrors 1:1: column bytes, bit7=row0..bit0=row7).
#define LIVEMAP_UW_COLS 8
#define LIVEMAP_UW_ROWS 8

// A resolved item sprite reference (Sheet_PlayerAndItems AnimId + palette).
// animId < 0 means "no item here" (empty slot) — the renderer draws nothing
// but an empty badge outline. Resolution (ItemId -> AnimId/palette) happens
// game-side (LiveMap_state.cpp, via ItemObj.h's GetItemIcon()) so the citro2d
// renderer never needs game-logic headers — see the file comment above.
struct LiveMapIcon
{
    int8_t animId;
    int8_t palette;
};

// Fixed-order grid of the classic quest's ownable items (mirrors the pause
// menu's active+passive slot lists in Submenu.cpp): Boomerang, Bombs, Arrow,
// Bow, Candle, Recorder, Food, Potion/Letter, Rod, Raft, Book, Ring, Ladder,
// MagicKey, Bracelet.
#define LIVEMAP_ITEM_GRID_SLOTS 15
#define LIVEMAP_ITEM_GRID_COLS  5
#define LIVEMAP_ITEM_GRID_ROWS  3
// Native 320x240 touch-screen pixel space — shared by LiveMap_state.cpp
// (touch-to-assign hit-test, phase 8.2) and Graphics_3ds.cpp (drawing), same
// no-drift reasoning as the legend/tab constants below.
#define LIVEMAP_ITEM_GRID_X 14.0f
#define LIVEMAP_ITEM_GRID_Y 66.0f
#define LIVEMAP_ITEM_GRID_W 292.0f
#define LIVEMAP_ITEM_GRID_H 124.0f

// On-demand map legend (phase 9.x, replaces the old always-on bottom-left
// legend text). Native 320x240 touch-screen pixel space. Shared by
// LiveMap_state.cpp (touch hit-test) and Graphics_3ds.cpp (drawing the "?"
// button) so the two can't drift out of sync with each other.
#define LIVEMAP_LEGEND_BTN_X    282
#define LIVEMAP_LEGEND_BTN_Y    64
#define LIVEMAP_LEGEND_BTN_SIZE 24

// Death "continue" screen (modern bottom-screen panel, replaces the classic
// Select-cycle NES list). Two stacked touch buttons; shared by the renderer
// (Graphics_3ds.cpp DrawContinuePanel) and the hit-test (LiveMap_state.cpp).
#define LIVEMAP_CONT_BTN_X   40
#define LIVEMAP_CONT_BTN_W   240
#define LIVEMAP_CONT_BTN_H   42
#define LIVEMAP_CONT_BTN1_Y  108   // "CONTINUE" (where you died)
#define LIVEMAP_CONT_BTN2_Y  164   // "CONTINUE FROM START"

// Items screen's bottom row: rupee count / key count / SAVE, three equal
// pills instead of the original two — see LIVEMAP_SAVE_BTN_* below. Shared
// constants so LiveMap_state.cpp's touch hit-test and Graphics_3ds.cpp's
// drawing can't drift apart.
#define LIVEMAP_PILL_Y 204.0f
#define LIVEMAP_PILL_H 24.0f
#define LIVEMAP_PILL_W 90.0f
#define LIVEMAP_RUPEE_PILL_X 14.0f
#define LIVEMAP_KEY_PILL_X   114.0f
#define LIVEMAP_SAVE_BTN_X   214.0f

// Persistent header's MAP/ITEMS/OPTIONS toggle — also touch-tappable, not
// just the X button. Same shared-constants reasoning as the legend button
// above. (Narrower than the old 2-tab layout to fit a 3rd tab in the same
// header row, between the equip badges and the heart group -- phase 11.1.)
#define LIVEMAP_TAB_MAP_X     94
#define LIVEMAP_TAB_ITEMS_X   146
#define LIVEMAP_TAB_OPTIONS_X 198
#define LIVEMAP_TAB_Y         20
#define LIVEMAP_TAB_W         48
#define LIVEMAP_TAB_H         26

// Options screen (phase 11.1): a real, tappable settings screen replacing the
// Start/Y+Start/B+Start button combos phase 11.2-11.4 shipped behind
// temporarily. One row per setting, each a two-segment tap control (left
// segment = the first named state, right = the second) -- shared constants
// so LiveMap_state.cpp's hit-test and Graphics_3ds.cpp's drawing can't drift.
// phase 10.1 added a 6th row (secret hints) -- rows shrunk 28->24px/gap
// unchanged to still fit the 240px screen (row 5 now ends at y=228, still a
// comfortable margin above the 240 edge).
// The options list scrolls: shrinking rows to cram in
// one more control (which the SAVE SLOTS button below briefly did, and
// phase 10.1 also did once before it) doesn't scale -- every future option
// would shrink things further. Real fix: a scrollable viewport. All 7
// entries (6 toggles + 1 "SAVE SLOTS" action row, same list) live at a
// comfortable, *not* re-shrunk row size; only LIVEMAP_OPT_VISIBLE_ROWS of
// them are drawn at a time, with small up/down arrow buttons to page.
// LIVEMAP_OPT_ROWS is the logical row *count* (used by both the toggle
// fields and the save-slots row's index, LIVEMAP_OPT_ROWS-1); the touch
// scroll code below is what maps a visible row back to a logical one.
// Later, 3 more action rows (MUSIC/STATS/MANUAL) joined SAVE SLOTS
// here -- see LIVEMAP_OPT_ACTION_ROWS and the Manual section below for why
// (X-cycling to reach those three was awkward) -- and moved ahead of
// the 7 toggles, MANUAL first, so the row order below
// is now action rows then toggles, not the other way around.
#define LIVEMAP_OPT_TOGGLE_ROWS  7   // the real GameOptions on/off fields
#define LIVEMAP_OPT_ACTION_ROWS  4   // MANUAL, MUSIC, STATS, SAVE SLOTS -- in that order
#define LIVEMAP_OPT_ROWS         ( LIVEMAP_OPT_TOGGLE_ROWS + LIVEMAP_OPT_ACTION_ROWS )
#define LIVEMAP_OPT_VISIBLE_ROWS 5
#define LIVEMAP_OPT_AREA_X   14.0f
#define LIVEMAP_OPT_AREA_W   292.0f
#define LIVEMAP_OPT_ROW0_Y   64.0f
#define LIVEMAP_OPT_ROW_H    24.0f
#define LIVEMAP_OPT_ROW_GAP  4.0f
#define LIVEMAP_OPT_SEG_W    54.0f
#define LIVEMAP_OPT_SEG_H    18.0f
// Up/down scroll arrows, stacked in the margin to the right of the rows.
#define LIVEMAP_OPT_ARROW_X    292.0f
#define LIVEMAP_OPT_ARROW_UP_Y  64.0f
#define LIVEMAP_OPT_ARROW_DN_Y 216.0f
#define LIVEMAP_OPT_ARROW_SIZE  14.0f
#define LIVEMAP_SLOTS_ROWS    3
#define LIVEMAP_SLOTS_X       30.0f
// Phase 14.3 follow-up (save-slot copy): Y0 moved from 70 to 84 to open up a
// proper header band for the new COPY toggle button, rather than squeezing
// it in alongside the "SAVE SLOTS" title text -- the 3 rows (84 + 3*40 =
// 204) still comfortably fit the overlay box's 180px height starting at
// y=56 (bottom at 236), there was ~46px of unused slack at the bottom
// before this change.
#define LIVEMAP_SLOTS_Y0      84.0f
#define LIVEMAP_SLOTS_W       260.0f
#define LIVEMAP_SLOTS_ROW_H   40.0f
#define LIVEMAP_SLOTS_DEL_W   28.0f   // delete icon: a fixed-width zone at each row's right edge
// COPY toggle button, in the header band opened up above (y 60-78, above
// LIVEMAP_SLOTS_Y0=84). Sits to the right of the "SAVE SLOTS" title text.
#define LIVEMAP_SLOTS_COPY_X  220.0f
#define LIVEMAP_SLOTS_COPY_Y  60.0f
#define LIVEMAP_SLOTS_COPY_W  70.0f
#define LIVEMAP_SLOTS_COPY_H  18.0f

// Idle bottom-screen dim (phase 13.6, battery). Frame counts at the fixed
// 60fps this game always runs at. Fade-in only (no fade-out): any input
// snaps the screen back to full brightness the same frame it's pressed, so
// "waking" the screen never reads as input lag -- only going dark is gradual.
#define LIVEMAP_DIM_IDLE_FRAMES 1200   // ~20s of no input before dimming starts
#define LIVEMAP_DIM_FADE_FRAMES 60     // ~1s ramp once the idle threshold is crossed
#define LIVEMAP_DIM_MAX_ALPHA   190    // out of 255 -- dark, but still glanceable

// Custom map pins (phase 10.2, "a la ALBW"): the player can tap any overworld
// room to drop a colour-coded reminder pin, cycling through 4 colours then
// back to none on repeated taps. One pin per room (matches this map's
// existing room-grid resolution -- see LiveMap_state.cpp's touch handler),
// persisted per save slot (sdmc:/3ds/zelda3ds/pins_<slot>.dat), unlike the
// shared/session-only `visited` grid above. 0 = no pin, 1..LIVEMAP_PIN_TYPES
// = colour index (see LIVEMAP_PIN_COLORS in Graphics_3ds.cpp).
#define LIVEMAP_PIN_TYPES 4

// Tap-for-info-card (phase 10.5): tapping a dungeon or POI (phase 10.3) badge
// on the overworld map opens a small card instead of cycling a pin there
// (you can't place a custom pin directly on a built-in badge anyway, so
// diverting that tap is a natural disambiguation, not a compromise -- see
// LiveMap_state.cpp's touch handler). Up to 3 resolved item icons for
// shop/potion/gift-cave cards; category/dungeon-number text is looked up by
// the renderer itself from LiveMapGeometry's shared tables using infoRoom,
// so no separate "kind" field is needed here.
#define LIVEMAP_INFO_ITEM_SLOTS 3

// Phase 7.5i: the detail-zoom view (phase 7.3)
// looked visually identical to the full map -- same badges, just bigger --
// so it gave "real extra details" or got cut. Chose to add real detail: the
// bigger zoomed cells have room to show a POI's actual primary item icon
// (shop/potion/gift assortment), not just its category-colour square. One
// resolved icon per LiveMapGeometry POI entry (indexed the same way
// GetPoiByIndex() already enumerates them), gated the same visited[]-only
// way as the badges/info-card themselves (phase 7.5e/7.5g) so this can't leak
// anything the badge layer doesn't already show. 64 is a fixed safe upper
// bound on GetPoiCount() (54 at the time this was written) -- livemap.h
// deliberately doesn't include LiveMapGeometry.h to get the real count, to
// stay free of anything but plain data (see the file comment above).
#define LIVEMAP_MAX_POIS 64

// Music player (phase 13.4): a vertical list, the 12 songs (Song_intro..
// Song_zelda, SoundId.h) plus a STOP row. Deliberately *not* a 4th header
// tab (LIVEMAP_TAB_MAP_X/ITEMS_X/OPTIONS_X, livemap.h above, are already
// tightly packed between the equip badges and the heart group -- a 4th 48px
// tab risks colliding with the heart group on a maxed-out save, since
// DrawHeartGroup's width grows with container count and isn't fixed). Only
// reachable via X-cycling (Map->Items->Options->Music->Map) instead -- see
// phase 13.4 for why that trade-off was accepted.
#define LIVEMAP_MUSIC_ROWS   13   // 12 songs (Song_Max, SoundId.h) + 1 STOP row
#define LIVEMAP_MUSIC_X      14.0f
#define LIVEMAP_MUSIC_Y0     64.0f
#define LIVEMAP_MUSIC_W      292.0f
#define LIVEMAP_MUSIC_ROW_H  13.0f

// Save statistics (phase 13.3): read-only, five rows (phase 10.4 added
// Secrets). Reached via an Options action row (was X-cycle-only, see the
// Manual comment below for why that changed) -- ROW_H trimmed
// 32->30 to keep 5 rows within the 240px screen (same "shrink row height"
// move phase 10.1 used for its 6-row Options screen) -- 5*30=150 from
// Y0=80 ends at 230, 10px margin.
#define LIVEMAP_STATS_ROWS  5
#define LIVEMAP_STATS_X     20.0f
#define LIVEMAP_STATS_Y0    80.0f
#define LIVEMAP_STATS_ROW_H 30.0f

// Manual (phase 13.2, expanded later: X-cycling to reach Music/Stats/the
// old boss-only Bestiary hid them too well, so those three are no longer
// X-cycle-only; they're now reached the same way SAVE SLOTS already was,
// via tappable action rows at the top of the (already scrollable, already
// arrow-paged) Options list, MANUAL first. See
// LIVEMAP_OPT_ACTION_ROWS above. The old boss-only screen is now one of
// three scrollable sections in a single
// combined book/achievement-style "Manual": BOSSES (7 rows, World.h's
// BossId, kept as a plain literal here not World::Boss_MaxBosses per this
// file's no-World.h rule), SWORD (3 tiers), ITEMS (LIVEMAP_ITEM_GRID_SLOTS
// rows, one per the Items screen's existing 15 ownable slots). Each data
// section is preceded by one non-tappable header row (just a section title)
// -- LIVEMAP_MANUAL_SECTION_ROWS below -- within the same scrolling logical-
// row list Options already uses, same "logical row -> is this an action row
// or a real entry" branching as that screen's SAVE SLOTS row.
#define LIVEMAP_MANUAL_BOSS_ROWS    7
#define LIVEMAP_MANUAL_SWORD_ROWS   3
#define LIVEMAP_MANUAL_SECTION_ROWS 3   // "BOSSES"/"SWORD"/"ITEMS" header rows
#define LIVEMAP_MANUAL_ROWS ( LIVEMAP_MANUAL_SECTION_ROWS + LIVEMAP_MANUAL_BOSS_ROWS \
                             + LIVEMAP_MANUAL_SWORD_ROWS + LIVEMAP_ITEM_GRID_SLOTS )
#define LIVEMAP_MANUAL_VISIBLE_ROWS 7
#define LIVEMAP_MANUAL_X       20.0f
#define LIVEMAP_MANUAL_Y0      64.0f
#define LIVEMAP_MANUAL_W       280.0f
#define LIVEMAP_MANUAL_ROW_H   20.0f
#define LIVEMAP_MANUAL_ROW_GAP 2.0f
// Up/down scroll arrows -- same shape/position convention as Options'
// LIVEMAP_OPT_ARROW_*, just in the Manual screen's own margin.
#define LIVEMAP_MANUAL_ARROW_X    292.0f
#define LIVEMAP_MANUAL_ARROW_UP_Y 64.0f
#define LIVEMAP_MANUAL_ARROW_DN_Y 216.0f
#define LIVEMAP_MANUAL_ARROW_SIZE 14.0f

// Options screen action rows (LIVEMAP_OPT_ACTION_ROWS, defined above near
// LIVEMAP_OPT_TOGGLE_ROWS): logical row 0..3, in this order: MANUAL, MUSIC,
// STATS, SAVE SLOTS -- the 7 toggle
// fields follow at logical row LIVEMAP_OPT_ACTION_ROWS onward.

enum LiveMapView : uint8_t
{
    LiveMapView_Map     = 0,
    LiveMapView_Items   = 1,
    LiveMapView_Options = 2,
    LiveMapView_Music   = 3,
    LiveMapView_Stats   = 4,
    LiveMapView_Manual  = 5,
    LiveMapView_Count   = 6,
};

struct LiveMapState
{
    bool    playing;        // in a gameplay mode (not menus/title)
    // Smoothed FPS (main_3ds.cpp, from real wall-clock frame-to-frame time,
    // vsync wait included -- what actually reaches the screen). Filled in
    // just before Video_DrawBottomMap() each frame; shown on the Options
    // screen. Always computed -- cheap (one tick diff + an EMA),
    // unlike the opt-in SD frame logging.
    float   fps;
    // Title screen (phase 11.1b): bottom screen shows a matching wood/gold
    // companion panel (crest + "PRESS START") instead of the normal header
    // + map/items/options content while this is true.
    bool    titleScreen;
    // Death continue screen (Mode_ContinueQuestion): the bottom screen shows a
    // modern GAME OVER panel with two touch buttons instead of the map/UI.
    bool    continueScreen;
    bool    overworld;      // on the overworld (vs. inside a dungeon/cave)
    // Phase 14.2: profile.Quest == 1 (second quest). Selects kDungeonRooms
    // vs. kDungeonRoomsQuest2 (LiveMapGeometry.h) -- only L3/L6 differ.
    bool    quest2;
    bool    detailZoom;     // Select-toggled 2x close-up centred on the player
    uint8_t view;            // LiveMapView -- which of Map/Items/Options is shown
    bool    legendOpen;      // touch-toggled overlay showing the map legend
    // phase 10.5: room whose info card is open, or -1 (closed). Resolved item
    // icons for that room's shop/potion/gift-cave assortment, if any (see
    // LIVEMAP_INFO_ITEM_SLOTS above); animId<0 (see LiveMapIcon) = no item
    // in that slot, same convention as the Items screen's grid.
    int         infoRoom;
    LiveMapIcon infoItems[LIVEMAP_INFO_ITEM_SLOTS];
    // Phase 7.5i: one resolved primary item icon per POI (GetPoiByIndex()
    // order), only used by the renderer while state.detailZoom is on --
    // see LIVEMAP_MAX_POIS's comment above.
    LiveMapIcon poiIcon[LIVEMAP_MAX_POIS];
    // Bitmask, bit (level-1) set once that dungeon's triforce piece is
    // collected (mirrors World.cpp's own `1 << (LevelNumber-1)` convention) --
    // shown on a dungeon-entrance info card. Filled unconditionally each
    // frame like the other Items-screen-adjacent state, not just while a
    // card is open.
    uint8_t     triforcePieces;
    // phase 13.4 music player: which row is highlighted as "now playing" --
    // -1 = stopped/nothing manually selected, else a SongId (0..Song_Max-1).
    // Session-only UI selection (LiveMap_state.cpp's touch handler), not
    // derived from any World:: state -- the game itself has no persistent
    // "which song is playing" concept to read back.
    int         playingSongId;
    // phase 13.3 save statistics -- straight copies of Profile fields (see
    // Profile.h), filled unconditionally every frame like the other Items-
    // screen-adjacent state.
    uint32_t    playTimeSeconds;
    uint16_t    killCount;
    uint16_t    repairRupeesSpent;
    uint8_t     deathCount;
    // phase 10.4: how many of LiveMapGeometry's GetSecretCount() overworld
    // secrets have a confirmed "found" signal (see phase 10.4 for
    // which Sparse_* types that covers and which are left out).
    uint8_t     secretsFound;
    uint8_t     secretsTotal;
    // phase 13.2 Manual, BOSSES section: straight copy of
    // Profile::EverDefeatedBoss (see Profile.h / World.h's BossId), filled
    // unconditionally every frame like the other Items-screen-adjacent
    // state above.
    bool        bossDefeated[LIVEMAP_MANUAL_BOSS_ROWS];
    // Manual, SWORD section: tier i (0=wooden,1=white,2=magical) owned once
    // Profile::Items[ItemSlot_Sword] has ever reached i+1 -- sword tier
    // never downgrades in this game, so the *current* value already doubles
    // as "ever had", no separate EverHadItem-style flag needed.
    bool        swordOwned[LIVEMAP_MANUAL_SWORD_ROWS];
    // Manual, ITEMS section: straight copy of Profile::EverHadItem for the
    // same 15 slots/order as the Items screen's itemGrid above (see
    // LiveMap_state.cpp's s_gridSlots).
    bool        itemDiscovered[LIVEMAP_ITEM_GRID_SLOTS];
    // Manual screen scroll offset, same convention as optScroll below.
    int         manualScroll;
    // Idle dim overlay strength, 0..LIVEMAP_DIM_MAX_ALPHA (phase 13.6). 0 =
    // full brightness. Computed in LiveMap_state.cpp from recent input
    // activity; the renderer just draws a black rect at this alpha on top
    // of everything else once non-zero.
    uint8_t dimAlpha;
    // phase 5.1b: the bottom-screen map/UI is 100% static during
    // Mode_LoadLevel (curRoom/dungeonLevel/etc. don't change while not
    // playing, so every one of the 8 stepped-load frames would draw
    // identical content) -- main_3ds.cpp skips calling Video_DrawBottomMap()
    // entirely when this is true, saving that frame's ~2.8-4 ms bottomMap
    // cost. Only true from the *3rd* load frame onward (LiveMap_state.cpp
    // tracks a small counter): the real screen is double-buffered, so the
    // first 2 load frames must still draw normally to put the same static
    // content in *both* physical buffers -- skip from frame 1 and whichever
    // buffer still has the pre-load content flickers in every other frame.
    bool    skipBottomMapDraw;
    // Frames left to show the Items screen's "SAVED" confirmation after
    // tapping its SAVE button (phase 9.x — the classic top-screen pause
    // menu's Up+A save, phase 8.5, moved here since 3DS no longer opens that
    // menu at all; see Graphics::ShouldUseClassicPauseMenu()). 0 = hidden.
    uint8_t saveFlashFrames;
    // Options screen scroll offset (phase save-slots): topmost visible
    // logical row, 0..(LIVEMAP_OPT_ROWS-LIVEMAP_OPT_VISIBLE_ROWS).
    int     optScroll;
    // Save-slot management overlay: opened from a button on the Options
    // screen. Per-slot summary is filled unconditionally (cheap: 3 small
    // file reads) whenever the overlay is open so the renderer never needs
    // SaveFolder.h itself. pendingDeleteSlot is the 2-tap delete-confirm
    // arm state (-1 = none armed, see LiveMap_state.cpp) -- tapping a
    // slot's delete icon once arms it, a second tap on the same icon
    // within a short window commits, anything else disarms.
    bool    slotsOpen;
    bool    slotActive[LIVEMAP_SLOTS_ROWS];
    bool    slotIsCurrent[LIVEMAP_SLOTS_ROWS];
    uint8_t slotHearts[LIVEMAP_SLOTS_ROWS];
    uint8_t slotDeaths[LIVEMAP_SLOTS_ROWS];
    // phase 14.3: playtime/triforce progress, shown alongside hearts/deaths.
    uint32_t slotPlayTimeSeconds[LIVEMAP_SLOTS_ROWS];
    uint8_t  slotTriforcePieces[LIVEMAP_SLOTS_ROWS];
    int     pendingDeleteSlot;
    // phase 14.1: which empty slot (if any) is showing the QUEST 1 / QUEST 2
    // choice, -1 = none armed. Only ever set when Options::Get().
    // Quest2Unlocked is true -- an unlocked-once-ever, all-slots flag (see
    // Options.h), not per-slot. The two choices render inside that same
    // row (left half = quest 1, right half = quest 2), not a separate
    // popup -- LIVEMAP_SLOTS_ROW_H already has the room.
    int     pendingNewGameSlot;
    // Phase 14.3 follow-up (save-slot copy): copyMode is whether the COPY
    // toggle is active; copySourceSlot is which active slot (if any, -1 =
    // none yet) is armed as the copy source -- see LiveMap_state.cpp's
    // s_copyMode/s_copySourceSlot comment for the full tap flow.
    bool    copyMode;
    int     copySourceSlot;
    int     curRoom;        // 0..127 (row*16+col) when overworld
    uint8_t visited[LIVEMAP_COLS * LIVEMAP_ROWS];   // 1 = seen this session
    // Custom pins (phase 10.2), per room: 0 = none, 1..LIVEMAP_PIN_TYPES = colour.
    uint8_t pins[LIVEMAP_COLS * LIVEMAP_ROWS];
    // Link's sub-cell position within his current room, 0..1 across the
    // room's play area (excludes the NES HUD strip) — lets the position
    // marker move smoothly within a cell instead of just jumping room to
    // room. Valid whenever `playing`.
    float   playerFracX, playerFracY;
    uint8_t fullHearts;     // full heart containers
    bool    halfHeart;      // one extra half heart beyond fullHearts
    uint8_t totalHearts;    // heart container count (max)

    // Items screen (phase 9.x bottom-screen remake). Equipped A (sword —
    // always equipped once owned, unlike B/Y which can be empty), B, and
    // Y-slot (phase 8.1) items; animId < 0 if nothing to show.
    LiveMapIcon equippedA, equippedB, equippedY;
    LiveMapIcon itemGrid[LIVEMAP_ITEM_GRID_SLOTS];
    uint8_t curRupees;
    uint8_t curBombs;
    // Key count: now filled unconditionally every frame (was previously only
    // valid inside a numbered dungeon) so the Items screen can show it too.
    uint8_t keyCount;

    // Dungeon mode — valid when playing && !overworld && dungeonLevel != 0.
    uint8_t dungeonLevel;    // 1..9, or 0 when not inside a numbered dungeon
    bool    hasDungeonMap;   // map item collected for this level
    bool    hasCompass;      // compass item collected for this level
    bool    bossCollected;   // level's triforce/boss-room item already taken
    int8_t  curCol, curRow;  // player's room within the grid, -1 if n/a (e.g. cellar)
    int8_t  bossCol, bossRow;// boss/triforce room within the grid, -1 if n/a
    // Per-column bitmask, bit7=row0..bit0=row7 (mirrors DrawnMap layout).
    uint8_t dungeonPresent[LIVEMAP_UW_COLS];   // room exists in this level's layout
    uint8_t dungeonVisited[LIVEMAP_UW_COLS];   // room has been entered (persisted in the save)
    // Per-cell door-open bitmask [col * LIVEMAP_UW_ROWS + row], bits match
    // Direction (Right=1, Left=2, Down=4, Up=8); only meaningful where visited.
    uint8_t dungeonDoors[LIVEMAP_UW_COLS * LIVEMAP_UW_ROWS];
    // phase 10.1/#3: hidden-passage secrets, same [col*ROWS+row] + Direction-bit
    // layout. dungeonBombWalls = walls you bomb (DoorType_Bombable),
    // dungeonFalseWalls = walk-through false walls (DoorType_FalseWall[2]).
    // Set for every *present* room (regardless of open/visited state) so the
    // zoom map can reveal them under Options::ShowSecretHints.
    uint8_t dungeonBombWalls[LIVEMAP_UW_COLS * LIVEMAP_UW_ROWS];
    uint8_t dungeonFalseWalls[LIVEMAP_UW_COLS * LIVEMAP_UW_ROWS];
};

// Fills `state` from the running World (called once per frame, game side).
void LiveMap_Update( LiveMapState& state );

// Renders the bottom screen map (renderer side, citro2d).
void Video_DrawBottomMap( const LiveMapState& state, uint32_t frame );
