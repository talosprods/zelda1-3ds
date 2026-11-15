/*
   Copyright 2016 Aldo J. Nunez

   Licensed under the Apache License, Version 2.0.
   See the LICENSE text file for details.

   Modified for The Legend of Zelda: 3DS Edition (Nintendo 3DS port).
*/

#pragma once

#include "Profile.h"
#include "Options.h"


enum
{
    MaxProfiles = 3,
};


struct ProfileSummary
{
    uint8_t     NameLength;
    uint8_t     Name[MaxNameLength];
    uint8_t     Quest;
    uint8_t     Deaths;
    uint8_t     HeartContainers;

    // phase 14.3: shown on the SAVE SLOTS overlay alongside the fields above.
    uint32_t    PlayTimeSeconds;
    uint8_t     TriforcePieceCount;   // popcount of Profile::Items[ItemSlot_TriforcePieces]

    bool IsActive() const
    {
        return NameLength > 0;
    }
};


struct ProfileSummarySnapshot
{
    ProfileSummary  Summaries[MaxProfiles];
};


class SaveFolder
{
public:
    static void ReadSummaries( ProfileSummarySnapshot& summaries );
    static bool ReadProfile( int index, Profile& profile );
    static bool WriteProfile( int index, const Profile& profile );

    // Which slot was last actively played, so the game can boot straight
    // back into it. Returns -1 if none has been recorded yet.
    static int  ReadLastSlot();
    static void WriteLastSlot( int index );

    // Global options (phase 11.1/11.2), separate from any save slot.
    // ReadOptions returns false (and zeroes options) if none is on disk yet
    // (first boot) -- Options::Init() then falls back to all-default values.
    static bool ReadOptions( GameOptions& options );
    static void WriteOptions( const GameOptions& options );
};
