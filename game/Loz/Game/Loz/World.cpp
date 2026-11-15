/*
   Copyright 2016 Aldo J. Nunez

   Licensed under the Apache License, Version 2.0.
   See the LICENSE text file for details.

   Modified for The Legend of Zelda: 3DS Edition (Nintendo 3DS port).
*/

#include "Common.h"
#include "World.h"
#include "WorldImpl.h"
#include "Graphics.h"
#include "Input.h"
#include "TileAttr.h"
#include "OverworldMidLayerTiles.gen.h"
#include "Player.h"
#include "ItemObj.h"
#include "PlayerItemsAnim.h"
#include "SpriteAnimator.h"
#include "ObjType.h"
#include "Monsters.h"
#include "Profile.h"
#include "Credits.h"
#include "TextBox.h"
#include "UWBossAnim.h"
#include "GameMenu.h"
#include "RegisterMenu.h"
#include "EliminateMenu.h"
#include "SaveFolder.h"
#include "Options.h"
#include "Sound.h"
#include "SoundId.h"


enum
{
    LevelGroups     = 3,
};

enum
{
    Cave_Items      = 0x79,
    Cave_Shortcut   = 0x7A,
};

enum Secret
{
    Secret_None,
    Secret_FoesDoor,
    Secret_Ringleader,
    Secret_LastBoss,
    Secret_BlockDoor,
    Secret_BlockStairs,
    Secret_MoneyOrLife,
    Secret_FoesItem
};

struct SparseRoomAttr
{
    uint8_t     roomId;
    OWRoomAttrs attrs;
};

struct SparseMaze
{
    uint8_t     roomId;
    uint8_t     exitDir;
    uint8_t     path[4];
};

struct SparseRoomItem
{
    uint8_t     roomId;
    uint8_t     x;
    uint8_t     y;
    uint8_t     itemId;
};


enum
{
    Sparse_ArmosStairs,
    Sparse_ArmosItem,
    Sparse_Dock,
    Sparse_Item,
    Sparse_Shortcut,
    Sparse_Maze,
    Sparse_SecretScroll,
    Sparse_Ladder,
    Sparse_Recorder,
    Sparse_Fairy,
    Sparse_RoomReplacement,
};

enum
{
    Extra_PondColors,
    Extra_SpawnSpots,
    Extra_ObjAttrs,
    Extra_CavePalettes,
    Extra_Caves,
    Extra_LevelPersonStringIds,
    Extra_HitPoints,
    Extra_PlayerDamage,
};

struct ColorSeq
{
    int     Length;
    uint8_t Colors[1];
};

struct SpotSeq
{
    int     Length;
    RSpot   Spots[1];
};

struct PaletteSet
{
    int     Length;
    int     Start;
    uint8_t Palettes[1][PaletteLength];
};

struct CaveSpecList
{
    int         Count;
    CaveSpec    Specs[1];
};

struct LevelPersonStrings
{
    uint8_t StringIds[LevelGroups][PersonTypes];
};

const int OWMarginRight  = 0xE0;
const int OWMarginLeft   = 0x10;
const int OWMarginTop    = 0x4D;
const int OWMarginBottom = 0xCD;

const int UWMarginRight  = 0xD0;
const int UWMarginLeft   = 0x20;
const int UWMarginTop    = 0x5D;
const int UWMarginBottom = 0xBD;

const int UWBorderRight  = 0xE0;
const int UWBorderLeft   = 0x20;
const int UWBorderTop    = 0x60;
const int UWBorderBottom = 0xD0;

const int UWBlockRow        = 5;

const int DoorWidth         = 32;
const int DoorHeight        = 32;
const int DoorOverlayBaseY  = 128;
const int DoorUnderlayBaseY = 0;

const int DoorHoleCoordH = 0x90;
const int DoorHoleCoordV = 0x78;

const int UWBombRadius = 32;

static const uint8_t levelGroups[] = 
{
    0, 0, 1, 1, 0, 1, 0, 1, 2
};

const Point doorMiddles[] = 
{
    { 0xE0, 0x98 },
    { 0x20, 0x98 },
    { 0x80, 0xD0 },
    { 0x80, 0x60 }
};

const int doorSrcYs[] = 
{
    64,
    96,
    0,
    32
};

const Point doorPos[] = 
{
    { 224,  136 },
    { 0,    136 },
    { 112,  208 },
    { 112,  64 }
};

struct DoorStateFaces
{
    uint8_t Closed;
    uint8_t Open;
};

const DoorStateFaces doorFaces[] = 
{
    { 0, 0 },
    { 3, 3 },
    { 3, 3 },
    { 3, 3 },
    { 3, 4 },
    { 1, 0 },
    { 1, 0 },
    { 2, 0 },
};

const bool doorFaceOpen[] = 
{
    true,
    false,
    false,
    false,
    true
};

struct EquipValue
{
    uint8_t Value;
    uint8_t Slot;
};

// The item ID to item slot map is at $6B14, and copied to RAM at $72A4.
// The item ID to item value map is at $6B38, and copied to RAM at $72C8.
// They're combined here.
static const EquipValue sItemToEquipment[] = 
{
    { 4, ItemSlot_Bombs },
    { 1, ItemSlot_Sword },
    { 2, ItemSlot_Sword },
    { 3, ItemSlot_Sword },
    { 1, ItemSlot_Food },
    { 1, ItemSlot_Recorder },
    { 1, ItemSlot_Candle },
    { 2, ItemSlot_Candle },
    { 1, ItemSlot_Arrow },
    { 2, ItemSlot_Arrow },
    { 1, ItemSlot_Bow },
    { 1, ItemSlot_MagicKey },
    { 1, ItemSlot_Raft },
    { 1, ItemSlot_Ladder },
    { 1, ItemSlot_PowerTriforce },
    { 5, ItemSlot_RupeesToAdd },
    { 1, ItemSlot_Rod },
    { 1, ItemSlot_Book },
    { 1, ItemSlot_Ring },
    { 2, ItemSlot_Ring },
    { 1, ItemSlot_Bracelet },
    { 1, ItemSlot_Letter },
    { 1, ItemSlot_Compass9 },
    { 1, ItemSlot_Map9 },
    { 1, ItemSlot_RupeesToAdd },
    { 1, ItemSlot_Keys },
    { 1, ItemSlot_HeartContainers },
    { 1, ItemSlot_TriforcePieces },
    { 1, ItemSlot_MagicShield },
    { 1, ItemSlot_Boomerang },
    { 2, ItemSlot_Boomerang },
    { 1, ItemSlot_Potion },
    { 2, ItemSlot_Potion },
    { 1, ItemSlot_Clock },
    { 1, ItemSlot_Bombs },
    { 3, ItemSlot_Bombs },
};

static const uint8_t teleportYs[] = { 0x8D, 0xAD, 0x8D, 0x8D, 0xAD, 0x8D, 0xAD, 0x5D };
static const uint8_t teleportRoomIds[] = { 0x36, 0x3B, 0x73, 0x44, 0x0A, 0x21, 0x41, 0x6C };

const int StartX = 0x78;
const int FirstCaveIndex = 0x10;
const int TriforcePieceX = 0x78;


WorldImpl::TileActionFunc WorldImpl::sActionFuncs[] = 
{
    &WorldImpl::NoneTileAction,
    &WorldImpl::PushTileAction,
    &WorldImpl::BombTileAction,
    &WorldImpl::BurnTileAction,
    &WorldImpl::HeadstoneTileAction,
    &WorldImpl::LadderTileAction,
    &WorldImpl::RaftTileAction,
    &WorldImpl::CaveTileAction,
    &WorldImpl::StairsTileAction,
    &WorldImpl::GhostTileAction,
    &WorldImpl::ArmosTileAction,
    &WorldImpl::BlockTileAction,
};

WorldImpl::UpdateFunc WorldImpl::sModeFuncs[Modes] = 
{
    nullptr,
    &WorldImpl::UpdateGameMenu,
    &WorldImpl::UpdateLoadLevel,
    &WorldImpl::UpdateUnfurl,
    &WorldImpl::UpdateEnter,
    &WorldImpl::UpdatePlay,
    &WorldImpl::UpdateLeave,
    &WorldImpl::UpdateScroll,
    &WorldImpl::UpdateContinueQuestion,
    &WorldImpl::UpdatePlay,
    &WorldImpl::UpdateLeaveCellar,
    &WorldImpl::UpdatePlay,
    nullptr,
    nullptr,
    &WorldImpl::UpdateRegisterMenu,
    &WorldImpl::UpdateEliminateMenu,
    &WorldImpl::UpdateStairsState,
    &WorldImpl::UpdateDie,
    &WorldImpl::UpdateEndLevel,
    &WorldImpl::UpdateWinGame,
    &WorldImpl::UpdatePlayCellar,
    &WorldImpl::UpdatePlayCave,
    &WorldImpl::UpdateTitle,
};

WorldImpl::DrawFunc WorldImpl::sDrawFuncs[Modes] = 
{
    nullptr,
    &WorldImpl::DrawGameMenu,
    &WorldImpl::DrawLoadLevel,
    &WorldImpl::DrawUnfurl,
    &WorldImpl::DrawEnter,
    &WorldImpl::DrawPlay,
    &WorldImpl::DrawLeave,
    &WorldImpl::DrawScroll,
    &WorldImpl::DrawContinueQuestion,
    &WorldImpl::DrawPlay,
    &WorldImpl::DrawLeaveCellar,
    &WorldImpl::DrawPlay,
    nullptr,
    nullptr,
    &WorldImpl::DrawGameMenu,
    &WorldImpl::DrawGameMenu,
    &WorldImpl::DrawStairsState,
    &WorldImpl::DrawDie,
    &WorldImpl::DrawEndLevel,
    &WorldImpl::DrawWinGame,
    &WorldImpl::DrawPlayCellar,
    &WorldImpl::DrawPlayCave,
    &WorldImpl::DrawTitle,
};

WorldImpl::UpdateFunc WorldImpl::sPlayCellarFuncs[PlayCellarState::MaxSubstate] =
{
    &WorldImpl::UpdatePlayCellar_Start,
    &WorldImpl::UpdatePlayCellar_FadeOut,
    &WorldImpl::UpdatePlayCellar_LoadRoom,
    &WorldImpl::UpdatePlayCellar_FadeIn,
    &WorldImpl::UpdatePlayCellar_Walk,
};

WorldImpl::UpdateFunc WorldImpl::sPlayCaveFuncs[PlayCaveState::MaxSubstate] =
{
    &WorldImpl::UpdatePlayCave_Start,
    &WorldImpl::UpdatePlayCave_Wait,
    &WorldImpl::UpdatePlayCave_LoadRoom,
    &WorldImpl::UpdatePlayCave_Walk,
};

WorldImpl::UpdateFunc WorldImpl::sEndLevelFuncs[EndLevelState::MaxSubstate] = 
{
    &WorldImpl::UpdateEndLevel_Start,
    &WorldImpl::UpdateEndLevel_Wait,
    &WorldImpl::UpdateEndLevel_Flash,
    &WorldImpl::UpdateEndLevel_FillHearts,
    &WorldImpl::UpdateEndLevel_Wait,
    &WorldImpl::UpdateEndLevel_Furl,
    &WorldImpl::UpdateEndLevel_Wait,
};

WorldImpl::UpdateFunc WorldImpl::sWinGameFuncs[WinGameState::MaxSubstate] = 
{
    &WorldImpl::UpdateWinGame_Start,
    &WorldImpl::UpdateWinGame_Text1,
    &WorldImpl::UpdateWinGame_Stand,
    &WorldImpl::UpdateWinGame_Hold1,
    &WorldImpl::UpdateWinGame_Colors,
    &WorldImpl::UpdateWinGame_Hold2,
    &WorldImpl::UpdateWinGame_Text2,
    &WorldImpl::UpdateWinGame_Hold3,
    &WorldImpl::UpdateWinGame_NoObjects,
    &WorldImpl::UpdateWinGame_Credits,
};

WorldImpl::UpdateFunc WorldImpl::sScrollFuncs[ScrollState::MaxSubstate] = 
{
    &WorldImpl::UpdateScroll_Start,
    &WorldImpl::UpdateScroll_AnimatingColors,
    &WorldImpl::UpdateScroll_FadeOut,
    &WorldImpl::UpdateScroll_LoadRoom,
    &WorldImpl::UpdateScroll_Scroll,
};

WorldImpl::UpdateFunc WorldImpl::sDeathFuncs[DeathState::MaxSubstate] = 
{
    &WorldImpl::UpdateDie_Start,
    &WorldImpl::UpdateDie_Flash,
    &WorldImpl::UpdateDie_Wait1,
    &WorldImpl::UpdateDie_Turn,
    &WorldImpl::UpdateDie_Fade,
    &WorldImpl::UpdateDie_GrayLink,
    &WorldImpl::UpdateDie_Spark,
    &WorldImpl::UpdateDie_Wait2,
    &WorldImpl::UpdateDie_GameOver,
};

WorldImpl::UpdateFunc WorldImpl::sLeaveCellarFuncs[LeaveCellarState::MaxSubstate] = 
{
    &WorldImpl::UpdateLeaveCellar_Start,
    &WorldImpl::UpdateLeaveCellar_FadeOut,
    &WorldImpl::UpdateLeaveCellar_LoadRoom,
    &WorldImpl::UpdateLeaveCellar_FadeIn,
    &WorldImpl::UpdateLeaveCellar_Walk,
    &WorldImpl::UpdateLeaveCellar_Wait,
    &WorldImpl::UpdateLeaveCellar_LoadOverworldRoom,
};

WorldImpl::UpdateFunc WorldImpl::sEnterFuncs[EnterState::MaxSubstate] =
{
    &WorldImpl::UpdateEnter_Start,
    &WorldImpl::UpdateEnter_Wait,
    &WorldImpl::UpdateEnter_FadeIn,
    &WorldImpl::UpdateEnter_Walk,
    &WorldImpl::UpdateEnter_WalkCave,
};

static WorldImpl* sWorld;


void GetWorldCoord( int roomId, int& row, int& col )
{
    row = (roomId & 0xF0) >> 4;
    col = roomId & 0x0F;
}

int MakeRoomId( int row, int col )
{
    return (row << 4) | col;
}

int GetNextRoomId( int curRoomId, Direction dir )
{
    int row;
    int col;

    GetWorldCoord( curRoomId, row, col );

    switch ( dir )
    {
    case Dir_Left:
        if ( col == 0 )
            return curRoomId;
        col--;
        break;

    case Dir_Right:
        if ( col == World::WorldWidth - 1 )
            return curRoomId;
        col++;
        break;

    case Dir_Up:
        if ( row == 0 )
            return curRoomId;
        row--;
        break;

    case Dir_Down:
        if ( row == World::WorldHeight - 1 )
            return curRoomId;
        row++;
        break;
    }

    int nextRoomId = MakeRoomId( row, col );
    return nextRoomId;
}

void GetRoomCoord( int position, int& row, int& col )
{
    row = position & 0x0F;
    col = (position & 0xF0) >> 4;
    row -= 4;
}

void GetRSpotCoord( uint32_t position, int& x, int& y )
{
    x = (position & 0x0F) << 4;
    y = (position & 0xF0) | 0xD;
}

Point GetRoomItemPosition( uint8_t position )
{
    Point point;
    point.X = position & 0xF0;
    point.Y = (uint8_t) (position << 4);
    return point;
}

int GetDoorStateFace( int type, bool state )
{
    if ( state )
        return doorFaces[type].Open;
    return doorFaces[type].Closed;
}

// On real hardware, in 3D mode, the ground layer went black while switching
// screens.
//
// "Clear the screen" means "reset the backdrop", so it has to land on the
// backdrop (Ground) layer. Only DrawPlay() happened to set that layer before
// calling this; every other draw function (DrawScroll, DrawRoomNoObjects,
// DrawUnfurl, DrawEndLevel, DrawLeaveCellar, DrawPlayCave, DrawSubmenu, ...)
// cleared whichever layer was simply left active from before -- in practice
// Mid, because that is what DrawPlay()/DrawScroll() end on. With 3D off that
// is harmless (SetTopLayer is a no-op, one shared buffer), but with 3D on it
// filled the *Mid* buffer with opaque black, and Mid composites on top of
// Ground -- so the entire room vanished behind a black sheet for the duration
// of every room transition.
//
// Fixed here rather than at each call site on purpose: patching the seven-odd
// callers is precisely the "handle it separately everywhere" pattern that
// keeps letting one straggler through. The previous layer is restored so this
// stays a pure backdrop clear with no effect on what the caller draws next.
void ClearScreen()
{
    TopLayer prev = Graphics::GetTopLayer();
    Graphics::SetTopLayer( TopLayer::Ground );
    al_clear_to_color( al_map_rgb( 0, 0, 0 ) );
    Graphics::SetTopLayer( prev );
}

void ClearScreen( int sysColor )
{
    ALLEGRO_COLOR color = Graphics::GetSystemColor( sysColor );
    TopLayer prev = Graphics::GetTopLayer();
    Graphics::SetTopLayer( TopLayer::Ground );
    al_clear_to_color( color );
    Graphics::SetTopLayer( prev );
}

void WorldImpl::ClearDeadObjectQueue()
{
    for ( int i = 0; i < objectsToDeleteCount; i++ )
    {
        delete objectsToDelete[i];
        objectsToDelete[i] = nullptr;
    }

    objectsToDeleteCount = 0;
}

void WorldImpl::SetOnlyObject( int slot, Object* obj )
{
    assert( slot >= 0 && slot < MaxObjects );
    if ( objects[slot] != nullptr )
    {
        if ( objectsToDeleteCount == MaxObjects )
            ClearDeadObjectQueue();
        objectsToDelete[objectsToDeleteCount] = objects[slot];
        objectsToDeleteCount++;
    }
    objects[slot] = obj;
}

Ladder* WorldImpl::GetLadderObj()
{
    return (Ladder*) objects[LadderSlot];
}

void WorldImpl::SetLadderObj( Ladder* ladder )
{
    SetOnlyObject( LadderSlot, ladder );
}

void WorldImpl::SetBlockObj( Object* block )
{
    SetOnlyObject( BlockSlot, block );
}

void WorldImpl::DeleteObjects()
{
    for ( int i = 0; i < MaxObjects; i++ )
    {
        if ( objects[i] != nullptr )
        {
            delete objects[i];
            objects[i] = nullptr;
        }
    }

    ClearDeadObjectQueue();
}

void WorldImpl::CleanUpRoomItems()
{
    DeleteObjects();
    World::SetItem( ItemSlot_Clock, 0 );
}

void WorldImpl::DeleteDeadObjects()
{
    for ( int i = 0; i < MaxObjects; i++ )
    {
        Object* obj = objects[i];
        if ( obj != nullptr && obj->IsDeleted() )
        {
            delete obj;
            objects[i] = nullptr;
        }
    }

    ClearDeadObjectQueue();
}

void WorldImpl::InitObjectTimers()
{
    for ( int i = 0; i < MaxObjects; i++ )
    {
        objectTimers[i] = 0;
    }
}

void WorldImpl::DecrementObjectTimers()
{
    for ( int i = 0; i < MaxObjects; i++ )
    {
        if ( objectTimers[i] != 0 )
            objectTimers[i]--;

        if ( objects[i] != nullptr )
            objects[i]->DecrementObjectTimer();
    }

    // ORIGINAL: Here the player isn't part of the array, but in the original it's the first element.
    player->DecrementObjectTimer();
}

void WorldImpl::InitStunTimers()
{
    longTimer = 0;
    for ( int i = 0; i < MaxObjects; i++ )
    {
        stunTimers[i] = 0;
    }
}

void WorldImpl::DecrementStunTimers()
{
    if ( longTimer > 0 )
    {
        longTimer--;
        return;
    }

    longTimer = 9;

    for ( int i = 0; i < MaxObjects; i++ )
    {
        if ( stunTimers[i] != 0 )
            stunTimers[i]--;

        if ( objects[i] != nullptr )
            objects[i]->DecrementStunTimer();
    }

    // ORIGINAL: Here the player isn't part of the array, but in the original it's the first element.
    player->DecrementStunTimer();
}

void WorldImpl::InitPlaceholderTypes()
{
    memset( placeholderTypes, 0, sizeof placeholderTypes );
}

int WorldImpl::FindEmptyMonsterSlot()
{
    for ( int i = LastMonsterSlot; i >= 0; i-- )
    {
        if ( objects[i] == nullptr )
            return i;
    }
    return -1;
}

void WorldImpl::ClearRoomItemData()
{
    recorderUsed = 0;
    summonedWhirlwind = false;
    shuttersPassedDirs = Dir_None;
    brightenRoom = false;
    activeShots = 0;
}

void WorldImpl::SetPlayerColor()
{
    static const uint8_t sysColors[] = 
    {
        0x29,
        0x32,
        0x16
    };

    int value = profile.Items[ItemSlot_Ring];

    Graphics::SetColorIndexed( PlayerPalette, 1, sysColors[value] );
}

static void SetFlashPalette()
{
    static const uint8_t palette[] = 
    {
        0x0F,
        0x30,
        0x30,
        0x30
    };

    for ( int i = 2; i < BackgroundPalCount; i++ )
    {
        Graphics::SetPaletteIndexed( i, palette );
    }

    Graphics::UpdatePalettes();
}

static void SetLevelPalettes( const uint8_t palettes[2][PaletteLength] )
{
    for ( int i = 0; i < 2; i++ )
    {
        Graphics::SetPaletteIndexed( 2 + i, palettes[i] );
    }

    Graphics::UpdatePalettes();
}

static void SetLevelPalette()
{
    const World::LevelInfoBlock* infoBlock = World::GetLevelInfo();

    for ( int i = 2; i < BackgroundPalCount; i++ )
    {
        Graphics::SetPaletteIndexed( i, infoBlock->Palettes[i] );
    }

    Graphics::UpdatePalettes();
}

static void SetLevelFgPalette()
{
    const World::LevelInfoBlock* infoBlock = World::GetLevelInfo();
    Graphics::SetPaletteIndexed( LevelFgPalette, infoBlock->Palettes[LevelFgPalette] );
}


WorldImpl::WorldImpl()
    :   curRoomId( 0 ),
        curTileMapIndex( 0 ),
        lastMode( Mode_Demo ),
        curMode( Mode_Play ),
        curColorSeqNum( 0 ),
        darkRoomFadeStep( 0 ),
        curMazeStep( 0 ),
        spotIndex( 0 ),
        tempShutterRoomId( 0 ),
        tempShutterDoorDir( 0 ),
        tempShuttersRoomId( 0 ),
        tempShutters( false ),
        wallsBmp( nullptr ),
        doorsBmp( nullptr ),
        edgeX( 0 ),
        edgeY( 0x40 ),
        worldKillCycle( 0 ),
        worldKillCount( 0 ),
        roomKillCount( 0 ),
        playTimeFrameAccum( 0 ),
        roomAllDead( false ),
        madeRoomItem( false ),
        enablePersonFireballs( false ),
        nextRoomHistorySlot( 0 ),
        helpDropCounter( 0 ),
        helpDropValue( 0 ),
        whirlwindTeleporting( 0 ),
        teleportingRoomIndex( 0 ),
        swordBlocked( false ),
        pause( 0 ),
        submenu( 0 ),
        submenuOffsetY( 0 ),
        statusBarVisible( false ),
        textBox1( nullptr ),
        textBox2( nullptr ),
        credits( nullptr ),
        gameMenu( nullptr ),
        nextGameMenu( nullptr ),

        player(),
        giveFakePlayerPos(),
        playerPosTimer(),
        fakePlayerPos(),

        objects(),
        objectsToDelete(),
        objectsToDeleteCount(),
        objectTimers(),
        curObjSlot(),
        longTimer(),
        stunTimers(),
        placeholderTypes(),

        doorwayDir(),
        triggeredDoorCmd(),
        triggeredDoorDir(),
        fromUnderground(),
        activeShots(),
        triggerShutters(),
        summonedWhirlwind(),
        powerTriforceFanfare(),
        recorderUsed(),
        shuttersPassedDirs(),
        brightenRoom(),
        // -1, not 0 (the default int-init this used to have): 0 is a real,
        // valid slot, so AutoSave()/SaveProfile()'s existing "profileSlot < 0
        // -> no active profile yet" guards need an actual sentinel to mean
        // anything. Matters now that phase 9.2's HOME-exit hook can call
        // World::SaveNow() while still on the file-select menu (never
        // reachable before — AutoSave() only ever fired from inside gameplay,
        // always after Start() had already set a real slot).
        profileSlot( -1 ),
        profile(),
        curUWBlockFlags(),
        checkpointHearts( 0 ),
        autoSaveLastFrame( -1 ),
        savedNotifyTimer( 0 ),
        lowHpBeepTimer( 0 ),
        loadStep( 0 ),
        loadOrigLevel( 0 ),
        loadOrigRoomId( 0 )
{
}

WorldImpl::~WorldImpl()
{
    delete player;
    player = nullptr;

    DeleteObjects();

    if ( wallsBmp != nullptr )
    {
        al_destroy_bitmap( wallsBmp );
        wallsBmp = nullptr;
    }

    if ( doorsBmp != nullptr )
    {
        al_destroy_bitmap( doorsBmp );
        doorsBmp = nullptr;
    }
}

void WorldImpl::LoadOpenRoomContext()
{
    colCount = 16;
    rowCount = 11;
    startRow = 0;
    startCol = 0;
    tileTypeCount = 56;
    marginRight = OWMarginRight;
    marginLeft = OWMarginLeft;
    marginBottom = OWMarginBottom;
    marginTop = OWMarginTop;
}

void WorldImpl::LoadClosedRoomContext()
{
    colCount = 12;
    rowCount = 7;
    startRow = 2;
    startCol = 2;
    tileTypeCount = 9;
    marginRight = UWMarginRight;
    marginLeft = UWMarginLeft;
    marginBottom = UWMarginBottom;
    marginTop = UWMarginTop;
}

// RoomCols/ColTables/TileAttrs are constant across every level that shares a
// context (e.g. every underworld dungeon reads the same "underworldRoomCols.
// dat" etc. — confirmed via ExtractLoz's Program.cs) but were unconditionally
// re-read from romfs on every LoadLevel() call. Skip the reload when the
// context (identified by its RoomCols path, unique per context) is already
// the one resident in roomCols/colTables/tileAttrs. See phase 5.1.
static char s_lastMapResourceKey[64] = "";

void WorldImpl::LoadMapResourcesFromDirectory( int uniqueRoomCount )
{
    if ( strcmp( s_lastMapResourceKey, directory.RoomCols ) != 0 )
    {
        Util::LoadList( directory.RoomCols, roomCols, uniqueRoomCount );
        Util::LoadResource( directory.ColTables, &colTables );
        Util::LoadList( directory.TileAttrs, tileAttrs, tileTypeCount );

        strncpy( s_lastMapResourceKey, directory.RoomCols, sizeof( s_lastMapResourceKey ) - 1 );
        s_lastMapResourceKey[sizeof( s_lastMapResourceKey ) - 1] = 0;
    }

    Graphics::LoadTileSheet( Sheet_Background, directory.TilesImage );
}

void WorldImpl::LoadOverworldContext()
{
    LoadOpenRoomContext();
    LoadMapResourcesFromDirectory( 124 );
}

void WorldImpl::LoadUnderworldContext()
{
    LoadClosedRoomContext();
    LoadMapResourcesFromDirectory( 64 );
}

void WorldImpl::LoadCellarContext()
{
    LoadOpenRoomContext();

    if ( strcmp( s_lastMapResourceKey, "underworldCellarRoomCols.dat" ) != 0 )
    {
        Util::LoadList( "underworldCellarRoomCols.dat", roomCols, 2 );

        Util::LoadResource( "underworldCellarCols.tab", &colTables );

        Util::LoadList( "underworldCellarTileAttrs.dat", tileAttrs, tileTypeCount );

        strncpy( s_lastMapResourceKey, "underworldCellarRoomCols.dat", sizeof( s_lastMapResourceKey ) - 1 );
        s_lastMapResourceKey[sizeof( s_lastMapResourceKey ) - 1] = 0;
    }

    Graphics::LoadTileSheet( Sheet_Background, "underworldCellarTiles.png" );
}

void WorldImpl::LoadLevel( int level )
{
    // Run every step in order — identical to the historical monolithic load.
    // The transition path (UpdateLoadLevel) instead calls LoadLevelStep() one
    // step per frame to spread the cost; see LoadLevelStep() and phase 5.1b.
    for ( int i = 0; i < LoadLevelStepCount; i++ )
        LoadLevelStep( level, i );
}

