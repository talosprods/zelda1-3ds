/*
   Unit tests for the pure overworld-map grid math (phase 10.2's
   ComputeMapCell()/CyclePin(), zelda3ds/source/LiveMapGeometry.cpp).

   No framework: this project has no prior unit-test infrastructure and these
   two functions are the first pieces of 3DS-side logic pure enough to be
   worth one (no World.h/citro2d/libctru). A dozen plain assert-style checks
   don't justify pulling in a dependency (Catch2/GoogleTest) the rest of the
   codebase has never needed -- see zelda3ds/tests/run.sh for how this is
   built and run (plain clang++, no CMake target).

   Coordinates/expectations for the non-zoom and zoom cases mirror what was
   independently confirmed live in Azahar during phase 10.2's verification
  : a tap at (55, 91) landed on room 18 there too.
*/
#include "../source/LiveMapGeometry.h"
#include <cstdio>
#include <cstring>

static int s_failures = 0;
static int s_checks = 0;

#define CHECK( cond ) \
    do { \
        s_checks++; \
        if ( !(cond) ) { \
            s_failures++; \
            std::printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond ); \
        } \
    } while ( 0 )

static LiveMapState MakeState( int curRoom, bool detailZoom )
{
    LiveMapState state;
    std::memset( &state, 0, sizeof state );
    state.curRoom = curRoom;
    state.detailZoom = detailZoom;
    return state;
}

static void TestNonZoomHit()
{
    LiveMapState state = MakeState( 0x77, false );   // curRoom irrelevant when not zoomed

    // Matches the live Azahar verification in phase 10.2: a tap at
    // (55, 91) landed on room 18 (row 1, col 2) on the real build.
    int room = -1;
    CHECK( ComputeMapCell( state, 55, 91, room ) );
    CHECK( room == 18 );

    // Cell (0,0)'s interior and (15,7)'s interior -- the two grid corners.
    CHECK( ComputeMapCell( state, (int) LIVEMAP_GRID_OX + 1,
                           (int) LIVEMAP_GRID_OY + 1, room ) );
    CHECK( room == 0 );

    float gw = LIVEMAP_COLS * LIVEMAP_CELL_W, gh = LIVEMAP_ROWS * LIVEMAP_CELL_H;
    CHECK( ComputeMapCell( state, (int) (LIVEMAP_GRID_OX + gw - 1),
                           (int) (LIVEMAP_GRID_OY + gh - 1), room ) );
    CHECK( room == LIVEMAP_COLS * LIVEMAP_ROWS - 1 );
}

static void TestOutsideGridMisses()
{
    LiveMapState state = MakeState( 0x77, false );
    int room = -1;

    CHECK( !ComputeMapCell( state, 55, (int) LIVEMAP_GRID_OY - 20, room ) );   // header area, above grid
    CHECK( !ComputeMapCell( state, (int) LIVEMAP_GRID_OX - 5, 91, room ) );    // left of grid
    float gw = LIVEMAP_COLS * LIVEMAP_CELL_W, gh = LIVEMAP_ROWS * LIVEMAP_CELL_H;
    CHECK( !ComputeMapCell( state, (int) (LIVEMAP_GRID_OX + gw + 5), 91, room ) );        // right of grid
    CHECK( !ComputeMapCell( state, 55, (int) (LIVEMAP_GRID_OY + gh + 5), room ) );        // below grid
}

static void TestZoomCentered()
{
    // curRoom = row 6, col 7 (room 103) -- the exact case live-verified in
    // Azahar (phase 10.2): zoomed tap near the viewport centre landed back
    // on/near the player's own room, in-bounds.
    LiveMapState state = MakeState( 6 * LIVEMAP_COLS + 7, true );

    const int viewCols = LIVEMAP_COLS / 2, viewRows = LIVEMAP_ROWS / 2;
    const float gw = LIVEMAP_COLS * LIVEMAP_CELL_W, gh = LIVEMAP_ROWS * LIVEMAP_CELL_H;
    const float cw = gw / viewCols, ch = gh / viewRows;
    // Centred viewport (no clamping needed away from the edges): vx0=3, vy0=4.
    int cx = (int) (LIVEMAP_GRID_OX + 4 * cw + 1);
    int cy = (int) (LIVEMAP_GRID_OY + 2 * ch + 1);

    int room = -1;
    CHECK( ComputeMapCell( state, cx, cy, room ) );
    CHECK( room == 6 * LIVEMAP_COLS + 7 );
}

