#include "LiveMapGeometry.h"

bool ComputeMapCell( const LiveMapState& state, int touchX, int touchY, int& outRoom )
{
    bool zoomOk = state.curRoom >= 0 && state.curRoom < LIVEMAP_COLS * LIVEMAP_ROWS;
    bool zoom = state.detailZoom && zoomOk;
    int viewCols = zoom ? LIVEMAP_COLS / 2 : LIVEMAP_COLS;
    int viewRows = zoom ? LIVEMAP_ROWS / 2 : LIVEMAP_ROWS;
    int vx0 = 0, vy0 = 0;
    if ( zoom )
    {
        int cc = state.curRoom % LIVEMAP_COLS, cr = state.curRoom / LIVEMAP_COLS;
        vx0 = cc - viewCols / 2;
        vy0 = cr - viewRows / 2;
        if ( vx0 < 0 ) vx0 = 0;
        if ( vy0 < 0 ) vy0 = 0;
        if ( vx0 > LIVEMAP_COLS - viewCols ) vx0 = LIVEMAP_COLS - viewCols;
        if ( vy0 > LIVEMAP_ROWS - viewRows ) vy0 = LIVEMAP_ROWS - viewRows;
    }

    const float gw = LIVEMAP_COLS * LIVEMAP_CELL_W, gh = LIVEMAP_ROWS * LIVEMAP_CELL_H;
    const float cw = gw / viewCols, ch = gh / viewRows;
    const float ox = LIVEMAP_GRID_OX, oy = LIVEMAP_GRID_OY;

    if ( touchX < ox || touchX >= ox + gw || touchY < oy || touchY >= oy + gh )
        return false;

    int col = vx0 + (int) ((touchX - ox) / cw);
    int row = vy0 + (int) ((touchY - oy) / ch);
    if ( col < 0 || col >= LIVEMAP_COLS || row < 0 || row >= LIVEMAP_ROWS )
        return false;

    outRoom = row * LIVEMAP_COLS + col;
    return true;
}

uint8_t CyclePin( uint8_t current )
{
    return (uint8_t) ((current + 1) % (LIVEMAP_PIN_TYPES + 1));
}

const uint8_t kDungeonRooms[9] =
{
    0x37, 0x3C, 0x74, 0x45, 0x0B, 0x22, 0x42, 0x6D, 0x05
};

// Phase 14.2: quest 2's overworld dungeon entrances, decoded
// directly from `overworldRoomSparseAttr.tab`'s `Sparse_RoomReplacement`
// category (World.cpp only applies these replacements when `profile.Quest
// == 1`, i.e. second quest) -- not guessed. Method: parsed the 7-entry
// replacement list (roomId + 7-byte OWRoomAttrs, `Table<T>` heap layout) and
// cross-checked every *base* `overworldRoomAttr.dat` room whose `GetCaveId()`
// falls in 1..9 (all 128 rooms scanned, not just kDungeonRooms's 9) --
// several levels genuinely have *two* valid overworld entrances in the base
// (quest-1) data (a real, documented original-game feature: L5=0x0B/0x1B,
// L6=0x22/0x30, L7=0x19/0x42, L8=0x6C/0x6D, L9=0x00/0x05; kDungeonRooms above
// picked one canonical room per level for the map badge). Replacement
// results: room 0x74 (L3's only quest-1 entrance) is reassigned to a plain
// cave (caveId 30, not 1-9) and room 0x34 becomes L3's new entrance (caveId
// 3) -- L3 relocates 0x74->0x34. Room 0x22 (one of L6's *two* quest-1
// entrances) is similarly reassigned away (caveId 33), but L6's other
// quest-1 entrance, 0x30, is *not* touched by any replacement entry, so it
// stays a valid caveId-6 room in quest 2 -- L6 doesn't vanish, it just needs
// to use its already-existing second entrance for the badge instead. Rooms
// 0x0B (L5) and 0x3C (L2) are also listed in the replacement table but
// re-assigned the *same* caveId they already had (5 and 2) -- no location
// change, presumably other attrs (monster list/palette) differ for quest 2's
// version of those overworld rooms. L1/L4/L7/L8/L9's base rooms don't appear
// in the replacement list at all, so they're unchanged. Net result: only L3
// and L6 actually move.
const uint8_t kDungeonRoomsQuest2[9] =
{
    0x37, 0x3C, 0x34, 0x45, 0x0B, 0x30, 0x42, 0x6D, 0x05
};