// One chunk of LoadLevel()'s work. Steps run in the same order as the old
// monolithic body, so the end state is byte-for-byte identical; splitting only
// changes *when* each romfs read happens (one per blank "Wait" frame) so no
// single frame pays the whole ~6 ms. Step 0 must run first (loads `directory`,
// which every later step's paths come from); the final step creates the Player
// and applies second-quest room replacement (needs sparseRoomAttrs from just
// before it). Steps that only touch cached sheets/map-resources are near-free.
void WorldImpl::LoadLevelStep( int level, int step )
{
    switch ( step )
    {
    case 0:
    {
        LevelDirectory::FixedString levelDirName = "";
        sprintf_s( levelDirName, "levelDir_%d_%d.dat", profile.Quest, level );
        Util::LoadList( levelDirName, &directory, 1 );

        Util::BlobResLoader<LevelInfoBlock, 1> infoBlockLoader( &infoBlock );
        Util::LoadResource( directory.LevelInfoBlock, &infoBlockLoader );

        // wallsBmp/doorsBmp destroy-and-reload moved to step 1 (see the
        // s_lastWallsPath/s_lastDoorsPath comment there) -- no longer
        // unconditionally torn down here, so a dungeon re-entry with the
        // same walls/doors can skip the reload entirely.

        tempShutterRoomId = 0;
        tempShutterDoorDir = 0;
        tempShuttersRoomId = 0;
        tempShutters = false;
        prevRoomWasCellar = false;
        darkRoomFadeStep = 0;
        memset( levelKillCounts, 0, sizeof levelKillCounts );
        memset( roomHistory, 0, sizeof roomHistory );
        whirlwindTeleporting = 0;
        break;
    }
    case 1:
        if ( level == 0 )
        {
            LoadOverworldContext();
            curUWBlockFlags = nullptr;
        }
        else
        {
            LoadUnderworldContext();

            // Cache wallsBmp/doorsBmp by path, same "skip if it's what's
            // already resident" idea LoadMapResourcesFromDirectory() already
            // uses for RoomCols/ColTables/TileAttrs (s_lastMapResourceKey)
            // -- found via phase 5.1b JIT-warm profiling: these
            // two `al_load_bitmap()` (loadIndexed()) reads cost ~16ms *each*
            // on Azahar, ~32ms total, dwarfing every other LoadLevelStep
            // combined (all others are sub-3ms) -- and they were the only
            // per-level asset with no cache at all, unconditionally torn
            // down and reloaded on every dungeon entry even when
            // re-entering the exact same dungeon (a common pattern: leave
            // a dungeon, walk around outside, go back in). Persisting
            // across an overworld trip in between is intentional and safe:
            // going to level 0 never touches wallsBmp/doorsBmp/these path
            // statics (see the level==0 branch above), so a same-dungeon
            // re-entry after visiting the overworld still hits the cache.
            static char s_lastWallsPath[sizeof( LevelDirectory::FixedString )] = "";
            static char s_lastDoorsPath[sizeof( LevelDirectory::FixedString )] = "";
            if ( strcmp( s_lastWallsPath, directory.Extra2 ) != 0 )
            {
                if ( wallsBmp != nullptr )
                    al_destroy_bitmap( wallsBmp );
                wallsBmp = al_load_bitmap( directory.Extra2 );
                strncpy( s_lastWallsPath, directory.Extra2, sizeof( s_lastWallsPath ) - 1 );
                s_lastWallsPath[sizeof( s_lastWallsPath ) - 1] = 0;
            }
            if ( strcmp( s_lastDoorsPath, directory.Extra3 ) != 0 )
            {
                if ( doorsBmp != nullptr )
                    al_destroy_bitmap( doorsBmp );
                doorsBmp = al_load_bitmap( directory.Extra3 );
                strncpy( s_lastDoorsPath, directory.Extra3, sizeof( s_lastDoorsPath ) - 1 );
                s_lastDoorsPath[sizeof( s_lastDoorsPath ) - 1] = 0;
            }
            if ( level < 7 )
                curUWBlockFlags = profile.LevelFlags1;
            else
                curUWBlockFlags = profile.LevelFlags2;

            for ( int i = 0; i < _countof( tileMaps ); i++ )
            {
                memset( tileMaps[i].tileRefs, Tile_Wall, sizeof tileMaps[i].tileRefs );
            }
        }
        break;
    case 2:
        Graphics::LoadTileSheet( Sheet_PlayerAndItems, directory.PlayerImage, directory.PlayerSheet );
        Graphics::LoadTileSheet( Sheet_Npcs, directory.NpcImage, directory.NpcSheet );
        break;
    case 3:
        if ( directory.BossImage[0] != '\0' )
            Graphics::LoadTileSheet( Sheet_Boss, directory.BossImage, directory.BossSheet );
        break;
    case 4:
        Util::LoadList( directory.RoomAttrs, roomAttrs, Rooms );
        break;
    case 5:
        Util::LoadResource( directory.LevelInfoEx, &extraData );
        break;
    case 6:
        Util::LoadResource( directory.ObjLists, &objLists );
        break;
    case 7:
    {
        Util::LoadResource( directory.Extra1, &sparseRoomAttrs );

        Direction facing = Dir_Up;
        if ( player != nullptr )
        {
            facing = player->GetFacing();
            delete player;
        }
        player = new Player();
        player->SetFacing( facing );

        // Replace room attributes, if in second quest.
        if ( level == 0 && profile.Quest == 1 )
        {
            const uint8_t* pReplacement = sparseRoomAttrs.GetItem( Sparse_RoomReplacement );
            int replacementCount = *pReplacement;
            const SparseRoomAttr*   sparseAttr = (SparseRoomAttr*) &pReplacement[2];

            for ( int i = 0; i < replacementCount; i++ )
            {
                int roomId = sparseAttr[i].roomId;
                roomAttrs[roomId] = sparseAttr[i].attrs;
            }
        }
        break;
    }
    }
}

void WorldImpl::Init()
{
    Options::Init();   // phase 11.1/11.2: load persisted global options once, before any TextBox is created

    int sysPal[SysPaletteLength];
    Util::LoadList( "pal.dat", sysPal, SysPaletteLength );
    Graphics::LoadSystemPalette( sysPal );

    Graphics::LoadTileSheet( Sheet_Font, "font.png" );
    Graphics::LoadTileSheet( Sheet_PlayerAndItems, "playerItem.png", "playerItemsSheet.tab" );

    // Preload every level-context-independent sheet once, up front, so
    // LoadLevel()'s per-transition romfs reads (previously the dominant
    // frametime spike — see phase 5.1) become pure cache hits. These
    // filenames are constant across all levels that use them (confirmed via
    // ExtractLoz's Program.cs: playerItem.png for every level; Background/
    // Npcs vary only by context — overworld vs. underworld vs. cellar — not
    // by which specific dungeon).
    //
    // Real hardware crash: this used to also call
    // Graphics::PrewarmSheet() (all 8 palette rows -> real GPU/VRAM
    // textures) right after *every one* of these LoadTileSheet calls, so by
    // the end of Init() there were 3 Background + 2 Npcs variants' worth of
    // textures resident in VRAM simultaneously (the sheet cache keeps them
    // all, see SheetCacheDepth in Graphics_3ds.cpp) -- on top of Font and
    // PlayerAndItems, prewarmed below. That comfortably exceeded real
    // hardware's actual available VRAM (the "negligible VRAM cost" comment
    // on the cache was a source-level guess, never measured on hardware --
    // see DEVELOPMENT.md). C3D_TexInitVRAM returned success with a pointer
    // that then faulted on the very next memset, 100% reproducibly, every
    // boot. Azahar's allocator tolerated it; real hardware's didn't.
    //
    // Fix: only eagerly prewarm the variant actually needed the moment the
    // player presses Start (overworld) -- LoadTileSheet still runs for the
    // rest so their romfs reads are still cached ahead of time, they just
    // don't get GPU-texture-expanded until first actually drawn (entering a
    // dungeon/cellar for the first time after boot pays a small one-time
    // expansion cost then, same as before this optimization existed at
    // all -- far better than a boot-time crash).
    Graphics::LoadTileSheet( Sheet_Background, "underworldTiles.png" );
    Graphics::LoadTileSheet( Sheet_Background, "underworldCellarTiles.png" );
    Graphics::LoadTileSheet( Sheet_Background, "overworldTiles.png" );
    Graphics::PrewarmSheet( Sheet_Background );
    Graphics::LoadTileSheet( Sheet_Npcs, "uwNpcs.png", "uwNpcsSheet.tab" );
    Graphics::LoadTileSheet( Sheet_Npcs, "owNpcs.png", "owNpcsSheet.tab" );
    Graphics::PrewarmSheet( Sheet_Npcs );

    // Also preload the overworld's RoomCols/ColTables/TileAttrs (same
    // filenames/element counts LoadOverworldContext() uses — `directory`
    // isn't populated yet at Init() time, so this can't just call that
    // function; the literal names are confirmed via ExtractLoz's Program.cs)
    // so the very first LoadLevel(0) after "Start" is a full cache hit too.
    Util::LoadList( "overworldRoomCols.dat", roomCols, 124 );
    Util::LoadResource( "overworldCols.tab", &colTables );
    Util::LoadList( "overworldTileAttrs.dat", tileAttrs, 56 );
    strncpy( s_lastMapResourceKey, "overworldRoomCols.dat", sizeof( s_lastMapResourceKey ) - 1 );
    s_lastMapResourceKey[sizeof( s_lastMapResourceKey ) - 1] = 0;

    Util::LoadResource( "text.tab", &textTable );

    // Phase 11.1b: title screen first (on by default, so new players always
    // see it), unless the player has opted out (Options screen's
    // TITLE SCREEN row, SkipTitleScreen). The actual auto-continue/file-menu
    // decision (below) then happens once Start is pressed there --
    // UpdateTitle() redoes this exact same lastSlot/holdingSelect check.
    if ( Options::Get().SkipTitleScreen == 0 )
    {
        GotoTitle();
        return;
    }

    // Boot straight back into whichever save was last played — no file-
    // select screen on every launch. Hold Select while the game boots to
    // get the classic slot-select screen instead (new game / switch slot /
    // register / eliminate).
    int lastSlot = SaveFolder::ReadLastSlot();
    Profile lastProfile;
    bool holdingSelect = Input::IsButtonDown( InputButtons::Select );

    if ( !holdingSelect && lastSlot >= 0 && SaveFolder::ReadProfile( lastSlot, lastProfile ) )
    {
        Start( lastSlot, lastProfile );
    }
    else
    {
        GotoFileMenu();

        // Font/PlayerAndItems are what the file-select menu actually draws on
        // its very first frame — prewarm them *after* GotoFileMenu() (whose
        // GameMenu constructor sets the menu's real palette rows) rather than
        // before, so the expansion matches the colours that frame will really
        // use. Prewarming with the wrong (pre-menu) palette values earlier just
        // got redone anyway once the real palette differed — moving it here
        // fixed the one remaining startup spike. See phase 5.1.
        Graphics::PrewarmSheet( Sheet_Font );
        Graphics::PrewarmSheet( Sheet_PlayerAndItems );
    }
}

void WorldImpl::Start( int slot, const Profile& profile )
{
    this->profile = profile;
    // A freshly registered profile has Hearts == 0 — give it a full default
    // heart bar. A profile that has actually been played carries a valid
    // (autosaved) Hearts value, so respect it: booting/continuing into a save
    // after a power loss resumes with the health you had, not a flat reset.
    if ( this->profile.Hearts == 0 )
        this->profile.Hearts = Profile::GetMaxHeartsValue( DefaultHearts );
    profileSlot = slot;
    checkpointHearts = this->profile.Hearts;
    autoSaveLastFrame = -1;

    // phase 8.4e: resume at the last-saved overworld screen/position instead
    // of the fixed quest-start tile, if this profile ever recorded one (see
    // Profile.h's SavedOWRoomIdPlus1 comment -- 0 means never, e.g. an old
    // save from before this field existed, or a freshly registered one
    // that's never left the intro cave). savedOWRoomId (not curRoomId) is
    // the right member to preset here: it's what UpdateLoadLevel's level-0
    // completion step actually reads into curRoomId once the stepped load
    // finishes -- the same plumbing GotoLoadLevel(0) uses to return from a
    // dungeon. restartOW=false then makes GotoUnfurl() load that room and
    // checkpointX/Y (just set above/here) instead of infoBlock.StartRoomId.
    bool hasSavedLocation = this->profile.SavedOWRoomIdPlus1 != 0;
    if ( hasSavedLocation )
    {
        savedOWRoomId = this->profile.SavedOWRoomIdPlus1 - 1;
        checkpointX = this->profile.SavedX;
        checkpointY = this->profile.SavedY;
    }

    GotoLoadLevel( 0, !hasSavedLocation );
}

void WorldImpl::Update()
{
    GameMode mode = GetMode();

    if ( lastMode != mode )
    {
        if ( IsPlaying( lastMode ) && mode != Mode_WinGame )
        {
            CleanUpRoomItems();
            Graphics::DisableGrayscale();
            if ( mode != Mode_Unfurl )
            {
                OnLeavePlay();
                if ( player != nullptr )
                    player->Stop();
            }
        }

        lastMode = mode;

        if ( gameMenu != nullptr )
            delete gameMenu;
        gameMenu = nextGameMenu;
        nextGameMenu = nullptr;
    }

    UpdateFunc update = sModeFuncs[curMode];
    (this->*update)();

    // phase 13.3 stats: persistent play-time counter, whole seconds. Gated
    // on IsPlaying() (not e.g. Mode_Enter/Scroll) -- same "actually playing,
    // not just mid-transition" bar most games use for a playtime clock, and
    // deliberately excludes the file-select/pause/dialog/death screens.
    if ( IsPlaying( mode ) )
    {
        playTimeFrameAccum++;
        if ( playTimeFrameAccum >= 60 )
        {
            playTimeFrameAccum -= 60;
            if ( profile.PlayTimeSeconds < 0xFFFFFFFF )
                profile.PlayTimeSeconds++;
        }
    }
}

void WorldImpl::Draw()
{
    // On platforms where the HUD lives elsewhere (3DS bottom screen), hide
    // the persistent compact status bar during normal play and scale the
    // play area up to fill the freed space; on platforms where the top
    // screen is the only HUD (desktop), ShouldShowTopHud()/SetFullPlayView()
    // are no-ops and this behaves exactly as before. `submenuOffsetY == 0`
    // means "not paused" (it only becomes nonzero while the pause/inventory
    // submenu is sliding in or out — see the increment/decrement near
    // Submenu::Height below); while paused, use the normal view so the
    // equipment overlay stays correctly positioned.
    //
    // The death "GAME OVER" text (DrawDie) and the SAVE/CONTINUE/RETRY menu
    // (DrawContinueQuestion) draw at full-height NES coordinates (y up to
    // ~0x90/0x98, same convention as the paused inventory screen), not
    // confined to the 64-240 play area SetFullPlayView() crops to — under
    // that view they land oddly high/cropped (phase 6.4: the game-over screen
    // sat too high). Route them through
    // SetNormalView() like the pause menu already does.
    // Mode_Title is deliberately NOT included here: on 3DS, SetNormalView()
    // carries a viewOffsetX calibrated for the death/continue screen's
    // content (phase 6.4, later corrected -- see SetNormalView()'s
    // own comment, it was actually cropped/bleeding this whole time despite
    // being called "verified") -- not a generic 1:1 view despite the name.
    // Routing the title screen through it shifted/clipped its content
    // (found via a 3DS-only repro: desktop rendered fine, Azahar showed a
    // missing text line, a missing Triforce, and a stray clip-edge line).
    // DrawTitle() sets its own clean SetViewParams(1,0,0) instead of
    // relying on either branch below.
    // On 3DS the death/continue screen is a modern bottom-screen touch panel
    // (Graphics_3ds.cpp), so the top screen just runs the normal fullscreen
    // play view for both the death animation (it should fill the screen like
    // gameplay, not sit half-cropped in SetNormalView's shifted-down frame)
    // and the centred GAME OVER text drawn over it. SetNormalView's offsets are
    // calibrated for the classic desktop CONTINUE/SAVE/RETRY list, which 3DS no
    // longer shows: the death flash turned half black and GAME OVER was not
    // centred.
#ifdef __3DS__
    bool useNormalView = submenuOffsetY != 0;
#else
    bool useNormalView = submenuOffsetY != 0 || curMode == Mode_Death || curMode == Mode_ContinueQuestion;
#endif
    if ( !useNormalView )
    {
        Graphics::SetFullPlayView();
        if ( statusBarVisible && Graphics::ShouldShowTopHud() )
            statusBar.Draw( submenuOffsetY );
    }
    else
    {
        Graphics::SetNormalView();
        if ( statusBarVisible )
            statusBar.Draw( submenuOffsetY );
    }

    DrawFunc draw = sDrawFuncs[curMode];
    (this->*draw)();
}

void WorldImpl::DrawRoom()
{
    DrawMap( curRoomId, curTileMapIndex, 0, 0 );
}

void World::PauseFillHearts()
{
    sWorld->pause = 2;
}

void World::LeaveRoom( Direction dir, int roomId )
{
    sWorld->GotoLeave( dir, roomId );
}

void World::LeaveCellar()
{
    sWorld->GotoLeaveCellar();
}

void World::LeaveCellarByShortcut( int targetRoomId )
{
    sWorld->curRoomId = targetRoomId;
    sWorld->TakeShortcut();
    LeaveCellar();
}

void World::Die()
{
    sWorld->GotoDie();
}

void World::UnfurlLevel()
{
    sWorld->GotoUnfurl();
}

void World::ChooseFile( const std::shared_ptr<ProfileSummarySnapshot>& summaries )
{
    sWorld->GotoFileMenu( summaries );
}

void World::RegisterFile( const std::shared_ptr<ProfileSummarySnapshot>& summaries )
{
    sWorld->GotoRegisterMenu( summaries );
}

void World::EliminateFile( const std::shared_ptr<ProfileSummarySnapshot>& summaries )
{
    sWorld->GotoEliminateMenu( summaries );
}

bool World::IsPlaying()
{
    return IsPlaying( sWorld->curMode );
}

bool World::IsPlaying( GameMode mode )
{
    return mode == Mode_Play
        || mode == Mode_PlayCave
        || mode == Mode_PlayCellar
        || mode == Mode_PlayShortcuts;
}

bool World::IsPlayingCave()
{
    return GetMode() == Mode_PlayCave;
}

// 3DS-only pause hook (phase: no classic Start/Select pause menu, since
// Select/ZL/ZR are the bottom-screen map zoom, so opening the Options screen
// pauses instead). Reuses the same pause==1 state (and Sound::Pause/Unpause)
// the classic Select-pause toggle already uses in UpdatePlay(), so game
// logic stops the same proven way. Guarded against pause==2 (the separate
// heart-fill sequence, PauseFillHearts) so this can't clobber that.
bool World::IsPaused()
{
    return sWorld->pause == 1;
}

void World::SetPaused( bool paused )
{
    if ( paused )
    {
        if ( sWorld->pause == 0 )
        {
            sWorld->pause = 1;
            Sound::Pause();
        }
    }
    else if ( sWorld->pause == 1 )
    {
        sWorld->pause = 0;
        Sound::Unpause();
    }
}

GameMode World::GetMode()
{
    if ( sWorld->curMode == Mode_InitPlayCave )
        return Mode_PlayCave;
    if ( sWorld->curMode == Mode_InitPlayCellar )
        return Mode_PlayCellar;
    return sWorld->curMode;
}

int World::GetMarginRight()
{
    return sWorld->marginRight;
}

int World::GetMarginLeft()
{
    return sWorld->marginLeft;
}

int World::GetMarginBottom()
{
    return sWorld->marginBottom;
}

int World::GetMarginTop()
{
    return sWorld->marginTop;
}

Player* World::GetPlayer()
{
    return sWorld->player;
}

Point World::GetObservedPlayerPos()
{
    return sWorld->fakePlayerPos;
}

Ladder* World::GetLadder()
{
    return sWorld->GetLadderObj();
}

void World::SetLadder( Ladder* ladder )
{
    sWorld->SetLadderObj( ladder );
}

void World::UseRecorder()
{
    sWorld->UseRecorder();
}

void WorldImpl::UseRecorder()
{
    Sound::PushSong( Song_recorder );
    objectTimers[FluteMusicSlot] = 0x98;

    if ( IsOverworld() )
    {
        if ( IsPlaying() && state.play.roomType == PlayState::Regular )
        {
            static const uint8_t roomIds[] = 
            { 0x42, 0x06, 0x29, 0x2B, 0x30, 0x3A, 0x3C, 0x58, 0x60, 0x6E, 0x72 };

            bool makeWhirlwind = true;

            for ( int i = 0; i < _countof( roomIds ); i++ )
            {
                if ( roomIds[i] == curRoomId )
                {
                    if (   (i == 0 && profile.Quest == 0) 
                        || (i != 0 && profile.Quest != 0) )
                        makeWhirlwind = false;
                    break;
                }
            }

            if ( makeWhirlwind )
                SummonWhirlwind();
            else
                MakeFluteSecret();
        }
    }
    else
    {
        recorderUsed = 1;
    }
}

void WorldImpl::SummonWhirlwind()
{
    if ( !summonedWhirlwind
        && whirlwindTeleporting == 0 
        && IsOverworld()
        && IsPlaying()
        && state.play.roomType == PlayState::Regular 
        && GetItem( ItemSlot_TriforcePieces ) != 0 )
    {
        int slot = FindEmptyMonsterSlot();
        if ( slot >= 0 )
        {
            Whirlwind* whirlwind = new Whirlwind( 0, player->GetY() );
            SetObject( slot, whirlwind );

            summonedWhirlwind = true;
            teleportingRoomIndex = GetNextTeleportingRoomIndex();
            whirlwind->SetTeleportPrevRoomId( teleportRoomIds[teleportingRoomIndex] );
        }
    }
}

void WorldImpl::MakeFluteSecret()
{
    // TODO:
    // The original game makes a FluteSecret object (type $5E) and puts it in one of the first 9 
    // object slots that it finds going from higher to lower slots. The FluteSecret object manages 
    // the animation. See $EFA4, and the FluteSecret's update routine at $FEF4.
    // But, for now we'll keep doing it as we have been.

    if ( !state.play.uncoveredRecorderSecret && FindSparseFlag( Sparse_Recorder, curRoomId ) )
    {
        state.play.uncoveredRecorderSecret = true;
        state.play.animatingRoomColors = true;
        state.play.timer = 88;
    }
}

int World::GetRecorderUsed()
{
    return sWorld->recorderUsed;
}

void World::SetRecorderUsed( int value )
{
    sWorld->recorderUsed = value;
}

int World::GetWhirlwindTeleporting()
{
    return sWorld->whirlwindTeleporting;
}

void World::SetWhirlwindTeleporting( int value )
{
    sWorld->whirlwindTeleporting = value;
}

bool World::IsSwordBlocked()
{
    return sWorld->swordBlocked;
}

void World::SetSwordBlocked( bool value )
{
    sWorld->swordBlocked = value;
}

int WorldImpl::GetMapTile( int row, int col )
{
    return tileMaps[curTileMapIndex].tileRefs[row][col];
}

void World::SetTile( int x, int y, int tileType )
{
    sWorld->SetTile( x, y, tileType );
}

void WorldImpl::SetTile( int x, int y, int tileType )
{
    int col = x / TileWidth;
    int row = (y - TileMapBaseY) / TileHeight;

    if ( col < 0 || col >= Columns || row < 0 || row >= Rows )
        return;

    tileMaps[curTileMapIndex].tileRefs[row][col] = tileType;
}

int World::GetInnerPalette()
{
    return sWorld->GetInnerPalette();
}

int WorldImpl::GetInnerPalette()
{
    return roomAttrs[curRoomId].GetInnerPalette();
}

Point World::GetRandomWaterTile()
{
    return sWorld->GetRandomWaterTile();
}

Point WorldImpl::GetRandomWaterTile()
{
    uint8_t waterList[Rows * Columns];
    int waterCount = 0;

    for ( int r = 0; r < Rows; r++ )
    {
        for ( int c = 0; c < Columns; c++ )
        {
            int tileRef = tileMaps[curTileMapIndex].tileRefs[r][c];
            uint8_t attr = tileAttrs[tileRef];
            int action = TileAttr::GetAction( attr );

            if ( action == TileAction_Ladder )
            {
                waterList[waterCount] = (r << 4) | c;
                waterCount++;
            }
        }
    }

    assert( waterCount > 0 );

    int r = Util::GetRandom( waterCount );
    uint8_t pos = waterList[r];
    Point p = { pos & 0xF, (pos >> 4) + BaseRows };
    return p;
}

Object* World::GetObject( int slot )
{
    if ( slot == PlayerSlot )
        return sWorld->player;
    return sWorld->objects[slot];
}

void World::SetObject( int slot, Object* obj )
{
    sWorld->SetOnlyObject( slot, obj );
}

int World::FindEmptyMonsterSlot()
{
    return sWorld->FindEmptyMonsterSlot();
}

int World::FindEmptyFireSlot()
{
    for ( int i = FirstFireSlot; i < LastFireSlot; i++ )
    {
        if ( sWorld->objects[i] == nullptr )
            return i;
    }
    return -1;
}

int World::GetCurrentObjectSlot()
{
    return sWorld->curObjSlot;
}

void World::SetCurrentObjectSlot( int slot )
{
    sWorld->curObjSlot = slot;
}

int& World::GetObjectTimer( int slot )
{
    return sWorld->objectTimers[slot];
}

void World::SetObjectTimer( int slot, int value )
{
    sWorld->objectTimers[slot] = value;
}

int  World::GetStunTimer( int slot )
{
    return sWorld->stunTimers[slot];
}

void World::SetStunTimer( int slot, int value )
{
    sWorld->stunTimers[slot] = value;
}

void World::PushTile( int row, int col )
{
    sWorld->InteractTile( row, col, WorldImpl::TInteract_Push );
}

void World::TouchTile( int row, int col )
{
    sWorld->InteractTile( row, col, WorldImpl::TInteract_Touch );
}

void World::CoverTile( int row, int col )
{
    sWorld->InteractTile( row, col, WorldImpl::TInteract_Cover );
}

void WorldImpl::InteractTile( int row, int col, TileInteraction interaction )
{
    if ( row < 0 || col < 0 || row >= Rows * 2 || col >= Columns * 2 )
        return;

    int coarseRow = row / 2;
    int coarseCol = col / 2;
    uint8_t t = tileMaps[curTileMapIndex].tileRefs[coarseRow][coarseCol];
    uint8_t attr = tileAttrs[t];
    int action = TileAttr::GetAction( attr );

    TileActionFunc actionFunc = sActionFuncs[action];
    (this->*actionFunc)( coarseRow, coarseCol, interaction );
}

bool WorldImpl::CollidesTile( int row, int col )
{
    int tileRef = tileMaps[curTileMapIndex].tileRefs[row][col];
    uint8_t attr = tileAttrs[tileRef];
    return TileAttr::IsTileBlocked( attr );
}

// In dungeons everything used to sit in the same 3D layer; blocks you can't
// walk through belong one layer up. Solid dungeon
// obstacles -- wall corners, block/statue tiles -- currently draw at the
// same Ground depth as the plain floor (see DrawMap()'s single per-tile pass
// for !IsOverworld()), so they recede into the background exactly like the
// bushes-on-Ground bug IsOverworldTreeTile() above fixes for the overworld.
// Reuses the same tileAttrs the real collision check (CollidesTile() above)
// reads, so "gets the Mid layer" and "actually blocks you" can never drift
// apart. Deliberately excludes tiles with a non-None action (Ladder/water,
// Stairs, Push, Bomb, Headstone, ...): those are floor-level or special-
// interaction tiles, not the plain solid "blocks" meant here --
// verified against the actual 12-entry underworldTileAttrs.dat: only 0x00
// (wall corner), 0x02/0x03 (block/statue) and 0x08 carry both a blocked
// quadrant and action None; 0x01/0x05/0x07 are plain floor (unblocked),
// 0x04 is Stairs and 0x06 is Ladder-gated water, none of which should pop
// forward.
bool WorldImpl::IsUnderworldWallTile( int tileRef )
{
    uint8_t attr = tileAttrs[tileRef];
    return TileAttr::IsTileBlocked( attr ) && TileAttr::GetAction( attr ) == TileAction_None;
}

TileCollision World::CollidesWithTileStill( int x, int y )
{
    return sWorld->CollidesWithTile( x, y, Dir_None, 0 );
}

TileCollision World::CollidesWithTileMoving( int x, int y, Direction dir, bool isPlayer )
{
    int offset;

    if ( dir == Dir_Right )
        offset = 0x10;
    else if ( dir == Dir_Down )
        offset = 8;
    else if ( isPlayer )
        offset = -8;
    else
        offset = -0x10;

    TileCollision collision = sWorld->CollidesWithTile( x, y, dir, offset );

    // Special tile collision in top right corner of OW. Go thru the wall.
    if (   isPlayer 
        && sWorld->infoBlock.LevelNumber == 0
        && sWorld->curRoomId == 0x1F
        && Util::IsVertical( dir )
        && x == 0x80
        && y < 0x56 )
        collision.Collides = false;

    return collision;
}

