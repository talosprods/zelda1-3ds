/*
   Copyright 2016 Aldo J. Nunez

   Licensed under the Apache License, Version 2.0.
   See the LICENSE text file for details.

   Modified for The Legend of Zelda: 3DS Edition (Nintendo 3DS port).
*/

#pragma once

enum
{
    MaxNameLength = 8,
    DefaultHearts = 3,
    DefaultBombs  = 8,
};

enum ItemSlot
{
    ItemSlot_Sword,
    ItemSlot_Bombs,
    ItemSlot_Arrow,
    ItemSlot_Bow,
    ItemSlot_Candle,
    ItemSlot_Recorder,
    ItemSlot_Food,
    ItemSlot_Potion,
    ItemSlot_Rod,
    ItemSlot_Raft,
    ItemSlot_Book,
    ItemSlot_Ring,
    ItemSlot_Ladder,
    ItemSlot_MagicKey,
    ItemSlot_Bracelet,
    ItemSlot_Letter,
    ItemSlot_Compass,
    ItemSlot_Map,
    ItemSlot_Compass9,
    ItemSlot_Map9,
    ItemSlot_Clock,
    ItemSlot_Rupees,
    ItemSlot_Keys,
    ItemSlot_HeartContainers,
    ItemSlot_PartialHeart_Unused,
    ItemSlot_TriforcePieces,
    ItemSlot_PowerTriforce,
    ItemSlot_Boomerang,
    ItemSlot_MagicShield,
    ItemSlot_MaxBombs,
    ItemSlot_RupeesToAdd,
    ItemSlot_RupeesToSubtract,

    ItemSlot_MaxItems
};

struct OWRoomFlags
{
    enum
    {
        ItemState       = 0x10,
        ShortcutState   = 0x20,
        SecretState     = 0x80,

        CountMask       = 7,
        CountShift      = 0,
    };

    uint8_t Data;

    bool GetItemState()
    {
        return Data & ItemState;
    }

    void SetItemState()
    {
        Data |= ItemState;
    }

    bool GetShortcutState()
    {
        return Data & ShortcutState;
    }

    void SetShortcutState()
    {
        Data |= ShortcutState;
    }

    bool GetSecretState()
    {
        return Data & SecretState;
    }

    void SetSecretState()
    {
        Data |= SecretState;
    }

    int GetObjCount()
    {
        return (Data & CountMask) >> CountShift;
    }

    void SetObjCount( int count )
    {
        Data = (Data & ~CountMask) | (count << CountShift);
    }
};

struct UWRoomFlags
{
    enum
    {
        ItemState       = 0x10,
        VisitState      = 0x20,

        CountMask       = 0xC0,
        CountShift      = 6,
    };

    uint8_t Data;

    bool GetItemState()
    {
        return Data & ItemState;
    }

    void SetItemState()
    {
        Data |= ItemState;
    }

    bool GetVisitState()
    {
        return Data & VisitState;
    }

    void SetVisitState()
    {
        Data |= VisitState;
    }

    bool GetDoorState( int dir )
    {
        return (Data & dir) != 0;
    }

    void SetDoorState( int dir )
    {
        Data |= dir;
    }

    int GetObjCount()
    {
        return (Data & CountMask) >> CountShift;
    }

    void SetObjCount( int count )
    {
        Data = (Data & ~CountMask) | (count << CountShift);
    }
};


struct Profile
{
    uint8_t     NameLength;
    uint8_t     Name[MaxNameLength];
    uint8_t     Quest;
    uint8_t     Deaths;
    uint8_t     SelectedItem;
    uint16_t    Hearts;
    uint8_t     Items[ItemSlot_MaxItems];
    OWRoomFlags OverworldFlags[LevelBlockRooms];
    UWRoomFlags LevelFlags1[LevelBlockRooms];
    UWRoomFlags LevelFlags2[LevelBlockRooms];

    // phase 13.3 stats -- appended at the end so old saves (fewer bytes)
    // still load correctly and leave these at their zero default; see
    // the ReadProfile growth-safety note for why that's safe
    // now (it wasn't, before that fix -- see phase 8.1's updated note).
    uint32_t    PlayTimeSeconds;    // WorldImpl::Update(), only while IsPlaying()
    uint16_t    KillCount;          // World::IncrementKilledObjectCount() (Object.cpp's normal DealDamage->death path)
    uint16_t    RepairRupeesSpent;  // Person::UpdateDialog(), the String_DoorRepair branch (always 20/repair)

    // phase 10.4: one bit per Sparse_ArmosStairs room (6 total, see
    // LiveMapGeometry's kSecretRooms), set once that room's armos has ever
    // been pushed onto its correct spot. OnActivatedArmos() never persisted
    // this itself (phase 10.4) -- the stairs tile reappears fine on
    // revisit, but the map had no way to know it was already solved.
    uint8_t     ArmosStairsSolved;

    // phase 13.2: permanent "ever owned" record per item slot, for the
    // planned bestiary/manual item checklist. Items[] itself isn't safe for
    // this -- Food/MagicShield/Potion can legitimately drop back to 0 during
    // normal play (traded away, taken by a Like-Like, used up), and Candle/
    // Ring/Boomerang can in theory be downgraded by repurchasing a lower
    // tier from a shop (see DEVELOPMENT.md §11 for the full per-slot
    // analysis). Set once in WorldImpl::AddItem(), never cleared.
    uint8_t     EverHadItem[ItemSlot_MaxItems];

    // phase 13.2: permanent "ever defeated" record per boss species, for the
    // bestiary screen. Indexed by World::BossId (World.h) -- 7 of the game's
    // bosses route through Object::KillObjectNormally() and are set from
    // there; Ganon doesn't (his death is a custom dyingTimer sequence in
    // Monsters.cpp, not the normal DealDamage/hp==0 path) and is set
    // directly from Ganon::CheckCollision() at the confirmed killing blow.
    // Never cleared, same growth-safe append-at-the-end reasoning as
    // EverHadItem above.
    uint8_t     EverDefeatedBoss[7];

    // Save slots remember where you were. Classic Zelda 1 never saved your
    // overworld position --
    // continuing a file always resumed at the fixed quest-start tile
    // (infoBlock.StartRoomId/StartY). This records the last overworld
    // screen/position AutoSave() saw (same cadence as checkpointHearts/
    // checkpointX/Y, phase 8.3/8.4c -- WorldImpl::Start() reads it back to
    // resume there instead of the fixed tile.
    // SavedOWRoomIdPlus1 is the room id *plus one*, not the room id itself,
    // so plain zero-fill (both a genuinely new profile and an old save from
    // before this field existed -- ReadProfile() memsets the whole struct
    // to 0 before reading whatever bytes an older/shorter file actually
    // has, see SaveFolder.cpp) unambiguously means "never saved on the
    // overworld yet", not "room 0" -- keeps the classic fixed-start
    // behaviour for those instead of teleporting into a bogus room 0.
    uint8_t     SavedOWRoomIdPlus1;
    uint8_t     SavedX;
    uint8_t     SavedY;

    uint GetMaxHeartsValue();
    static uint GetMaxHeartsValue( uint heartContainers );
};