struct PoiRoom { uint8_t room; uint8_t kind; };

// See PoiKind's own comment (LiveMapGeometry.h) for what each category means
// and where its data came from.
static const PoiRoom kPoiRooms[] =
{
    // Shop (13 rooms: CaveSpecList indices 13/14/15/16).
    { 0x0C, Poi_Shop }, { 0x12, Poi_Shop }, { 0x15, Poi_Shop }, { 0x25, Poi_Shop },
    { 0x26, Poi_Shop }, { 0x34, Poi_Shop }, { 0x44, Poi_Shop }, { 0x46, Poi_Shop },
    { 0x4A, Poi_Shop }, { 0x4D, Poi_Shop }, { 0x5E, Poi_Shop }, { 0x66, Poi_Shop },
    { 0x6F, Poi_Shop },
    // Potion shop (10 rooms: CaveSpecList index 10, Obj_CaveMedicineShop).
    { 0x02, Poi_Potion }, { 0x04, Poi_Potion }, { 0x0D, Poi_Potion }, { 0x18, Poi_Potion },
    { 0x27, Poi_Potion }, { 0x29, Poi_Potion }, { 0x33, Poi_Potion }, { 0x4B, Poi_Potion },
    { 0x64, Poi_Potion }, { 0x78, Poi_Potion },
    // Door repair (10 rooms: CaveSpecList index 7, the door-repair charge).
    { 0x01, Poi_Repair }, { 0x03, Poi_Repair }, { 0x07, Poi_Repair }, { 0x14, Poi_Repair },
    { 0x1E, Poi_Repair }, { 0x63, Poi_Repair }, { 0x68, Poi_Repair }, { 0x6A, Poi_Repair },
    { 0x72, Poi_Repair }, { 0x7D, Poi_Repair },
    // Tunnel/warp cave (4 rooms: CaveSpecList index 4, the warp stairs).
    { 0x1D, Poi_Tunnel }, { 0x23, Poi_Tunnel }, { 0x49, Poi_Tunnel }, { 0x79, Poi_Tunnel },
    // Gift cave, may include a heart container (7 rooms: CaveSpecList index 1,
    // (the "take one" choice) -- items are RedPotion/(empty)/HeartContainer).
    { 0x06, Poi_Gift }, { 0x20, Poi_Gift }, { 0x2C, Poi_Gift }, { 0x2F, Poi_Gift },
    { 0x3A, Poi_Gift }, { 0x47, Poi_Gift }, { 0x7B, Poi_Gift },
    // Fairy fountain (2 rooms: Sparse_Fairy).
    { 0x39, Poi_Fairy }, { 0x43, Poi_Fairy },
    // Flute/whirlwind warp destination (8 rooms: World.cpp's teleportRoomIds[]).
    { 0x36, Poi_FluteWarp }, { 0x3B, Poi_FluteWarp }, { 0x73, Poi_FluteWarp },
    { 0x44, Poi_FluteWarp }, { 0x0A, Poi_FluteWarp }, { 0x21, Poi_FluteWarp },
    { 0x41, Poi_FluteWarp }, { 0x6C, Poi_FluteWarp },
};

bool FindDungeonNumber( int room, bool quest2, int& outLevel )
{
    const uint8_t* rooms = quest2 ? kDungeonRoomsQuest2 : kDungeonRooms;
    for ( int i = 0; i < 9; i++ )
    {
        if ( rooms[i] == room )
        {
            outLevel = i + 1;
            return true;
        }
    }
    return false;
}

int GetDungeonRoom( int level, bool quest2 )
{
    const uint8_t* rooms = quest2 ? kDungeonRoomsQuest2 : kDungeonRooms;
    return rooms[level - 1];
}

