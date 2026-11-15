/*
   Copyright 2016 Aldo J. Nunez

   Licensed under the Apache License, Version 2.0.
   See the LICENSE text file for details.

   Modified for The Legend of Zelda: 3DS Edition (Nintendo 3DS port).
*/

#pragma once

#include "Object.h"
#include "SpriteAnimator.h"

#include "Input.h"

typedef std::array<uint8_t, 4> Limits;


class Player : public Object, public IThrower
{
public:
    enum
    {
        WalkSpeed       = 0x60,
        StairsSpeed     = 0x30,
    };

    enum State
    {
        Idle,
        Wielding,
        Paused,
    };

private:
    uint8_t             state;
    uint8_t             speed;
    uint8_t             tileRef;
    bool                paralyzed;
    uint8_t             animTimer;
    uint8_t             avoidTurningWhenDiag;   // 56
    uint8_t             keepGoingStraight;      // 57
    InputButtons        curButtons;
    SpriteAnimator      animator;

public:
    Player();

    virtual void Update();
    virtual void Draw();
    virtual void* GetInterface( ObjInterfaces iface );

    int GetState();
    void SetState( State state );
    void SetFacing( Direction dir );
    Bounds GetBounds();
    Point GetMiddle();
    SpriteAnimator* GetAnimator();
    int  GetInvincibilityTimer();
    void SetInvincibilityTimer( int value );
    bool IsParalyzed();
    void SetParalyzed( bool value );
    void ResetShove();

    void Catch();
    void BeHarmed( Object* collider, Point& otherMiddle );
    void BeHarmed( Object* collider, Point& otherMiddle, uint16_t damage );
    void DecInvincibleTimer();
    void Stop();
    void MoveLinear( Direction dir, int speed );

    static Limits GetPlayerLimits();

    // Second equipped item (phase 8.1, Y button — 3DS only, never set on
    // desktop). Deliberately *not* part of Profile: it's session-only (not
    // save-persisted), so adding it can't change Profile's binary layout and
    // break existing save files. Assigned from the pause inventory (see
    // Submenu.cpp) the same way Profile::SelectedItem is, just via Y instead
    // of the Left/Right cursor. Static (not per-Player-instance) so it
    // survives the Player object being recreated on every level transition.
    static uint8_t SelectedItem2;

    // True while the 3DS bottom screen is showing its touch Items screen.
    // Set per frame by that screen; never set on desktop, which has none.
    //
    // Deliberately a plain statement of fact, not a policy flag: what to *do*
    // about it is decided in Player.cpp, next to the item-use dispatch that
    // already knows which items cost something. See IsConsumableItem() there.
    //
    // Why it exists: that screen assigns an item to the second slot by holding
    // Y and tapping the grid (phase 8.2), while gameplay deliberately keeps
    // running underneath. So the same Y press that means "assign to slot 2"
    // also fired UseItem2() on the top screen. With bombs in that slot you drop
    // one every time you re-equip, by accident. LiveMap_state.cpp knew about the
    // overlap and wrote it off as "a minor, harmless overlap, not a bug".
    static bool InventoryScreenOpen;

private:
    void SetFacingAnim();

    void HandleInput();
    void SetMovingInDoorway();
    void FilterBorderInput();
    void SetSpeed();
    void Align();
    void CalcAlignedMoving();
    void CheckWater();
    void CheckWarp();
    void Animate();

    int UseWeapon();
    int UseWeapon( ObjType type, int itemSlot );
    int UseItem();
    int UseItem2();

    void Move();
    Direction CheckSubroom( Direction dir );
    Direction CheckDoorways( Direction dir );
    Direction CheckDoor( Direction dir, int dirOrd );
    Direction HandleLadder( Direction dir );
    Direction MoveOnLadder( Direction dir, int distance );
    Direction StopAtBlock( Direction dir );

    Direction CheckWorldBounds( Direction dir );
    Direction CheckTileCollision( Direction dir );
    Direction FindUnblockedDir( Direction dir );

    void PushOWTile( TileCollision& collision );
    bool HitsWorldLimit();
    void StopPlayer();
};