TileCollision WorldImpl::CollidesWithTile( 
    int x, int y, Direction dir, int offset )
{
    y += 0xB;

    if ( Util::IsVertical( dir ) )
    {
        if ( dir == Dir_Up || y < 0xDD )
            y += offset;
    }
    else
    {
        if ( (dir == Dir_Left && x >= 0x10) || (dir == Dir_Right && x < 0xF0) )
            x += offset;
    }

    bool firstTile = true;
    uint8_t ref;
    bool collides = false;
    int fineRow = (y - TileMapBaseY) / 8;
    int coarseRow = fineRow / 2;
    int quadRow = fineRow % 2;

    int fineCol1 = x / 8;
    int fineCol2;
    int hitFineCol = fineCol1;

    if ( Util::IsVertical( dir ) )
        fineCol2 = (x + 8) / 8;
    else
        fineCol2 = x / 8;

    if ( !IsOverworld() && CollidesWithUWBorder( fineRow, fineCol1, fineCol2 ) )
    {
        // If you hit the border, then I don't think it's important to know the exact fineCol.
        TileCollision collision = { true, Tile_Wall, fineCol1, fineRow };
        return collision;
    }

    for ( int c = fineCol1; c <= fineCol2; c++ )
    {
        int coarseCol = c / 2;
        int quadCol = c % 2;
        int tileRef = GetMapTile( coarseRow, coarseCol );
        uint8_t attr = tileAttrs[tileRef];

        if ( firstTile )
        {
            ref = tileRef;
            firstTile = false;
        }

        if ( TileAttr::IsQuadrantBlocked( attr, quadRow, quadCol ) )
        {
            int action = TileAttr::GetAction( attr );
            if ( action == TileAction_Ladder && state.play.allowWalkOnWater )
                continue;
            if ( action != 0 )
            {
                TileCollision collision = { true, tileRef, c, fineRow };
                return collision;
            }
            collides = true;
            ref = tileRef;
            hitFineCol = c;
        }
    }

    TileCollision collision = { collides, ref, hitFineCol, fineRow };
    return collision;
}

bool WorldImpl::CollidesWithUWBorder( int fineRow, int fineCol1, int fineCol2 )
{
    if ( (fineRow >= startRow * 2) && (fineRow  < (startRow + rowCount) * 2) 
        && (fineCol1 >= startCol * 2) && (fineCol2 < (startCol + colCount) * 2) )
        return false;

    // Coarse checks for collision are done here. All of the border's treated the same.
    // Finer checks for collision are done in Player.

    return true;
}

void World::OnPushedBlock()
{
    sWorld->OnPushedBlock();
}

void WorldImpl::OnPushedBlock()
{
    Sound::PlayEffect( SEffect_secret );

    if ( IsOverworld() )
    {
        if ( !GotShortcut( curRoomId ) )
        {
            if ( FindSparseFlag( Sparse_Shortcut, curRoomId ) )
            {
                TakeShortcut();
                ShowShortcutStairs( curRoomId, curTileMapIndex );
            }
        }
    }
    else
    {
        UWRoomAttrs& uwRoomAttrs = (UWRoomAttrs&) roomAttrs[curRoomId];
        int secret = uwRoomAttrs.GetSecret();

        if ( secret == Secret_BlockDoor )
        {
            triggerShutters = true;
        }
        else if ( secret == Secret_BlockStairs )
        {
            AddUWRoomStairs();
        }
    }
}

void World::OnActivatedArmos( int x, int y )
{
    sWorld->OnActivatedArmos( x, y );
}

// phase 10.4: the 6 Sparse_ArmosStairs rooms, in the same fixed order as
// LiveMapGeometry.cpp's kSecretRooms[0..5] (zelda3ds/source), so a bit index
// here means the same room on the map-renderer side. Duplicated rather than
// shared because this file (Loz/Game/Loz) is also compiled by the desktop
// CMake build, which has no include path to zelda3ds/source. If you ever reorder/add an armos-stairs room, update both.
static const uint8_t sArmosStairsRooms[] = { 0x0B, 0x1C, 0x22, 0x34, 0x3D, 0x4E };

static int GetArmosStairsBit( int room )
{
    for ( size_t i = 0; i < sizeof( sArmosStairsRooms ); i++ )
    {
        if ( sArmosStairsRooms[i] == room )
            return (int) i;
    }
    return -1;
}

void WorldImpl::OnActivatedArmos( int x, int y )
{
    const SparsePos2*   pos = FindSparsePos2( Sparse_ArmosStairs, curRoomId );

    if ( pos != nullptr && x == pos->x && y == pos->y )
    {
        SetTile( x, y, Tile_Stairs );
        Sound::PlayEffect( SEffect_secret );

        int bit = GetArmosStairsBit( curRoomId );
        if ( bit >= 0 )
            profile.ArmosStairsSolved |= (uint8_t) (1 << bit);
    }
    else
    {
        SetTile( x, y, Tile_Ground );
    }

    if ( !GotItem() )
    {
        const SparseRoomItem* roomItem = FindSparseItem( Sparse_ArmosItem, curRoomId );

        if ( roomItem != nullptr && x == roomItem->x && y == roomItem->y )
        {
            ItemObj* itemObj = new ItemObj( roomItem->itemId, roomItem->x, roomItem->y, true );
            objects[ItemSlot] = itemObj;
        }
    }
}

void World::OnTouchedPowerTriforce()
{
    sWorld->OnTouchedPowerTriforce();
}

void WorldImpl::OnTouchedPowerTriforce()
{
    powerTriforceFanfare = true;
    player->SetState( Player::Paused );
    player->SetObjectTimer( 0xC0 );

    const static uint8_t palette[] = { 0, 0x0F, 0x10, 0x30 };
    Graphics::SetPaletteIndexed( LevelFgPalette, palette );
    Graphics::UpdatePalettes();
}

void WorldImpl::CheckPowerTriforceFanfare()
{
    if ( !powerTriforceFanfare )
        return;

    if ( player->GetObjectTimer() == 0 )
    {
        powerTriforceFanfare = false;
        player->SetState( Player::Idle );
        World::AddItem( Item_PowerTriforce );
        SetPilePalette();
        Graphics::UpdatePalettes();
        Sound::PlaySong( Song_level9, Sound::MainSongStream, true );
    }
    else
    {
        uint timer = player->GetObjectTimer();
        if ( timer & 4 )
            SetFlashPalette();
        else
            SetLevelPalette();
    }
}

void WorldImpl::AdjustInventory()
{
    if ( profile.SelectedItem == 0 )
        profile.SelectedItem = ItemSlot_Boomerang;

    for ( int i = 0; i < 10; i++ )
    {
        if (   profile.SelectedItem == ItemSlot_Arrow
            || profile.SelectedItem == ItemSlot_Bow )
        {
            if (   profile.Items[ItemSlot_Arrow] != 0
                && profile.Items[ItemSlot_Bow] != 0 )
                break;
        }
        else
        {
            if ( profile.Items[profile.SelectedItem] != 0 )
                break;
        }

        switch ( profile.SelectedItem )
        {
        case 0x07: profile.SelectedItem = 0x0F; break;
        case 0x0F: profile.SelectedItem = 0x06; break;
        case 0x01: profile.SelectedItem = 0x1B; break;
        case 0x1B: profile.SelectedItem = 0x08; break;
        default:   profile.SelectedItem--; break;
        }
    }
}

void WorldImpl::WarnLowHPIfNeeded()
{
    if ( profile.Hearts >= 0x100 )
        return;

    // Phase 11.3: some players find the classic repeating low-health alarm
    // obnoxious enough to want it gone entirely (quieter/short/off were the
    // candidates). Off (LowHealthBeep!=0) is a plain skip. True
    // per-call volume/duration control would need a gain parameter plumbed
    // through both Sound.cpp and Sound_3ds.cpp -- a bigger cross-platform
    // change left for a later pass.
    //
    // The actual root cause of "obnoxious" turned out to be simpler than a
    // volume problem, though: this function is called every single frame
    // while below 1 heart (see the UpdatePlay() call site), and used to
    // call Sound::PlayEffect() every one of those 60 frames/sec -- the beep
    // was retriggering over its own tail continuously, which reads as a
    // harsh drone rather than a warning beep, regardless of its volume.
    // Throttling the repeat rate to twice a second (matching a "beep...
    // beep... beep" cadence rather than a siren) addresses the actual
    // complaint without needing any new gain plumbing or Options UI change
    // -- this applies to the existing "normal" (LowHealthBeep==0) state.
    if ( Options::Get().LowHealthBeep != 0 )
    {
        lowHpBeepTimer = 0;
        return;
    }

    if ( lowHpBeepTimer > 0 )
    {
        lowHpBeepTimer--;
        return;
    }

    lowHpBeepTimer = 30;   // ~0.5s at 60fps -- a beep cadence, not a drone
    Sound::PlayEffect( SEffect_low_hp );
}

void WorldImpl::PlayAmbientSounds()
{
    bool playedSound = false;

    if ( IsOverworld() )
    {
        if ( GetMode() == Mode_Play )
        {
            OWRoomAttrs& owRoomAttrs = (OWRoomAttrs&) roomAttrs[curRoomId];
            if ( owRoomAttrs.HasAmbientSound() )
            {
                Sound::PlayEffect( SEffect_sea, true, Sound::AmbientInstance );
                playedSound = true;
            }
        }
    }
    else
    {
        if ( curUWBlockFlags[infoBlock.BossRoomId].GetObjCount() == 0 )
        {
            UWRoomAttrs& uwRoomAttrs = (UWRoomAttrs&) roomAttrs[curRoomId];
            int ambientSound = uwRoomAttrs.GetAmbientSound();
            if ( ambientSound != 0 )
            {
                int id = SEffect_boss_roar1 + ambientSound - 1;
                Sound::PlayEffect( id, true, Sound::AmbientInstance );
                playedSound = true;
            }
        }
    }

    if ( !playedSound )
        Sound::StopEffects();
}

void WorldImpl::ShowShortcutStairs( int roomId, int tileMapIndex )
{
    OWRoomAttrs& owRoomAttrs = (OWRoomAttrs&) roomAttrs[roomId];
    int index = owRoomAttrs.GetShortcutStairsIndex();
    int pos = infoBlock.ShortcutPosition[index];
    int row;
    int col;
    GetRoomCoord( pos, row, col );
    tileMaps[tileMapIndex].tileRefs[row][col] = Tile_Stairs;
}

// Phase 12.2b follow-up (Link and enemies seemed to float over the ground;
// trees and bushes belong in the same middle layer as them): these overworld
// tileRefs draw into the
// Mid depth layer instead of Ground, so they sit at the same depth as Link/
// enemies rather than receding into the background with the bare terrain.
// Identified from a real gameplay screenshot (Azahar, an explored forest
// room), not guessed off the raw tile sheet: 0x1A/0x1B tile pixel-for-pixel
// into the exact forest-wall texture seen on screen; 0x1C is the matching
// diagonal forest-edge transition; 0x19/0x28 are the standalone single-
// tree/bush graphic (0x28 doubles as phase 10.1's confirmed "Burn"
// secret-tree action tile -- same graphic, already known non-guessed).
// The table itself lives in OverworldMidLayerTiles.gen.h, generated by
// zelda3ds/tools/gen-terrain-tables.py from the same TERRAIN_FAMILIES that
// decide the bottom-screen map colours. That shared source is deliberate:
// this used to be a hand-written switch, and it kept disagreeing with the map
// about which tiles are foliage -- a tile would be fixed in one place and stay
// wrong in the other. Edit the tables in that script, not this function.
//
// Only meaningful on the overworld: the exact same tileRef byte is a
// completely different graphic in dungeons (a different Sheet_Background
// asset is loaded there), so this would misclassify random dungeon tiles if
// it ran there too.
//
// 0x32-0x35: these are the forest wall's *diagonal corner*
// tiles -- the pieces that round off a clearing's boundary where the solid
// wall turns. They used to be excluded here on the grounds that they "tile
// into a jagged mountain-ridge silhouette"; that was a misdiagnosis, and it
// is what kept showing up: the corners of the bushes were not in the same
// layer, and after a first wrong fix, only the half tiles were.
// The graphic alone genuinely is ambiguous -- the same tileRef is drawn
// with whichever palette row the room specifies, so it renders as green
// foliage in a forest room and as brown rock in a mountain room, and
// judging it from a raw sheet render (or one screenshot) can land either
// way. Settled from the room data instead, which is unambiguous: across all
// 128 overworld rooms every single occurrence of 0x32/0x33/0x34/0x35 is
// adjacent to forest wall (0x1A/0x1B) and/or open ground, and *never once*
// to a mountain tile (0x02/0x03/0x22/0x24/0x25/0x2D/0x36). They are forest
// edges, so they belong on Mid with the wall they finish -- leaving them on
// Ground is exactly the "corners sit on a different layer than the bushes"
// artefact. Their collision is deliberately left alone: each has one free
// quadrant that measures 81-93% bare-ground pixels, so blocking it to stop
// enemies clipping the corner would just trade a depth bug for an invisible
// wall on open ground.
//
// The corners of the bushes were not in the same layer. The freestanding overworld tree/bush is not one tile but a 2x2
// cluster -- 0x1C 0x1E / 0x1F 0x20 -- and only its top-left quarter (0x1C)
// was listed above, so three quarters of every bush stayed behind on Ground
// while the fourth popped forward. Verified exhaustively against the decoded
// room layouts (all 128 rooms, tools/gen-terrain-tables.py's own decoder):
// 0x1C occurs exactly 25 times and 0x1E/0x1F/0x20 exactly 25 times each,
// always in that 2x2 arrangement, never standalone. 0x1D is the same tree
// widened by one tile for the two "cave mouth under a tree" clusters
// (0x1C 0x1D 0x1E / 0x1F 0x0C 0x20, rooms 30 and 54), so it belongs with
// them; the 0x0C cave mouth itself stays on Ground, it's a hole in the
// terrain, not foliage.
static bool IsOverworldTreeTile( int tileRef )
{
    return tileRef >= 0
        && tileRef < (int) _countof( kOverworldMidLayerTile )
        && kOverworldMidLayerTile[tileRef];
}

// What the Ground layer draws in a cell whose real tile is foreground (a bush,
// the forest wall, a dungeon block) and therefore belongs in Mid. Returns a
// tileRef, or -1 if this cell has no terrain neighbour at all.
//
// Nothing may appear in more than one layer, and floor tiles under a bush
// must not get black edges from being in the wrong one -- which is exactly
// the constraint. A thing must appear in exactly one depth
// layer, and Ground must still be a gapless image.
//
// The two earlier attempts each satisfied one half and broke the other. First,
// Ground simply skipped foreground tiles: gapless failed, Ground is cleared to
// opaque black, so parallax slid a black hole out from under every bush (the
// lines around the bushes). Then Ground drew the foreground tile itself: gapless
// passed, but now the bush existed in both layers at different offsets, and a
// strip of it stuck out beside its own Mid copy -- a horizontally doubled edge
// along every forest wall, which fuses as blur.
//
// Draw the *terrain* there instead. Which terrain is decided by geometry, not
// taste: the stereo offset is horizontal (Video_BlitTopScreen shifts Ground by
// -kGroundParallaxPx for the left eye and +kGroundParallaxPx for the right), so
// what each eye uncovers is this cell's own leftmost / rightmost few pixels,
// sitting directly against the left / right neighbour. Those two neighbours are
// therefore what the fill has to blend into -- hence horizontal first, vertical
// only as a fallback.
//
// Walkable beats blocked: next to both a clearing and a mountain, a bush stands
// on the clearing. Read from the same tileAttrs table CollidesTile() uses, so
// "counts as floor here" and "you can walk on it" can't drift apart.
//
// A bush deep inside the forest wall has no terrain neighbour and returns -1;
// the caller then keeps the original tile. That is correct rather than a
// fallback: its neighbours are bushes whose own Mid copies cover the strip, so
// nothing it draws is ever visible.
int WorldImpl::GroundTileUnderForeground( const TileMap* map, int row, int col, bool overworld )
{
    static const int neighborRow[] = {  0,  0, -1,  1 };
    static const int neighborCol[] = { -1,  1,  0,  0 };

    int blockedTerrain = -1;

    for ( int i = 0; i < 4; i++ )
    {
        int r = row + neighborRow[i];
        int c = col + neighborCol[i];
        if ( r < 0 || r >= Rows || c < 0 || c >= Columns )
            continue;

        int ref = map->tileRefs[r][c];
        bool foreground = overworld ? IsOverworldTreeTile( ref )
                                    : IsUnderworldWallTile( ref );
        if ( foreground )
            continue;

        if ( ref >= 0 && ref < TileTypes && !TileAttr::IsTileBlocked( tileAttrs[ref] ) )
            return ref;

        if ( blockedTerrain < 0 )
            blockedTerrain = ref;
    }

    return blockedTerrain;
}

void WorldImpl::DrawMap( int roomId, int mapIndex, int offsetX, int offsetY )
{
    Graphics::Begin();

    int outerPalette = roomAttrs[roomId].GetOuterPalette();
    int innerPalette = roomAttrs[roomId].GetInnerPalette();
    TileMap*    map = &tileMaps[mapIndex];

    if ( IsUWCellar( roomId )
        || IsPlayingCave() )
    {
        outerPalette = 3;
        innerPalette = 2;
    }

    int firstRow = 0;
    int lastRow = Rows;
    int tileOffsetY = offsetY;

    int firstCol = 0;
    int lastCol = Columns;
    int tileOffsetX = offsetX;

    if ( offsetY < 0 )
    {
        firstRow = -offsetY / TileHeight;
        tileOffsetY = -(-offsetY % TileHeight);
    }
    else if ( offsetY > 0 )
    {
        lastRow = Rows - offsetY / TileHeight;
    }
    else if ( offsetX < 0 )
    {
        firstCol = -offsetX / TileWidth;
        tileOffsetX = -(-offsetX % TileWidth);
    }
    else if ( offsetX > 0 )
    {
        lastCol = Columns - offsetX / TileWidth;
    }

    int endCol = startCol + colCount;
    int endRow = startRow + rowCount;

    // Phase 12.2b: this is Ground (recedes) -- see DrawRoom()/DrawPlay()'s
    // own comments. Set here too (not just by callers) since DrawMap() is
    // also called directly by DrawScroll()/DrawRoomNoObjects() during room
    // transitions, which used to leave those draws in whatever layer was
    // last active (Mid, 0 offset) -- a real bug: every screen change went
    // briefly 2D. Scrolling between
    // rooms briefly lost all depth because the sliding background never got
    // routed to Ground during the transition. Setting it here fixes every
    // caller at once.
    Graphics::SetTopLayer( TopLayer::Ground );

    if ( IsUWMain( roomId ) )
    {
        Graphics::DrawBitmap(
            wallsBmp,
            0, 0,
            TileMapWidth, TileMapHeight,
            offsetX, TileMapBaseY + offsetY,
            outerPalette, 0 );
    }

    // See IsOverworldTreeTile()'s comment for which tileRefs and why. Two
    // passes (matching DrawObjects()'s established pattern) instead of
    // switching layers per tile, so the layer-switch count per DrawMap()
    // call stays a small constant regardless of how many tree tiles happen
    // to be scattered through the room. Skipped entirely in dungeons.
    bool checkTrees = IsOverworld();
    // Dungeon counterpart: same two-pass idea, other direction
    // -- see IsUnderworldWallTile()'s comment. Only meaningful underground;
    // mutually exclusive with checkTrees since a room is never both.
    bool checkWalls = !checkTrees;
    // The whole second pass exists only to put foreground tiles at a
    // different stereoscopic depth. With no real layer split (desktop, or the
    // 3D slider at 0) SetTopLayer is a no-op, so it would redraw every tree
    // on top of itself in the same buffer -- pure waste, up to ~176 extra
    // quads in a dense forest room, on a build that has to hold 60fps at the
    // old 3DS's 268MHz. Skipped entirely there; the first pass below already
    // draws the complete room.
    const bool layered = Graphics::IsTopLayered();

    auto drawTile = [&]( int tileRef, int x, int y, int r, int c )
    {
        int srcX = (tileRef & 0x0F) * TileWidth;
        int srcY = ((tileRef & 0xF0) >> 4) * TileHeight;
        int palette = ( r < 2 || r >= 9 || c < 2 || c >= 14 ) ? outerPalette : innerPalette;
        Graphics::DrawTile( Sheet_Background, srcX, srcY, TileWidth, TileHeight, x, y, palette, 0 );
    };

    {
        int y = TileMapBaseY + tileOffsetY;
        for ( int r = firstRow; r < lastRow; r++, y += TileHeight )
        {
            if ( r < startRow || r >= endRow )
                continue;

            int x = tileOffsetX;

            for ( int c = firstCol; c < lastCol; c++, x += TileWidth )
            {
                if ( c < startCol || c >= endCol )
                    continue;

                // Ground gets a tile for every cell -- it must stay a gapless
                // image, or parallax slides its opaque-black clear out from
                // under whatever Mid drew there. But a foreground cell gets
                // the *terrain* that belongs there, not its own bush, so the
                // bush exists in Mid alone. See GroundTileUnderForeground().
                //
                // Only when actually layered: with one shared buffer (desktop,
                // or the slider at 0) this pass is the whole room, and
                // substituting here would erase every bush in the game.
                int tileRef = map->tileRefs[r][c];
                if ( layered )
                {
                    bool isForeground = checkTrees ? IsOverworldTreeTile( tileRef )
                                                    : IsUnderworldWallTile( tileRef );
                    if ( isForeground )
                    {
                        int terrain = GroundTileUnderForeground( map, r, c, checkTrees );
                        if ( terrain >= 0 )
                            tileRef = terrain;
                    }
                }
                drawTile( tileRef, x, y, r, c );
            }
        }
    }

    if ( layered && ( checkTrees || checkWalls ) )
    {
        Graphics::SetTopLayer( TopLayer::Mid );

        // Two sub-passes: every foreground cell gets an opaque black backdrop
        // first, then the tiles themselves go on top.
        //
        // The backdrop is not decoration, it is what makes the layer split
        // valid at all. Palette index 0 is transparent in this port, and
        // background tiles lean on it hard (forest wall 0x1B is 27% index-0,
        // the lone boulder 0x13 29%). In one buffer those holes fall through
        // to the black ClearScreen() and read as the dark gaps the artwork
        // intends. In Mid they would fall through to Ground -- which is drawn
        // shifted -- so every bush becomes a window onto the terrain behind it,
        // offset per eye. That is what showed on hardware: the ground layer
        // doubled with a faint layer on top, and water appeared where there
        // seemed to be none -- the water was genuinely in
        // that room (0x65, a river with a bridge, confirmed in the room data),
        // seen through the bushes beside it. Filling black first reproduces
        // exactly what the single-buffer path shows.
        //
        // Split in two loops rather than backdrop-then-tile per cell because
        // citro2d flushes its batch whenever the draw mode changes between
        // solid rects and textured quads -- alternating would cost two GPU
        // draw calls per bush instead of two for the whole room.
        {
            int y = TileMapBaseY + tileOffsetY;
            for ( int r = firstRow; r < lastRow; r++, y += TileHeight )
            {
                if ( r < startRow || r >= endRow )
                    continue;

                int x = tileOffsetX;

                for ( int c = firstCol; c < lastCol; c++, x += TileWidth )
                {
                    if ( c < startCol || c >= endCol )
                        continue;

                    int tileRef = map->tileRefs[r][c];
                    bool isForeground = checkTrees ? IsOverworldTreeTile( tileRef )
                                                    : IsUnderworldWallTile( tileRef );
                    if ( !isForeground )
                        continue;

                    Graphics::DrawTileBackdrop( x, y, TileWidth, TileHeight );
                }
            }
        }

        int y = TileMapBaseY + tileOffsetY;
        for ( int r = firstRow; r < lastRow; r++, y += TileHeight )
        {
            if ( r < startRow || r >= endRow )
                continue;

            int x = tileOffsetX;

            for ( int c = firstCol; c < lastCol; c++, x += TileWidth )
            {
                if ( c < startCol || c >= endCol )
                    continue;

                int tileRef = map->tileRefs[r][c];
                bool isForeground = checkTrees ? IsOverworldTreeTile( tileRef )
                                                : IsUnderworldWallTile( tileRef );
                if ( !isForeground )
                    continue;

                drawTile( tileRef, x, y, r, c );
            }
        }

        Graphics::SetTopLayer( TopLayer::Ground );
    }

    if ( IsUWMain( roomId ) )
        DrawDoors( roomId, false, offsetX, offsetY );

    Graphics::End();
}

void WorldImpl::DrawDoors( int roomId, bool above, int offsetX, int offsetY )
{
    int outerPalette = roomAttrs[roomId].GetOuterPalette();
    int baseY = above ? DoorOverlayBaseY : DoorUnderlayBaseY;
    UWRoomAttrs& uwRoomAttr = (UWRoomAttrs&) roomAttrs[roomId];

    for ( int i = 0; i < 4; i++ )
    {
        int doorDir = Util::GetOrdDirection( i );
        int doorType = uwRoomAttr.GetDoor( i );
        bool doorState = GetDoorState( roomId, doorDir );
        if ( tempShutterDoorDir != 0 && roomId == tempShutterRoomId && doorType == DoorType_Shutter )
        {
            if ( doorDir == tempShutterDoorDir )
                doorState = true;
        }
        if ( doorType == DoorType_Shutter && tempShutters && tempShuttersRoomId == roomId )
            doorState = true;
        int doorFace = GetDoorStateFace( doorType, doorState );
        Graphics::DrawBitmap(
            doorsBmp,
            DoorWidth * doorFace,
            doorSrcYs[i] + baseY,
            DoorWidth,
            DoorHeight,
            doorPos[i].X + offsetX,
            doorPos[i].Y + offsetY,
            outerPalette,
            0 );
    }
}

Profile& World::GetProfile()
{
    return sWorld->profile;
}

int World::GetProfileSlot()
{
    return sWorld->profileSlot;
}

uint8_t World::GetItem( int itemSlot )
{
    return sWorld->profile.Items[itemSlot];
}

void World::SetItem( int itemSlot, int value )
{
    sWorld->profile.Items[itemSlot] = value;
}

void WorldImpl::PostRupeeChange( uint8_t value, int itemSlot )
{
    uint8_t curValue = profile.Items[itemSlot];
    uint8_t newValue = curValue + value;

    if ( newValue < curValue )
        newValue = 255;

    profile.Items[itemSlot] = newValue;
}

void World::PostRupeeWin( uint8_t value )
{
    sWorld->PostRupeeChange( value, ItemSlot_RupeesToAdd );
}

void World::PostRupeeLoss( uint8_t value )
{
    sWorld->PostRupeeChange( value, ItemSlot_RupeesToSubtract );
}

void World::FillHearts( int heartValue )
{
    sWorld->FillHearts( heartValue );
}

void WorldImpl::FillHearts( int heartValue )
{
    unsigned int maxHeartValue = profile.Items[ItemSlot_HeartContainers] << 8;

    profile.Hearts += heartValue;

    if ( profile.Hearts >= maxHeartValue )
        profile.Hearts = maxHeartValue - 1;
}

void World::AddItem( int itemId )
{
    sWorld->AddItem( itemId );
}

void WorldImpl::AddItem( int itemId )
{
    if ( itemId >= Item_None )
        return;

    PlayItemSound( itemId );

    const EquipValue& equip = sItemToEquipment[itemId];
    unsigned int slot = equip.Slot;
    unsigned int value = equip.Value;

    if (   itemId == Item_Heart 
        || itemId == Item_Fairy )
    {
        unsigned int heartValue = value << 8;
        FillHearts( heartValue );
    }
    else
    {
        if ( slot == ItemSlot_Bombs )
        {
            value += profile.Items[ItemSlot_Bombs];
            if ( value > profile.Items[ItemSlot_MaxBombs] )
                value = profile.Items[ItemSlot_MaxBombs];
        }
        else if ( slot == ItemSlot_RupeesToAdd
            ||    slot == ItemSlot_Keys
            ||    slot == ItemSlot_HeartContainers )
        {
            value += profile.Items[slot];
            if ( value > 255 )
                value = 255;
        }
        else if ( itemId == Item_Compass )
        {
            if ( sWorld->infoBlock.LevelNumber < 9 )
            {
                unsigned int bit = 1 << (sWorld->infoBlock.LevelNumber - 1);
                value = profile.Items[ItemSlot_Compass] | bit;
                slot = ItemSlot_Compass;
            }
        }
        else if ( itemId == Item_Map )
        {
            if ( sWorld->infoBlock.LevelNumber < 9 )
            {
                unsigned int bit = 1 << (sWorld->infoBlock.LevelNumber - 1);
                value = profile.Items[ItemSlot_Map] | bit;
                slot = ItemSlot_Map;
            }
        }
        else if ( itemId == Item_TriforcePiece )
        {
            unsigned int bit = 1 << (sWorld->infoBlock.LevelNumber - 1);
            value = profile.Items[ItemSlot_TriforcePieces] | bit;
        }

        profile.Items[slot] = value;
        profile.EverHadItem[slot] = 1;   // phase 13.2, see Profile.h -- permanent, never cleared

        if ( slot == ItemSlot_Ring )
        {
            SetPlayerColor();
            Graphics::UpdatePalettes();
        }
        if ( slot == ItemSlot_HeartContainers )
            FillHearts( 0x100 );
    }
}