static void TestZoomClampsAtTopLeftCorner()
{
    // Player in the very first room (col 0, row 0): the 8x4 zoom viewport
    // can't centre without going negative, so it must clamp to (0,0) --
    // same ClampViewport() rule the renderer (Graphics_3ds.cpp) uses.
    LiveMapState state = MakeState( 0, true );

    int room = -1;
    CHECK( ComputeMapCell( state, (int) LIVEMAP_GRID_OX + 1,
                           (int) LIVEMAP_GRID_OY + 1, room ) );
    CHECK( room == 0 );
}

static void TestZoomClampsAtBottomRightCorner()
{
    // Player in the very last room (col 15, row 7): viewport clamps to its
    // maximum vx0/vy0 instead of centring (which would run off the grid).
    LiveMapState state = MakeState( LIVEMAP_COLS * LIVEMAP_ROWS - 1, true );

    const int viewCols = LIVEMAP_COLS / 2, viewRows = LIVEMAP_ROWS / 2;
    const float gw = LIVEMAP_COLS * LIVEMAP_CELL_W, gh = LIVEMAP_ROWS * LIVEMAP_CELL_H;
    int room = -1;
    CHECK( ComputeMapCell( state, (int) (LIVEMAP_GRID_OX + gw - 1),
                           (int) (LIVEMAP_GRID_OY + gh - 1), room ) );
    CHECK( room == LIVEMAP_COLS * LIVEMAP_ROWS - 1 );
    (void) viewCols; (void) viewRows;
}

static void TestCyclePinSequenceAndWraparound()
{
    uint8_t v = 0;
    for ( int i = 1; i <= LIVEMAP_PIN_TYPES; i++ )
    {
        v = CyclePin( v );
        CHECK( v == i );
    }
    // One more step from the last colour wraps back to "no pin".
    v = CyclePin( v );
    CHECK( v == 0 );
}

// phase 10.5: dungeon-entrance / POI lookups. Room 0x37 is level 1's entrance
// per kDungeonRooms -- cross-checked in phase 10.3 against the real
// quest-1 asset data (independent of this table). Room 0x44 deliberately has
// two POI entries (Shop + FluteWarp, a genuine coincidence in the source
// data -- see phase 10.3) and is the only such room, so it doubles as
// the multi-match test for FindPoiAt()'s startIndex chaining.
static void TestFindDungeonNumber()
{
    int level = -1;
    CHECK( FindDungeonNumber( 0x37, false, level ) );
    CHECK( level == 1 );

    level = -1;
    CHECK( FindDungeonNumber( 0x05, false, level ) );
    CHECK( level == 9 );

    CHECK( !FindDungeonNumber( 0x00, false, level ) );   // not a dungeon entrance

    // Phase 14.2: quest 2 only relocates L3 (0x74->0x34) and L6 (0x22->0x30);
    // everything else is shared with quest 1 -- see LiveMapGeometry.cpp's
    // kDungeonRoomsQuest2 comment for the full decode writeup.
    level = -1;
    CHECK( !FindDungeonNumber( 0x74, true, level ) );   // quest-1-only L3 room
    CHECK( FindDungeonNumber( 0x34, true, level ) );
    CHECK( level == 3 );

    level = -1;
    CHECK( !FindDungeonNumber( 0x22, true, level ) );   // quest-1-only L6 room
    CHECK( FindDungeonNumber( 0x30, true, level ) );
    CHECK( level == 6 );

    // Unrelocated levels stay identical in both quests.
    level = -1;
    CHECK( FindDungeonNumber( 0x37, true, level ) );
    CHECK( level == 1 );

    CHECK( GetDungeonRoom( 3, false ) == 0x74 );
    CHECK( GetDungeonRoom( 3, true )  == 0x34 );
    CHECK( GetDungeonRoom( 6, false ) == 0x22 );
    CHECK( GetDungeonRoom( 6, true )  == 0x30 );
    CHECK( GetDungeonRoom( 1, true )  == 0x37 );
}