bool FindPoiAt( int room, int startIndex, int& outKind, int& outNextIndex )
{
    int count = (int) (sizeof( kPoiRooms ) / sizeof( kPoiRooms[0] ));
    for ( int i = startIndex; i < count; i++ )
    {
        if ( kPoiRooms[i].room == room )
        {
            outKind = kPoiRooms[i].kind;
            outNextIndex = i + 1;
            return true;
        }
    }
    return false;
}

int GetPoiCount()
{
    return (int) (sizeof( kPoiRooms ) / sizeof( kPoiRooms[0] ));
}

void GetPoiByIndex( int index, int& outRoom, int& outKind )
{
    outRoom = kPoiRooms[index].room;
    outKind = kPoiRooms[index].kind;
}

// See SecretRoom's comment (LiveMapGeometry.h) for the data source and why
// Sparse_SecretScroll isn't included. fracX/fracY computed offline from the
// raw NES x/y bytes: fracX = x/256, fracY = (y-64)/176 (StdViewWidth=256,
// StdViewHeight=240, TileMapBaseY=64 -- World.h's own constants).
//
// Phase 14.2 leftover, resolved: these locations do NOT need a
// quest-2 variant (unlike kDungeonRoomsQuest2 above). Checked by decoding
// `overworldRoomSparseAttr.tab` directly: Sparse_ArmosStairs/ArmosItem/Item
// are single, quest-independent tables -- World.cpp's Sparse_RoomReplacement
// (the only quest-2-conditional data, `profile.Quest == 1 && level == 0`)
// only overwrites `roomAttrs[roomId]` (tile layout/cave id/monster list),
// never the sparse position tables the armos statue / buried item lookups
// use (`SparsePos2`, WorldImpl.h: roomId+x+y, one fixed entry per room).
// Three of the six armos-stairs rooms (0x0B, 0x22, 0x34) *are* in the quest-2
// replacement list, so their surrounding room changes, but comparing base
// vs. replacement byte-for-byte: 0x22 and 0x34 change *only* the cave-id byte
// (Specific[1]) -- 0x22's stairs go from leading to L6's dungeon (caveId 6)
// to a plain cave (caveId 33, since L6 relocates to its other entrance,
// 0x30); 0x34's go from a plain cave (caveId 32) to L3's dungeon (caveId 3,
// matching kDungeonRoomsQuest2's 0x74->0x34 move above) -- same tile layout,
// same monster list, so the armos statue itself is unaffected, only its
// prize changes. 0x0B keeps the same cave id (5, L5) in both quests, though
// its tile layout/monster list do change (a real quest-2 room reskin,
// unrelated to the secret). Net effect for the map: identical secret-dot
// positions in both quests; only what's *behind* two of them differs, and
// that's already fully covered by the dungeon-badge layer (kDungeonRoomsQuest2)
// drawing on top -- no separate quest2 kSecretRooms table needed.
static const SecretRoom kSecretRooms[] =
{
    // Sparse_ArmosStairs (6): moving the armos statue reveals hidden stairs.
    { 0x0B, 0.6875f, 0.363636f },
    { 0x1C, 0.6875f, 0.363636f },
    { 0x22, 0.1875f, 0.363636f },
    { 0x34, 0.25f,   0.363636f },
    { 0x3D, 0.5625f, 0.363636f },
    { 0x4E, 0.625f,  0.363636f },
    // Sparse_ArmosItem (1): moving the armos statue reveals a buried item.
    { 0x24, 0.875f, 0.363636f },
    // Sparse_Item (1): a burn/bomb-revealed buried item.
    { 0x5F, 0.75f, 0.454545f },
};

int GetSecretCount()
{
    return (int) (sizeof( kSecretRooms ) / sizeof( kSecretRooms[0] ));
}

void GetSecretByIndex( int index, int& outRoom, float& outFracX, float& outFracY )
{
    outRoom = kSecretRooms[index].room;
    outFracX = kSecretRooms[index].fracX;
    outFracY = kSecretRooms[index].fracY;
}

// Matches kSecretRooms' layout above: the first 6 entries are the
// Sparse_ArmosStairs rooms (bit index == array index), the rest have a real
// item-pickup flag instead.
static const int kArmosStairsCount = 6;