void World::DecrementItem( int itemSlot )
{
    sWorld->DecrementItem( itemSlot );
}

void WorldImpl::DecrementItem( int itemSlot )
{
    if ( profile.Items[itemSlot] != 0 )
        profile.Items[itemSlot]--;
}

bool World::HasCurrentMap()
{
    return HasCurrentLevelItem( ItemSlot_Map, ItemSlot_Map9 );
}

bool World::HasCurrentCompass()
{
    return HasCurrentLevelItem( ItemSlot_Compass, ItemSlot_Compass9 );
}

bool World::HasCurrentLevelItem( int itemSlot1To8, int itemSlot9 )
{
    return sWorld->HasCurrentLevelItem( itemSlot1To8, itemSlot9 );
}

bool WorldImpl::HasCurrentLevelItem( int itemSlot1To8, int itemSlot9 )
{
    if ( sWorld->infoBlock.LevelNumber == 0 )
        return false;

    if ( sWorld->infoBlock.LevelNumber < 9 )
    {
        int itemValue = profile.Items[itemSlot1To8];
        int bit = 1 << (sWorld->infoBlock.LevelNumber - 1);
        return (itemValue & bit) != 0;
    }

    return profile.Items[itemSlot9] != 0;
}

int World::GetRoomId()
{
    return sWorld->curRoomId;
}

int World::GetScrollOldRoomId()
{
    return sWorld->state.scroll.oldRoomId;
}

DoorType World::GetDoorType( Direction dir )
{
    return GetDoorType( sWorld->curRoomId, dir );
}

DoorType World::GetDoorType( int roomId, Direction dir )
{
    int dirOrd = Util::GetDirectionOrd( dir );
    UWRoomAttrs& uwRoomAttrs = (UWRoomAttrs&) sWorld->roomAttrs[roomId];
    return (DoorType) uwRoomAttrs.GetDoor( dirOrd );
}

bool WorldImpl::GetDoorState( int door )
{
    // TODO: the original game does it a little different, by looking at $EE.
    return sWorld->GetDoorState( sWorld->curRoomId, door )
        || (GetDoorType( (Direction) door ) == DoorType_Shutter 
            && sWorld->tempShutters && sWorld->curRoomId == sWorld->tempShuttersRoomId)
        || (sWorld->tempShutterDoorDir == door && sWorld->curRoomId == sWorld->tempShutterRoomId);
}

bool World::GetDoorState( int door )
{
    return sWorld->GetDoorState( door );
}

UWRoomFlags& World::GetUWRoomFlags( int curRoomId )
{
    return sWorld->curUWBlockFlags[curRoomId];
}

const World::LevelInfoBlock* World::GetLevelInfo()
{
    return &sWorld->infoBlock;
}

bool World::IsOverworld()
{
    return sWorld->infoBlock.LevelNumber == 0;
}

bool World::DoesRoomSupportLadder()
{
    return sWorld->FindSparseFlag( Sparse_Ladder, GetRoomId() );
}

int World::GetTileAction( int tileRef )
{
    uint8_t attr = sWorld->tileAttrs[tileRef];
    return TileAttr::GetAction( attr );
}

bool World::IsUWMain( int roomId )
{
    return !IsOverworld() && (sWorld->roomAttrs[roomId].GetUniqueRoomId() < 0x3E);
}

bool WorldImpl::IsUWCellar( int roomId )
{
    return !IsOverworld() && (roomAttrs[roomId].GetUniqueRoomId() >= 0x3E);
}

bool World::IsUWCellar()
{
    return sWorld->IsUWCellar( GetRoomId() );
}

bool WorldImpl::GotShortcut( int roomId )
{
    return profile.OverworldFlags[roomId].GetShortcutState();
}

bool WorldImpl::GotSecret()
{
    return profile.OverworldFlags[curRoomId].GetSecretState();
}

Util::Array<uint8_t> World::GetShortcutRooms()
{
    const uint8_t*  valueArray = sWorld->sparseRoomAttrs.GetItem( Sparse_Shortcut );

    Util::Array<uint8_t> array( valueArray[0], &valueArray[2] );
    // elemSize is at 1, but we don't need it.
    return array;
}

void WorldImpl::TakeShortcut()
{
    profile.OverworldFlags[curRoomId].SetShortcutState();
}

void World::TakeSecret()
{
    sWorld->profile.OverworldFlags[sWorld->curRoomId].SetSecretState();
}

bool World::GotItem()
{
    return GotItem( sWorld->curRoomId );
}

bool World::GotItem( int roomId )
{
    if ( IsOverworld() )
    {
        return sWorld->profile.OverworldFlags[roomId].GetItemState();
    }
    else
    {
        return sWorld->curUWBlockFlags[roomId].GetItemState();
    }
}

void World::MarkItem()
{
    if ( IsOverworld() )
    {
        sWorld->profile.OverworldFlags[sWorld->curRoomId].SetItemState();
    }
    else
    {
        sWorld->curUWBlockFlags[sWorld->curRoomId].SetItemState();
    }
}

void World::LiftItem( int itemId, uint16_t timer )
{
    sWorld->LiftItem( itemId, timer );
}

void WorldImpl::LiftItem( int itemId, uint16_t timer )
{
    if ( !IsPlaying() )
        return;

    if ( itemId == Item_None || itemId == 0 )
    {
        state.play.liftItemTimer = 0;
        state.play.liftItemId = 0;
        return;
    }

    state.play.liftItemTimer = timer;
    state.play.liftItemId = itemId;

    player->SetState( Player::Paused );
}

bool World::IsLiftingItem()
{
    if ( !IsPlaying() )
        return false;

    return sWorld->state.play.liftItemId != 0;
}

void World::OpenShutters()
{
    sWorld->tempShutters = true;
    sWorld->tempShuttersRoomId = sWorld->curRoomId;
    Sound::PlayEffect( SEffect_door );
}

void World::IncrementKilledObjectCount( bool allowBombDrop )
{
    sWorld->worldKillCount++;

    // phase 13.3 stats: the persistent kill counter. Covers every monster
    // killed through the normal weapon-damage path (Object::DealDamage() ->
    // KillObjectNormally(), the only caller of this function) -- doesn't
    // catch a few special-cased deaths (Ganon's own transform sequence,
    // instant environmental deaths) that don't route through here, an
    // accepted scope limit rather than an attempt at perfect coverage.
    if ( sWorld->profile.KillCount < 0xFFFF )
        sWorld->profile.KillCount++;

    if ( sWorld->helpDropCounter < 0xA )
    {
        sWorld->helpDropCounter++;
        if ( sWorld->helpDropCounter == 0xA )
        {
            if ( allowBombDrop )
                sWorld->helpDropValue++;
        }
    }
}

// phase 13.2 bestiary: called from Object::KillObjectNormally() (normal boss
// deaths) and directly from Ganon::CheckCollision() (Ganon's death doesn't
// route through the normal DealDamage/hp==0 path). Idempotent -- fine to
// call once per Dodongo in a "3 Dodongos" room or once per detached Gleeok
// head, same flag either way.
void World::MarkBossDefeated( int bossId )
{
    if ( bossId >= 0 && bossId < Boss_MaxBosses )
        sWorld->profile.EverDefeatedBoss[bossId] = 1;
}

// $7B67
void World::ResetKilledObjectCount()
{
    sWorld->worldKillCount = 0;
    sWorld->helpDropCounter = 0;
    sWorld->helpDropValue = 0;
}

void World::IncrementRoomKillCount()
{
    sWorld->roomKillCount++;
}

void World::SetBombItemDrop()
{
    sWorld->helpDropCounter = 0xA;
    sWorld->helpDropValue = 0xA;
}

int World::GetRoomObjCount()
{
    return sWorld->roomObjCount;
}

void World::SetRoomObjCount( int value )
{
    sWorld->roomObjCount = value;
}

int  World::GetRoomObjId()
{
    return sWorld->roomObjId;
}

void World::SetObservedPlayerPos( int x, int y )
{
    sWorld->fakePlayerPos.X = x;
    sWorld->fakePlayerPos.Y = y;
}

void World::SetPersonWallY( int y )
{
    sWorld->state.play.personWallY = y;
}

int  World::GetFadeStep()
{
    return sWorld->darkRoomFadeStep;
}

void World::BeginFadeIn()
{
    if ( sWorld->darkRoomFadeStep > 0 )
        sWorld->brightenRoom = true;
}

void World::FadeIn()
{
    sWorld->FadeIn();
}

void WorldImpl::FadeIn()
{
    if ( darkRoomFadeStep == 0 )
    {
        brightenRoom = false;
        return;
    }

    int& timer = GetObjectTimer( FadeTimerSlot );

    if ( timer == 0 )
    {
        darkRoomFadeStep--;
        timer = 10;

        for ( int i = 0; i < 2; i++ )
        {
            Graphics::SetPaletteIndexed( i + 2, infoBlock.DarkPaletteSeq[darkRoomFadeStep][i] );
        }
        Graphics::UpdatePalettes();
    }
}

bool WorldImpl::UseKey()
{
    if ( GetItem( ItemSlot_MagicKey ) != 0 )
        return true;

    int keyCount = GetItem( ItemSlot_Keys );

    if ( keyCount > 0 )
    {
        keyCount--;
        SetItem( ItemSlot_Keys, keyCount );
        return true;
    }

    return false;
}

bool WorldImpl::GetDoorState( int roomId, int door )
{
    return curUWBlockFlags[roomId].GetDoorState( door );
}

void WorldImpl::SetDoorState( int roomId, int door )
{
    curUWBlockFlags[roomId].SetDoorState( door );
}

bool WorldImpl::IsRoomInHistory()
{
    for ( int i = 0; i < RoomHistoryLength; i++ )
    {
        if ( roomHistory[i] == curRoomId )
            return true;
    }
    return false;
}

void WorldImpl::AddRoomToHistory()
{
    int i = 0;

    for ( ; i < RoomHistoryLength; i++ )
    {
        if ( roomHistory[i] == curRoomId )
            break;
    }

    if ( i == RoomHistoryLength )
    {
        roomHistory[nextRoomHistorySlot] = curRoomId;
        nextRoomHistorySlot++;
        if ( nextRoomHistorySlot >= RoomHistoryLength )
            nextRoomHistorySlot = 0;
    }
}

bool WorldImpl::FindSparseFlag( int attrId, int roomId )
{
    return nullptr != Util::FindSparseAttr( sparseRoomAttrs, attrId, roomId );
}

const WorldImpl::SparsePos* WorldImpl::FindSparsePos( int attrId, int roomId )
{
    return (SparsePos*) Util::FindSparseAttr( sparseRoomAttrs, attrId, roomId );
}

const WorldImpl::SparsePos2* WorldImpl::FindSparsePos2( int attrId, int roomId )
{
    return (SparsePos2*) Util::FindSparseAttr( sparseRoomAttrs, attrId, roomId );
}

const SparseRoomItem* WorldImpl::FindSparseItem( int attrId, int roomId )
{
    return (SparseRoomItem*) Util::FindSparseAttr( sparseRoomAttrs, attrId, roomId );
}

const ObjectAttr* WorldImpl::GetObjectAttrs()
{
    return (ObjectAttr*) extraData.GetItem( Extra_ObjAttrs );
}

ObjectAttr World::GetObjectAttrs( int type )
{
    const ObjectAttr* objAttrs = sWorld->GetObjectAttrs();
    return objAttrs[type];
}

int World::GetObjectMaxHP( int type )
{
    const HPAttr* hpAttrs = (HPAttr*) sWorld->extraData.GetItem( Extra_HitPoints );
    int index = type / 2;
    return hpAttrs[index].GetHP( type );
}

int World::GetPlayerDamage( int type )
{
    const uint8_t* damageAttrs = (uint8_t*) sWorld->extraData.GetItem( Extra_PlayerDamage );
    const uint8_t damageByte = damageAttrs[type];
    return ((damageByte & 0xF) << 8) | (damageByte & 0xF0);
}

void WorldImpl::LoadRoom( int roomId, int tileMapIndex )
{
    if ( IsUWCellar( roomId ) )
    {
        LoadCellarContext();
        prevRoomWasCellar = true;
    }
    else if ( prevRoomWasCellar )
    {
        LoadUnderworldContext();
        prevRoomWasCellar = false;
    }

    curRoomId = roomId;
    curTileMapIndex = tileMapIndex;

    LoadMap( roomId, tileMapIndex );

    if ( IsOverworld() )
    {
        if ( GotShortcut( roomId ) )
        {
            if ( FindSparseFlag( Sparse_Shortcut, roomId ) )
            {
                ShowShortcutStairs( roomId, tileMapIndex );
            }
        }

        if ( !GotItem() )
        {
            const SparseRoomItem* roomItem = FindSparseItem( Sparse_Item, roomId );

            if ( roomItem != nullptr )
            {
                ItemObj* itemObj = new ItemObj( roomItem->itemId, roomItem->x, roomItem->y, true );
                objects[ItemSlot] = itemObj;
            }
        }
    }
    else
    {
        if ( !GotItem() )
        {
            UWRoomAttrs& uwRoomAttrs = (UWRoomAttrs&) roomAttrs[roomId];

            if (   uwRoomAttrs.GetSecret() != Secret_FoesItem
                && uwRoomAttrs.GetSecret() != Secret_LastBoss )
                AddUWRoomItem( roomId );
        }
    }
}

void World::AddUWRoomItem()
{
    sWorld->AddUWRoomItem( sWorld->curRoomId );
}

void WorldImpl::AddUWRoomItem( int roomId )
{
    UWRoomAttrs& uwRoomAttrs = (UWRoomAttrs&) roomAttrs[roomId];
    int itemId = uwRoomAttrs.GetItemId();

    if ( itemId != Item_None )
    {
        int     posIndex = uwRoomAttrs.GetItemPositionIndex();
        Point   pos = GetRoomItemPosition( infoBlock.ShortcutPosition[posIndex] );

        if ( itemId == Item_TriforcePiece )
            pos.X = TriforcePieceX;

        ItemObj* itemObj = new ItemObj( itemId, pos.X, pos.Y, true );
        objects[ItemSlot] = itemObj;

        if (   uwRoomAttrs.GetSecret() == Secret_FoesItem
            || uwRoomAttrs.GetSecret() == Secret_LastBoss )
            Sound::PlayEffect( SEffect_room_item );
    }
}

Direction World::GetDoorwayDir()
{
    return sWorld->doorwayDir;
}

void World::SetDoorwayDir( Direction dir )
{
    sWorld->doorwayDir = dir;
}

int  World::GetFromUnderground()
{
    return sWorld->fromUnderground;
}

void World::SetFromUnderground( int value )
{
    sWorld->fromUnderground = value;
}

int  World::GetActiveShots()
{
    return sWorld->activeShots;
}

void World::SetActiveShots( int count )
{
    sWorld->activeShots = count;
}

Direction World::GetShuttersPassedDirs()
{
    return sWorld->shuttersPassedDirs;
}

void World::SetShuttersPassedDirs( Direction dir )
{
    sWorld->shuttersPassedDirs = dir;
}

int  World::GetTriggeredDoorCmd()
{
    return sWorld->triggeredDoorCmd;
}

void World::SetTriggeredDoorCmd( int value )
{
    sWorld->triggeredDoorCmd = value;
}

Direction World::GetTriggeredDoorDir()
{
    return sWorld->triggeredDoorDir;
}

void World::SetTriggeredDoorDir( Direction dir )
{
    sWorld->triggeredDoorDir = dir;
}

void WorldImpl::LoadCaveRoom( int uniqueRoomId )
{
    curTileMapIndex = 0;

    LoadLayout( uniqueRoomId, 0, true );
}

void WorldImpl::LoadMap( int roomId, int tileMapIndex )
{
    bool        owTileFormat = false;
    int         uniqueRoomId = roomAttrs[roomId].GetUniqueRoomId();

    if ( IsOverworld() )
    {
        owTileFormat = true;
    }
    else if ( uniqueRoomId >= 0x3E )
    {
        owTileFormat = true;
        uniqueRoomId -= 0x3E;
    }

    LoadLayout( uniqueRoomId, tileMapIndex, owTileFormat );
}

void WorldImpl::LoadLayout( int uniqueRoomId, int tileMapIndex, bool owTileFormat )
{
    const int MaxColumnStartOffset = (colCount - 1) * rowCount;

    RoomCols*   columns = &roomCols[uniqueRoomId];
    TileMap*    map = &tileMaps[tileMapIndex];
    int         actionFound = 0;
    int         actionRow = 0;
    int         actionCol = 0;
    int         rowEnd = startRow + rowCount;

    for ( int i = 0; i < colCount; i++ )
    {
        uint8_t columnDesc = columns->ColumnDesc[i];
        uint8_t tableIndex = (columnDesc & 0xF0) >> 4;
        uint8_t columnIndex = (columnDesc & 0x0F);

        const uint8_t* table = colTables.GetItem( tableIndex );
        int k = 0;
        int j = 0;

        for ( j = 0; j <= MaxColumnStartOffset; j++ )
        {
            uint8_t t = table[j];

            if ( (t & 0x80) != 0 )
            {
                if ( k == columnIndex )
                    break;
                k++;
            }
        }

        assert( j <= MaxColumnStartOffset );

        int c = startCol + i;

        for ( int r = startRow; r < rowEnd; j++ )
        {
            uint8_t t = table[j];
            uint8_t tileRef;

            if ( owTileFormat )
                tileRef = t & 0x3F;
            else
                tileRef = t & 0x7;

            uint8_t attr = tileAttrs[tileRef];
            int action = TileAttr::GetAction( attr );

            if ( action != 0 && action <= LoadingTileActions )
            {
                actionFound = action;
                actionRow = r;
                actionCol = c;
            }

            map->tileRefs[r++][c] = tileRef;

            if ( owTileFormat )
            {
                if ( (t & 0x40) != 0 && r < rowEnd )
                    map->tileRefs[r++][c] = tileRef;
            }
            else
            {
                int repeat = (t >> 4) & 0x7;
                for ( int m = 0; m < repeat && r < rowEnd; m++ )
                {
                    map->tileRefs[r++][c] = tileRef;
                }
            }
        }
    }

    if ( IsUWMain( curRoomId ) )
    {
        UWRoomAttrs& uwRoomAttrs = (UWRoomAttrs&) roomAttrs[curRoomId];
        if ( uwRoomAttrs.HasBlock() )
        {
            for ( int c = startCol; c < startCol + colCount; c++ )
            {
                uint8_t tileRef = tileMaps[curTileMapIndex].tileRefs[UWBlockRow][c];
                if ( tileRef == Tile_Block )
                {
                    actionFound = TileAction_Block;
                    actionCol = c;
                    actionRow = UWBlockRow;
                    break;
                }
            }
        }
    }

    // Only 1 secret is allowed in each room. So, handle it after loading all the tiles.
    if ( actionFound != 0 )
    {
        TileActionFunc actionFunc = sActionFuncs[actionFound];
        (this->*actionFunc)( actionRow, actionCol, TInteract_Load );
    }
}

void WorldImpl::GotoPlay( PlayState::RoomType roomType )
{
    switch ( roomType )
    {
    case PlayState::Regular:  curMode = Mode_Play;        break;
    case PlayState::Cave:     curMode = Mode_PlayCave;    break;
    case PlayState::Cellar:   curMode = Mode_PlayCellar;  break;
    default:
        assert( false );
        curMode = Mode_Play;
        break;
    }
    curColorSeqNum = 0;
    tempShutters = false;
    roomObjCount = 0;
    roomObjId = 0;
    roomKillCount = 0;
    roomAllDead = false;
    madeRoomItem = false;
    enablePersonFireballs = false;

    state.play.substate = PlayState::Active;
    state.play.animatingRoomColors = false;
    state.play.allowWalkOnWater = false;
    state.play.uncoveredRecorderSecret = false;
    state.play.roomType = roomType;
    state.play.liftItemTimer = 0;
    state.play.liftItemId = 0;
    state.play.personWallY = 0;

    if ( FindSparseFlag( Sparse_Dock, curRoomId ) )
    {
        int slot = FindEmptyMonsterSlot();
        Dock* dock = new Dock( 0, 0 );
        objects[slot] = dock;
    }

    // Set the level's level foreground palette before making objects,
    // so that if we make a boss, we won't override a palette that it might set.
    SetLevelFgPalette();
    Graphics::UpdatePalettes();
    PlayAmbientSounds();

    Direction dir = player->GetFacing();

    ClearRoomItemData();
    ClearRoomMonsterData();
    InitObjectTimers();
    InitStunTimers();
    InitPlaceholderTypes();
    MakeObjects( dir );
    MakeWhirlwind();
    AddRoomToHistory();
    MoveRoomItem();

    if ( !IsOverworld() )
        curUWBlockFlags[curRoomId].SetVisitState();

    // Autosave on entering a room: this is the natural per-screen checkpoint
    // for both the overworld and dungeons. See AutoSave(). phase 8.3.
    AutoSave();
}

// Persist the profile at the current screen checkpoint (phase 8.3).
// Called on every room entry (GotoPlay). The Hearts value at this moment is
// remembered as the death-penalty checkpoint regardless of throttling; the
// actual SD write is throttled to ~1/sec (SD wear + never a frame hitch, the
// payload is a single small Profile). Persisting here means a power loss keeps
// items/rupees/keys/map flags/triforce up to the last screen you entered.
void WorldImpl::AutoSave()
{
    if ( profileSlot < 0 )
        return;

    // Checkpoint the hearts you entered this screen with — the mild death
    // penalty (phase 8.4) restores to this, not a flat reset. Also remember the
    // (walkable) position you entered at, so a Continue respawns there rather
    // than at the room's cave-exit tile (phase 8.4c, "respawn in the bushes" bug).
    checkpointHearts = profile.Hearts;
    if ( player != nullptr && IsOverworld() )
    {
        checkpointX = player->GetX();
        checkpointY = player->GetY();

        // phase 8.4e: remember where a *saved* (not just death-continue)
        // game should resume -- see Profile.h's SavedOWRoomIdPlus1 comment.
        profile.SavedOWRoomIdPlus1 = (uint8_t) (curRoomId + 1);
        profile.SavedX = (uint8_t) checkpointX;
        profile.SavedY = (uint8_t) checkpointY;
    }

    int now = (int) GetFrameCounter();
    if ( autoSaveLastFrame >= 0 && (now - autoSaveLastFrame) < 60 )
        return;

    SaveProfile();
}

// Unthrottled profile write to the active slot. Used by the throttled
// AutoSave() and by the manual pause-screen save (phase 8.5). Resets the
// autosave throttle window so an autosave doesn't immediately re-write.
void WorldImpl::SaveProfile()
{
    if ( profileSlot < 0 )
        return;
    autoSaveLastFrame = (int) GetFrameCounter();
    SaveFolder::WriteProfile( profileSlot, profile );
}

// Public entry point for platform exit/suspend hooks (phase 9.2's 3DS
// HOME-exit) — forwards to the same unthrottled write SaveProfile() does.
void World::SaveNow()
{
    sWorld->SaveProfile();
}

void WorldImpl::UpdatePlay()
{
    if ( state.play.substate == PlayState::Active )
    {
        if ( brightenRoom )
        {
            FadeIn();
            DecrementObjectTimers();
            DecrementStunTimers();
            return;
        }

        if ( submenu != 0 )
        {
            if ( Input::IsButtonPressing( InputButtons::Select ) )
            {
                submenu = 0;
                submenuOffsetY = 0;
                GotoContinueQuestion();
            }
            else
            {
                UpdateSubmenu();
            }
            return;
        }

        if ( pause == 0 )
        {
            if ( Input::IsButtonPressing( InputButtons::Select )
                    && Graphics::ShouldUseClassicPauseMenu() )
            {
                pause = 1;
                Sound::Pause();
                return;
            }
            else if ( Input::IsButtonPressing( InputButtons::Start )
                    && Graphics::ShouldUseClassicPauseMenu() )
            {
                submenu = 1;
                return;
            }
            // On 3DS (ShouldUseClassicPauseMenu()==false), Start is a no-op
            // during normal play -- phase 8.8 disabled the classic inventory
            // submenu there since the bottom screen already covers it.
            // (Phase 11.2-11.4 briefly repurposed Start/Y+Start/B+Start as a
            // stand-in options toggle before the real Options tab existed --
            // see phase 11.1. That's gone now; settings are tapped
            // directly on the bottom screen's OPTIONS tab, LiveMap_state.cpp.)
            //
            // Real hardware bug (Select zoomed the map but also paused the
            // game): Select was *also* this classic
            // NES pause toggle the whole time -- phase 7.3's 3DS map-zoom
            // Select binding (LiveMap_state.cpp) was added on top without
            // gating this one, on the wrong assumption ("unused by shared
            // game code during normal play", a comment there that was never
            // actually true). Every Select press did both at once: toggled
            // the bottom-screen zoom *and* froze the whole game-logic update
            // loop (pause==1 makes this function return before
            // DecrementObjectTimers()/room updates/etc. even run). Gated the
            // same way Start's classic-submenu path already was.
        }
        else if ( pause == 1 )
        {
            if ( Input::IsButtonPressing( InputButtons::Select ) )
            {
                pause = 0;
                Sound::Unpause();
            }
            return;
        }

        DecrementObjectTimers();
        DecrementStunTimers();

        if ( objectTimers[FluteMusicSlot] != 0 )
            return;

        if ( pause == 2 )
        {
            FillHeartsStep();
            return;
        }

        if ( state.play.animatingRoomColors )
            UpdateRoomColors();

        if ( IsUWMain( curRoomId ) )
            CheckBombables();

        UpdateRupees();
        UpdateLiftItem();

        curObjSlot = PlayerSlot;
        player->DecInvincibleTimer();
        player->Update();

        // The player's update might have changed the world's state.
        if ( !IsPlaying() )
            return;

        UpdateObservedPlayerPos();

        for ( curObjSlot = MaxObjects - 1; curObjSlot >= 0; curObjSlot-- )
        {
            Object* obj = objects[curObjSlot];
            if ( obj != nullptr && !obj->IsDeleted() )
            {
                if ( obj->DecoratedUpdate() )
                    HandleNormalObjectDeath();
            }
            else if ( placeholderTypes[curObjSlot] != 0 )
            {
                PutEdgeObject();
            }
        }

        DeleteDeadObjects();

        CheckSecrets();
        CheckShutters();
        UpdateDoors2();
        UpdateStatues();
        MoveRoomItem();
        CheckPowerTriforceFanfare();
        AdjustInventory();
        WarnLowHPIfNeeded();

// TEST:
#if 1
        if ( Input::IsKeyPressing( ALLEGRO_KEY_H ) )
        {
            OpenShutters();
        }

        if ( Input::IsKeyPressing( ALLEGRO_KEY_EQUALS ) )
        {
            profile.Items[ItemSlot_RupeesToAdd] += 0x20;
        }
        else if ( Input::IsKeyPressing( ALLEGRO_KEY_MINUS ) )
        {
            profile.Items[ItemSlot_RupeesToSubtract] += 0x20;
        }
#endif
    }
}

void WorldImpl::UpdateSubmenu()
{
    if ( submenu == 1 )
    {
        menu.Enable();
        submenu++;
        statusBar.EnableFeatures( StatusBar::Feature_Equipment, false );
    }
    else if ( submenu == 7 )
    {
        submenuOffsetY += 3;
        if ( submenuOffsetY >= Submenu::Height )
        {
            menu.Activate();
            submenu++;
        }
    }
    else if ( submenu == 8 )
    {
        // Manual save (phase 8.5): hold Up + press A on the fully-open
        // pause/inventory screen writes the profile in place — an explicit,
        // reassuring save point that replaces the classic Up+A save gesture.
        // Autosave (phase 8.3) already persists on every screen; this stays in
        // the game (no quit to the file menu) and shows a brief "SAVED".
        if ( Input::IsButtonDown( InputButtons::Up )
            && Input::IsButtonPressing( InputButtons::A ) )
        {
            SaveProfile();
            Sound::PlayEffect( SEffect_secret );
            savedNotifyTimer = 90;
        }
        else if ( Input::IsButtonPressing( InputButtons::Start ) )
        {
            menu.Deactivate();
            submenu++;
        }
    }
    else if ( submenu == 9 )
    {
        submenuOffsetY -= 3;
        if ( submenuOffsetY == 0 )
        {
            menu.Disable();
            submenu = 0;
            statusBar.EnableFeatures( StatusBar::Feature_Equipment, true );
        }
    }
    else
    {
        submenu++;
    }

    if ( submenu != 0 )
        menu.Update();

    if ( savedNotifyTimer > 0 )
        savedNotifyTimer--;
}

void WorldImpl::CheckShutters()
{
    if ( triggerShutters )
    {
        triggerShutters = false;

        int dirs = 0;

        for ( int i = 0; i < 4; i++ )
        {
            Direction dir = Util::GetOrdDirection( i );

            if ( GetDoorType( dir ) == DoorType_Shutter 
                && !GetDoorState( dir ) )
            {
                dirs |= dir;
            }
        }

        if ( dirs != 0 && triggeredDoorCmd == 0 )
        {
            triggeredDoorCmd = 6;
            triggeredDoorDir = (Direction) (triggeredDoorDir | 0x10);
        }
    }
}