static void TestFindPoiAtSingleMatch()
{
    int kind = -1, nextIdx = -1;
    CHECK( FindPoiAt( 0x39, 0, kind, nextIdx ) );   // fairy fountain room
    CHECK( kind == Poi_Fairy );

    CHECK( !FindPoiAt( 0x00, 0, kind, nextIdx ) );   // no POI here (it's a dungeon entrance)
}

static void TestFindPoiAtMultiMatch()
{
    // Room 0x44: both Shop and FluteWarp (see this file's header comment).
    int kind1 = -1, nextIdx1 = -1;
    CHECK( FindPoiAt( 0x44, 0, kind1, nextIdx1 ) );
    int kind2 = -1, nextIdx2 = -1;
    CHECK( FindPoiAt( 0x44, nextIdx1, kind2, nextIdx2 ) );
    CHECK( kind1 != kind2 );
    bool sawShop = (kind1 == Poi_Shop || kind2 == Poi_Shop);
    bool sawFlute = (kind1 == Poi_FluteWarp || kind2 == Poi_FluteWarp);
    CHECK( sawShop && sawFlute );
    // No third match.
    int kind3, nextIdx3;
    CHECK( !FindPoiAt( 0x44, nextIdx2, kind3, nextIdx3 ) );
}

static void TestPoiIterationCoversAllKnownRooms()
{
    // GetPoiByIndex() must agree with FindPoiAt() for every entry -- i.e.
    // the renderer's full-table draw loop and the touch hit-test's by-room
    // lookup can never disagree about what's where.
    int count = GetPoiCount();
    CHECK( count > 0 );
    for ( int i = 0; i < count; i++ )
    {
        int room, kind;
        GetPoiByIndex( i, room, kind );
        bool found = false;
        int k = -1, next = 0;
        while ( FindPoiAt( room, next, k, next ) )
        {
            if ( k == kind ) { found = true; break; }
        }
        CHECK( found );
    }
}

static void TestSecretKindAndArmosBit()
{
    // phase 10.4: the first 6 kSecretRooms entries must be ArmosStairs with
    // bit==index (World.cpp's sArmosStairsRooms relies on this exact same
    // ordering, duplicated there since it can't include this header -- see
    // phase 10.4); the rest must be the item-pickup-flag kind.
    int count = GetSecretCount();
    CHECK( count == 8 );
    for ( int i = 0; i < count; i++ )
    {
        if ( i < 6 )
        {
            CHECK( GetSecretKind( i ) == SecretKind_ArmosStairs );
            CHECK( GetArmosStairsBit( i ) == i );
        }
        else
        {
            CHECK( GetSecretKind( i ) == SecretKind_ArmosItemOrBuried );
        }
    }
}

static void TestTreeRockSecrets()
{
    // phase 10.4: 45 bombable-wall/burnable-tree cave entrances decoded offline
    // from WorldImpl::LoadLayout. Sanity-check the baked table: count, the
    // bomb/burn split, valid room ids, in-range fractions, and no duplicate
    // room (LoadLayout keeps exactly one active secret per room).
    int count = GetTreeRockSecretCount();
    CHECK( count == 45 );
    int bombs = 0, burns = 0;
    bool seen[256] = { false };
    for ( int i = 0; i < count; i++ )
    {
        int room; bool isBomb; float fx, fy;
        GetTreeRockSecretByIndex( i, room, isBomb, fx, fy );
        CHECK( room >= 0 && room < LIVEMAP_COLS * LIVEMAP_ROWS );
        CHECK( !seen[room] );   // one secret per room
        seen[room] = true;
        CHECK( fx >= 0.0f && fx <= 1.0f );
        CHECK( fy >= 0.0f && fy <= 1.0f );
        if ( isBomb ) bombs++; else burns++;
    }
    CHECK( bombs == 27 );
    CHECK( burns == 18 );
}

int main()
{
    TestNonZoomHit();
    TestOutsideGridMisses();
    TestZoomCentered();
    TestZoomClampsAtTopLeftCorner();
    TestZoomClampsAtBottomRightCorner();
    TestCyclePinSequenceAndWraparound();
    TestFindDungeonNumber();
    TestFindPoiAtSingleMatch();
    TestFindPoiAtMultiMatch();
    TestPoiIterationCoversAllKnownRooms();
    TestSecretKindAndArmosBit();
    TestTreeRockSecrets();

    std::printf( "%d/%d checks passed\n", s_checks - s_failures, s_checks );
    return s_failures == 0 ? 0 : 1;
}
