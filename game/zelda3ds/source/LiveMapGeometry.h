#pragma once
/*
   Pure overworld map grid geometry (phase 10.2). Deliberately free of both
   World.h and citro2d/libctru (see livemap.h's own file comment for why that
   split matters) -- these two functions are plain integer/float math over
   LiveMapState, so unlike the rest of LiveMap_state.cpp/Graphics_3ds.cpp they
   don't need __3DS__ at all, and can be unit-tested on the host directly
   (see zelda3ds/tests/).
*/
#include "livemap.h"

// Maps a touch point to an overworld room index, accounting for the phase 7.3
// detail-zoom viewport -- mirrors Graphics_3ds.cpp's Video_DrawBottomMap grid
// math exactly (same shared LIVEMAP_GRID_*/LIVEMAP_CELL_* constants, same
// ClampViewport formula inlined here since that helper is otherwise
// citro2d-adjacent renderer code). Returns false if the touch missed the
// grid entirely (e.g. landed on the header or the legend button).
bool ComputeMapCell( const LiveMapState& state, int touchX, int touchY, int& outRoom );

// Advances a room's pin through 0 (none) -> 1..LIVEMAP_PIN_TYPES (colour) ->
// back to 0, one step per call -- the phase 10.2 tap-to-cycle gesture.
uint8_t CyclePin( uint8_t current );

// Overworld points-of-interest (phase 10.3/10.5) and dungeon entrances. Moved
// here (out of Graphics_3ds.cpp) so both the renderer *and* LiveMap_state.
// cpp's touch hit-test (phase 10.5's tap-for-info-card) share one source of
// truth for "what's in this room" -- same "can't drift apart" reasoning as
// every other shared table in this file/livemap.h. Every room ID was read
// directly out of the real quest-1 asset data, not guessed. CaveId<=9 rooms (dungeon entrances)
// are deliberately excluded from kPoiRooms -- same GetCaveId() field, a
// different meaning (World.cpp:5415).
enum PoiKind
{
    Poi_Shop,     // pay-for-goods cave (shield/bombs/arrows/keys/candle/food/ring)
    Poi_Potion,   // the medicine/potion shop specifically (Obj_CaveMedicineShop)
    Poi_Repair,   // the door-repair charge
    Poi_Tunnel,   // "take any road you want" -- a warp/shortcut cave
    Poi_Gift,     // "take any one you want" -- free pick of 3, one slot can be a heart container
    Poi_Fairy,    // fairy fountain (Sparse_Fairy, otherwise-unused sparse-table entry)
    Poi_FluteWarp,// whirlwind/recorder teleport destination (World.cpp's teleportRoomIds[])
    Poi_Count,
};

// The 9 first-quest dungeon entrance overworld rooms (row*16+col), index 0 = level 1.
extern const uint8_t kDungeonRooms[9];
// Phase 14.2: quest 2's variant -- only L3 (index 2) and L6 (index 5) differ
// from kDungeonRooms; see the .cpp for the decoded-from-real-data writeup.
extern const uint8_t kDungeonRoomsQuest2[9];

// Looks up which dungeon (1..9) has its entrance at `room`, if any (`quest2`
// selects kDungeonRooms vs kDungeonRoomsQuest2).
bool FindDungeonNumber( int room, bool quest2, int& outLevel );

// The overworld room (row*16+col) for a given dungeon level's entrance
// (1..9), quest-aware. The inverse of FindDungeonNumber().
int GetDungeonRoom( int level, bool quest2 );

// Looks up the PoiKind at `room`, if any (a room can have more than one --
// see kPoiRooms's own comment in the .cpp -- this returns all matches via
// repeated calls with increasing `startIndex`).
bool FindPoiAt( int room, int startIndex, int& outKind, int& outNextIndex );

// Full-table iteration (for the renderer's per-frame draw loop, which needs
// every POI, not a lookup by room). kPoiRooms itself stays file-local to
// LiveMapGeometry.cpp -- this is its only external access.
int GetPoiCount();
void GetPoiByIndex( int index, int& outRoom, int& outKind );

// Overworld secret locations (phase 10.1, Options::ShowSecretHints). Only the
// entries with a real, data-confirmed on-screen position -- armos-revealed
// stairs/items (Sparse_ArmosStairs/ArmosItem) and one buried item
// (Sparse_Item) -- see phase 10.1 for the offline dump. Sparse_
// SecretScroll's table entry has no x/y (just a 1-byte payload of unknown
// meaning), so it's deliberately left out rather than guessed at; same
// rule as kPoiRooms: only values derived from the data. fracX/fracY are pre-computed
// (NES x/(StdViewWidth) , (y-TileMapBaseY)/(StdViewHeight-TileMapBaseY) --
// World.h's own constants, mirrored here as plain floats since this file
// can't include World.h) so the renderer can place a marker within the
// room's cell exactly like the player-position dot (phase 7.2) does.
struct SecretRoom { uint8_t room; float fracX; float fracY; };

int GetSecretCount();
void GetSecretByIndex( int index, int& outRoom, float& outFracX, float& outFracY );

// phase 10.4: which persistent-state check applies to a given kSecretRooms
// index. ArmosStairs entries (index 0-5) need Profile::ArmosStairsSolved's
// matching bit (see World.cpp's sArmosStairsRooms, same fixed room order);
// ArmosItem/BuriedItem entries (index 6, 7) already have a real "found" flag
// via the normal item pickup path (OWRoomFlags::GetItemState()) -- see
// phase 10.4 for why the other Sparse_* secret types aren't counted.
enum SecretKind { SecretKind_ArmosStairs, SecretKind_ArmosItemOrBuried };
SecretKind GetSecretKind( int index );
// Only meaningful when GetSecretKind(index) == SecretKind_ArmosStairs.
int GetArmosStairsBit( int index );

// phase 10.4: overworld cave entrances hidden behind a bombable wall (RockWall)
// or a burnable tree (Tree). Decoded offline from the game's OWN room-load
// logic (WorldImpl::LoadLayout, World.cpp): every overworld room's tile map is
// walked and its single active "loading tile action" recorded; the rooms whose
// action is TileAction_Bomb (tileRef 0x27) or TileAction_Burn (0x28) are these.
// Cross-checked against kPoiRooms (most reveal a cave that's already a POI; the
// rest are non-POI cave types like the money/hint caves). fracX/fracY are the
// tile's on-screen position (col/16, row/11), same convention as SecretRoom.
// Map philosophy: the big overworld map stays spoiler-light, so these show
// ONLY on the 2x zoom (the zoomed-in map is where the detail lives);
// still gated behind Options::ShowSecretHints like the other secret dots.
struct TreeRockSecret { uint8_t room; uint8_t isBomb; float fracX; float fracY; };

int GetTreeRockSecretCount();
void GetTreeRockSecretByIndex( int index, int& outRoom, bool& outIsBomb, float& outFracX, float& outFracY );