void WorldImpl::UpdateDoors2()
{
    if ( GetMode() == Mode_EndLevel
        || objectTimers[DoorSlot] != 0
        || triggeredDoorCmd == 0 )
        return;

    if ( (triggeredDoorCmd & 1) == 0 )
    {
        if ( triggeredDoorCmd == 2 )
            player->SetObjectTimer( 0x30 );

        triggeredDoorCmd++;
        objectTimers[DoorSlot] = 8;
    }
    else
    {
        if ( (triggeredDoorDir & 0x10) != 0 )
        {
            OpenShutters();
        }

        int d = 1;

        for ( int i = 0; i < 4; i++, d <<= 1 )
        {
            if ( (triggeredDoorDir & d) == 0 )
                continue;

            Direction dir = (Direction) d;
            DoorType type = GetDoorType( dir );

            if (   type == DoorType_Bombable
                || type == DoorType_Key
                || type == DoorType_Key2 )
            {
                if ( !GetDoorState( curRoomId, dir ) )
                {
                    Direction oppositeDir = Util::GetOppositeDir( dir );
                    int nextRoomId = GetNextRoomId( curRoomId, dir );

                    SetDoorState( curRoomId, dir );
                    SetDoorState( nextRoomId, oppositeDir );
                    if ( type != DoorType_Bombable )
                        Sound::PlayEffect( SEffect_door );
                }
            }
        }

        triggeredDoorCmd = 0;
        triggeredDoorDir = Dir_None;
    }
}

int WorldImpl::GetNextTeleportingRoomIndex()
{
    Direction facing = player->GetFacing();
    bool growing = facing == Dir_Up || facing == Dir_Right;

    uint pieces = GetItem( ItemSlot_TriforcePieces );
    uint index  = teleportingRoomIndex;
    uint mask   = 1 << teleportingRoomIndex;

    if ( pieces == 0 )
        return 0;

    do
    {
        if ( growing )
        {
            index = (index + 1) & 7;
            mask <<= 1;
            if ( mask >= 0x100 )
                mask = 1;
        }
        else
        {
            index = (index - 1) & 7;
            mask >>= 1;
            if ( mask == 0 )
                mask = 0x80;
        }
    } while ( (pieces & mask) == 0 );

    return index;
}

void WorldImpl::UpdateRoomColors()
{
    if ( state.play.timer == 0 )
    {
        state.play.animatingRoomColors = false;
        const SparsePos* posAttr = FindSparsePos( Sparse_Recorder, curRoomId );
        if ( posAttr != nullptr )
        {
            int row;
            int col;
            GetRoomCoord( posAttr->pos, row, col );
            tileMaps[curTileMapIndex].tileRefs[row][col] = Tile_Stairs;
            Sound::PlayEffect( SEffect_secret );
        }
        return;
    }

    if ( (state.play.timer % 8) == 0 )
    {
        const ColorSeq* colorSeq = (ColorSeq*) extraData.GetItem( Extra_PondColors );
        if ( curColorSeqNum < colorSeq->Length - 1 )
        {
            if ( curColorSeqNum == colorSeq->Length - 2 )
            {
                state.play.allowWalkOnWater = true;
            }

            int colorIndex = colorSeq->Colors[curColorSeqNum];
            curColorSeqNum++;
            Graphics::SetColorIndexed( 3, 3, colorIndex );
            Graphics::UpdatePalettes();
        }
    }

    state.play.timer--;
}

void WorldImpl::CheckBombables()
{
    UWRoomAttrs& uwRoomAttrs = (UWRoomAttrs&) roomAttrs[curRoomId];

    for ( int iBomb = FirstBombSlot; iBomb < LastBombSlot; iBomb++ )
    {
        Bomb* bomb = (Bomb*) objects[iBomb];
        if ( bomb == nullptr
            || bomb->GetLifetimeState() != Bomb::Fading )
            continue;

        int bombX = bomb->GetX() + 8;
        int bombY = bomb->GetY() + 8;

        for ( int iDoor = 0; iDoor < 4; iDoor++ )
        {
            int doorType = uwRoomAttrs.GetDoor( iDoor );
            if ( doorType == DoorType_Bombable )
            {
                int doorDir = Util::GetOrdDirection( iDoor );
                bool doorState = GetDoorState( curRoomId, doorDir );
                if ( !doorState )
                {
                    if (   abs( bombX - doorMiddles[iDoor].X ) < UWBombRadius 
                        && abs( bombY - doorMiddles[iDoor].Y ) < UWBombRadius )
                    {
                        triggeredDoorCmd = 6;
                        triggeredDoorDir = (Direction) doorDir;
                        break;
                    }
                }
            }
        }
    }
}

bool World::HasLivingObjects()
{
    return !sWorld->roomAllDead;
}

bool WorldImpl::CalcHasLivingObjects()
{
    for ( int i = MonsterSlot1; i < MonsterSlotEnd; i++ )
    {
        Object* obj = objects[i];
        if ( obj != nullptr )
        {
            ObjType type = obj->GetType();
            if (    type < Obj_Bubble1
                || (type > Obj_Bubble3 && type < Obj_Trap) )
                return true;
        }
    }

    return false;
}

void WorldImpl::CheckSecrets()
{
    if ( IsOverworld() )
        return;

    if ( !roomAllDead )
    {
        if ( !CalcHasLivingObjects() )
        {
            player->SetParalyzed( false );
            roomAllDead = true;
        }
    }

    UWRoomAttrs& uwRoomAttrs = (UWRoomAttrs&) roomAttrs[curRoomId];
    int secret = uwRoomAttrs.GetSecret();

    switch ( secret )
    {
    case Secret_Ringleader:
        if (   objects[MonsterSlot1] == nullptr 
            || objects[MonsterSlot1]->GetType() >= Obj_Person_End )
            KillAllObjects();
        break;

    case Secret_LastBoss:
        if ( GetItem( ItemSlot_PowerTriforce ) != 0 )
            triggerShutters = true;
        break;

    // ORIGINAL: BlockDoor and BlockStairs are handled here.

    case Secret_FoesItem:
        if ( roomAllDead )
        {
            if ( !madeRoomItem && !GotItem() )
            {
                madeRoomItem = true;
                AddUWRoomItem( curRoomId );
            }
        }
        // fall thru

    case Secret_FoesDoor:
        if ( roomAllDead )
            triggerShutters = true;
        break;
    }
}

void WorldImpl::AddUWRoomStairs()
{
    SetTile( 0xD0, 0x60, Tile_UW_Stairs );
}

void WorldImpl::KillAllObjects()
{
    for ( int i = MonsterSlot1 + 1; i < MonsterSlotEnd; i++ )
    {
        Object* obj = objects[i];
        if (   obj != nullptr 
            && obj->GetType() < Obj_Person_End 
            && obj->GetDecoration() == 0 )
        {
            obj->SetDecoration( 0x10 );
        }
    }
}

void World::EnablePersonFireballs()
{
    sWorld->enablePersonFireballs = true;
}

void WorldImpl::MoveRoomItem()
{
    Object* foe = objects[MonsterSlot1];
    if ( foe == nullptr )
        return;

    Object* item = objects[ItemSlot];
    ObjType type = foe->GetType();

    if ( item != nullptr
        && (type == Obj_LikeLike 
         || type == Obj_Stalfos 
         || type == Obj_Gibdo) )
    {
        item->SetX( foe->GetX() );
        item->SetY( foe->GetY() );
    }
}

void WorldImpl::UpdateStatues()
{
    static const int fireballLayouts[] = { 0x24, 0x23 };

    if ( IsOverworld() )
        return;

    int pattern = -1;

    if ( enablePersonFireballs )
    {
        pattern = 2;
    }
    else
    {
        UWRoomAttrs& uwRoomAttrs = (UWRoomAttrs&) roomAttrs[curRoomId];
        int layoutId = uwRoomAttrs.GetUniqueRoomId();

        for ( int i = 0; i < _countof( fireballLayouts ); i++ )
        {
            if ( fireballLayouts[i] == layoutId )
            {
                pattern = i;
                break;
            }
        }
    }

    if ( pattern >= 0 )
        Statues::Update( pattern );
}

void WorldImpl::OnLeavePlay()
{
    if ( lastMode == Mode_Play )
        SaveObjectCount();
}

void WorldImpl::ClearLevelData()
{
    curColorSeqNum = 0;
    darkRoomFadeStep = 0;
    curMazeStep = 0;
    tempShutterRoomId = 0;
    tempShutterDoorDir = 0;
    tempShuttersRoomId = 0;
    tempShutters = false;
    prevRoomWasCellar = false;
    whirlwindTeleporting = 0;

    roomKillCount = false;
    roomAllDead = false;
    madeRoomItem = false;
    enablePersonFireballs = false;
}

static bool IsRecurringFoe( int type )
{
    return type <  Obj_1Dodongo
        || type == Obj_RedLamnola
        || type == Obj_BlueLamnola
        || type >= Obj_Trap
        ;
}

void WorldImpl::SaveObjectCount()
{
    if ( IsOverworld() )
    {
        OWRoomFlags& flags = profile.OverworldFlags[curRoomId];
        int savedCount = flags.GetObjCount();
        int count;

        if ( roomKillCount >= roomObjCount )
        {
            count = 7;
        }
        else
        {
            count = (roomKillCount & 7) + savedCount;
            if ( count > 7 )
                count = 7;
        }

        flags.SetObjCount( count );
    }
    else
    {
        UWRoomFlags& flags = curUWBlockFlags[curRoomId];
        int count;

        if ( roomObjCount != 0 )
        {
            if ( roomKillCount == 0
                || IsRecurringFoe( roomObjId ) )
            {
                if ( roomKillCount < roomObjCount )
                {
                    levelKillCounts[curRoomId] += roomKillCount;
                    if ( levelKillCounts[curRoomId] < 3 )
                        count = levelKillCounts[curRoomId];
                    else
                        count = 2;
                    flags.SetObjCount( count );
                    return;
                }
            }
        }

        levelKillCounts[curRoomId] = 0xF;
        flags.SetObjCount( 3 );
    }
}

void WorldImpl::CalcObjCountToMake( int& objId, int& count )
{
    if ( IsOverworld() )
    {
        OWRoomFlags& flags = profile.OverworldFlags[curRoomId];

        if ( !IsRoomInHistory() && (flags.GetObjCount() == 7) )
        {
            flags.SetObjCount( 0 );
        }
        else
        {
            if ( flags.GetObjCount() == 7 )
            {
                objId = 0;
                count = 0;
            }
            else if ( flags.GetObjCount() != 0 )
            {
                int savedCount = flags.GetObjCount();
                if ( count < savedCount )
                {
                    objId = 0;
                    count = 0;
                }
                else
                {
                    count -= savedCount;
                }
            }
        }
    }
    else // Is Underworld
    {
        UWRoomFlags& flags = curUWBlockFlags[curRoomId];

        if ( IsRoomInHistory() || flags.GetObjCount() != 3 )
        {
            if ( count < levelKillCounts[curRoomId] )
            {
                objId = 0;
                count = 0;
            }
            else
            {
                count -= levelKillCounts[curRoomId];
            }
        }
        else
        {
            if ( IsRecurringFoe( objId ) )
            {
                flags.SetObjCount( 0 );
                levelKillCounts[curRoomId] = 0;
            }
            else
            {
                objId = 0;
                count = 0;
            }
        }
    }
}

void WorldImpl::UpdateObservedPlayerPos()
{
    // ORIGINAL: This happens after player updates and before player items update.

    if ( !giveFakePlayerPos )
    {
        fakePlayerPos.X = player->GetX();
        fakePlayerPos.Y = player->GetY();
    }

    // ORIGINAL: This happens after player items update and before the rest of objects update.

    if ( stunTimers[ObservedPlayerTimerSlot] == 0 )
    {
        stunTimers[ObservedPlayerTimerSlot] = Util::GetRandom( 8 );

        giveFakePlayerPos = !giveFakePlayerPos;
        if ( giveFakePlayerPos )
        {
            if ( fakePlayerPos.X == player->GetX() )
            {
                fakePlayerPos.X = fakePlayerPos.X ^ 0xFF;
                fakePlayerPos.Y = fakePlayerPos.Y ^ 0xFF;
            }
        }
    }
}

void WorldImpl::UpdateRupees()
{
    if ( (GetFrameCounter() & 1) == 0 )
    {
        unsigned int rupeesToAdd = profile.Items[ItemSlot_RupeesToAdd];
        unsigned int rupeesToSubtract = profile.Items[ItemSlot_RupeesToSubtract];

        if ( rupeesToAdd > 0 && rupeesToSubtract == 0 )
        {
            if ( profile.Items[ItemSlot_Rupees] < 255 )
                profile.Items[ItemSlot_Rupees]++;
            else
                profile.Items[ItemSlot_RupeesToAdd] = 0;

            Sound::PlayEffect( SEffect_character );
        }
        else if ( rupeesToAdd == 0 && rupeesToSubtract > 0 )
        {
            // Phase 11.6: deduct the whole payment in one go instead of
            // ticking it down one rupee every other frame. The classic
            // tick-tick-tick is a pleasant surprise for a handful of rupees,
            // but turns into a forced multi-second wait for real prices
            // (shops, the 20-rupee door-repair toll, gambling, bomb
            // refills). Pickups (RupeesToAdd, the
            // other branch) keep the one-at-a-time count-up + chime
            // unchanged: that's the satisfying half, not the annoying one.
            if ( profile.Items[ItemSlot_Rupees] >= rupeesToSubtract )
                profile.Items[ItemSlot_Rupees] -= rupeesToSubtract;
            else
                profile.Items[ItemSlot_Rupees] = 0;
            profile.Items[ItemSlot_RupeesToSubtract] = 0;

            Sound::PlayEffect( SEffect_character );
        }

        if ( profile.Items[ItemSlot_RupeesToAdd] > 0 )
            profile.Items[ItemSlot_RupeesToAdd]--;
    }
}

void WorldImpl::UpdateLiftItem()
{
    if ( state.play.liftItemId == 0 )
        return;

    state.play.liftItemTimer--;

    if ( state.play.liftItemTimer == 0 )
    {
        state.play.liftItemId = 0;
        player->SetState( Player::Idle );
    }
    else
    {
        player->SetState( Player::Paused );
    }
}

void WorldImpl::DrawPlay()
{
    if ( submenu != 0 )
    {
        DrawSubmenu();
        return;
    }

    // Phase 12.2b: room background + door frames are the "Ground" depth
    // layer (furthest back) -- see Graphics::SetTopLayer()'s comment.
    Graphics::SetTopLayer( TopLayer::Ground );
    Graphics::SetClip( 0, TileMapBaseY, TileMapWidth, TileMapHeight );
    ClearScreen();
    DrawRoom();
    Graphics::ResetClip();

    Object* objOverPlayer = nullptr;

    // DrawObjects() itself switches between Mid/Fore per object (regular
    // objects vs. projectiles) -- see its own comment.
    DrawObjects( &objOverPlayer );

    Graphics::SetTopLayer( TopLayer::Mid );
    if ( IsLiftingItem() )
        DrawLinkLiftingItem( state.play.liftItemId );
    else
        player->Draw();

    if ( objOverPlayer != nullptr )
    {
        // The one object Link is actively lifting above his head -- already
        // deliberately drawn last/on top; "Fore" (nearest) fits it exactly.
        Graphics::SetTopLayer( TopLayer::Fore );
        objOverPlayer->DecoratedDraw();
    }

    if ( IsUWMain( curRoomId ) )
    {
        // Door frames count as room scenery (Ground), same as the
        // background tiles above, even though they're drawn last here --
        // each layer is its own offscreen buffer, so population order
        // doesn't affect the final composited depth ordering.
        Graphics::SetTopLayer( TopLayer::Ground );
        DrawDoors( curRoomId, true, 0, 0 );
    }

    Graphics::SetTopLayer( TopLayer::Mid );
}

void WorldImpl::DrawSubmenu()
{
    Graphics::SetClip( 0, TileMapBaseY + submenuOffsetY, TileMapWidth, TileMapHeight - submenuOffsetY );
    ClearScreen();
    DrawMap( curRoomId, curTileMapIndex, 0, submenuOffsetY );
    Graphics::ResetClip();

    if ( IsUWMain( curRoomId ) )
        DrawDoors( curRoomId, true, 0, submenuOffsetY );

    menu.Draw( submenuOffsetY );

    // Manual-save confirmation (phase 8.5): a brief centred "SAVED" over the
    // open inventory panel after Up+A. Drawn last so it sits on top. Font
    // codes: S=0x1C A=0x0A V=0x1F E=0x0E D=0x0D (same encoding as the
    // Save/Continue strings in DrawContinueQuestion).
    if ( savedNotifyTimer > 0 )
    {
        static const uint8_t Saved[] = { 0x1C, 0x0A, 0x1F, 0x0E, 0x0D };
        int pal = (GetFrameCounter() / 4) & 1;   // gentle flash, like the menu cursor
        DrawString( Saved, _countof( Saved ), 0x6C, 0x60, pal );
    }
}

// Phase 12.2b: which ObjTypes get the "Fore" (nearest, pops toward the
// viewer) stereoscopic depth layer instead of "Mid" -- every projectile,
// Link's own (sword beam/arrow/boomerang/bomb) and enemies' (fireballs,
// Octorok rocks): all projectiles.
// Obj_MagicWave/Obj_Arrow are aliases of Obj_OldMan/Obj_FriendlyMoblin (a
// 1986 NES value-reuse trick, see ObjType.h) -- safe to check here since
// those NPC types only ever appear in Mode_PlayCave, never in this
// function's Mode_Play object list. Deliberately NOT here: Obj_Boulder
// (rolls on the ground, not airborne) and Obj_StandingFire/Obj_GuardFire
// (stationary hazards) -- those stay "Mid" with everything else.
static bool IsProjectileType( ObjType type )
{
    switch ( type )
    {
    case Obj_PlayerSwordShot:
    case Obj_MagicWave:    // == Obj_OldMan
    case Obj_MagicWave2:   // == Obj_OldWoman
    case Obj_Arrow:        // == Obj_FriendlyMoblin
    case Obj_Boomerang:
    case Obj_Bomb:
    case Obj_Fireball:
    case Obj_Fireball2:
    case Obj_FlyingRock:
        return true;
    default:
        return false;
    }
}

void WorldImpl::DrawObjects( Object** objOverPlayer )
{
    // Phase 12.2b: two passes instead of one, so the stereoscopic depth
    // layer only switches twice total (Mid then Fore) rather than
    // potentially once per object if projectiles and regular objects were
    // interleaved in the objects[] array -- see Graphics::SetTopLayer()'s
    // comment for why minimizing switches matters. MaxObjects is small
    // enough that iterating twice is negligible.
    // Splitting into two passes doesn't change *which* object becomes
    // objOverPlayer (checked, not assumed): GetDrawAbovePlayer() is only
    // ever set by LikeLike/Wallmaster (Monsters.cpp, both grab-attacks) --
    // never by any IsProjectileType() object -- so the claim can only ever
    // happen in the first (Mid) pass, exactly matching the original single-
    // pass order.
    Graphics::SetTopLayer( TopLayer::Mid );
    for ( int i = 0; i < MaxObjects; i++ )
    {
        curObjSlot = i;

        Object* obj = objects[i];
        if ( obj != nullptr && !obj->IsDeleted() && !IsProjectileType( obj->GetType() ) )
        {
            if ( !obj->GetFlags().GetDrawAbovePlayer()
                || objOverPlayer == nullptr
                || *objOverPlayer != nullptr )
                obj->DecoratedDraw();
            else
                *objOverPlayer = obj;
        }
    }

    Graphics::SetTopLayer( TopLayer::Fore );
    for ( int i = 0; i < MaxObjects; i++ )
    {
        curObjSlot = i;

        Object* obj = objects[i];
        if ( obj != nullptr && !obj->IsDeleted() && IsProjectileType( obj->GetType() ) )
        {
            if ( !obj->GetFlags().GetDrawAbovePlayer()
                || objOverPlayer == nullptr
                || *objOverPlayer != nullptr )
                obj->DecoratedDraw();
            else
                *objOverPlayer = obj;
        }
    }
}

void DrawZeldaLiftingTriforce( int x, int y )
{
    SpriteImage image;
    image.anim = Graphics::GetAnimation( Sheet_Boss, Anim_B3_Zelda_Lift );
    image.Draw( Sheet_Boss, x, y, PlayerPalette );

    DrawItem( Item_TriforcePiece, x, y - 0x10, 0 );
}

void WorldImpl::DrawLinkLiftingItem( int itemId )
{
    int animIndex = (itemId == Item_TriforcePiece) ? Anim_PI_LinkLiftHeavy : Anim_PI_LinkLiftLight;
    SpriteImage image;
    image.anim = Graphics::GetAnimation( Sheet_PlayerAndItems, animIndex );
    image.Draw( Sheet_PlayerAndItems, player->GetX(), player->GetY(), PlayerPalette );

    DrawItem( itemId, player->GetX(), player->GetY() - 0x10, 0 );
}

void WorldImpl::MakeObjects( Direction entryDir )
{
    if ( IsUWCellar( curRoomId ) )
    {
        MakeCellarObjects();
        return;
    }
    else if ( state.play.roomType == PlayState::Cave )
    {
        MakeCaveObjects();
        return;
    }

    RoomAttrs& roomAttr = roomAttrs[curRoomId];
    int slot = MonsterSlot1;
    int objId = roomAttr.MonsterListId;
    bool edgeObjects = false;

    if ( objId >= Obj_Person1 && objId < Obj_Person_End
        || objId == Obj_Grumble )
    {
        MakeUnderworldPerson( objId );
        return;
    }

    if ( IsOverworld() )
    {
        OWRoomAttrs& owRoomAttrs = (OWRoomAttrs&) roomAttrs[curRoomId];
        edgeObjects = owRoomAttrs.MonstersEnter();
    }

    int count = roomAttr.GetMonsterCount();

    if ( objId >= Obj_1Dodongo && objId < 0x62 )
        count = 1;

    CalcObjCountToMake( objId, count );
    roomObjCount = count;

    if ( objId > 0 && count > 0 )
    {
        bool            isList = objId >= 0x62;
        uint8_t         repeatedIds[MaxMonsters];
        const uint8_t*  list = nullptr;

        if ( isList )
        {
            int listId = objId - 0x62;
            list = objLists.GetItem( listId );
        }
        else
        {
            memset( repeatedIds, objId, count );
            list = repeatedIds;
        }

        int dirOrd = Util::GetDirectionOrd( entryDir );
        const SpotSeq* spotSeq = (SpotSeq*) extraData.GetItem( Extra_SpawnSpots );
        int spotsLen = spotSeq->Length / 4;
        const RSpot* dirSpots = &spotSeq->Spots[spotsLen * dirOrd];

        for ( int i = 0; i < count; i++, slot++ )
        {
            // An earlier objects that's made might make some objects in slots after it.
            // Maybe MakeMonster should take a reference to the current index.
            if ( objects[slot] != nullptr )
                continue;

            curObjSlot = slot;

            int type = list[slot];
            int x, y;

            if ( edgeObjects 
                && type != Obj_Zora
                && type != Obj_Armos
                && type != Obj_StandingFire
                && type != Obj_Whirlwind
                )
            {
                placeholderTypes[slot] = type;
            }
            else if ( FindSpawnPos( type, dirSpots, spotsLen, x, y ) )
            {
                Object* obj = MakeMonster( (ObjType) type, x, y );
                objects[slot] = obj;
            }
        }
    }

    if ( objects[MonsterSlot1] != nullptr )
        roomObjId = objects[MonsterSlot1]->GetType();

    if ( IsOverworld() )
    {
        OWRoomAttrs& owRoomAttr = (OWRoomAttrs&) roomAttr;
        if ( owRoomAttr.HasZora() )
        {
            curObjSlot = slot;

            Object* zora = MakeMonster( Obj_Zora, 0, 0 );
            SetObject( slot, zora );
            slot++;
        }
    }
}

void WorldImpl::MakeCellarObjects()
{
    static const int startXs[] = { 0x20, 0x60, 0x90, 0xD0 };
    const int startY = 0x9D;

    for ( int i = 0; i < 4; i++ )
    {
        curObjSlot = i;

        Object* keese = MakeMonster( Obj_BlueKeese, startXs[i], startY );
        SetObject( i, keese );
    }
}

void WorldImpl::MakeCaveObjects()
{
    OWRoomAttrs& owRoomAttrs = (OWRoomAttrs&) roomAttrs[curRoomId];
    int caveIndex = owRoomAttrs.GetCaveId() - FirstCaveIndex;

    const CaveSpecList* caves = (CaveSpecList*) extraData.GetItem( Extra_Caves );

    if ( caveIndex >= caves->Count )
        return;

    const CaveSpec*     cave = &caves->Specs[caveIndex];
    int type = Obj_Cave1 + caveIndex;

    MakePersonRoomObjects( type, cave );
}

void WorldImpl::MakeUnderworldPerson( int objId )
{
    CaveSpec    cave = { 0 };

    cave.Items[0] = Item_None;
    cave.Items[1] = Item_None;
    cave.Items[2] = Item_None;

    UWRoomAttrs& uwRoomAttrs = (UWRoomAttrs&) roomAttrs[curRoomId];
    Secret secret = (Secret) uwRoomAttrs.GetSecret();

    if ( objId == Obj_Grumble )
    {
        cave.StringId = String_Grumble;
        cave.DwellerType = Obj_FriendlyMoblin;
    }
    else if ( secret == Secret_MoneyOrLife )
    {
        cave.StringId = String_MoneyOrLife;
        cave.DwellerType = Obj_OldMan;
        cave.Items[0] = Item_HeartContainer;
        cave.Prices[0] = 1;
        cave.Items[2] = Item_Rupee;
        cave.Prices[2] = 50;
        cave.SetShowNegative();
        cave.SetShowItems();
        cave.SetSpecial();
        cave.SetPickUp();
    }
    else
    {
        const LevelPersonStrings* stringIdTables = (LevelPersonStrings*) 
            extraData.GetItem( Extra_LevelPersonStringIds );

        int levelIndex      = infoBlock.EffectiveLevelNumber - 1;
        int levelTableIndex = levelGroups[levelIndex];
        int stringSlot      = objId - Obj_Person1;
        int stringId        = stringIdTables->StringIds[levelTableIndex][stringSlot];

        cave.DwellerType = Obj_OldMan;
        cave.StringId = stringId;

        if ( stringId == String_MoreBombs )
        {
            cave.Items[1] = Item_Rupee;
            cave.Prices[1] = 100;
            cave.SetShowNegative();
            cave.SetShowItems();
            cave.SetSpecial();
            cave.SetPickUp();
        }
    }

    MakePersonRoomObjects( objId, &cave );
}

void WorldImpl::MakePersonRoomObjects( int type, const CaveSpec* spec )
{
    static const int fireXs[] = { 0x48, 0xA8 };

    if ( spec->DwellerType != Obj_None )
    {
        curObjSlot = 0;
        Object* person = MakePerson( type, spec, 0x78, 0x80 );
        SetObject( 0, person );
    }

    for ( int i = 0; i < 2; i++ )
    {
        curObjSlot++;
        StandingFire* fire = new StandingFire( fireXs[i], 0x80 );
        SetObject( curObjSlot, fire );
    }
}

void WorldImpl::MakeWhirlwind()
{
    if ( whirlwindTeleporting != 0 )
    {
        int y = teleportYs[teleportingRoomIndex];

        whirlwindTeleporting = 2;

        Whirlwind* whirlwind = new Whirlwind( 0, y );
        SetObject( WhirlwindSlot, whirlwind );

        player->SetState( Player::Paused );
        player->SetX( whirlwind->GetX() );
        player->SetY( 0xF8 );
    }
}

bool WorldImpl::FindSpawnPos( int type, const RSpot* spots, int len, int& x, int& y )
{
    const ObjectAttr* objAttrs = GetObjectAttrs();

    int playerX = player->GetX();
    int playerY = player->GetY();
    bool noWorldCollision = !objAttrs[type].GetWorldCollision();

    int i;
    for ( i = 0; i < MaxObjListSize; i++ )
    {
        GetRSpotCoord( spots[spotIndex], x, y );
        spotIndex = (spotIndex + 1) % len;

        if ( (playerX != x || playerY != y)
            && (noWorldCollision || !CollidesWithTileStill( x, y )) )
            break;
    }

    if ( i == 9 )
        return false;

    return true;
}