SecretKind GetSecretKind( int index )
{
    return index < kArmosStairsCount ? SecretKind_ArmosStairs : SecretKind_ArmosItemOrBuried;
}

int GetArmosStairsBit( int index )
{
    return index;
}

// phase 10.4 -- see TreeRockSecret's comment in LiveMapGeometry.h for the data
// source (offline replication of WorldImpl::LoadLayout over all 128 overworld
// rooms; tileRef 0x27 = Bomb, 0x28 = Burn are the only two tiles with those
// actions). 27 bombable walls + 18 burnable trees = 45 hidden cave entrances.
static const TreeRockSecret kTreeRockSecrets[] =
{
    { 0x00, 1, 0.625000f, 0.363636f },
    { 0x01, 1, 0.562500f, 0.090909f },
    { 0x02, 1, 0.375000f, 0.363636f },
    { 0x03, 1, 0.437500f, 0.363636f },
    { 0x05, 1, 0.312500f, 0.545455f },
    { 0x07, 1, 0.625000f, 0.363636f },
    { 0x0D, 1, 0.562500f, 0.090909f },
    { 0x10, 1, 0.562500f, 0.090909f },
    { 0x12, 1, 0.500000f, 0.090909f },
    { 0x13, 1, 0.125000f, 0.090909f },
    { 0x14, 1, 0.750000f, 0.090909f },
    { 0x15, 1, 0.750000f, 0.090909f },
    { 0x16, 1, 0.375000f, 0.090909f },
    { 0x18, 1, 0.750000f, 0.272727f },
    { 0x19, 1, 0.250000f, 0.272727f },
    { 0x1E, 1, 0.750000f, 0.090909f },
    { 0x26, 1, 0.187500f, 0.090909f },
    { 0x27, 1, 0.875000f, 0.090909f },
    { 0x28, 0, 0.812500f, 0.545455f },
    { 0x2C, 1, 0.562500f, 0.545455f },
    { 0x2D, 1, 0.312500f, 0.090909f },
    { 0x33, 1, 0.625000f, 0.090909f },
    { 0x46, 0, 0.562500f, 0.636364f },
    { 0x47, 0, 0.687500f, 0.636364f },
    { 0x48, 0, 0.812500f, 0.181818f },
    { 0x4B, 0, 0.687500f, 0.181818f },
    { 0x4D, 0, 0.812500f, 0.545455f },
    { 0x51, 0, 0.562500f, 0.545455f },
    { 0x53, 0, 0.375000f, 0.545455f },
    { 0x56, 0, 0.625000f, 0.545455f },
    { 0x5B, 0, 0.125000f, 0.545455f },
    { 0x62, 0, 0.500000f, 0.181818f },
    { 0x63, 0, 0.375000f, 0.545455f },
    { 0x67, 1, 0.437500f, 0.090909f },
    { 0x68, 0, 0.125000f, 0.545455f },
    { 0x6A, 0, 0.750000f, 0.545455f },
    { 0x6B, 0, 0.500000f, 0.545455f },
    { 0x6C, 0, 0.312500f, 0.181818f },
    { 0x6D, 0, 0.625000f, 0.181818f },
    { 0x71, 1, 0.312500f, 0.090909f },
    { 0x76, 1, 0.375000f, 0.090909f },
    { 0x78, 0, 0.250000f, 0.545455f },
    { 0x7B, 1, 0.562500f, 0.090909f },
    { 0x7C, 1, 0.375000f, 0.090909f },
    { 0x7D, 1, 0.375000f, 0.090909f },
};

int GetTreeRockSecretCount()
{
    return (int) (sizeof( kTreeRockSecrets ) / sizeof( kTreeRockSecrets[0] ));
}

void GetTreeRockSecretByIndex( int index, int& outRoom, bool& outIsBomb, float& outFracX, float& outFracY )
{
    outRoom = kTreeRockSecrets[index].room;
    outIsBomb = kTreeRockSecrets[index].isBomb != 0;
    outFracX = kTreeRockSecrets[index].fracX;
    outFracY = kTreeRockSecrets[index].fracY;
}