void WorldImpl::PutEdgeObject()
{
    if ( stunTimers[EdgeObjTimerSlot] != 0 )
        return;

    stunTimers[EdgeObjTimerSlot] = Util::GetRandom( 4 ) + 2;

    int x = edgeX;
    int y = edgeY;

    for ( ; ; )
    {
        if ( x == 0 )
            y += 0x10;
        else if ( x == 0xF0 )
            y -= 0x10;

        if ( y == 0x40 )
            x -= 0x10;
        else if ( y == 0xE0 )
            x += 0x10;

        int row = (y / 16) - 4;
        int col = (x / 16);
        int tileRef = GetMapTile( row, col );

        if ( (tileRef != Tile_Sand) && !CollidesTile( row, col ) )
            break;
        if ( y == edgeY && x == edgeX )
            break;
    }

    edgeX = x;
    edgeY = y;

    if (   abs( player->GetX() - x ) >= 0x22 
        || abs( player->GetY() - y ) >= 0x22 )
    {
        Object* obj = MakeMonster( (ObjType) placeholderTypes[curObjSlot], x, y - 3 );
        objects[curObjSlot] = obj;
        placeholderTypes[curObjSlot] = 0;
        obj->SetDecoration( 0 );
    }
}

void WorldImpl::HandleNormalObjectDeath()
{
    Object* obj = objects[curObjSlot];
    int x = obj->GetX();
    int y = obj->GetY();
    int type = obj->GetType();

    delete obj;
    objects[curObjSlot] = nullptr;

    if (   type != Obj_ChildGel
        && type != Obj_RedKeese
        && type != Obj_DeadDummy )
    {
        int cycle = worldKillCycle + 1;
        if ( cycle == 10 )
            cycle = 0;
        worldKillCycle = cycle;

        if ( type != Obj_Zora )
            roomKillCount++;
    }

    TryDroppingItem( type, x, y );
}

void WorldImpl::TryDroppingItem( int origType, int x, int y )
{
    static const uint8_t classBases[] = { 0, 10, 20, 30 };
    static const uint8_t classRates[] = { 0x50, 0x98, 0x68, 0x68 };
    static const uint8_t dropItems[] = 
    {
        0x22, 0x18, 0x22, 0x18, 0x23, 0x18, 0x22, 0x22, 0x18, 0x18, 0x0F, 0x18, 0x22, 0x18, 0x0F, 0x22, 
        0x21, 0x18, 0x18, 0x18, 0x22, 0x00, 0x18, 0x21, 0x18, 0x22, 0x00, 0x18, 0x00, 0x22, 0x22, 0x22, 
        0x23, 0x18, 0x22, 0x23, 0x22, 0x22, 0x22, 0x18
    };

    if ( curObjSlot == MonsterSlot1 && (origType == Obj_Stalfos || origType == Obj_Gibdo) )
        return;

    const ObjectAttr* objAttrs = GetObjectAttrs();
    int objClass = objAttrs[origType].GetItemDropClass();
    if ( objClass == 0 )
        return;
    objClass--;

    int itemId;

    if ( worldKillCount == 0x10 )
    {
        itemId = Item_Fairy;
        helpDropCounter = 0;
        helpDropValue = 0;
    }
    else if ( helpDropCounter >= 0xA )
    {
        if ( helpDropValue == 0 )
            itemId = Item_5Rupees;
        else
            itemId = Item_Bomb;
        helpDropCounter = 0;
        helpDropValue = 0;
    }
    else
    {
        int r = Util::GetRandom( 256 );
        int rate = classRates[objClass];

        if ( r >= rate )
            return;

        int classIndex = classBases[objClass] + worldKillCycle;
        itemId = dropItems[classIndex];
    }

    Object* obj = MakeItem( itemId, x, y, false );
    objects[curObjSlot] = obj;
}

void WorldImpl::FillHeartsStep()
{
    Sound::PlayEffect( SEffect_character );

    Profile& profile = World::GetProfile();
    int maxHeartsValue = profile.GetMaxHeartsValue();

    World::FillHearts( 6 );

    if ( profile.Hearts == maxHeartsValue )
    {
        pause = 0;
        World::SetSwordBlocked( false );
    }
}

void WorldImpl::GotoScroll( Direction dir )
{
    assert( dir != Dir_None );

    state.scroll.curRoomId = curRoomId;
    // Set here too, not just later in UpdateScroll_LoadRoom() (which sets it
    // again to the same value right before LoadRoom() flips the real
    // curRoomId) -- this makes World::GetScrollOldRoomId() valid for the
    // *entire* Mode_Scroll duration instead of leaving a gap during the
    // Start/AnimatingColors substates where it would still hold whatever
    // room the *previous* scroll transition left behind.
    state.scroll.oldRoomId = curRoomId;
    state.scroll.scrollDir = dir;
    state.scroll.substate = ScrollState::Start;
    curMode = Mode_Scroll;
}

void WorldImpl::GotoScroll( Direction dir, int currentRoomId )
{
    GotoScroll( dir );
    state.scroll.curRoomId = currentRoomId;
}

bool WorldImpl::CalcMazeStayPut( Direction dir )
{
    if ( !IsOverworld() )
        return false;

    bool stayPut = false;
    const SparseMaze* maze 
        = (SparseMaze*) Util::FindSparseAttr( sparseRoomAttrs, Sparse_Maze, curRoomId );
    if ( maze != nullptr )
    {
        if ( dir != maze->exitDir )
        {
            if ( dir == maze->path[curMazeStep] )
            {
                curMazeStep++;
                if ( curMazeStep == _countof( maze->path ) )
                {
                    curMazeStep = 0;
                    Sound::PlayEffect( SEffect_secret );
                }
                else
                    stayPut = true;
            }
            else
            {
                curMazeStep = 0;
                stayPut = true;
            }
        }
        else
        {
            curMazeStep = 0;
        }
    }
    return stayPut;
}

void WorldImpl::UpdateScroll()
{
    UpdateFunc update = sScrollFuncs[state.scroll.substate];
    (this->*update)();
}

void WorldImpl::UpdateScroll_Start()
{
    int roomRow;
    int roomCol;

    GetWorldCoord( state.scroll.curRoomId, roomRow, roomCol );
    Util::MoveSimple( roomCol, roomRow, state.scroll.scrollDir, 1 );

    int nextRoomId;
    if ( CalcMazeStayPut( state.scroll.scrollDir ) )
        nextRoomId = state.scroll.curRoomId;
    else
        nextRoomId = MakeRoomId( roomRow, roomCol );

    state.scroll.nextRoomId = nextRoomId;
    state.scroll.substate = ScrollState::AnimatingColors;
}

void WorldImpl::UpdateScroll_AnimatingColors()
{
    if ( curColorSeqNum == 0 )
    {
        state.scroll.substate = ScrollState::LoadRoom;
        return;
    }

    if ( (GetFrameCounter() & 4) != 0 )
    {
        curColorSeqNum--;

        const ColorSeq* colorSeq = (ColorSeq*) extraData.GetItem( Extra_PondColors );
        int color = colorSeq->Colors[curColorSeqNum];
        Graphics::SetColorIndexed( 3, 3, color );
        Graphics::UpdatePalettes();

        if ( curColorSeqNum == 0 )
            state.scroll.substate = ScrollState::LoadRoom;
    }
}

void WorldImpl::UpdateScroll_FadeOut()
{
    if ( state.scroll.timer == 0 )
    {
        for ( int i = 0; i < 2; i++ )
        {
            Graphics::SetPaletteIndexed( i + 2, infoBlock.DarkPaletteSeq[darkRoomFadeStep][i] );
        }
        Graphics::UpdatePalettes();

        darkRoomFadeStep++;

        if ( darkRoomFadeStep == 4 )
        {
            state.scroll.substate = ScrollState::Scroll;
            state.scroll.timer = ScrollState::StateTime;
        }
        else
        {
            state.scroll.timer = 9;
        }
    }
    else
    {
        state.scroll.timer--;
    }
}

void WorldImpl::UpdateScroll_LoadRoom()
{
    if ( state.scroll.scrollDir == Dir_Down
        && !IsOverworld()
        && curRoomId == infoBlock.StartRoomId )
    {
        // Walked out through the dungeon's own entrance room -- see
        // unfurledFromDungeon's comment (WorldImpl.h) for why this is set
        // explicitly here rather than inferred in UpdateLoadLevel().
        unfurledFromDungeon = true;
        GotoLoadLevel( 0 );
        return;
    }

    state.scroll.offsetX = 0;
    state.scroll.offsetY = 0;
    state.scroll.speedX = 0;
    state.scroll.speedY = 0;
    state.scroll.oldMapToNewMapDistX = 0;
    state.scroll.oldMapToNewMapDistY = 0;

    switch ( state.scroll.scrollDir )
    {
    case Dir_Left:
        state.scroll.offsetX = -TileMapWidth;
        state.scroll.speedX = ScrollSpeed;
        state.scroll.oldMapToNewMapDistX = TileMapWidth;
        break;

    case Dir_Right:
        state.scroll.offsetX = TileMapWidth;
        state.scroll.speedX = -ScrollSpeed;
        state.scroll.oldMapToNewMapDistX = -TileMapWidth;
        break;

    case Dir_Up:
        state.scroll.offsetY = -TileMapHeight;
        state.scroll.speedY = ScrollSpeed;
        state.scroll.oldMapToNewMapDistY = TileMapHeight;
        break;

    case Dir_Down:
        state.scroll.offsetY = TileMapHeight;
        state.scroll.speedY = -ScrollSpeed;
        state.scroll.oldMapToNewMapDistY = -TileMapHeight;
        break;
    }

    state.scroll.oldRoomId = curRoomId;

    int nextRoomId = state.scroll.nextRoomId;
    int nextTileMapIndex = (curTileMapIndex + 1) % 2;
    state.scroll.oldTileMapIndex = curTileMapIndex;

    tempShutterRoomId = nextRoomId;
    tempShutterDoorDir = Util::GetOppositeDir( state.scroll.scrollDir );

    LoadRoom( nextRoomId, nextTileMapIndex );

    UWRoomAttrs& uwRoomAttrs = (UWRoomAttrs&) roomAttrs[nextRoomId];
    if ( uwRoomAttrs.IsDark() && darkRoomFadeStep == 0 )
    {
        state.scroll.substate = ScrollState::FadeOut;
        state.scroll.timer = 9;
    }
    else
    {
        state.scroll.substate = ScrollState::Scroll;
        state.scroll.timer = ScrollState::StateTime;
    }
}

void WorldImpl::UpdateScroll_Scroll()
{
    if ( state.scroll.timer > 0 )
    {
        state.scroll.timer--;
        return;
    }

    if ( state.scroll.offsetX == 0 && state.scroll.offsetY == 0 )
    {
        GotoEnter( state.scroll.scrollDir );
        if ( IsOverworld() && state.scroll.nextRoomId == 0x0F )
            Sound::PlayEffect( SEffect_secret );
        return;
    }

    state.scroll.offsetX += state.scroll.speedX;
    state.scroll.offsetY += state.scroll.speedY;

    Limits playerLimits = Player::GetPlayerLimits();

    if ( state.scroll.speedX != 0 )
    {
        int x = player->GetX() + state.scroll.speedX;
        if ( x < playerLimits[1] )
            x = playerLimits[1];
        else if ( x > playerLimits[0] )
            x = playerLimits[0];
        player->SetX( x );
    }
    else
    {
        int y = player->GetY() + state.scroll.speedY;
        if ( y < playerLimits[3] )
            y = playerLimits[3];
        else if ( y > playerLimits[2] )
            y = playerLimits[2];
        player->SetY( y );
    }

    player->GetAnimator()->Advance();
}

void WorldImpl::DrawScroll()
{
    Graphics::SetClip( 0, TileMapBaseY, TileMapWidth, TileMapHeight );
    ClearScreen();

    if ( state.scroll.substate == ScrollState::Scroll 
        || state.scroll.substate == ScrollState::FadeOut )
    {
        int oldMapOffsetX = state.scroll.offsetX + state.scroll.oldMapToNewMapDistX;
        int oldMapOffsetY = state.scroll.offsetY + state.scroll.oldMapToNewMapDistY;

        DrawMap( curRoomId, curTileMapIndex, state.scroll.offsetX, state.scroll.offsetY );
        DrawMap( state.scroll.oldRoomId, state.scroll.oldTileMapIndex, oldMapOffsetX, oldMapOffsetY );
    }
    else
    {
        DrawMap( curRoomId, curTileMapIndex, 0, 0 );
    }

    Graphics::ResetClip();

    // Phase 12.2b: Link is Mid, not Ground -- DrawMap() above already leaves
    // Ground active when it returns (see its own comment), so this needs an
    // explicit switch back to Mid, same as DrawRoomNoObjects().
    if ( IsOverworld() )
    {
        Graphics::SetTopLayer( TopLayer::Mid );
        player->Draw();
    }
}

void WorldImpl::GotoLeave( Direction dir )
{
    assert( dir != Dir_None );

    state.leave.curRoomId = curRoomId;
    state.leave.scrollDir = dir;
    state.leave.timer = LeaveState::StateTime;
    curMode = Mode_Leave;
}

void WorldImpl::GotoLeave( Direction dir, int currentRoomId )
{
    GotoLeave( dir );
    state.leave.curRoomId = currentRoomId;
}

void WorldImpl::UpdateLeave()
{
    Limits playerLimits = Player::GetPlayerLimits();
    int dirOrd = Util::GetDirectionOrd( player->GetFacing() );
    uint coord = Util::IsVertical( player->GetFacing() ) ? player->GetY() : player->GetX();

    if ( coord != playerLimits[dirOrd] )
    {
        player->MoveLinear( state.leave.scrollDir, Player::WalkSpeed );
        player->GetAnimator()->Advance();
        return;
    }

    if ( state.leave.timer == 0 )
    {
        player->GetAnimator()->AdvanceFrame();
        GotoScroll( state.leave.scrollDir, state.leave.curRoomId );
        return;
    }
    state.leave.timer--;
}

void WorldImpl::DrawLeave()
{
    Graphics::SetClip( 0, TileMapBaseY, TileMapWidth, TileMapHeight );
    DrawRoomNoObjects();
    Graphics::ResetClip();
}

void WorldImpl::GotoEnter( Direction dir )
{
    state.enter.substate = EnterState::Start;
    state.enter.scrollDir = dir;
    state.enter.timer = 0;
    state.enter.playerPriority = SpritePri_AboveBg;
    state.enter.playerSpeed = Player::WalkSpeed;
    state.enter.gotoPlay = false;
    curMode = Mode_Enter;
}

void WorldImpl::MovePlayer( Direction dir, int speed, int& fraction )
{
    fraction += speed;
    int carry = fraction >> 8;
    fraction &= 0xFF;

    int x = player->GetX();
    int y = player->GetY();
    Util::MoveSimple( x, y, dir, carry );

    player->SetX( x );
    player->SetY( y );
}

void WorldImpl::UpdateEnter()
{
    UpdateFunc update = sEnterFuncs[state.enter.substate];
    (this->*update)();

    if ( state.enter.gotoPlay )
    {
        if ( IsUWMain( curRoomId ) 
            && (tempShutterDoorDir != Dir_None) 
            && GetDoorType( curRoomId, (Direction) tempShutterDoorDir ) == DoorType_Shutter )
        {
            Sound::PlayEffect( SEffect_door );
        }
        tempShutterDoorDir = Dir_None;
        statusBar.EnableFeatures( StatusBar::Feature_All, true );
        if ( IsOverworld() && fromUnderground != 0 )
            Sound::PlaySong( infoBlock.Song, Sound::MainSongStream, true );
        GotoPlay();
        return;
    }
    player->GetAnimator()->Advance();
}

void WorldImpl::UpdateEnter_Start()
{
    triggeredDoorCmd = 0;
    triggeredDoorDir = Dir_None;

    if ( IsOverworld() )
    {
        int row = (player->GetY() + 3 - TileMapBaseY) / TileHeight;
        int col = player->GetX() / TileWidth;

        uint8_t tileRef = tileMaps[curTileMapIndex].tileRefs[row][col];
        if ( tileRef == Tile_Cave )
        {
            player->SetY( player->GetY() + TileHeight );
            player->SetFacing( Dir_Down );

            state.enter.playerFraction = 0;
            state.enter.playerSpeed = 0x40;
            state.enter.playerPriority = SpritePri_BelowBg;
            state.enter.scrollDir = Dir_Up;
            state.enter.targetX = player->GetX();
            state.enter.targetY = player->GetY() - 0x10;
            state.enter.substate = EnterState::WalkCave;

            Sound::StopAll();
            Sound::PlayEffect( SEffect_stairs );
        }
        else
        {
            state.enter.substate = EnterState::Wait;
            state.enter.timer = EnterState::StateTime;
        }
    }
    else if ( state.enter.scrollDir != Dir_None )
    {
        UWRoomAttrs& uwRoomAttrs = (UWRoomAttrs&) roomAttrs[curRoomId];
        Direction oppositeDir = Util::GetOppositeDir( state.enter.scrollDir );
        int door = Util::GetDirectionOrd( oppositeDir );
        int doorType = uwRoomAttrs.GetDoor( door );
        int distance;

        if ( doorType == DoorType_Shutter || doorType == DoorType_Bombable )
            distance = TileWidth * 2;
        else
            distance = TileWidth;

        state.enter.targetX = player->GetX();
        state.enter.targetY = player->GetY();
        Util::MoveSimple( 
            state.enter.targetX, 
            state.enter.targetY, 
            state.enter.scrollDir, 
            distance );

        if ( !uwRoomAttrs.IsDark() && darkRoomFadeStep > 0 )
        {
            state.enter.substate = EnterState::FadeIn;
            state.enter.timer = 9;
        }
        else
        {
            state.enter.substate = EnterState::Walk;
        }

        player->SetFacing( state.enter.scrollDir );
    }
    else
    {
        state.enter.substate = EnterState::Wait;
        state.enter.timer = EnterState::StateTime;
    }

    if ( IsUWMain( curRoomId ) )
    {
        doorwayDir = state.enter.scrollDir;
        triggeredDoorDir = Util::GetOppositeDir( doorwayDir );
        if ( GetDoorState( triggeredDoorDir ) )
        {
            triggeredDoorCmd = 2;
        }
        else
            triggeredDoorDir = Dir_None;
    }
    else
        doorwayDir = Dir_None;
}

void WorldImpl::UpdateEnter_Wait()
{
    state.enter.timer--;
    if ( state.enter.timer == 0 )
        state.enter.gotoPlay = true;
}

void WorldImpl::UpdateEnter_FadeIn()
{
    if ( darkRoomFadeStep == 0 )
    {
        state.enter.substate = EnterState::Walk;
    }
    else
    {
        if ( state.enter.timer == 0 )
        {
            darkRoomFadeStep--;
            state.enter.timer = 9;

            for ( int i = 0; i < 2; i++ )
            {
                Graphics::SetPaletteIndexed( i + 2, infoBlock.DarkPaletteSeq[darkRoomFadeStep][i] );
            }
            Graphics::UpdatePalettes();
        }
        else
        {
            state.enter.timer--;
        }
    }
}

void WorldImpl::UpdateEnter_Walk()
{
    if (   player->GetX() == state.enter.targetX 
        && player->GetY() == state.enter.targetY )
        state.enter.gotoPlay = true;
    else
        player->MoveLinear( state.enter.scrollDir, state.enter.playerSpeed );
}

void WorldImpl::UpdateEnter_WalkCave()
{
    if (   player->GetX() == state.enter.targetX 
        && player->GetY() == state.enter.targetY )
        state.enter.gotoPlay = true;
    else
        MovePlayer( state.enter.scrollDir, state.enter.playerSpeed, state.enter.playerFraction );
}

void WorldImpl::DrawEnter()
{
    Graphics::SetClip( 0, TileMapBaseY, TileMapWidth, TileMapHeight );

    if ( state.enter.substate != EnterState::Start )
        DrawRoomNoObjects( state.enter.playerPriority );

    Graphics::ResetClip();
}

void WorldImpl::GotoLoadLevel( int level, bool restartOW )
{
    state.loadLevel.level = level;
    state.loadLevel.substate = LoadLevelState::Load;
    state.loadLevel.timer = 0;
    state.loadLevel.restartOW = restartOW;

    curMode = Mode_LoadLevel;
}

void WorldImpl::SetPlayerExitPosOW( int roomId )
{
    int             row, col;
    OWRoomAttrs&    owRoomAttrs = (OWRoomAttrs&) roomAttrs[roomId];
    RSpot           exitRPos = owRoomAttrs.GetExitPosition();

    col = exitRPos & 0xF;
    row = (exitRPos >> 4) + 4;

    player->SetX( col * TileWidth );
    player->SetY( row * TileHeight + 0xD );
}

// Phase 11.5: the original eastern-peninsula hint is a famous
// bad-translation line. Encoded by hand
// against this port's real NES text format -- verified against known-correct
// decoded strings before writing this: 0x00-0x09 = digits, 0x0A-0x23 = A-Z,
// 0x24/0x25 = space, 0x28 = comma, 0x29 = '!', 0x2A = apostrophe, 0x2C =
// period, 0x2E = '?', 0x2F = '-'; top two bits of a byte are 0x40 (newline)
// or 0xC0 (end of string) rather than part of the character. Two lines,
// centred the same rough way the original's leading spaces do:
//   "  THE SECRET IS ON THE" / " EASTERNMOST PENINSULA."
static const uint8_t s_improvedEastmostSecret[] = {
    0x25, 0x25, 0x1D, 0x11, 0x0E, 0x25, 0x1C, 0x0E, 0x0C, 0x1B, 0x0E, 0x1D,
    0x25, 0x12, 0x1C, 0x25, 0x18, 0x17, 0x25, 0x1D, 0x11, 0x4E,
    0x25, 0x0E, 0x0A, 0x1C, 0x1D, 0x0E, 0x1B, 0x17, 0x16, 0x18, 0x1C, 0x1D,
    0x25, 0x19, 0x0E, 0x17, 0x12, 0x17, 0x1C, 0x1E, 0x15, 0x0A, 0xEC,
};

// Phase 11.5, two more: same bar as EastmostSecret above (a genuinely famous
// confusing line, not just a stylistic nitpick) and the same rule (fix
// grammar/formatting only, never add information the original didn't
// contain -- clearer wording, not new hints).
// The original lost-woods hint (the four directions to the forest maze) -- the
// Lost Woods hint, arguably as notorious as Eastmost Peninsula (players who
// don't already know the "repeat the same direction" trick get stuck for
// hours). Rewrite keeps the exact same four directions and forest-maze
// meaning, just readable comma spacing and a backwards "forest OF maze" ->
// "forest maze":
//   "  GO NORTH, WEST, SOUTH, WEST" / " TO ESCAPE THE FOREST MAZE."
static const uint8_t s_improvedLostWoodsHint[] = {
    0x25, 0x25, 0x10, 0x18, 0x25, 0x17, 0x18, 0x1B, 0x1D, 0x11, 0x28, 0x25,
    0x20, 0x0E, 0x1C, 0x1D, 0x28, 0x25, 0x1C, 0x18, 0x1E, 0x1D, 0x11, 0x28,
    0x25, 0x20, 0x0E, 0x1C, 0x5D,
    0x25, 0x1D, 0x18, 0x25, 0x0E, 0x1C, 0x0C, 0x0A, 0x19, 0x0E, 0x25, 0x1D,
    0x11, 0x0E, 0x25, 0x0F, 0x18, 0x1B, 0x0E, 0x1C, 0x1D, 0x25, 0x16, 0x0A,
    0x23, 0x0E, 0xEC,
};
// The original Level-9 entrance line (no Triforce, no entry) -- the Level-9
// entrance sign. Subject/verb disagreement ("ones who does") is a genuine
// grammar break, not just old-fashioned phrasing. Rewrite: same information
// (own the Triforce or you can't enter -- doesn't add an "8 pieces" count
// the original never specified), correct grammar, split across two lines:
//   " THOSE WHO DO NOT HAVE THE TRIFORCE" / " CANNOT ENTER."
static const uint8_t s_improvedEnterLevel9[] = {
    0x25, 0x1D, 0x11, 0x18, 0x1C, 0x0E, 0x25, 0x20, 0x11, 0x18, 0x25, 0x0D,
    0x18, 0x25, 0x17, 0x18, 0x1D, 0x25, 0x11, 0x0A, 0x1F, 0x0E, 0x25, 0x1D,
    0x11, 0x0E, 0x25, 0x1D, 0x1B, 0x12, 0x0F, 0x18, 0x1B, 0x0C, 0x4E,
    0x25, 0x0C, 0x0A, 0x17, 0x17, 0x18, 0x1D, 0x25, 0x0E, 0x17, 0x1D, 0x0E,
    0x1B, 0xEC,
};

const uint8_t* World::GetString( int stringId )
{
    return sWorld->GetString( stringId );
}

const uint8_t* WorldImpl::GetString( int stringId )
{
    if ( Options::Get().TextVariant != 0 )
    {
        if ( stringId == String_EastmostSecret ) return s_improvedEastmostSecret;
        if ( stringId == String_LostWoodsHint )  return s_improvedLostWoodsHint;
        if ( stringId == String_EnterLevel9 )    return s_improvedEnterLevel9;
    }
    return textTable.GetItem( stringId );
}

void WorldImpl::UpdateLoadLevel()
{
    if ( state.loadLevel.substate == LoadLevelState::Load )
    {
        state.loadLevel.timer = LoadLevelState::StateTime;
        state.loadLevel.substate = LoadLevelState::Wait;

        // Capture the outgoing level/room *before* step 0 overwrites infoBlock,
        // then begin the stepped load — the actual romfs reads run one per Wait
        // frame below (phase 5.1b), not all at once here.
        loadOrigLevel = infoBlock.LevelNumber;
        loadOrigRoomId = curRoomId;
        loadStep = 0;

        Sound::StopAll();
        statusBarVisible = false;
    }
    else if ( state.loadLevel.substate == LoadLevelState::Wait )
    {
        // Spread LoadLevel()'s work across the blank Wait frames: one step per
        // frame, so no single frame pays the whole ~6 ms (phase 5.1b). Only once
        // every step is done do we finalise curRoomId (needs infoBlock from step
        // 0) and then let the timer run out into Unfurl. The unfurl is gated on
        // the steps being complete, so it can never fire on a half-loaded level.
        if ( loadStep < LoadLevelStepCount )
        {
            LoadLevelStep( state.loadLevel.level, loadStep );
            loadStep++;

            if ( loadStep == LoadLevelStepCount )
            {
                // Let the Unfurl game mode load the room and reset colors.
                if ( state.loadLevel.level == 0 )
                {
                    curRoomId = savedOWRoomId;
                    savedOWRoomId = -1;
                    fromUnderground = 2;
                    // unfurledFromDungeon (see UpdateUnfurl()) is set
                    // explicitly by the three call sites that mean "a real
                    // dungeon/cave/cellar exit", not inferred here from
                    // loadOrigLevel -- infoBlock (and loadOrigLevel, copied
                    // from it) is a member of the one long-lived WorldImpl
                    // singleton (World::Init() constructs it exactly once),
                    // so it can carry a stale nonzero LevelNumber left over
                    // from a *previous* profile's dungeon session across into
                    // WorldImpl::Start() for a newly chosen save (die inside
                    // a dungeon -> "give up" to the file menu -> pick another
                    // slot never touches infoBlock in between) -- inferring
                    // from it here would wrongly fire for that unrelated
                    // load.
                }
                else
                {
                    curRoomId = infoBlock.StartRoomId;
                    if ( loadOrigLevel == 0 )
                        savedOWRoomId = loadOrigRoomId;
                }
            }
            return;
        }

        if ( state.loadLevel.timer == 0 )
        {
            GotoUnfurl( state.loadLevel.restartOW );
            return;
        }

        state.loadLevel.timer--;
    }
}

void WorldImpl::DrawLoadLevel()
{
    Graphics::SetClip( 0, 0, StdViewWidth, StdViewHeight );
    ClearScreen();
    Graphics::ResetClip();
}

void WorldImpl::GotoUnfurl( bool restartOW )
{
    state.unfurl.substate = UnfurlState::Start;
    state.unfurl.timer = UnfurlState::StateTime;
    state.unfurl.stepTimer = 0;
    state.unfurl.left = 0x80;
    state.unfurl.right = 0x80;
    state.unfurl.restartOW = restartOW;

    ClearLevelData();

    curMode = Mode_Unfurl;
}

void WorldImpl::UpdateUnfurl()
{
    if ( state.unfurl.substate == UnfurlState::Start )
    {
        state.unfurl.substate = UnfurlState::Unfurl;
        statusBarVisible = true;
        statusBar.EnableFeatures( StatusBar::Feature_All, false );

        if ( infoBlock.LevelNumber == 0 && !state.unfurl.restartOW )
        {
            LoadRoom( curRoomId, 0 );

            if ( unfurledFromDungeon )
            {
                // Walking/being sent out of a dungeon (or cave/
                // cellar) -- always the dungeon's own marked exit tile, never
                // the stale "where you entered this OW screen" checkpoint
                // (which could be a completely different edge of the screen,
                // reachable from any of several neighbouring rooms). This is
                // curRoomId's *own* GetExitPosition(), i.e. the dungeon's
                // entrance tile -- guaranteed walkable (it's where you walked
                // in from), unlike the phase 8.4c "respawn in the bushes" case below,
                // which was a different room's unrelated exit position.
                SetPlayerExitPosOW( curRoomId );
                unfurledFromDungeon = false;
            }
            // phase 8.4c: respawn where you safely entered this screen, not at
            // the room's cave-exit tile (which SetPlayerExitPosOW returns and
            // can sit inside bushes/impassable terrain). checkpointX/Y is
            // captured on every overworld screen entry (AutoSave); fall back to
            // the exit position only if it was never set.
            else if ( checkpointX >= 0 && checkpointY >= 0 )
            {
                player->SetX( checkpointX );
                player->SetY( checkpointY );
            }
            else
            {
                SetPlayerExitPosOW( curRoomId );
            }
        }
        else
        {
            LoadRoom( infoBlock.StartRoomId, 0 );
            player->SetX( StartX );
            player->SetY( infoBlock.StartY );
        }

        for ( int i = 0; i < LevelInfoBlock::LevelPaletteCount; i++ )
        {
            Graphics::SetPaletteIndexed( i, infoBlock.Palettes[i] );
        }

        SetPlayerColor();
        Graphics::UpdatePalettes();
        return;
    }

    if ( state.unfurl.timer > 0 )
    {
        state.unfurl.timer--;
        return;
    }

    if ( state.unfurl.left == 0 )
    {
        statusBar.EnableFeatures( StatusBar::Feature_EquipmentAndMap, true );
        if ( !IsOverworld() )
            Sound::PlaySong( infoBlock.Song, Sound::MainSongStream, true );
        GotoEnter( Dir_Up );
        return;
    }

    if ( state.unfurl.stepTimer == 0 )
    {
        state.unfurl.left -= 8;
        state.unfurl.right += 8;
        state.unfurl.stepTimer = 4;
    }
    else
    {
        state.unfurl.stepTimer--;
    }
}

void WorldImpl::DrawUnfurl()
{
    if ( state.unfurl.substate == UnfurlState::Start )
        return;

    int width = state.unfurl.right - state.unfurl.left;

    Graphics::SetClip( 0, TileMapBaseY, TileMapWidth, TileMapHeight );
    ClearScreen();
    Graphics::ResetClip();

    Graphics::SetClip( state.unfurl.left, TileMapBaseY, width, TileMapHeight );
    DrawRoomNoObjects( SpritePri_None );
    Graphics::ResetClip();
}

void World::EndLevel()
{
    sWorld->GotoEndLevel();
}

void WorldImpl::GotoEndLevel()
{
    state.endLevel.substate = EndLevelState::Start;
    curMode = Mode_EndLevel;
}

void WorldImpl::UpdateEndLevel()
{
    UpdateFunc update = sEndLevelFuncs[state.endLevel.substate];
    (this->*update)();
}

void WorldImpl::UpdateEndLevel_Start()
{
    state.endLevel.substate = EndLevelState::Wait1;
    state.endLevel.timer = EndLevelState::Wait1Time;

    state.endLevel.left = 0;
    state.endLevel.right = World::TileMapWidth;
    state.endLevel.stepTimer = 4;

    statusBar.EnableFeatures( StatusBar::Feature_Equipment, false );
    Sound::PlaySong( Song_triforce, Sound::MainSongStream, false );
}

void WorldImpl::UpdateEndLevel_Wait()
{
    if ( state.endLevel.timer == 0 )
    {
        if ( state.endLevel.substate == EndLevelState::Wait3 )
        {
            // Finished the dungeon (boss defeated) -- also a real dungeon
            // exit, see unfurledFromDungeon's comment.
            unfurledFromDungeon = true;
            GotoLoadLevel( 0 );
        }
        else
        {
            state.endLevel.substate = (EndLevelState::Substate) (state.endLevel.substate + 1);
            if ( state.endLevel.substate == EndLevelState::Flash )
                state.endLevel.timer = EndLevelState::FlashTime;
        }
    }
    else
        state.endLevel.timer--;
}

void WorldImpl::UpdateEndLevel_Flash()
{
    if ( state.endLevel.timer == 0 )
        state.endLevel.substate = (EndLevelState::Substate) (state.endLevel.substate + 1);
    else
    {
        int step = state.endLevel.timer & 0x7;
        if ( step == 0 )
            SetFlashPalette();
        else if ( step == 3 )
            SetLevelPalette();
        state.endLevel.timer--;
    }
}

void WorldImpl::UpdateEndLevel_FillHearts()
{
    int maxHeartValue = profile.GetMaxHeartsValue();

    Sound::PlayEffect( SEffect_character );

    if ( profile.Hearts == maxHeartValue )
    {
        state.endLevel.substate = (EndLevelState::Substate) (state.endLevel.substate + 1);
        state.endLevel.timer = EndLevelState::Wait2Time;
    }
    else
    {
        FillHearts( 6 );
    }
}

void WorldImpl::UpdateEndLevel_Furl()
{
    if ( state.endLevel.left == WorldMidX )
    {
        state.endLevel.substate = (EndLevelState::Substate) (state.endLevel.substate + 1);
        state.endLevel.timer = EndLevelState::Wait3Time;
    }
    else if ( state.endLevel.stepTimer == 0 )
    {
        state.endLevel.left += 8;
        state.endLevel.right -= 8;
        state.endLevel.stepTimer = 4;
    }
    else
    {
        state.endLevel.stepTimer--;
    }
}

void WorldImpl::DrawEndLevel()
{
    int left    = 0;
    int width   = TileMapWidth;

    if ( state.endLevel.substate >= EndLevelState::Furl )
    {
        left = state.endLevel.left;
        width = state.endLevel.right - state.endLevel.left;

        Graphics::SetClip( 0, TileMapBaseY, TileMapWidth, TileMapHeight );
        ClearScreen();
        Graphics::ResetClip();

    }

    Graphics::SetClip( left, TileMapBaseY, width, TileMapHeight );
    DrawRoomNoObjects( SpritePri_None );
    Graphics::ResetClip();

    DrawLinkLiftingItem( Item_TriforcePiece );
}

void World::WinGame()
{
    sWorld->GotoWinGame();
}

void WorldImpl::GotoWinGame()
{
    // phase 14.1: winning quest 1 (Ganon defeated, Zelda reached) on *any*
    // slot permanently unlocks the quest-2 option everywhere -- see
    // Options.h's Quest2Unlocked comment for why this lives in the global
    // GameOptions rather than per-profile. Quest 2's own win doesn't need
    // to (re-)set it: it's already unlocked by definition if you're playing
    // quest 2 at all.
    if ( profile.Quest == 0 && !Options::Get().Quest2Unlocked )
    {
        Options::Get().Quest2Unlocked = 1;
        Options::Save();
    }

    state.winGame.substate = WinGameState::Start;
    state.winGame.timer = 162;
    state.winGame.left = 0;
    state.winGame.right = World::TileMapWidth;
    state.winGame.stepTimer = 0;
    state.winGame.npcVisual = WinGameState::Npc_Stand;

    curMode = Mode_WinGame;
}

void WorldImpl::UpdateWinGame()
{
    UpdateFunc update = sWinGameFuncs[state.winGame.substate];
    (this->*update)();
}

void WorldImpl::UpdateWinGame_Start()
{
    if ( state.winGame.timer > 0 )
    {
        state.winGame.timer--;
    }
    else if ( state.winGame.left == WorldMidX )
    {
        state.winGame.substate = WinGameState::Text1;
        statusBar.EnableFeatures( StatusBar::Feature_EquipmentAndMap, false );

        // A959
        const static uint8_t str1[] = {
    0x1d, 0x11, 0x0a, 0x17, 0x14, 0x1c, 0x24, 0x15, 0x12, 0x17, 0x14, 0x28, 0x22, 0x18, 0x1e, 0x2a,
    0x1b, 0x8e, 0x64, 0x1d, 0x11, 0x0e, 0x24, 0x11, 0x0e, 0x1b, 0x18, 0x24, 0x18, 0x0f, 0x24, 0x11,
    0x22, 0x1b, 0x1e, 0x15, 0x0e, 0xec
        };
        textBox1 = new TextBox( str1 );
    }
    else if ( state.winGame.stepTimer == 0 )
    {
        state.winGame.left += 8;
        state.winGame.right -= 8;
        state.winGame.stepTimer = 4;
    }
    else
    {
        state.winGame.stepTimer--;
    }
}

void WorldImpl::UpdateWinGame_Text1()
{
    textBox1->Update();
    if ( textBox1->IsDone() )
    {
        state.winGame.substate = WinGameState::Stand;
        state.winGame.timer = 76;
    }
}

void WorldImpl::UpdateWinGame_Stand()
{
    state.winGame.timer--;
    if ( state.winGame.timer == 0 )
    {
        state.winGame.substate = WinGameState::Hold1;
        state.winGame.timer = 64;
    }
}

void WorldImpl::UpdateWinGame_Hold1()
{
    state.winGame.npcVisual = WinGameState::Npc_Lift;
    state.winGame.timer--;
    if ( state.winGame.timer == 0 )
    {
        state.winGame.substate = WinGameState::Colors;
        state.winGame.timer = 127;
    }
}

void WorldImpl::UpdateWinGame_Colors()
{
    state.winGame.timer--;
    if ( state.winGame.timer == 0 )
    {
        state.winGame.substate = WinGameState::Hold2;
        state.winGame.timer = 131;
        Sound::PlaySong( Song_ending, Sound::MainSongStream, true );
    }
}

void WorldImpl::UpdateWinGame_Hold2()
{
    state.winGame.timer--;
    if ( state.winGame.timer == 0 )
    {
        // AB07
        const static uint8_t str2[] = {
    0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 0x25, 
    0x0f, 0x12, 0x17, 0x0a, 0x15, 0x15, 0x22, 0x28, 
    0xa5, 0x65,
    0x19, 0x0e, 0x0a, 0x0c, 0x0e, 0x24, 0x1b, 0x0e,
    0x1d, 0x1e, 0x1b, 0x17, 0x1c, 0x24, 0x1d, 0x18, 0x24, 0x11, 0x22, 0x1b, 0x1e, 0x15, 0x0e, 0x2c,
    0xa5, 0x65, 0x65, 0x25, 0x25,
    0x1d, 0x11, 0x12, 0x1c, 0x24, 0x0e, 0x17, 0x0d, 0x1c, 0x24, 0x1d, 0x11, 0x0e, 0x24, 0x1c, 0x1d,
    0x18, 0x1b, 0x22, 0x2c, 0xe5
        };

        state.winGame.substate = WinGameState::Text2;
        textBox2 = new TextBox( str2, 8 );
        textBox2->SetY( WinGameState::TextBox2Top );
    }
}

void WorldImpl::UpdateWinGame_Text2()
{
    textBox2->Update();
    if ( textBox2->IsDone() )
    {
        state.winGame.substate = WinGameState::Hold3;
        state.winGame.timer = 129;
    }
}

void WorldImpl::UpdateWinGame_Hold3()
{
    state.winGame.timer--;
    if ( state.winGame.timer == 0 )
    {
        state.winGame.substate = WinGameState::NoObjects;
        state.winGame.timer = 32;
    }
}

void WorldImpl::UpdateWinGame_NoObjects()
{
    state.winGame.npcVisual = WinGameState::Npc_None;
    state.winGame.timer--;
    if ( state.winGame.timer == 0 )
    {
        credits = new Credits();
        state.winGame.substate = WinGameState::Credits;
    }
}

void WorldImpl::UpdateWinGame_Credits()
{
    TextBox** boxes[] = { &textBox1, &textBox2 };
    int startYs[] = { TextBox::StartY, WinGameState::TextBox2Top };

    for ( int i = 0; i < _countof( boxes ); i++ )
    {
        TextBox* box = *boxes[i];
        if ( box != nullptr )
        {
            int textToCreditsY = Credits::StartY - startYs[i];
            box->SetY( credits->GetTop() - textToCreditsY );
            int bottom = box->GetY() + box->GetHeight();
            if ( bottom <= 0 )
            {
                delete box;
                *boxes[i] = nullptr;
            }
        }
    }

    credits->Update();
    if ( credits->IsDone() )
    {
        if ( Input::IsButtonPressing( InputButtons::Start ) )
        {
            delete credits;
            credits = nullptr;
            delete player;
            player = nullptr;
            DeleteObjects();
            submenuOffsetY = 0;
            statusBarVisible = false;
            statusBar.EnableFeatures( StatusBar::Feature_All, true );

            uint8_t name[MaxNameLength];
            uint8_t nameLength = profile.NameLength;
            uint8_t deaths = profile.Deaths;
            memcpy( name, profile.Name, nameLength );
            memset( &profile, 0, sizeof profile );
            memcpy( profile.Name, name, nameLength );
            profile.NameLength = nameLength;
            profile.Deaths = deaths;
            profile.Quest = 1;
            profile.Items[ItemSlot_HeartContainers] = DefaultHearts;
            profile.Items[ItemSlot_MaxBombs] = DefaultBombs;
            SaveFolder::WriteProfile( profileSlot, profile );

            Sound::StopAll();
            GotoFileMenu();
        }
    }
    else
    {
        int statusTop = credits->GetTop() - Credits::StartY;
        int statusBottom = statusTop + StatusBar::StatusBarHeight;
        if ( statusBottom > 0 )
            submenuOffsetY = statusTop;
        else
            submenuOffsetY = -StatusBar::StatusBarHeight;
    }
}

void WorldImpl::DrawWinGame()
{
    ALLEGRO_COLOR backColor;

    Graphics::SetClip( 0, 0, StdViewWidth, StdViewHeight );
    if ( state.winGame.substate == WinGameState::Colors )
    {
        int sysColors[] = { 0x0F, 0x2A, 0x16, 0x12 };
        int frame = state.winGame.timer & 3;
        int sysColor = sysColors[frame];
        ClearScreen( sysColor );
        backColor = Graphics::GetSystemColor( sysColor );
    }
    else
    {
        ClearScreen();
        backColor = al_map_rgb( 0, 0, 0 );
    }
    Graphics::ResetClip();

    statusBar.Draw( submenuOffsetY, backColor );

    if ( state.winGame.substate == WinGameState::Start )
    {
        int left    = state.winGame.left;
        int width   = state.winGame.right - state.winGame.left;

        Graphics::SetClip( left, TileMapBaseY, width, TileMapHeight );
        DrawRoomNoObjects( SpritePri_None );
        Graphics::ResetClip();

        player->Draw();
        DrawObjects();
    }
    else
    {
        Object* zelda = objects[MonsterSlot1];

        if ( state.winGame.npcVisual == WinGameState::Npc_Stand )
        {
            zelda->Draw();
            player->Draw();
        }
        else if ( state.winGame.npcVisual == WinGameState::Npc_Lift )
        {
            DrawZeldaLiftingTriforce( zelda->GetX(), zelda->GetY() );
            DrawLinkLiftingItem( Item_TriforcePiece );
        }

        if ( credits != nullptr )
            credits->Draw();
        if ( textBox1 != nullptr )
            textBox1->Draw();
        if ( textBox2 != nullptr )
            textBox2->Draw();
    }
}

void WorldImpl::GotoStairs( int tileRef )
{
    state.stairs.substate = StairsState::Start;
    state.stairs.tileRef = tileRef;
    state.stairs.playerPriority = SpritePri_AboveBg;

    curMode = Mode_Stairs;
}

void WorldImpl::UpdateStairsState()
{
    if ( state.stairs.substate == StairsState::Start )
    {
        state.stairs.playerPriority = SpritePri_BelowBg;

        if ( IsOverworld() )
            Sound::StopAll();

        if ( state.stairs.tileRef == Tile_Cave )
        {
            player->SetFacing( Dir_Up );

            state.stairs.targetX = player->GetX();
            state.stairs.targetY = player->GetY() + 0x10;
            state.stairs.scrollDir = Dir_Down;
            state.stairs.playerSpeed = 0x40;
            state.stairs.playerFraction = 0;

            state.stairs.substate = StairsState::WalkCave;
            Sound::PlayEffect( SEffect_stairs );
        }
        else
        {
            state.stairs.substate = StairsState::Walk;
        }
    }
    else if ( state.stairs.substate == StairsState::Walk )
    {
        if ( IsOverworld() )
        {
            OWRoomAttrs& owRoomAttrs = (OWRoomAttrs&) roomAttrs[curRoomId];
            int cave = owRoomAttrs.GetCaveId();

            if ( cave <= 9 )
                GotoLoadLevel( cave );
            else
                GotoPlayCave();
        }
        else
            GotoPlayCellar();
    }
    else if ( state.stairs.substate == StairsState::WalkCave )
    {
        if (  player->GetX() == state.stairs.targetX 
           && player->GetY() == state.stairs.targetY )
        {
            OWRoomAttrs& owRoomAttrs = (OWRoomAttrs&) roomAttrs[curRoomId];
            int cave = owRoomAttrs.GetCaveId();

            if ( cave <= 9 )
                GotoLoadLevel( cave );
            else
                GotoPlayCave();
        }
        else
        {
            MovePlayer( state.stairs.scrollDir, state.stairs.playerSpeed, state.stairs.playerFraction );
            player->GetAnimator()->Advance();
        }
    }
}

void WorldImpl::DrawStairsState()
{
    Graphics::SetClip( 0, TileMapBaseY, TileMapWidth, TileMapHeight );
    DrawRoomNoObjects( state.stairs.playerPriority );
    Graphics::ResetClip();
}

void WorldImpl::GotoPlayCellar()
{
    state.playCellar.substate = PlayCellarState::Start;
    state.playCellar.playerPriority = SpritePri_None;

    curMode = Mode_InitPlayCellar;
}

void WorldImpl::UpdatePlayCellar()
{
    UpdateFunc update = sPlayCellarFuncs[state.playCellar.substate];
    (this->*update)();
}

void WorldImpl::UpdatePlayCellar_Start()
{
    state.playCellar.substate = PlayCellarState::FadeOut;
    state.playCellar.fadeTimer = 11;
    state.playCellar.fadeStep = 0;
}

void WorldImpl::UpdatePlayCellar_FadeOut()
{
    if ( state.playCellar.fadeTimer == 0 )
    {
        for ( int i = 0; i < LevelInfoBlock::FadePals; i++ )
        {
            int step = state.playCellar.fadeStep;
            Graphics::SetPaletteIndexed( i + 2, infoBlock.OutOfCellarPaletteSeq[step][i] );
        }
        Graphics::UpdatePalettes();
        state.playCellar.fadeTimer = 9;
        state.playCellar.fadeStep++;

        if ( state.playCellar.fadeStep == LevelInfoBlock::FadeLength )
            state.playCellar.substate = PlayCellarState::LoadRoom;
    }
    else
    {
        state.playCellar.fadeTimer--;
    }
}

void WorldImpl::UpdatePlayCellar_LoadRoom()
{
    bool isLeft;
    int roomId = FindCellarRoomId( curRoomId, isLeft );

    if ( roomId >= 0 )
    {
        int x = isLeft ? 0x30 : 0xC0;

        LoadRoom( roomId, 0 );

        player->SetX( x );
        player->SetY( 0x44 );
        player->SetFacing( Dir_Down );

        state.playCellar.targetY = 0x60;
        state.playCellar.substate = PlayCellarState::FadeIn;
        state.playCellar.fadeTimer = 35;
        state.playCellar.fadeStep = 3;
    }
    else
    {
        GotoPlay();
    }
}

void WorldImpl::UpdatePlayCellar_FadeIn()
{
    if ( state.playCellar.fadeTimer == 0 )
    {
        for ( int i = 0; i < LevelInfoBlock::FadePals; i++ )
        {
            int step = state.playCellar.fadeStep;
            Graphics::SetPaletteIndexed( i + 2, infoBlock.InCellarPaletteSeq[step][i] );
        }
        Graphics::UpdatePalettes();
        state.playCellar.fadeTimer = 9;
        state.playCellar.fadeStep--;

        if ( state.playCellar.fadeStep < 0 )
            state.playCellar.substate = PlayCellarState::Walk;
    }
    else
    {
        state.playCellar.fadeTimer--;
    }
}

void WorldImpl::UpdatePlayCellar_Walk()
{
    state.playCellar.playerPriority = SpritePri_AboveBg;

    if ( player->GetY() == state.playCellar.targetY )
    {
        fromUnderground = 1;
        GotoPlay( PlayState::Cellar );
    }
    else
    {
        player->MoveLinear( Dir_Down, Player::WalkSpeed );
        player->GetAnimator()->Advance();
    }
}

void WorldImpl::DrawPlayCellar()
{
    Graphics::SetClip( 0, TileMapBaseY, TileMapWidth, TileMapHeight );
    DrawRoomNoObjects( state.playCellar.playerPriority );
    Graphics::ResetClip();
}

void WorldImpl::GotoLeaveCellar()
{
    state.leaveCellar.substate = LeaveCellarState::Start;

    curMode = Mode_LeaveCellar;
}

void WorldImpl::UpdateLeaveCellar()
{
    UpdateFunc update = sLeaveCellarFuncs[state.leaveCellar.substate];
    (this->*update)();
}

void WorldImpl::UpdateLeaveCellar_Start()
{
    if ( IsOverworld() )
    {
        state.leaveCellar.substate = LeaveCellarState::Wait;
        state.leaveCellar.timer = 29;
    }
    else
    {
        state.leaveCellar.substate = LeaveCellarState::FadeOut;
        state.leaveCellar.fadeTimer = 11;
        state.leaveCellar.fadeStep = 0;
    }
}

void WorldImpl::UpdateLeaveCellar_FadeOut()
{
    if ( state.leaveCellar.fadeTimer == 0 )
    {
        for ( int i = 0; i < LevelInfoBlock::FadePals; i++ )
        {
            int step = state.leaveCellar.fadeStep;
            Graphics::SetPaletteIndexed( i + 2, infoBlock.InCellarPaletteSeq[step][i] );
        }
        Graphics::UpdatePalettes();
        state.leaveCellar.fadeTimer = 9;
        state.leaveCellar.fadeStep++;

        if ( state.leaveCellar.fadeStep == LevelInfoBlock::FadeLength )
            state.leaveCellar.substate = LeaveCellarState::LoadRoom;
    }
    else
    {
        state.leaveCellar.fadeTimer--;
    }
}

void WorldImpl::UpdateLeaveCellar_LoadRoom()
{
    UWRoomAttrs& uwRoomAttrs = (UWRoomAttrs&) roomAttrs[curRoomId];
    int nextRoomId;

    if ( player->GetX() < 0x80 )
        nextRoomId = uwRoomAttrs.GetLeftCellarExitRoomId();
    else
        nextRoomId = uwRoomAttrs.GetRightCellarExitRoomId();

    LoadRoom( nextRoomId, 0 );

    player->SetX( 0x60 );
    player->SetY( 0xA0 );
    player->SetFacing( Dir_Down );

    state.leaveCellar.substate = LeaveCellarState::FadeIn;
    state.leaveCellar.fadeTimer = 35;
    state.leaveCellar.fadeStep = 3;
}

void WorldImpl::UpdateLeaveCellar_FadeIn()
{
    if ( state.leaveCellar.fadeTimer == 0 )
    {
        for ( int i = 0; i < LevelInfoBlock::FadePals; i++ )
        {
            int step = state.leaveCellar.fadeStep;
            Graphics::SetPaletteIndexed( i + 2, infoBlock.OutOfCellarPaletteSeq[step][i] );
        }
        Graphics::UpdatePalettes();
        state.leaveCellar.fadeTimer = 9;
        state.leaveCellar.fadeStep--;

        if ( state.leaveCellar.fadeStep < 0 )
            state.leaveCellar.substate = LeaveCellarState::Walk;
    }
    else
    {
        state.leaveCellar.fadeTimer--;
    }
}

void WorldImpl::UpdateLeaveCellar_Walk()
{
    GotoEnter( Dir_None );
}

void WorldImpl::UpdateLeaveCellar_Wait()
{
    if ( state.leaveCellar.timer == 0 )
    {
        state.leaveCellar.substate = LeaveCellarState::LoadOverworldRoom;
    }
    else
    {
        state.leaveCellar.timer--;
    }
}

void WorldImpl::UpdateLeaveCellar_LoadOverworldRoom()
{
    for ( int i = 0; i < 2; i++ )
    {
        Graphics::SetPaletteIndexed( i + 2, infoBlock.Palettes[i + 2] );
    }
    Graphics::UpdatePalettes();

    LoadRoom( curRoomId, 0 );
    SetPlayerExitPosOW( curRoomId );
    GotoEnter( Dir_None );
    player->SetFacing( Dir_Down );
}

void WorldImpl::DrawLeaveCellar()
{
    Graphics::SetClip( 0, TileMapBaseY, TileMapWidth, TileMapHeight );

    if ( state.leaveCellar.substate == LeaveCellarState::Start )
        ;
    else if ( state.leaveCellar.substate == LeaveCellarState::Wait 
        || state.leaveCellar.substate == LeaveCellarState::LoadOverworldRoom )
        ClearScreen();
    else
        DrawRoomNoObjects( SpritePri_None );

    Graphics::ResetClip();
}

void WorldImpl::GotoPlayCave()
{
    state.playCave.substate = PlayCaveState::Start;

    curMode = Mode_InitPlayCave;
}

void WorldImpl::UpdatePlayCave()
{
    UpdateFunc update = sPlayCaveFuncs[state.playCave.substate];
    (this->*update)();
}

void WorldImpl::UpdatePlayCave_Start()
{
    state.playCave.substate = PlayCaveState::Wait;
    state.playCave.timer = 27;
}

void WorldImpl::UpdatePlayCave_Wait()
{
    if ( state.playCave.timer == 0 )
        state.playCave.substate = PlayCaveState::LoadRoom;
    else
        state.playCave.timer--;
}

void WorldImpl::UpdatePlayCave_LoadRoom()
{
    const PaletteSet* paletteSet = (PaletteSet*) extraData.GetItem( Extra_CavePalettes );
    int caveLayout;

    if ( FindSparseFlag( Sparse_Shortcut, curRoomId ) )
        caveLayout = Cave_Shortcut;
    else
        caveLayout = Cave_Items;

    LoadCaveRoom( caveLayout );

    state.playCave.substate = PlayCaveState::Walk;
    state.playCave.targetY = 0xD5;

    player->SetX( 0x70 );
    player->SetY( 0xDD );
    player->SetFacing( Dir_Up );

    for ( int i = 0; i < 2; i++ )
    {
        Graphics::SetPaletteIndexed( i + 2, paletteSet->Palettes[i] );
    }
    Graphics::UpdatePalettes();
}

void WorldImpl::UpdatePlayCave_Walk()
{
    if ( player->GetY() == state.playCave.targetY )
    {
        fromUnderground = 1;
        GotoPlay( PlayState::Cave );
    }
    else
    {
        player->MoveLinear( Dir_Up, Player::WalkSpeed );
        player->GetAnimator()->Advance();
    }
}

void WorldImpl::DrawPlayCave()
{
    Graphics::SetClip( 0, TileMapBaseY, TileMapWidth, TileMapHeight );

    if ( state.playCave.substate == PlayCaveState::Wait 
        || state.playCave.substate == PlayCaveState::LoadRoom )
    {
        ClearScreen();
    }
    else if ( state.playCave.substate == PlayCaveState::Walk )
    {
        DrawRoomNoObjects();
    }

    Graphics::ResetClip();
}

void WorldImpl::GotoDie()
{
    state.death.substate = DeathState::Start;

    curMode = Mode_Death;
}

void WorldImpl::UpdateDie()
{
    // ORIGINAL: Some of these are handled with object timers.
    if ( state.death.timer > 0 )
        state.death.timer--;

    UpdateFunc update = sDeathFuncs[state.death.substate];
    (this->*update)();
}

void WorldImpl::UpdateDie_Start()
{
    player->SetInvincibilityTimer( 0x10 );
    state.death.timer = 0x20;
    state.death.substate = DeathState::Flash;
    Sound::StopEffects();
    Sound::PlaySong( Song_death, Sound::MainSongStream, false );
}

void WorldImpl::UpdateDie_Flash()
{
    player->DecInvincibleTimer();

    if ( state.death.timer == 0 )
    {
        state.death.timer = 6;
        state.death.substate = DeathState::Wait1;
    }
}

void WorldImpl::UpdateDie_Wait1()
{
    // TODO: the last 2 frames make the whole play area use palette 3.

    if ( state.death.timer == 0 )
    {
        static const uint8_t redPals[2][4] = 
        {
            { 0x0F, 0x17, 0x16, 0x26 },
            { 0x0F, 0x17, 0x16, 0x26 }
        };

        SetLevelPalettes( redPals );

        state.death.step = 16;
        state.death.timer = 0;
        state.death.substate = DeathState::Turn;
    }
}

void WorldImpl::UpdateDie_Turn()
{
    if ( state.death.step == 0 )
    {
        state.death.step = 4;
        state.death.timer = 0;
        state.death.substate = DeathState::Fade;
    }
    else
    {
        if ( state.death.timer == 0 )
        {
            state.death.timer = 5;
            state.death.step--;

            static const Direction dirs[] = 
            {
                Dir_Down,
                Dir_Left,
                Dir_Up,
                Dir_Right,
            };

            Direction dir = dirs[state.death.step & 3];
            player->SetFacing( dir );
        }
    }
}

void WorldImpl::UpdateDie_Fade()
{
    if ( state.death.step == 0 )
    {
        state.death.substate = DeathState::GrayLink;
    }
    else
    {
        if ( state.death.timer == 0 )
        {
            state.death.timer = 10;
            state.death.step--;

            int seq = 3 - state.death.step;

            SetLevelPalettes( infoBlock.DeathPaletteSeq[seq] );
        }
    }
}

void WorldImpl::UpdateDie_GrayLink()
{
    static const uint8_t grayPal[4] = { 0, 0x10, 0x30, 0 };

    Graphics::SetPaletteIndexed( PlayerPalette, grayPal );
    Graphics::UpdatePalettes();

    state.death.substate = DeathState::Spark;
    state.death.timer = 0x18;
    state.death.step = 0;
}

void WorldImpl::UpdateDie_Spark()
{
    if ( state.death.timer == 0 )
    {
        if ( state.death.step == 0 )
        {
            state.death.timer = 10;
            Sound::PlayEffect( SEffect_character );
        }
        else if ( state.death.step == 1 )
        {
            state.death.timer = 4;
        }
        else
        {
            state.death.substate = DeathState::Wait2;
            state.death.timer = 46;
        }
        state.death.step++;
    }
}

void WorldImpl::UpdateDie_Wait2()
{
    if ( state.death.timer == 0 )
    {
        state.death.substate = DeathState::GameOver;
        state.death.timer = 0x60;
    }
}

void WorldImpl::UpdateDie_GameOver()
{
    if ( state.death.timer == 0 )
    {
        profile.Deaths++;
        GotoContinueQuestion();
    }
}

void WorldImpl::DrawDie()
{
    if ( state.death.substate < DeathState::GameOver )
    {
        Graphics::SetClip( 0, TileMapBaseY, TileMapWidth, TileMapHeight );
        DrawRoomNoObjects( SpritePri_None );
        Graphics::ResetClip();

        if ( state.death.substate == DeathState::Spark && state.death.step > 0 )
        {
            DrawSparkle( player->GetX(), player->GetY(), BlueFgPalette, state.death.step - 1 );
        }
        else if ( state.death.substate <= DeathState::Spark )
        {
            player->Draw();
        }
    }
    else
    {
        static const uint8_t GameOver[] = { 0x10, 0x0A, 0x16, 0x0E, 0x24, 0x18, 0x1F, 0x0E, 0x1B };
#ifdef __3DS__
        // Centred in the full play view (see WorldImpl::Draw's 3DS view note):
        // 9 chars * 8px = 72 wide -> x=(256-72)/2=92; vertically centred in the
        // 64..240 play area.
        DrawString( GameOver, sizeof GameOver, 92, 148, 0 );
#else
        DrawString( GameOver, sizeof GameOver, 0x60, 0x90, 0 );
#endif
    }
}

#ifdef __3DS__
// 3DS death-continue choice, set by the bottom-screen touch panel's handler
// (LiveMap_state.cpp) and consumed in UpdateContinueQuestion below. 0 = none
// yet, 1 = continue where you died, 2 = continue from the start tile.
static int s_continueChoice3ds = 0;
void World::ChooseContinue( int choice ) { s_continueChoice3ds = choice; }
#endif

void WorldImpl::GotoContinueQuestion()
{
    state.continueQuestion.substate = ContinueState::Start;
    state.continueQuestion.selectedIndex = 0;
#ifdef __3DS__
    s_continueChoice3ds = 0;   // clear any stale tap from a previous death
#endif

    curMode = Mode_ContinueQuestion;
}

void WorldImpl::UpdateContinueQuestion()
{
    if ( state.continueQuestion.substate == ContinueState::Start )
    {
        statusBarVisible = false;
        Sound::PlaySong( Song_game_over, Sound::MainSongStream, true );
        state.continueQuestion.substate = ContinueState::Idle;
    }
    else if ( state.continueQuestion.substate == ContinueState::Idle )
    {
#ifdef __3DS__
        // Modern bottom-screen touch panel drives the choice (see World.h's
        // ChooseContinue). selectedIndex 0 = continue here, 3 = from the start
        // tile (a 3DS-only extra option; 1/2 are the desktop Save/Retry).
        if ( s_continueChoice3ds != 0 )
        {
            state.continueQuestion.selectedIndex = ( s_continueChoice3ds == 2 ) ? 3 : 0;
            s_continueChoice3ds = 0;
            state.continueQuestion.substate = ContinueState::Chosen;
            state.continueQuestion.timer = 0x40;
        }
#else
        if ( Input::IsButtonPressing( InputButtons::Select ) )
        {
            state.continueQuestion.selectedIndex++;
            if ( state.continueQuestion.selectedIndex == 3 )
                state.continueQuestion.selectedIndex = 0;
        }
        else if ( Input::IsButtonPressing( InputButtons::Start ) )
        {
            state.continueQuestion.substate = ContinueState::Chosen;
            state.continueQuestion.timer = 0x40;
        }
#endif
    }
    else if ( state.continueQuestion.substate == ContinueState::Chosen )
    {
        if ( state.continueQuestion.timer == 0 )
        {
            statusBarVisible = true;
            Sound::StopAll();

            if ( state.continueQuestion.selectedIndex == 0
                || state.continueQuestion.selectedIndex == 3 )
            {
                // index 0 = "Continue" (where you died); index 3 = "Continue
                // from start tile" on the overworld, or "Exit dungeon" while
                // inside a dungeon (3DS modern panel, phase 8.4 follow-up --
                // playtest fixes, see phase 8.4d). So,
                // that the OW song is played in the Enter mode.
                fromUnderground = 2;
                delete player;
                player = new Player();

                bool secondButton = state.continueQuestion.selectedIndex == 3;
                bool overworld = IsOverworld();

                // This game has no reliable way to
                // heal outside of items you may not have, so a checkpoint
                // restore that leaves you at a sliver of health (or even
                // exactly the fraction you died with, which used to only get
                // floored at *exactly* 0) risks an unwinnable death spiral.
                // Floor every checkpoint-hearts restore at one full heart;
                // the two "start over" options (from-start on the overworld,
                // dungeon continue always restarting at the entrance) go
                // further and always give full hearts.
                if ( !overworld && secondButton )
                {
                    // "Exit dungeon": same destination the normal walk-out-
                    // the-entrance path uses (savedOWRoomId, set when this
                    // dungeon was entered) -- GotoLoadLevel(0) is exactly
                    // that path's own call. Heading back into open
                    // exploration, not a dungeon restart, so the floor
                    // (not a flat full refill) applies here too.
                    profile.Hearts = checkpointHearts;
                    if ( profile.Hearts < Profile::GetMaxHeartsValue( 1 ) )
                        profile.Hearts = Profile::GetMaxHeartsValue( 1 );
                    // Also a real dungeon exit -- see unfurledFromDungeon's
                    // comment.
                    unfurledFromDungeon = true;
                    GotoLoadLevel( 0 );
                }
                else if ( !overworld )
                {
                    // Dungeon "Continue": always restarts at the entrance
                    // regardless of which button (classic behaviour, see the
                    // GotoUnfurl comment below) -- so always a full refill
                    // here, not the 1-heart floor.
                    profile.Hearts = profile.GetMaxHeartsValue();
                    GotoUnfurl( false );
                }
                else if ( secondButton )
                {
                    // Overworld "Continue from start": a clean full-hearts
                    // restart here too, not just the floor.
                    profile.Hearts = profile.GetMaxHeartsValue();
                    GotoUnfurl( true );
                }
                else
                {
                    // Overworld "Continue": restore the hearts you entered
                    // the current screen with (the last autosave checkpoint),
                    // floored at one full heart (see above), and respawn on
                    // the same overworld screen you died on. restartOW=false
                    // makes Unfurl load curRoomId (the death screen).
                    profile.Hearts = checkpointHearts;
                    if ( profile.Hearts < Profile::GetMaxHeartsValue( 1 ) )
                        profile.Hearts = Profile::GetMaxHeartsValue( 1 );
                    GotoUnfurl( false );
                }
            }
            else if ( state.continueQuestion.selectedIndex == 1 )
            {
                SaveFolder::WriteProfile( profileSlot, profile );
                GotoFileMenu();
            }
            else if ( state.continueQuestion.selectedIndex == 2 )
            {
                GotoFileMenu();
            }
        }
        else
        {
            state.continueQuestion.timer--;
        }
    }
}

void WorldImpl::DrawContinueQuestion()
{
#ifdef __3DS__
    // The continue options live on the modern bottom-screen touch panel
    // (Graphics_3ds.cpp's DrawContinuePanel); the top screen just holds a
    // centred GAME OVER, matching the death sequence's own text.
    ClearScreen();
    static const uint8_t GameOver[] = { 0x10, 0x0A, 0x16, 0x0E, 0x24, 0x18, 0x1F, 0x0E, 0x1B };
    // Centred in the full play view (matches DrawDie's GAME OVER above).
    DrawString( GameOver, sizeof GameOver, 92, 148, 0 );
    return;
#else
    static const uint8_t Continue[] = { 0x0C, 0x18, 0x17, 0x1D, 0x12, 0x17, 0x1E, 0x0E };
    static const uint8_t Save[]     = { 0x1C, 0x0A, 0x1F, 0x0E, 0x24, 0x24, 0x24, 0x24 };
    static const uint8_t Retry[]    = { 0x1B, 0x0E, 0x1D, 0x1B, 0x22, 0x24, 0x24, 0x24 };
    static const uint8_t* Strs[]    = { Continue, Save, Retry };

    ClearScreen();

    int y = 0x50;

    for ( int i = 0; i < 3; i++, y += 24 )
    {
        int pal = 0;
        if ( state.continueQuestion.substate == ContinueState::Chosen 
            && state.continueQuestion.selectedIndex == i )
            pal = (GetFrameCounter() / 4) & 1;

        DrawString( Strs[i], _countof( Continue ), 0x50, y, pal );
    }

    y = 0x50 + (state.continueQuestion.selectedIndex * 24 );
    DrawChar( Char_FullHeart, 0x40, y, RedFgPalette );
#endif
}

void WorldImpl::GotoFileMenu()
{
    auto summaries = std::make_shared<ProfileSummarySnapshot>();
    SaveFolder::ReadSummaries( *summaries.get() );
    GotoFileMenu( summaries );
}

void WorldImpl::GotoFileMenu( const std::shared_ptr<ProfileSummarySnapshot>& summaries )
{
    if ( nextGameMenu != nullptr )
        delete nextGameMenu;

    nextGameMenu = new GameMenu( summaries );
    curMode = Mode_GameMenu;
}

// Phase 11.1b: title screen shown before the auto-continue/file-menu decision
// (an option, on by default so new players always see it). Strings
// hand-encoded against this port's
// real NES text format (see World.cpp's GetString() comment for the byte
// mapping this was verified against) -- DrawString() takes plain bytes, no
// attr/terminator bits (unlike TextBox's dialogue format). Uses 0x24
// (Char_Space, a real blank glyph in the font sheet) for spaces, NOT 0x25
// (Char_JustSpace) -- that one's a TextBox-dialogue-only concept (skips the
// per-letter blip sound during the reveal) and is NOT a blank tile by
// itself; using it here drew a visible glyph instead of a gap (found via
// screenshot -- stray white squares between every word).
static const uint8_t s_titleThe[]    = { 0x1D, 0x11, 0x0E, 0x24, 0x15, 0x0E, 0x10, 0x0E, 0x17, 0x0D, 0x24, 0x18, 0x0F };  // "THE LEGEND OF"
static const uint8_t s_titleZelda[]  = { 0x23, 0x0E, 0x15, 0x0D, 0x0A };                                                  // "ZELDA"
static const uint8_t s_titleEdition[]= { 0x03, 0x0D, 0x1C, 0x24, 0x0E, 0x0D, 0x12, 0x1D, 0x12, 0x18, 0x17 };              // "3DS EDITION"
static const uint8_t s_titlePress[]  = { 0x19, 0x1B, 0x0E, 0x1C, 0x1C, 0x24, 0x1C, 0x1D, 0x0A, 0x1B, 0x1D };              // "PRESS START"

void WorldImpl::GotoTitle()
{
    curMode = Mode_Title;
}

void WorldImpl::UpdateTitle()
{
    if ( Input::IsButtonPressing( InputButtons::Start ) )
    {
        int lastSlot = SaveFolder::ReadLastSlot();
        Profile lastProfile;
        bool holdingSelect = Input::IsButtonDown( InputButtons::Select );

        if ( !holdingSelect && lastSlot >= 0 && SaveFolder::ReadProfile( lastSlot, lastProfile ) )
        {
            Start( lastSlot, lastProfile );
        }
        else
        {
            GotoFileMenu();
            // Same reasoning as the old boot-time prewarm (see the comment
            // this replaced in Init()): Font/PlayerAndItems are what the
            // next screen actually draws on its first frame.
            Graphics::PrewarmSheet( Sheet_Font );
            Graphics::PrewarmSheet( Sheet_PlayerAndItems );
        }
    }
}

void WorldImpl::DrawTitle()
{
    // Graphics::Begin()/End() bind the shader's palette-lookup texture for
    // this draw (see GameMenu::Draw()/DrawMap() -- every top-level screen
    // wraps its own drawing in this pair; there's no single outer wrapper
    // that does it once for everyone). Missing this was the actual cause of
    // an all-black desktop repro: DrawItem/DrawString both ran with valid
    // data and no crash, just produced literally no visible pixels, because
    // the shader had no palette texture bound to sample from.
    Graphics::Begin();

    // Own clean 1:1 view (no-op on desktop, where this is the only view that
    // ever exists) -- see the comment on useNormalView's declaration above
    // for why this doesn't go through SetFullPlayView()/SetNormalView().
    // Phase 11.1b bugfix: the offscreen top-screen buffer World::
    // Draw() renders into (s_topOffscreen, phase 6.1) is sized 512x352 -- the
    // 256x176 play area at a *2x* integer scale, same as SetFullPlayView()
    // uses for normal gameplay. This call was left at 1x (no HUD-crop offset
    // needed for a title screen, which is right, but 1x scale isn't) --
    // meaning every NES-logical-pixel coordinate below only fills half the
    // buffer's actual pixel density, so the whole composition ends up
    // shrunk into a small block in the upper-left quarter of the top screen
    // once Video_BlitTopScreen() scales the *entire* buffer up to fill the
    // physical screen. Match SetFullPlayView()'s 2x scale (just without its
    // -128 HUD-crop offset, since the title screen wants the full logical
    // canvas, not just the play area). #ifdef'd: desktop's single-window
    // view has no such 2x offscreen buffer -- viewScale must stay 1:1 there,
    // same reasoning as the comment above already explains for this call.
#ifdef __3DS__
    Graphics::SetViewParams( 2.0f, 0.0f, 0.0f );
#else
    Graphics::SetViewParams( 1.0f, 0.0f, 0.0f );
#endif

#ifndef __3DS__
    // Desktop has no automatic per-frame clear, so every top-level screen
    // does its own (see GameMenu::Draw()). SetClip() first, since
    // al_clear_to_color() respects whatever clip is currently active.
    Graphics::SetClip( 0, 0, StdViewWidth, StdViewHeight );
    al_clear_to_color( al_map_rgb( 0, 0, 0 ) );
#endif
    // Deliberately NOT calling SetClip() on 3DS: this target (the offscreen
    // 2x buffer, phase 6.1) already gets cleared to black unconditionally
    // every frame in Video_BeginFrame(), before World::Draw() ever runs, so
    // no manual clear is needed there -- and this content doesn't need
    // cropping in the first place. Found the hard way: Graphics::SetClip()
    // on 3DS also programs a *real* GPU scissor test (C3D_SetScissor, not
    // just the clipX/Y/W/H bookkeeping desktop's version uses), and that
    // scissor's Y origin turned out to run bottom-up on this
    // non-screen-linked render-to-texture target -- SetClip(0,0,256,240)
    // silently clipped away everything above roughly y=112 (this screen's
    // Triforce icon and "THE LEGEND OF" line), while content below that
    // line ("ZELDA", "3DS EDITION", "PRESS START") stayed visible. Skipping
    // the call entirely sidesteps the coordinate-convention mismatch rather
    // than trying to compensate for it here.

    // Same small palette setup GameMenu's own constructor uses (proven
    // colours for this same pre-gameplay boot context) -- rows 4/5/6
    // (PlayerPalette/BluePal/RedPal) all identical, so Item_PowerTriforce's
    // flash attribute (alternates those two rows) is a steady gold instead
    // of an unintended red/blue flicker.
    static const uint8_t palettes[PaletteCount][4] =
    {
        { 0x0F, 0x30, 0x00, 0x12 },
        { 0x0F, 0x16, 0x27, 0x36 },
        { 0x0F, 0x0C, 0x1C, 0x2C },
        { 0x0F, 0x12, 0x1C, 0x2C },
        { 0x00, 0x29, 0x27, 0x07 },
        { 0x00, 0x29, 0x27, 0x07 },
        { 0x00, 0x29, 0x27, 0x07 },
        { 0x00, 0x15, 0x27, 0x30 }
    };
    static bool paletteSet = false;
    if ( !paletteSet )
    {
        paletteSet = true;
        for ( int i = 0; i < PaletteCount; i++ )
            Graphics::SetPaletteIndexed( i, palettes[i] );
        Graphics::SetColor( 0, 0, 0xFF000000 );
        Graphics::UpdatePalettes();
    }

    // Force a black backdrop first (3DS-only; no-op on desktop) -- see
    // Graphics::FillBlack()'s comment for why this exists.
    Graphics::FillBlack();

    // x centred for each string's pixel width (8px/char) on the 256px canvas.
    DrawItem( Item_PowerTriforce, 0x78, 0x28, 0 );                              // 16px wide, centred
    DrawString( s_titleThe,     _countof( s_titleThe ),     0x4C, 0x60, 0 );     // 13 chars
    DrawString( s_titleZelda,   _countof( s_titleZelda ),   0x6C, 0x70, 0 );     // 5 chars
    DrawString( s_titleEdition, _countof( s_titleEdition ), 0x54, 0x84, 0 );     // 11 chars

    // Blinking "PRESS START" -- same cadence as the classic pause-menu
    // "SAVED" toast blink elsewhere in this file.
    if ( (GetFrameCounter() / 30) & 1 )
        DrawString( s_titlePress, _countof( s_titlePress ), 0x54, 0xB0, 0 );     // 11 chars

    Graphics::End();
}

void WorldImpl::GotoRegisterMenu( const std::shared_ptr<ProfileSummarySnapshot>& summaries )
{
    if ( nextGameMenu != nullptr )
        delete nextGameMenu;

    nextGameMenu = new RegisterMenu( summaries );
    curMode = Mode_Register;
}

void WorldImpl::GotoEliminateMenu( const std::shared_ptr<ProfileSummarySnapshot>& summaries )
{
    if ( nextGameMenu != nullptr )
        delete nextGameMenu;

    nextGameMenu = new EliminateMenu( summaries );
    curMode = Mode_Elimination;
}

void WorldImpl::UpdateGameMenu()
{
    gameMenu->Update();
}

void WorldImpl::UpdateRegisterMenu()
{
    gameMenu->Update();
}

void WorldImpl::UpdateEliminateMenu()
{
    gameMenu->Update();
}

void WorldImpl::DrawGameMenu()
{
    if ( gameMenu != nullptr )
        gameMenu->Draw();
}

int WorldImpl::FindCellarRoomId( int mainRoomId, bool& isLeft )
{
    for ( int i = 0; i < LevelInfoBlock::LevelCellarCount; i++ )
    {
        uint8_t cellarRoomId = infoBlock.CellarRoomIds[i];

        if ( cellarRoomId >= 0x80 )
            break;

        UWRoomAttrs& uwRoomAttrs = (UWRoomAttrs&) roomAttrs[cellarRoomId];

        if ( mainRoomId == uwRoomAttrs.GetLeftCellarExitRoomId() )
        {
            isLeft = true;
            return cellarRoomId;
        }
        else if ( mainRoomId == uwRoomAttrs.GetRightCellarExitRoomId() )
        {
            isLeft = false;
            return cellarRoomId;
        }
    }

    return -1;
}

void WorldImpl::DrawRoomNoObjects( SpritePriority playerPriority )
{
    ClearScreen();

    // Phase 12.2b: Link is Mid; DrawRoom() (via DrawMap()) manages Ground/
    // tree-Mid switching internally and always leaves Ground active when it
    // returns -- see DrawMap()'s comment. Explicit switches here so Link
    // and the closing DrawDoors() call (drawn directly, not through
    // DrawMap()) land in the right layer regardless of which of these
    // three draw calls run.
    Graphics::SetTopLayer( TopLayer::Mid );
    if ( playerPriority == SpritePri_BelowBg )
        player->Draw();

    DrawRoom();

    Graphics::SetTopLayer( TopLayer::Mid );
    if ( playerPriority == SpritePri_AboveBg )
        player->Draw();

    if ( IsUWMain( curRoomId ) )
    {
        Graphics::SetTopLayer( TopLayer::Ground );
        DrawDoors( curRoomId, true, 0, 0 );
    }

    Graphics::SetTopLayer( TopLayer::Mid );
}

void WorldImpl::NoneTileAction( int row, int col, TileInteraction interaction )
{
    // Nothing to do. Should never be called.
}

void WorldImpl::PushTileAction( int row, int col, TileInteraction interaction )
{
    if ( interaction != TInteract_Load )
        return;

    Rock*   rock = new Rock();
    rock->SetX( col * TileWidth );
    rock->SetY( TileMapBaseY + row * TileHeight );
    SetBlockObj( rock );
}

void WorldImpl::BombTileAction( int row, int col, TileInteraction interaction )
{
    if ( interaction != TInteract_Load )
        return;

    if ( GotSecret() )
    {
        tileMaps[curTileMapIndex].tileRefs[row][col] = Tile_Cave;
    }
    else
    {
        RockWall*   rockWall = new RockWall();
        rockWall->SetX( col * TileWidth );
        rockWall->SetY( TileMapBaseY + row * TileHeight );
        SetBlockObj( rockWall );
    }
}

void WorldImpl::BurnTileAction( int row, int col, TileInteraction interaction )
{
    if ( interaction != TInteract_Load )
        return;

    if ( GotSecret() )
    {
        tileMaps[curTileMapIndex].tileRefs[row][col] = Tile_Stairs;
    }
    else
    {
        Tree*   tree = new Tree();
        tree->SetX( col * TileWidth );
        tree->SetY( TileMapBaseY + row * TileHeight );
        SetBlockObj( tree );
    }
}

void WorldImpl::HeadstoneTileAction( int row, int col, TileInteraction interaction )
{
    if ( interaction != TInteract_Load )
        return;

    Headstone*  headstone = new Headstone();
    headstone->SetX( col * TileWidth );
    headstone->SetY( TileMapBaseY + row * TileHeight );
    SetBlockObj( headstone );
}

void WorldImpl::LadderTileAction( int row, int col, TileInteraction interaction )
{
    if ( interaction != TInteract_Touch )
        return;

    _RPT2( _CRT_WARN, "Touch water: %d, %d\n", row, col );
}

void WorldImpl::RaftTileAction( int row, int col, TileInteraction interaction )
{
    // TODO: instantiate the Dock here on Load interaction, and set its position.

    if ( interaction != TInteract_Cover )
        return;

    _RPT2( _CRT_WARN, "Cover dock: %d, %d\n", row, col );

    if ( World::GetItem( ItemSlot_Raft ) == 0 )
        return;
    if ( !FindSparseFlag( Sparse_Dock, curRoomId ) )
        return;
}

void WorldImpl::CaveTileAction( int row, int col, TileInteraction interaction )
{
    if ( interaction != TInteract_Cover )
        return;

    if ( IsOverworld() )
    {
        int t = GetMapTile( row, col );
        GotoStairs( t );
    }

    _RPT2( _CRT_WARN, "Cover cave: %d, %d\n", row, col );
}

void WorldImpl::StairsTileAction( int row, int col, TileInteraction interaction )
{
    if ( interaction != TInteract_Cover )
        return;

    if ( GetMode() == Mode_Play )
        GotoStairs( Tile_Stairs );

    _RPT2( _CRT_WARN, "Cover stairs: %d, %d\n", row, col );
}

void WorldImpl::GhostTileAction( int row, int col, TileInteraction interaction )
{
    if ( interaction != TInteract_Push )
        return;

    _RPT2( _CRT_WARN, "Push headstone: %d, %d\n", row, col );

    MakeActivatedObject( Obj_FlyingGhini, row, col );
}

void WorldImpl::ArmosTileAction( int row, int col, TileInteraction interaction )
{
    if ( interaction != TInteract_Push )
        return;

    _RPT2( _CRT_WARN, "Push armos: %d, %d\n", row, col );

    MakeActivatedObject( Obj_Armos, row, col );
}

void WorldImpl::MakeActivatedObject( int type, int row, int col )
{
    row += 4;

    int x = col * TileWidth;
    int y = row * TileHeight;

    for ( int i = LastMonsterSlot; i >= 0; i-- )
    {
        Object* obj = objects[i];
        if ( obj == nullptr || obj->GetType() != type )
            continue;

        int objCol = obj->GetX() / TileWidth;
        int objRow = obj->GetY() / TileHeight;

        if ( objCol == col && objRow == row )
            return;
    }

    int freeSlot = FindEmptyMonsterSlot();
    if ( freeSlot >= 0 )
    {
        Object* obj = MakeMonster( (ObjType) type, x, y );
        objects[freeSlot] = obj;
        obj->SetObjectTimer( 0x40 );
    }
}

void WorldImpl::BlockTileAction( int row, int col, TileInteraction interaction )
{
    if ( interaction != TInteract_Load )
        return;

    Block*  block = new Block();
    block->SetX( col * TileWidth );
    block->SetY( TileMapBaseY + row * TileHeight );
    SetBlockObj( block );
}


//----------------------------------------------------------------------------

World::World()
{
}

void World::Init()
{
    sWorld = new WorldImpl();
    sWorld->Init();
}

void World::Uninit()
{
    delete sWorld;
    sWorld = nullptr;
}

void World::Start( int slot, const Profile& profile )
{
    sWorld->Start( slot, profile );
}

void World::StartNewGameInSlot( int slot, int quest )
{
    // No name typing (just slot 1, 2 and 3 to pick from) -- a blank/all-
    // spaces name still needs NameLength > 0
    // so ProfileSummary::IsActive() (and the classic GameMenu's own cursor
    // logic, if that path is ever used) correctly see this slot as
    // occupied. Same minimal field set RegisterMenu::CommitFiles() used to
    // write for a freshly typed name, just without the typing step.
    //
    // `quest`: removing RegisterMenu's name-typing screen from the normal
    // flow also removed its only way to reach quest 2 (the classic game's
    // "name your file ZELDA" trick, RegisterMenu.cpp's Quest2Name check) --
    // a real regression, not just a cosmetic change (quest 2 is a fully
    // separate, working set of overworld/dungeon data, see World.cpp's
    // `profile.Quest`-keyed level dir lookups). GameMenu.cpp's classic
    // desktop-only file-select screen still passes 1 here via its own
    // "hold Select while pressing Start" convention (fine for a keyboard,
    // never shown on 3DS). The 3DS SAVE SLOTS overlay (LiveMap_state.cpp)
    // no longer uses a hold/combo at all: phase 14.1 added a real QUEST 1 /
    // QUEST 2 tap choice, shown on an empty slot only once
    // `Options::Get().Quest2Unlocked` is set (permanently, the moment quest
    // 1 is first won on any slot -- see WorldImpl::GotoWinGame()):
    // hold/combo-gated options are hard to discover and to use.
    Profile profile = { 0 };
    profile.NameLength = MaxNameLength;
    memset( profile.Name, Char_Space, MaxNameLength );
    profile.Quest = (uint8_t) quest;
    profile.Items[ItemSlot_HeartContainers] = DefaultHearts;
    profile.Items[ItemSlot_MaxBombs] = DefaultBombs;
    SaveFolder::WriteProfile( slot, profile );
    SwitchToSlot( slot );
}

void World::SwitchToSlot( int slot )
{
    Profile profile;
    if ( !SaveFolder::ReadProfile( slot, profile ) )
        return;
    SaveFolder::WriteLastSlot( slot );
    sWorld->Start( slot, profile );
}

void World::DeleteSlot( int slot )
{
    Profile empty = { 0 };
    SaveFolder::WriteProfile( slot, empty );
    // Don't leave lastslot.dat pointing at a now-blank profile -- a future
    // auto-continue boot would otherwise silently "continue" into an empty
    // save instead of showing the slot picker.
    if ( SaveFolder::ReadLastSlot() == slot )
        SaveFolder::WriteLastSlot( -1 );
}

void World::CopySlot( int srcSlot, int dstSlot )
{
    if ( srcSlot == GetProfileSlot() )
        SaveNow();

    Profile profile;
    if ( SaveFolder::ReadProfile( srcSlot, profile ) )
        SaveFolder::WriteProfile( dstSlot, profile );
}

void World::Update()
{
    sWorld->Update();
}

void World::Draw()
{
    sWorld->Draw();
}
